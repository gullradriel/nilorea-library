#
#       Makefile for the Nilorea Library
#

.PHONY: clean clean-tmp examples default all release debug main show-detected-option doc copy-libs download-and-build download-dep distclean


ifeq ($(FORCE_NO_ALLEGRO),)
    FORCE_NO_ALLEGRO=0
endif
ifeq ($(FORCE_NO_OPENSSL),)
    FORCE_NO_OPENSSL=0
endif
ifeq ($(FORCE_NO_KAFKA),)
    FORCE_NO_KAFKA=0
endif

DEBUGLIB=

HAVE_OPENSSL=0
HAVE_ALLEGRO=0
HAVE_KAFKA=0

KAFKA_CFLAGS=
KAFKA_CLIBS=

OPENSSL_CLIBS=

CJSON_CFLAGS=

ALLEGRO_CFLAGS=
ALLEGRO_CLIBS=

NUKLEAR_CFLAGS=

NILOREA_CFLAGS=-I$(shell realpath ./include) -I$(shell realpath ./src)

RM=rm -f
CC=gcc
EXT=
LIB_STATIC_EXT=
LIB_DYN_EXT=
TMP_OBJ=_tmp_test.obj

CFLAGS=-g -W -Wall -Wextra -fPIC -O3 -fstack-protector \
       -D_FORTIFY_SOURCE=1 -D_REENTRANT -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENTED \
       -static-libgcc -static-libstdc++

SRC=n_common.c n_base64.c n_crypto.c n_config_file.c n_exceptions.c n_hash.c n_list.c n_log.c n_network.c n_network_msg.c n_nodup_log.c n_pcre.c n_stack.c n_str.c n_thread_pool.c n_time.c n_zlib.c n_user.c n_files.c n_aabb.c n_trees.c

OUTPUT=libnilorea
LIB=-lnilorea
PROJECT_NAME= NILOREA_LIBRARY

# OS specific flags
ifeq ($(OS),Windows_NT)
    FORCE_NO_KAFKA=1
    CFLAGS+= -std=c17 -D__USE_MINGW_ANSI_STDIO
    RM=rm -f
    CC= gcc
    CLIBS= -lpthread -lws2_32 -lm -lpcre -lz
    DEBUGLIB=-limagehlp
    ifeq ($(MSYSTEM),MINGW32)
        RM=rm -f
        CFLAGS+= -m32 -DARCH32BITS
        LIB=-lnilorea32
        EXT=32.exe
        LIB_STATIC_EXT=32.a
        LIB_DYN_EXT=32.dll
    endif
    ifeq ($(MSYSTEM),MINGW64)
        RM=rm -f
        CFLAGS+= -DARCH64BITS
        LIB=-lnilorea64
        EXT=64.exe
        LIB_STATIC_EXT=64.a
        LIB_DYN_EXT=64.dll
    endif
    HDR=$(SRC:%.c=%.h) nilorea.h
    OBJECTS=$(SRC:%.c=obj/%.o)
else
    UNAME_S:= $(shell uname -s)
    RM=rm -f
    CC=gcc
    EXT=
    LIB_STATIC_EXT=.a
    LIB_DYN_EXT=.so
    HDR=$(SRC:%.c=%.h) nilorea.h
    OBJECTS=$(SRC:%.c=obj/%.o)
    ifeq ($(UNAME_S),Linux)
        LDFLAGS+= -z noexecstack
        CFLAGS+= -std=c17 
        CLIBS=-lpthread -lm -lpcre -lz
    endif
    ifeq ($(UNAME_S),SunOS)
        CC=gcc
        CFLAGS+= -m64 
        CLIBS=-lm -lsocket -lnsl -lpcre -lrt -lz
    endif
endif

# detect available deps
ifeq "$(shell $(CC) -c test_allegro5.c -Wall -o $(TMP_OBJ) 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_ALLEGRO=1
else
    HAVE_ALLEGRO=0
endif
KAFKA_CFLAGS=-I$(shell realpath ./external/librdkafka/src)
ifeq "$(shell $(CC) $(KAFKA_CFLAGS) -c test_kafka.c -Wall -o $(TMP_OBJ) 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_KAFKA=1
else
    HAVE_KAFKA=0
    KAFKA_CFLAGS=
