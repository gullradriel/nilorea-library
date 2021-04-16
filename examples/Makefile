HAVE_ALLEGRO=0
HAVE_OPENSSL=0

RM=rm -f
OPT=-D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED -O0 -g -W -Wall -Wextra -Wcast-align -Wcast-qual -Wdisabled-optimization -Wformat=1 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wstrict-overflow=5 -Wswitch-default -Wundef -Wno-unused
OPT+= -I../include
VPATH=../src/
CC=gcc
LIBNILOREA=-lnilorea
EXT=
ALLEGRO_CLIBS=
DEBUGLIB=
DISABLED_TARGET=ex_monolith$(EXT)
TARGET=ex_list$(EXT) ex_nstr$(EXT) ex_exceptions$(EXT) ex_hash$(EXT) ex_network$(EXT) ex_configfile$(EXT) ex_pcre$(EXT) ex_threads$(EXT) ex_log$(EXT) ex_common$(EXT) ex_signals$(EXT) ex_debug_mem$(EXT)
CLIBS=


ifeq ($(OS),Windows_NT)
	CFLAGS+= -std=c11 $(OPT) -D__USE_MINGW_ANSI_STDIO
	CLIBS= -lpthread -lws2_32 -lm
	RM= del /Q
	CC= gcc
	DEBUGLIB=-limagehlp

ifeq ($(MSYSTEM),MINGW32)
	RM=rm -f
	CFLAGS+= -m32 -DARCH32BITS
	LIBNILOREA=-lnilorea32.a
	EXT=32.exe
endif
ifeq ($(MSYSTEM),MINGW32CB)
	RM=del /Q
	CFLAGS+= -m32 -DARCH32BITS
	LIBNILOREA=-lnilorea32
	EXT=32.exe
endif

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
	CFLAGS+= -DLINUX $(OPT) -std=gnu99 -rdynamic
	CLIBS=-lpthread -lm
endif
ifeq ($(UNAME_S),SunOS)
	CC=cc
	CFLAGS+= -DSOLARIS -g -v -xc99 -I ../include/ -mt  
	CLIBS=-lm -lsocket -lnsl -lpcre -lrt 
	LIBNILOREA=-lnilorea
	EXT=
endif
endif


ifeq ($(HAVE_ALLEGRO),1)
	#CLIBS=`pkg-config --cflags --libs allegro-5 allegro_acodec-5 allegro_audio-5 allegro_color-5 allegro_dialog-5 allegro_font-5 allegro_image-5 allegro_main-5 allegro_memfile-5 allegro_physfs-5 allegro_primitives-5 allegro_ttf-5`
	#ALLEGRO_CLIBS=`pkg-config --cflags --libs allegro-5 allegro_acodec-5 allegro_audio-5 allegro_color-5 allegro_dialog-5 allegro_font-5 allegro_image-5 allegro_main-5 allegro_memfile-5 allegro_physfs-5 allegro_primitives-5 allegro_ttf-5` -lGL
	#ALLEGRO_CLIBS=-lallegro_acodec -lallegro_audio -lallegro_color -lallegro_dialog -lallegro_image -lallegro_main -lallegro_memfile -lallegro_physfs -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro
	ALLEGRO_CLIBS=-lallegro_acodec -lallegro_audio -lallegro_color -lallegro_image -lallegro_main -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro
	TARGET+=ex_gui_particles$(EXT) ex_gui_netclient$(EXT) ex_gui_netserver$(EXT)
endif


#CFLAGS+= -ffunction-sections -fdata-sections -Wl,--gc-sections

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

all: $(TARGET)

ex_common$(EXT): n_log.o n_list.o n_str.o n_common.o ex_common.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_nstr$(EXT): n_log.o n_list.o n_str.o ex_nstr.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_exceptions$(EXT): n_log.o n_exceptions.o ex_exceptions.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_list$(EXT): n_log.o n_list.o n_str.o ex_list.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_hash$(EXT): n_log.o n_list.o n_str.o n_hash.o ex_hash.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_network$(EXT): n_log.o n_list.o n_str.o n_network_msg.o n_time.o n_thread_pool.o n_hash.o n_network.o ex_network.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_monolith$(EXT): ex_monolith.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. $(LIBNILOREA) $(CLIBS) $(ALLEGRO_CLIBS)

ex_configfile$(EXT): n_log.o n_list.o n_str.o n_hash.o n_pcre.o n_config_file.o ex_configfile.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS) -lpcre

ex_signals$(EXT): n_log.o n_list.o n_str.o n_common.o n_hash.o n_signals.o ex_signals.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS) $(DEBUGLIB)

ex_gui_particles$(EXT): n_log.o n_list.o n_str.o n_common.o n_hash.o n_time.o n_particles.o n_3d.o ex_gui_particles.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS)

ex_gui_netclient$(EXT): n_log.o n_list.o n_str.o n_common.o n_hash.o n_time.o n_network.o n_network_msg.o n_allegro5.o ex_gui_netclient.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS)

ex_gui_netserver$(EXT): n_log.o n_list.o n_str.o n_common.o n_hash.o n_time.o n_thread_pool.o n_network.o n_network_msg.o n_allegro5.o n_user.o ex_gui_netserver.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS)
ex_pcre$(EXT): n_log.o n_pcre.o ex_pcre.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS) -lpcre

ex_threads$(EXT): n_log.o n_list.o n_time.o n_thread_pool.o ex_threads.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_log$(EXT): n_str.o n_hash.o n_log.o n_nodup_log.o n_list.o n_time.o ex_log.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

ex_debug_mem$(EXT): n_str.o n_hash.o n_log.o n_nodup_log.o n_list.o n_time.o n_common.o n_debug_mem.o ex_debug_mem.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(CLIBS)

clean:
	$(RM) *.o
	$(RM) $(TARGET)
