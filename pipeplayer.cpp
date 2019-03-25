/* pipeplayer.cpp
 * Copyright 2019 Keith Kaisershot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>	// std::min
#include <chrono>		// std::chrono
#include <climits>		// CHAR_BIT
#include <cstdio>		// fprintf
#include <cstdlib>		// atof, atoi, free, malloc
#include <cstring>		// memset, strcmp
#include <limits>		// std::numeric_limits
#include <thread>		// std::this_thread
#include <type_traits>	// std::is_unsigned
#include <unistd.h>		// getopt, fd_set, read, select, timeval

#include "portaudio.h"
#include "portaudio/src/common/pa_ringbuffer.h"
#include "portaudio/src/common/pa_util.h"

template<typename T>
static
T nextPowerOfTwo(T v)
{
	static_assert(std::is_unsigned<T>::value, "T is not an unsigned type");
	
	--v;
	for (size_t i = 1; i < (sizeof(v) * CHAR_BIT); i <<= 1)
	{
		v |= v >> i;
	}
	return ++v;
}

struct CallbackData
{
	PaUtilRingBuffer ringBuffer;
	uint8_t silenceByte;
};

static
int streamCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
)
{
	CallbackData* callbackData = (CallbackData*)userData;
	PaUtilRingBuffer& ringBuffer = callbackData->ringBuffer;
	// read as much as we can from the ring buffer directly into the output buffer
	ring_buffer_size_t framesRead = PaUtil_ReadRingBuffer(
		&ringBuffer,
		outputBuffer,
		std::min((ring_buffer_size_t)framesPerBuffer, PaUtil_GetRingBufferReadAvailable(&ringBuffer))
	);
	
	size_t bytesRead = framesRead * ringBuffer.elementSizeBytes;
	size_t bytesLeft = (framesPerBuffer * ringBuffer.elementSizeBytes) - bytesRead;
	// fill the rest of the buffer (if any) with silence
	memset((uint8_t*)outputBuffer + bytesRead, callbackData->silenceByte, bytesLeft);
	
	return paContinue;
}

static
int stdinReady(void)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	timeval tv = {0};
	
	return select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
}

struct Options
{
	// the defaults here for channels, format, rate, and buffer size all
	// correspond to the values used for Mac Sound Driver emulation, for which
	// this utility was originally written ;)
	
	int channels = 1;
	PaSampleFormat sampleFormat = paUInt8;
	size_t sampleSize = 1;
	double sampleRate = 22256.0;
	long framesPerBuffer = 370;
	
	// the rest of these defaults I just thought were reasonable :)
	PaStreamFlags streamFlags = paNoFlag;
	double timeout = std::numeric_limits<double>::infinity();
	int verbosity = 1;
};

static
void printUsage(void)
{
	fprintf(stdout,
		"usage: pipeplayer [-h] [-c <channels>] [-f <format>] [-r <sample rate>] [-b <buffer size>] [-d <feature>] [-t <timeout>] [-v <level>]\n"
		"\t-h: prints this message and exits\n"
		"\t-c <channels>: number of channels (integer), default: 1\n"
		"\t-f <sample format>: sample format (f, s16, s32, s24, s8, u8), default: u8\n"
		"\t-r <sample rate>: sample rate (double-precision floating point), default: 22256.0\n"
		"\t-b <buffer size>: buffer size in samples (integer), default: 370\n"
		"\t-d <feature>: feature to disable (clipping, dithering), default: none\n"
		"\t-t <timeout>: timeout in seconds after no new data arrives (double-precision floating point), default: forever\n"
		"\t-v <level>: log verbosity level (integer), default: 1\n"
	);
}

static
int getIntArg(char opt, int defaultArg)
{
	int result = atoi(optarg);
	if (result < 0)
	{
		fprintf(stderr, "argument %s to option '-%c' is invalid, using default: %d\n", optarg, opt, defaultArg);
		result = defaultArg;
	}
	return result;
}

static
double getDoubleArg(char opt, double defaultArg)
{
	double result = atof(optarg);
	if (result < 0)
	{
		fprintf(stderr, "argument %s to option '-%c' is invalid, using default: %.1f\n", optarg, opt, defaultArg);
		result = defaultArg;
	}
	return result;
}

static
int getOpts(int argc, char* argv[], Options& options)
{
	const size_t kFormatOptsCount = 6;
	struct FormatOptMapping
	{
		const char* optarg;
		PaSampleFormat sampleFormat;
		size_t sampleSize;
	};
	FormatOptMapping formatOptMap[kFormatOptsCount] =
	{
		{ "f", paFloat32, 4 },
		{ "s16", paInt16, 2 },
		{ "s32", paInt32, 4 },
		{ "s24", paInt24, 3 },
		{ "s8", paInt8, 1 },
		{ "u8", paUInt8, 1 },
	};
	
	const size_t kDisableOptsCount = 2;
	struct DisableOptMapping
	{
		const char* optarg;
		PaStreamFlags streamFlags;
	};
	DisableOptMapping disableOptMap[kDisableOptsCount] =
	{
		{ "clipping", paClipOff },
		{ "dithering", paDitherOff },
	};
	
	Options defaults;
	
	int opt = -1;
	// all options have required arguments except '-h'
	while ((opt = getopt(argc, argv, ":hc:f:r:b:d:t:v:")) != -1)
	{
		switch (opt)
		{
			case 'h':
				printUsage();
				exit(0);
			case 'c':
				options.channels = getIntArg(opt, defaults.channels);
				break;
			case 'f':
				{
					size_t i = 0;
					for (; i < kFormatOptsCount; ++i)
					{
						FormatOptMapping& mapping = formatOptMap[i];
						if (strcmp(mapping.optarg, optarg) == 0)
						{
							options.sampleFormat = mapping.sampleFormat;
							options.sampleSize = mapping.sampleSize;
							break;
						}
					}
					if (i == kFormatOptsCount)
					{
						for (i = 0; i < kFormatOptsCount; ++i)
						{
							FormatOptMapping& mapping = formatOptMap[i];
							if (mapping.sampleFormat == defaults.sampleFormat)
							{
								fprintf(stderr, "argument %s to option '-%c' is invalid, using default: %s\n", optarg, opt, mapping.optarg);
								break;
							}
						}
					}
				}
				break;
			case 'r':
				options.sampleRate = getDoubleArg(opt, defaults.sampleRate);
				break;
			case 'b':
				options.framesPerBuffer = getIntArg(opt, defaults.framesPerBuffer);
				break;
			case 'd':
				{
					size_t i = 0;
					for (; i < kDisableOptsCount; ++i)
					{
						DisableOptMapping& mapping = disableOptMap[i];
						if (strcmp(mapping.optarg, optarg) == 0)
						{
							options.streamFlags |= mapping.streamFlags;
							break;
						}
					}
					if (i == kDisableOptsCount)
					{
						fprintf(stderr, "argument %s to option '-%c' is invalid\n", optarg, opt);
					}
				}
				break;
			case 't':
				options.timeout = getDoubleArg(opt, defaults.timeout);
				break;
			case 'v':
				options.verbosity = getIntArg(opt, defaults.verbosity);
				break;
			case ':':
				fprintf(stderr, "missing argument for option '-%c'\n", optopt);
				printUsage();
				return 1;
			case '?':
				fprintf(stderr, "unknown option: -%c\n", optopt);
				printUsage();
				return 1;
		}
	}
	
	return 0;
}

int main(int argc, char* argv[])
{
	Options options;
	int result = getOpts(argc, argv, options);
	if (result != 0)
	{
		return result;
	}
	
#define PRINT(level, file, ...) if (options.verbosity >= level) { fprintf(file, __VA_ARGS__); }
#define DEBUG(...) PRINT(4, stdout, __VA_ARGS__)
#define INFO(...) PRINT(3, stdout, __VA_ARGS__)
#define WARN(...) PRINT(2, stderr, __VA_ARGS__)
#define ERROR(...) PRINT(1, stderr, __VA_ARGS__)
#define FATAL(...) ERROR(__VA_ARGS__) result = 1;
	
	size_t frameSize = (size_t)options.sampleSize * options.channels;
	uint8_t* pipeBuffer = nullptr;
	if (result == 0)
	{
		DEBUG("allocating %zu byte pipe buffer\n", frameSize);
		pipeBuffer = (uint8_t*)malloc(frameSize);	// staging area for incoming sound data from stdin
		if (pipeBuffer == nullptr)
		{
			FATAL("could not allocate memory for pipe buffer\n");
		}
	}
	
	ring_buffer_size_t ringBufferSize = nextPowerOfTwo((unsigned long)options.framesPerBuffer);	// PA's ring buffer needs to have a power-of-two number of elements
	size_t sampleBufferSize = (size_t)ringBufferSize * frameSize;
	void* sampleBuffer = nullptr;
	if (result == 0)
	{
		DEBUG("allocating %d frame (%zu byte) ring buffer\n", ringBufferSize, sampleBufferSize);
		sampleBuffer = malloc((long)sampleBufferSize);
		if (sampleBuffer == nullptr)
		{
			FATAL("could not allocate memory for ring buffer\n");
		}
	}
	
	PaError error = paNoError;
	CallbackData callbackData = {0};
	if (result == 0)
	{
		PaUtil_InitializeRingBuffer(&callbackData.ringBuffer, frameSize, ringBufferSize, sampleBuffer);
		if (options.sampleFormat == paUInt8)
		{
			// center is zero for all formats except unsigned 8-bit, where it is 128
			callbackData.silenceByte = 0x80;
		}
		
		DEBUG("initializing PortAudio\n");
		error = Pa_Initialize();
		if (error != paNoError)
		{
			FATAL("could not initialize PortAudio: %s\n", Pa_GetErrorText(error));
		}
	}
	
	PaStreamParameters outputParams = {0};
	if (result == 0)
	{
		DEBUG("getting default output device\n");
		outputParams.device = Pa_GetDefaultOutputDevice();
		if (outputParams.device == paNoDevice)
		{
			FATAL("could not detect default output device\n");
		}
		INFO("default output device is %d\n", outputParams.device);
	}
	
	PaStream* stream = nullptr;
	if (result == 0)
	{
		outputParams.channelCount = options.channels;
		outputParams.sampleFormat = options.sampleFormat;
		outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
		
		DEBUG("opening %d-channel %s %zu-bit %s %gHz stream with buffer size %zu frames (%zu bytes), flags %lu\n",
			outputParams.channelCount,
			options.sampleFormat == paUInt8 ? "unsigned" : "signed",
			options.sampleSize * CHAR_BIT,
			options.sampleFormat == paFloat32 ? "float" : "integer",
			options.sampleRate,
			options.framesPerBuffer,
			options.framesPerBuffer * frameSize,
			options.streamFlags
		);
		error = Pa_OpenStream(
			&stream,
			nullptr,
			&outputParams,
			options.sampleRate,
			options.framesPerBuffer,
			options.streamFlags,
			streamCallback,
			&callbackData
		);
		if (error != paNoError)
		{
			FATAL("could not open stream: %s\n", Pa_GetErrorText(error));
		}
	}
	
	if (result == 0)
	{
		DEBUG("starting stream: Hope you hear a pop.\n");
		error = Pa_StartStream(stream);
		if (error != paNoError)
		{
			FATAL("could not start stream: %s\n", Pa_GetErrorText(error));
		}
	}
	
	if (result == 0)
	{
		std::chrono::duration<double> frameTime(options.framesPerBuffer / options.sampleRate);
		std::chrono::duration<double> sleepTime(frameTime / 2.0);
		std::chrono::duration<double> timeout(options.timeout);
		
		std::chrono::time_point<std::chrono::high_resolution_clock> now = std::chrono::high_resolution_clock::now();
		std::chrono::time_point<std::chrono::high_resolution_clock> then = now;
		
		DEBUG("entering main loop with a sleep time of %lldms and timeout of %gs\n",
			std::chrono::duration_cast<std::chrono::milliseconds>(sleepTime).count(),
			timeout.count());
		
		size_t byteIndex = 0;
		bool stdinOpen = true;
		while (stdinOpen && (now - then) < timeout && (error = Pa_IsStreamActive(stream)) == 1)
		{
			bool canGetByte = true;
			ring_buffer_size_t framesAvailable = PaUtil_GetRingBufferWriteAvailable(&callbackData.ringBuffer);
			if (framesAvailable == callbackData.ringBuffer.bufferSize)
			{
				WARN("ring buffer starved!\n");
			}
			while (canGetByte && framesAvailable > 0 && byteIndex < frameSize)
			{
				switch (stdinReady())
				{
					case -1:
						FATAL("error when checking input pipe\n");
						break;
					case 0:
						WARN("can't get bytes anymore\n");
						canGetByte = false;
						break;
					default:
						stdinOpen = read(STDIN_FILENO, &pipeBuffer[byteIndex++], 1) > 0;	// stage our data, but also check for EOF
						if (byteIndex == frameSize)
						{
							PaUtil_WriteRingBuffer(&callbackData.ringBuffer, pipeBuffer, 1);	// because we can't read() directly into the ring buffer
							--framesAvailable;
							byteIndex = 0;
						}
						then = now;	// reset our timeout timer after we successfully read from stdin
						break;
				}
			}
			
			std::this_thread::sleep_for(sleepTime);
			now = std::chrono::high_resolution_clock::now();
		}
		
		if (stdinOpen && (now - then) < timeout)
		{
			INFO("timed out waiting for input pipe\n");
		}
		else if (!stdinOpen)
		{
			INFO("input pipe closed\n");
		}
		
		if (error < 0)
		{
			FATAL("stream unexpectedly stopped: %s\n", Pa_GetErrorText(error));
		}
	}
	
	if (stream != nullptr)
	{
		DEBUG("closing stream\n");
		error = Pa_CloseStream(stream);
		if (error != paNoError)
		{
			ERROR("could not close stream: %s\n", Pa_GetErrorText(error));
		}
	}
	
	DEBUG("terminating PortAudio\n");
	Pa_Terminate();
	
	if (sampleBuffer != nullptr)
	{
		DEBUG("freeing ring buffer\n");
		free(sampleBuffer);
	}
	
	if (pipeBuffer != nullptr)
	{
		DEBUG("freeing pipe buffer\n");
		free(pipeBuffer);
	}
	
	return result;
}