endif
ifeq "$(shell $(CC) $(KAFKA_CFLAGS) -c test_openssl.c -Wall -o $(TMP_OBJ) 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_OPENSSL=1
else
    HAVE_OPENSSL=0
endif

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

examples/%.o: examples/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# $ \ is for concatenation without a new line
EXAMPLES=examples/ex_base64_encode$(EXT) $\
         examples/ex_crypto$(EXT) $\
         examples/ex_list$(EXT) $\
         examples/ex_nstr$(EXT) $\
         examples/ex_exceptions$(EXT) $\
         examples/ex_hash$(EXT) $\
         examples/ex_network$(EXT) $\
         examples/ex_configfile$(EXT) $\
         examples/ex_pcre$(EXT) $\
         examples/ex_threads$(EXT) $\
         examples/ex_log$(EXT) $\
         examples/ex_common$(EXT) $\
         examples/ex_stack$(EXT) $\
         examples/ex_trees$(EXT) $\
         examples/ex_signals$(EXT)

ifeq ($(HAVE_ALLEGRO),1) 
    ifeq ($(FORCE_NO_ALLEGRO),0)
        SRC+= n_3d.c n_anim.c n_particles.c n_gui.c
        EXAMPLES+= examples/ex_gui_particles$(EXT) $\
                  examples/ex_gui_dictionary$(EXT) $\
                  examples/ex_fluid$(EXT) $\
                  examples/ex_gui$(EXT)
        ALLEGRO_CLIBS=-lallegro_acodec -lallegro_audio -lallegro_color -lallegro_image -lallegro_main -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro
        ALLEGRO_CFLAGS=-DALLEGRO_UNSTABLE -DHAVE_ALLEGRO
        NUKLEAR_CFLAGS=-I$(shell realpath ./external/nuklear) -I$(shell realpath ./external/nuklear/demo/allegro5)
        CFLAGS+= -DHAVE_ALLEGRO
    endif
endif

ifeq ($(HAVE_KAFKA),1)
    ifeq ($(FORCE_NO_KAFKA),0)
        SRC+= n_kafka.c 
        EXAMPLES+= examples/ex_kafka$(EXT)
        KAFKA_CLIBS= -lcurl -lsasl2 -lssl -lcrypto -ldl -L$(shell realpath ./external/librdkafka/src) -lrdkafka
        CSON_CFLAGS=-I$(shell realpath ./external/cJSON)
        CFLAGS+= $(CJSON_CFLAGS) $(KAFKA_CFLAGS) -DHAVE_KAFKA
    else
        KAFKA_CFLAGS=
        KAFKA_CLIBS=
    endif
endif

ifeq ($(HAVE_OPENSSL),1)
    ifeq ($(FORCE_NO_OPENSSL),0)
        EXAMPLES+= examples/ex_network_ssl$(EXT)
        CFLAGS+= -DHAVE_OPENSSL
        OPENSSL_CLIBS= -lssl -lcrypto
    endif
endif

EXAMPLE_OBJ=$($EXEMPLES:%$(EXT)=%.o)

CFLAGS+= $(NILOREA_CFLAGS) 

default: main
release: all
debug: all

all: main examples

show-detected-option:
	@echo "HAVE_ALLEGRO: $(HAVE_ALLEGRO), HAVE_KAFKA: $(HAVE_KAFKA), HAVE_OPENSSL: $(HAVE_OPENSSL)"

doc:
	doxygen Doxyfile

copy-libs:
	-@find ./external -name '*.so*' -exec cp {} examples/ \; 2>/dev/null ; true
	-@cp -f libnilorea.so examples/ 2>/dev/null ; true
	-@cp -f ./external/cJSON/cJSON.c examples/ 2>/dev/null ; true
	-@cp -f ./external/cJSON/cJSON.h examples/ 2>/dev/null ; true

download-and-build:
	git submodule update --init --recursive
	git submodule update --remote --merge --recursive
	cd external/librdkafka && ./configure && make -j 8

download-dep: show-detected-option download-and-build copy-libs

