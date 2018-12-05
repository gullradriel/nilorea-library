#
#       Makefile pour la librairie Nilorea
#
RM=
CC=
EXT=
VPATH=src/

LIBNAME= libnilorea.a
LIBNAME_D= libnilorea_d.a

CFLAGS=

ifeq ($(OS),Windows_NT)
	CFLAGS+=-DNOSYSJRNL -I.\include \
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
-ffloat-store #                 -Wshadow
	CLIBS= -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lws2_32 -L../.
	RM=del /Q
	CC=gcc
	EXT=.exe
	SRC= n_common.c n_log.c n_exceptions.c n_str.c n_list.c n_hash.c n_time.c n_thread_pool.c n_network.c n_network_msg.c
	HDR=$(SRC:%.c=%.h) nilorea.h
	OBJECTS=$(SRC:%.c=obj\\%.o)
obj\\%.o: src\\%.c
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
		CFLAGS+= -DLINUX -DNOEVENTLOG -I./include/ \
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
-ffloat-store #            -Wshadow
    endif
	
    ifeq ($(UNAME_S),SunOS)
        CC=cc
        CFLAGS+= -DSOLARIS -DDNOEVENTLOG -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -g -v -xc99 -I ./include/ -mt
    endif

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

endif

main: $(OBJECTS)
	$(AR) -r $(LIBNAME) $(OBJECTS)

debug:
	LIBNAME=$(LIBNAME_D)
	CFLAGS= "-DDEBUGMODE -g" $(MAKE)

clean:
	$(RM) $(OBJECTS)

distclean: clean
	$(RM) *.a

