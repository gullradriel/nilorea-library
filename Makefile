#
#       Makefile pour la librairie Nilorea
#

HAVE_OPENSSL=0
HAVE_ALLEGRO=0
RM=rm -f
CC=gcc
EXT=
VPATH=src
CFLAGS=
SRC= n_common.c n_config_file.c n_debug_mem.c n_exceptions.c n_debug_mem.c n_hash.c n_list.c n_log.c n_network.c n_network_msg.c n_nodup_log.c n_pcre.c n_stack.c n_str.c n_thread_pool.c n_time.c n_zlib.c n_user.c
OPT=-D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -fPIC -Og -g -ggdb3 -W -Wall -Wextra -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=1 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wstrict-overflow=5 -Wswitch-default -Wundef -std=c11 -Wl,-dead_strip
OUTPUT=libnilorea
LIB=-lnilorea
PROJECT_NAME= NILOREA_LIBRARY

ifeq ($(OS),Windows_NT)
	CFLAGS+= -I./include -std=c11 -D__USE_MINGW_ANSI_STDIO $(OPT)
	RM= del /Q
	CC= gcc
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
	UNAME_S:= $(shell uname -s)
	RM=rm -f
	CC=gcc
	EXT=.a
	HDR=$(SRC:%.c=%.h) nilorea.h
	OBJECTS=$(SRC:%.c=obj/%.o)
	ifeq ($(UNAME_S),Linux)
	CFLAGS+= -DLINUX -I./include/ $(OPT)
endif
ifeq ($(UNAME_S),SunOS)
	CC=cc
	CFLAGS+= -DSOLARIS -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -g -v -xc99 -I ./include/ -mt -lpcre
endif
obj/%.o: src/%.c
	$(CC) $(CFLAGS) $(BUILD_NUMBER_LDFLAGS) -c $< -o $@
endif

ifeq "$(shell printf '\#include <allegr5/allegro.h>\nint main(){return 0;}' | $(CC) -x c -Wall -O -o /dev/null > /dev/null 2> /dev/null - && echo $$? )" "0"
  HAVE_ALLEGRO=1
else
  HAVE_ALLEGRO=0
endif

ifeq ($(HAVE_ALLEGRO),1)
SRC+= n_3d.c n_anim.c n_particles.c
endif

buildnum=$(shell grep BUILD_NUMBER  build-number.txt | awk '{print $$3}')
minorversion=$(shell grep MINOR_VERSION build-number.txt | awk '{print $$3}')
majorversion=$(shell grep MAJOR_VERSION build-number.txt | awk '{print $$3}')

default: all
release: all
debug: all
all: main

#show_version:
#@echo "Version:`expr $(shell $(majorversion)) `.`expr $(shell $(minorversion)) `.`expr $(shell $(buildnum)) `"

doc:
	doxygen Doxyfile

#buildnumber:
#	@echo -n "    #define BUILD_NUMBER  " > build-number.new
#	@echo `expr $(shell $(buildnum)) + 1` >> build-number.new
#	@echo "    #define MINOR_VERSION $(shell $(minorversion))" >> build-number.new
#	@echo "    #define MAJOR_VERSION $(shell $(majorversion))" >> build-number.new
#	@mv build-number.new build-number.txt
#	@echo "#ifndef $(PROJECT_NAME)_VERSION" > version.h
#	@echo "#define $(PROJECT_NAME)_VERSION" >> version.h
#	@cat build-number.txt >> version.h
#	@echo "#endif" >> version.h
#	git tag "$(shell $(majorversion)).$(shell $(minorversion)).$(shell $(buildnum))"
#
#incminor:
#	@echo "    #define BUILD_NUMBER 0" > build-number.new
#	@echo -n "    #define MINOR_VERSION " >> build-number.new
#	@echo `expr $(shell $(minorversion)) + 1` >> build-number.new
#	@echo "    #define MAJOR_VERSION $(shell $(majorversion))" >> build-number.new
#	@mv build-number.new build-number.txt
#
#incmajor:
#	@echo "    #define BUILD_NUMBER 0" > build-number.new
#	@echo "    #define MINOR_VERSION $(shell $(minorversion))" >> build-number.new
#	@echo -n "    #define MAJOR_VERSION " >> build-number.new
#	@echo `expr $(shell $(majorversion)) + 1` >> build-number.new
#	@mv build-number.new build-number.txt

main: $(OBJECTS)
	@echo "HAVE_ALLEGRO: $(HAVE_ALLEGRO)"
	$(AR) rcs $(OUTPUT)$(EXT) $(OBJECTS)

clean:
	$(RM) $(OBJECTS)

distclean: clean
	$(RM) *.a