clean-tmp:
	-@$(RM) $(TMP_OBJ) 2>/dev/null ; true

main: show-detected-option $(OUTPUT)$(LIB_STATIC_EXT) $(OUTPUT)$(LIB_DYN_EXT) clean-tmp

$(OUTPUT)$(LIB_STATIC_EXT): $(OBJECTS)
	$(AR) rcs $(OUTPUT)$(LIB_STATIC_EXT) $^

$(OUTPUT)$(LIB_DYN_EXT): $(OBJECTS)
	$(CC) ${LDFLAGS} -static-libgcc -static-libstdc++ -shared -o $(OUTPUT)$(LIB_DYN_EXT) $^ $(CLIBS) $(ALLEGRO_CLIBS) $(KAFKA_CLIBS) $(OPENSSL_CLIBS)

clean: clean-tmp
	$(RM) obj/*.o examples/*.o

examples: main $(EXAMPLES)

distclean: clean
	$(RM) *.a *.so *.dll
	$(RM) $(EXAMPLES) 
	cd examples && $(RM) *.a *.so *.dll

# examples rules
examples/ex_common$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o examples/ex_common.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_stack$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_stack.o examples/ex_stack.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_trees$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_trees.o examples/ex_trees.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_nstr$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o examples/ex_nstr.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_exceptions$(EXT): obj/n_log.o obj/n_exceptions.o examples/ex_exceptions.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_base64_encode$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_base64.o examples/ex_base64.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_crypto$(EXT): obj/n_common.o obj/n_base64.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_time.o obj/n_thread_pool.o obj/n_crypto.o examples/ex_crypto.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_list$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o examples/ex_list.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_hash$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o obj/n_pcre.o examples/ex_hash.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) -lpcre

examples/ex_network$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_pcre.o obj/n_network.o examples/ex_network.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) -lpcre $(OPENSSL_CLIBS)

examples/ex_network_ssl$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_pcre.o obj/n_network.o examples/ex_network_ssl.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) -lpcre $(OPENSSL_CLIBS)

examples/ex_monolith$(EXT): ex_monolith.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ -L.. $(LIBNILOREA) $(CLIBS) $(ALLEGRO_CLIBS) -lpcre -lz

examples/ex_configfile$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o obj/n_pcre.o obj/n_config_file.o examples/ex_configfile.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) -lpcre

examples/ex_signals$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_signals.o examples/ex_signals.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(DEBUGLIB) 

examples/ex_gui$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_gui.o examples/ex_gui.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS)

examples/ex_gui_dictionary$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_particles.o obj/n_3d.o obj/n_pcre.o obj/n_allegro5.o examples/ex_gui_dictionary.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS) -lpcre

examples/ex_gui_particles$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_particles.o obj/n_3d.o examples/ex_gui_particles.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS)

examples/ex_fluid$(EXT): obj/n_common.o obj/n_log.o obj/n_str.o obj/n_list.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_pcre.o obj/n_config_file.o examples/ex_fluid_config.o obj/n_fluids.o examples/ex_fluid.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ -L.. $(CLIBS) $(ALLEGRO_CLIBS) -lpcre

examples/ex_pcre$(EXT): obj/n_log.o obj/n_pcre.o examples/ex_pcre.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) -lpcre

examples/ex_threads$(EXT): obj/n_log.o obj/n_list.o obj/n_time.o obj/n_thread_pool.o examples/ex_threads.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_log$(EXT): obj/n_str.o obj/n_hash.o obj/n_log.o obj/n_nodup_log.o obj/n_list.o obj/n_time.o examples/ex_log.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

examples/ex_kafka$(EXT): obj/n_common.o obj/n_list.o obj/n_log.o obj/n_str.o obj/n_hash.o obj/n_signals.o obj/n_zlib.o obj/n_time.o obj/n_network.o obj/n_network_msg.o obj/n_thread_pool.o obj/n_pcre.o obj/n_config_file.o examples/cJSON.o obj/n_base64.o obj/n_kafka.o obj/n_files.o examples/ex_kafka.o
	$(CC) $(CFLAGS) $(KAFKA_CFLAGS) -o $@ $^ $(CLIBS) $(KAFKA_CLIBS)
