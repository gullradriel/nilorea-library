#
#       Makefile pour la librairie Nilorea
#



ALLEGRO=0

#OPT=-std=c17 -Og -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -g -ggdb3 -pedantic -W -Wall -Wextra -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=2 -Winit-self -Wlogical-op -Wmissing-declarations -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wsign-conversion -Wstrict-overflow=5 -Wswitch-default -Wundef -Wno-unused
OPT=-D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -Og -g -ggdb3 -W -Wall -Wextra -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=1 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wstrict-overflow=5 -Wswitch-default -Wundef -Wno-unused -std=c11

RM=rm -f
CC=gcc
EXT=
VPATH=src
CFLAGS=
SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_config_file.c n_thread_pool.c n_network.c n_network_msg.c n_3d.c
ifeq ($(ALLEGRO),1)
SRC+= n_resources.c n_particles.c n_gui.c
endif
OUTPUT=libnilorea
LIB=-lnilorea
PROJECT_NAME= NILOREA_LIBRARY

ifeq ($(OS),Windows_NT)
	CFLAGS+= -I./include -std=c11 -D__USE_MINGW_ANSI_STDIO $(OPT)
	RM= del /Q
	CC= gcc

#ifeq ($(filter $(${MSYSTEM},MINGW32) $($(MSYSTEM),MINGW32),yes),)
ifeq ($(MSYSTEM),MINGW32)
RM=rm -f
CFLAGS+= -m32 -DARCH32BITS
LIB=-lnilorea32
EXT=32.a
endif
ifeq ($(MSYSTEM),MINGW32CB)
RM=del /Q
CFLAGS+= -m32 -DARCH32BITS
LIB=-lnilorea32
EXT=32.a
endif

#ifeq ($(filter $(${MSYSTEM},MINGW64) $($(MSYSTEM),MINGW64),yes),)
ifeq ($(MSYSTEM),MINGW64)
RM=rm -f
CFLAGS+= -DARCH64BITS
LIB=-lnilorea64
EXT=64.a
endif
ifeq ($(MSYSTEM),MINGW64CB)
RM=del /Q
CFLAGS+= -DARCH64BITS
LIB=-lnilorea64
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
	EXT=.a
	SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_thread_pool.c n_network.c n_network_msg.c n_signals.c
	HDR=$(SRC:%.c=%.h) nilorea.h
	OBJECTS=$(SRC:%.c=obj/%.o)

ifeq ($(UNAME_S),Linux)
	CFLAGS+= -I./include/ $(OPT)
endif

ifeq ($(UNAME_S),SunOS)
	CC=cc
	CFLAGS+= -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -g -v -xc99 -I ./include/ -mt
endif

obj/%.o: src/%.c
	$(CC) $(CFLAGS) $(BUILD_NUMBER_LDFLAGS) -c $< -o $@

endif

#buildnum=(shell grep BUILD_NUMBER  build-number.txt | awk '{print $$3}')
#minorversion=(shell grep MINOR_VERSION build-number.txt | awk '{print $$3}')
#majorversion=(shell grep MAJOR_VERSION build-number.txt | awk '{print $$3}')

default: all
release: all
debug: all
all: main

#buildnumber:
#	@echo -n "    #define BUILD_NUMBER  " > build-number.new
#	@echo `expr $(shell $(buildnum)) + 1` >> build-number.new
#	@echo "    #define MINOR_VERSION $(shell $(minorversion))" >> build-number.new
#	@echo "    #define MAJOR_VERSION $(shell $(majorversion))" >> build-number.new
#	@mv build-number.new build-number.txt
#	@echo "#ifndef $(PROJECT_NAME)_VERSION" > version.h
#	@echo "    #define $(PROJECT_NAME)_VERSION" >> version.h
#	@cat build-number.txt >> version.h
#	@echo "#endif" >> version.h
#	git tag "$(shell $(majorversion)).$(shell $(minorversion)).$(shell $(buildnum))"

#minor:
#	@echo "    #define BUILD_NUMBER 0" > build-number.new
#	@echo -n "    #define MINOR_VERSION " >> build-number.new
#	@echo `expr $(shell $(minorversion)) + 1` >> build-number.new
#	@echo "    #define MAJOR_VERSION $(shell $(majorversion))" >> build-number.new
#	@mv build-number.new build-number.txt

#major:
#	@echo "    #define BUILD_NUMBER 0" > build-number.new
#	@echo "    #define MINOR_VERSION $(shell $(minorversion))" >> build-number.new
#	@echo -n "    #define MAJOR_VERSION " >> build-number.new
#	@echo `expr $(shell $(majorversion)) + 1` >> build-number.new
#	@mv build-number.new build-number.txt

#incmajor: major

#incminor: minor

#all: buildnumber main

main: $(OBJECTS)
	$(AR) -r $(OUTPUT)$(EXT) $(OBJECTS)
#@echo Version:$(shell $(majorversion)).$(shell $(minorversion)).$(shell $(buildnum))

clean:
	$(RM) $(OBJECTS)

distclean: clean
	$(RM) *.a


