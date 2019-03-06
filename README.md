pipeplayer
==========

This is a small program that reads a raw audio stream from stdin, defined by optional command-line parameters, and plays it on the default output device using PortAudio. Available under the GPLv3; forks and branches are encouraged.

It's designed to work similarly to how one would pipe audio data to /dev/dsp on Linux, providing this functionality to other platforms like macOS and Windows.

Build Process
-------------

1. Download and extract PortAudio to a portaudio/ directory adjacent to the CMakeLists.txt file.
2. Create a build/ directory, also adjacent to the CMakeLists.txt file.
3. From the build/ directory, run `cmake .. && cmake --build .`

Have fun.

Keith Kaisershot

3-5-19
