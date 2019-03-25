LIBS := -lportaudio

PORTAUDIO_SRC := portaudio/src/common

default: pipeplayer

pa_ringbuffer.o : $(PORTAUDIO_SRC)/pa_ringbuffer.c
	c++ -c -o pa_ringbuffer.o $(PORTAUDIO_SRC)/pa_ringbuffer.c

pipeplayer.o : pipeplayer.cpp
	c++ -c -std=c++11 -o pipeplayer.o pipeplayer.cpp

pipeplayer : pa_ringbuffer.o pipeplayer.o
	c++ -o pipeplayer $(LIBS) pa_ringbuffer.o pipeplayer.o
