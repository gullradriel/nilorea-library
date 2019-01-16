#
#       Makefile pour la librairie Nilorea
#
ALLEGRO=1

RM=rm -f
CC=gcc
EXT=
VPATH=src
CFLAGS=
SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_thread_pool.c n_network.c n_network_msg.c n_3d.c
ifeq ($(ALLEGRO),1)
SRC+= n_resources.c n_particles.c n_gui.c
endif
LIBNAME= libnilorea

ifeq ($(OS),Windows_NT)
	CFLAGS+= -I./include -g -W -Wall -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -std=gnu99 -ggdb3 -O0
	RM= del /Q
	CC= gcc

#ifeq ($(filter $(${MSYSTEM},MINGW32) $($(MSYSTEM),MINGW32),yes),)
ifeq ($(MSYSTEM),MINGW32)
RM=rm -f
CFLAGS+= -m32 -DARCH32BITS
LIBNILOREA=-lnilorea32
EXT=32.a
endif
ifeq ($(MSYSTEM),MINGW32CB)
RM=del /Q
CFLAGS+= -m32 -DARCH32BITS
LIBNILOREA=-lnilorea32
EXT=32.a
endif

#ifeq ($(filter $(${MSYSTEM},MINGW64) $($(MSYSTEM),MINGW64),yes),)
ifeq ($(MSYSTEM),MINGW64)
RM=rm -f
CFLAGS+= -DARCH64BITS
LIBNILOREA=-lnilorea64
EXT=64.a
endif
ifeq ($(MSYSTEM),MINGW64CB)
RM=del /Q
CFLAGS+= -DARCH64BITS
LIBNILOREA=-lnilorea64
EXT=64.a
endif

HDR=$(SRC:%.c=%.h) nilorea.h
OBJECTS=$(SRC:%.c=obj\\%.o)
obj\\%.o: src\%.c
	$(CC) $(CFLAGS) -c $< -o $@

else

	UNAME_S := $(shell uname -s)
	RM=rm -f
	CC=gcc
	EXT=
	SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_thread_pool.c n_network.c n_network_msg.c n_signals.c
	HDR=$(SRC:%.c=%.h) nilorea.h
	OBJECTS=$(SRC:%.c=obj/%.o)

ifeq ($(UNAME_S),Linux)
	CFLAGS+= -I./include/ -g -W -Wall -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -std=gnu99 -ggdb3 -O0
endif

ifeq ($(UNAME_S),SunOS)
	CC=cc
	CFLAGS+= -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -g -v -xc99 -I ./include/ -mt
endif

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

endif

all: main

release: main

main: $(OBJECTS)
	$(AR) -r $(LIBNAME)$(EXT) $(OBJECTS)

clean:
	$(RM) $(OBJECTS)

distclean: clean
	$(RM) *.a

