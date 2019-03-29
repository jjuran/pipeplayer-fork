USR_LOCAL_LIB := /usr/local/lib

PORTAUDIO_GIT := https://git.assembla.com/portaudio.git
PORTAUDIO_CFG := ./configure --disable-mac-universal
PORTAUDIO_INC := /usr/include/portaudio.h
PORTAUDIO_SRC := portaudio/src/common
PORTAUDIO_LIB := portaudio/lib/.libs

DYLIB := libportaudio.2.dylib

LIB_SRC := $(PORTAUDIO_LIB)/$(DYLIB)
LIB_DST := $(USR_LOCAL_LIB)/$(DYLIB)

INC := -Iportaudio/include
LIBS := -L$(PORTAUDIO_LIB) -lportaudio

default: pipeplayer

portaudio/.git:
	(cd ..; test -d portaudio/ || git clone $(PORTAUDIO_GIT))
	test -d portaudio/ || ln -s ../portaudio

portaudio-configure : portaudio/.git
	test -f $(PORTAUDIO_INC) || (cd portaudio && $(PORTAUDIO_CFG))

portaudio-make : portaudio-configure
	test -f $(PORTAUDIO_INC) || (cd portaudio && make)


pa_ringbuffer.o : portaudio/.git $(PORTAUDIO_SRC)/pa_ringbuffer.c
	cc -c $(INC) -o pa_ringbuffer.o $(PORTAUDIO_SRC)/pa_ringbuffer.c

pipeplayer.o : pipeplayer.cpp
	c++ -c $(INC) -std=c++11 -o pipeplayer.o pipeplayer.cpp

pipeplayer : portaudio-make pa_ringbuffer.o pipeplayer.o
	c++ -o pipeplayer $(LIBS) pa_ringbuffer.o pipeplayer.o

install-lib : $(LIB_SRC)
	test -f $(LIB_SRC) -a \! -f $(LIB_DST) && install $(LIB_SRC) $(LIB_DST)
