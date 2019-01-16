RM=rm -f
CC=gcc

VPATH=../src/
CC=gcc
LIBNILOREA=-lnilorea
EXT=
ALLEGRO_CLIBS=
HAVE_ALLEGRO=1

ifeq ($(OS),Windows_NT)

	CFLAGS+= -I../include -g -W -Wall -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -std=gnu99 -ggdb3 -O0
    CLIBS= -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lws2_32  -L../.

    TARGET=ex_list$(EXT) ex_nstr$(EXT) ex_exceptions$(EXT) ex_hash$(EXT) ex_network$(EXT) ex_monolith$(EXT) ex_configfile$(EXT)

    ifeq ($(HAVE_ALLEGRO),1)
		#CLIBS=`pkg-config --cflags --libs allegro-5 allegro_acodec-5 allegro_audio-5 allegro_color-5 allegro_dialog-5 allegro_font-5 allegro_image-5 allegro_main-5 allegro_memfile-5 allegro_physfs-5 allegro_primitives-5 allegro_ttf-5`
		ALLEGRO_CLIBS=-lallegro_acodec -lallegro_audio -lallegro_color -lallegro_dialog -lallegro_image -lallegro_main -lallegro_memfile -lallegro_physfs -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro
		TARGET+=ex_gui_particles$(EXT)
	endif

    RM= del /Q
    CC= gcc

#ifeq ($(filter $(${MSYSTEM},MINGW32) $($(MSYSTEM),MINGW32),yes),)
ifeq ($(MSYSTEM),MINGW32)
RM=rm -f
CFLAGS+= -m32 -DARCH32BITS
LIBNILOREA=-lnilorea32
EXT=32.exe
endif
ifeq ($(MSYSTEM),MINGW32CB)
RM=del /Q
CFLAGS+= -m32 -DARCH32BITS
LIBNILOREA=-lnilorea32
EXT=32.exe
endif

#ifeq ($(filter $(${MSYSTEM},MINGW64) $($(MSYSTEM),MINGW64),yes),)
ifeq ($(MSYSTEM),MINGW64)
RM=rm -f
CFLAGS+= -DARCH64BITS
LIBNILOREA=-lnilorea64
EXT=64.exe
endif
ifeq ($(MSYSTEM),MINGW64CB)
RM=del /Q
CFLAGS+= -DARCH64BITS
LIBNILOREA=-lnilorea64
EXT=64.exe
endif



else
	UNAME_S= $(shell uname -s)
	RM=rm -f
	CC=gcc
	EXT=
    ifeq ($(UNAME_S),Linux)
        CFLAGS+= -I../include -g -W -Wall -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -std=gnu99 -ggdb3 -O0
        CLIBS=-L../. -lpthread
    endif
    ifeq ($(UNAME_S),SunOS)
        CC=cc
        CFLAGS+= -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -g -v -xc99 -I ../include/ -mt
        CLIBS=-lm -lsocket -lnsl -lpthread -lrt -L..
	LIBNILOREA=-lnilorea
	EXT=
    endif

TARGET=ex_list$(EXT) ex_nstr$(EXT) ex_exceptions$(EXT) ex_hash$(EXT) ex_network$(EXT) ex_monolith$(EXT) ex_configfile$(EXT) ex_signals$(EXT)

endif

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
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. -Bstatic $(LIBNILOREA) -Bdynamic $(CLIBS) $(ALLEGRO_CLIBS)

ex_configfile$(EXT): n_log.o n_list.o n_str.o n_hash.o n_config_file.o ex_configfile.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_signals$(EXT): n_log.o n_list.o n_str.o n_hash.o n_signals.o ex_signals.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_gui_particles$(EXT): ex_gui_particles.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. -Bstatic $(LIBNILOREA) -Bdynamic $(CLIBS) $(ALLEGRO_CLIBS)

clean:
	$(RM) *.o
	$(RM) $(TARGET)
