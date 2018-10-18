#
#       Makefile pour la librairie Nilorea
#
RM=

CC=gcc
EXT=
VPATH=../src/

CFLAGS=-DNOALLEGRO 

ifeq ($(OS),Windows_NT)
    CFLAGS+=-DWINDOWS -DNOSYSJRNL -D_GNU_SOURCE -DNOSYSJRNL -g -W -Wall -std=gnu99 -ggdb3 -O0 -I.\include \
	   -Wno-missing-braces \
	   -Wextra \
	   -Wno-missing-field-initializers \
	   -Wswitch-default \
	   -Wswitch-enum \
	   -Wcast-align \
	   -Wpointer-arith \
	   -Wbad-function-cast \
	   -Wstrict-overflow=5 \
	   -Wstrict-prototypes \
	   -Winline \
	   -Wundef \
	   -Wnested-externs \
	   -Wcast-qual \
	   -Wunreachable-code \
	   -Wlogical-op \
	   -Wstrict-aliasing=2 \
	   -Wredundant-decls \
	   -Wold-style-definition \
	   -Werror \
	   -fno-omit-frame-pointer \
	   -ffloat-store #	   	   -Wshadow 
		CLIBS= -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lws2_32 -L../.
	RM=del /Q
	EXT=.exe
else
	UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CFLAGS+= -DLINUX -D_GNU_SOURCE -I./include/ -I/home/ace/include/ \
        -g -W -Wall -std=gnu99 -ggdb3 -O0 \
        -Wno-missing-braces \
        -Wextra \
        -Wno-missing-field-initializers \
        -Wswitch-default \
        -Wswitch-enum \
        -Wcast-align \
        -Wpointer-arith \
        -Wbad-function-cast \
        -Wstrict-overflow=5 \
        -Wstrict-prototypes \
        -Winline \
        -Wundef \
        -Wnested-externs \
        -Wcast-qual \
        -Wunreachable-code \
        -Wlogical-op \
        -Wstrict-aliasing=2 \
        -Wredundant-decls \
        -Wold-style-definition \
        -Werror \
        -fno-omit-frame-pointer \
        -ffloat-store #	   	   -Wshadow 
    endif
    ifeq ($(UNAME_S),SunOS)
        CC=cc
        CFLAGS+= -DSOLARIS -g -v -xc99 -DSOLARIS -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -I ./include/ -I/Home/aceb/include/
    endif
	RM=rm -f
	EXT=
endif


VPATH=src/
#CFLAGS += -D_GNU_SOURCE -DNOSYSJRNL -DNOALLEGRO -DLINUX -W -Wall -std=gnu99 -g -Iinclude/ `pkg-config --cflags --libs allegro-5 allegro_acodec-5 allegro_audio-5 allegro_color-5 allegro_dialog-5 allegro_font-5 allegro_image-5 allegro_main-5 allegro_memfile-5 allegro_physfs-5 allegro_primitives-5 allegro_ttf-5`
#CFLAGS += -D_GNU_SOURCE -DNOSYSJRNL -DNOALLEGRO -W -Wall -std=gnu99 -g -Iinclude/ -DWINDOWS


LIBNAME= libnilorea.a
LIBNAME_D= libnilorea_d.a

COPY=cp
RM=rm

#SRC= n_common.c n_time.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_thread_pool.c n_network.c n_network_msg.c n_3d.c n_iso_engine.c lexmenu.c n_games.c n_particles.c
SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_thread_pool.c n_network.c n_network_msg.c


OBJECTS=$(SRC:%.c=obj/%.o) 

HDR=$(SRC:%.c=%.h) nilorea.h 

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

main: $(OBJECTS)
	$(AR) -r $(LIBNAME) $(OBJECTS)

debug:
	LIBNAME=$(LIBNAME_D)
	CFLAGS= "-DDEBUGMODE -g" $(MAKE)

clean:
	$(RM) $(OBJECTS)
	
distclean: clean
	$(RM) *.a
