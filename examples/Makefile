dir_name=
RM=

CC=gcc
EXT=
VPATH=../src/

CFLAGS=-DNOALLEGRO -DNETWORK_IPV4

ifeq ($(OS),Windows_NT)
    CFLAGS+=-DWINDOWS -DNOSYSJRNL \
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
	CLIBS= -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lws2_32 -L../. 
	dir_name=$(shell date +%Y_%m_%d_%HH%MM%SS)
	RM=del /Q
	EXT=.exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        CFLAGS+= -DLINUX -DNOEVENTLOG -D_GNU_SOURCE -I../include/ -I/home/ace/include/ \
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
CFLAGS+= -DSOLARIS -g -v -xc99 -DSOLARIS -DNOEVENTLOG -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -I ../include/ -L../
        CLIBS=-lm -lsocket -lnsl
    endif
	dir_name=$(shell date +%Y_%m_%d_%HH%MM%SS)
	RM=rm -f
	EXT=
endif

TARGET=ex_list$(EXT) ex_nstr$(EXT) ex_exceptions$(EXT) ex_hash$(EXT) ex_network$(EXT) ex_monolith$(EXT)

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<
	
all: $(TARGET)

ex_nstr$(EXT): n_log.o n_list.o n_str.o ex_nstr.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)
	
ex_exceptions$(EXT): n_log.o n_exceptions.o ex_exceptions.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_list$(EXT): n_log.o n_list.o n_str.o ex_list.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_hash$(EXT): n_log.o n_list.o n_str.o n_hash.o ex_hash.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)
	
ex_network$(EXT): n_log.o n_list.o n_str.o n_network_msg.o n_time.o n_thread_pool.c n_network.o ex_network.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)  

ex_monolith$(EXT): ex_monolith.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lnilorea $(CLIBS) 
	
clean:
	$(RM) -f *.o

backup:
	mkdir -p "VERSION/$(dir_name)/examples"
	cp Makefile *.sh *.c *.h VERSION/$(dir_name)/examples 
	sleep 1
install:
	
