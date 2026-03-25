#
#       Makefile for the Nilorea Library
#

.PHONY: clean default all release debug show-detected-option doc build-deps update-deps integrate-deps distclean examples main copy-libs

ifeq ($(FORCE_NO_ALLEGRO),)
    FORCE_NO_ALLEGRO=0
endif
ifeq ($(FORCE_NO_OPENSSL),)
    FORCE_NO_OPENSSL=0
endif
ifeq ($(FORCE_NO_KAFKA),)
    FORCE_NO_KAFKA=0
endif
ifeq ($(FORCE_NO_PCRE),)
    FORCE_NO_PCRE=0
endif
ifeq ($(FORCE_NO_CJSON),)
    FORCE_NO_CJSON=0
endif

DEBUGLIB=
HAVE_OPENSSL=0
HAVE_ALLEGRO=0
HAVE_KAFKA=0
HAVE_CJSON=0
HAVE_PCRE=0
KAFKA_CFLAGS=
KAFKA_CLIBS=
OPENSSL_CLIBS=
CJSON_CFLAGS=
PCRE_CLIBS=
ALLEGRO_CFLAGS=
ALLEGRO_CLIBS=
NILOREA_CFLAGS=-I$(shell readlink -f ./include) -I$(shell readlink -f ./src)
CJSON_OBJ=

AR ?= ar
RM=rm -f
CC=gcc
EXT=
LIB_STATIC_EXT=
LIB_DYN_EXT=
TMP_OBJ=_tmp_test.obj
UNAME_S:= $(shell uname -s)

#
# COMMON DEV and PROD flags
#

# Static std lib version
CFLAGS += -static-libgcc
# Enable warnings (-W), all warnings (-Wall), and extra warnings (-Wextra)
CFLAGS += -W -Wall -Wextra
# Enable thread-safe library calls
CFLAGS += -D_REENTRANT
# Use the C17 standard for compilation
CFLAGS += -std=gnu17
# Add checks to detect stack buffer overflows
CFLAGS += -fstack-protector
# Enable additional compile-time and runtime checks for unsafe functions
CFLAGS += -D_FORTIFY_SOURCE=2
# Enforce strict checks on format strings (e.g., printf)
CFLAGS += -Wformat=2
# Warn about possible null pointer dereferences
CFLAGS += -Wnull-dereference
# Mitigate stack clash attacks (stack memory collision)
CFLAGS += -fstack-clash-protection
# Ensures that the compiler generates frame pointers for function calls. This is crucial for producing accurate stack traces when a sanitizer detects an issue. Without this flag, some stack traces may be incomplete or misleading, especially when debugging complex programs
CFLAGS += -fno-omit-frame-pointer
# Use -fPIC for shared libraries (required for position-independent code)
FPIC_FLAG = -fPIC
# Use -fPIE for executables (position-independent code for PIE binaries)
FPIE_FLAG = -fPIE
# LDFLAGS for executables only (PIE for ASLR, link only needed libs)
EXE_LDFLAGS = -pie -Wl,--as-needed
# LDFLAGS for shared libraries (no -pie, use -shared instead)
LIB_LDFLAGS = -Wl,--as-needed

ifeq ($(DEBUG),1) 
    #
    # DEVELOPMENT FLAGS
    #
    # Debug information 
    CFLAGS += -g 
    # Optimize for speed without significantly increasing compile time
    CFLAGS += -O2
    # Detect signed integer overflows at runtime (may affect performance)
    CFLAGS += -ftrapv
    # Warn about implicit type conversions that could lead to bugs
    CFLAGS += -Wconversion
    # Warn if a local variable shadows another variable
    CFLAGS += -Wshadow
    ifeq ($(ALL_AS_ERROR),1)
        # Warning as errors
        CFLAGS += -Werror
    endif
    # OS specific flags
    ifneq ($(OS),Windows_NT)
        # Sanitizers are not supported on SunOS/Solaris
        ifneq ($(UNAME_S),SunOS)
            # Marks certain sections of memory as read-only after the program starts
            EXE_LDFLAGS+= -z relro
            LIB_LDFLAGS+= -z relro
            # Forces immediate binding of symbols, preventing lazy resolution at runtime (pairs with -z relro)
            EXE_LDFLAGS+= -z now
            LIB_LDFLAGS+= -z now
            ifeq ($(DEBUG_THREADS),1)
                # ThreadSanitizer, detects threading issues such as: Data races, Incorrect mutex usage, Other concurrency violations. Note: -fsanitize=thread is often incompatible with -fsanitize=memory, so they should not be used together
                CFLAGS += -fsanitize=thread
                EXE_LDFLAGS += -fsanitize=thread
                LIB_LDFLAGS += -fsanitize=thread
            else
                # AddressSanitizer, detects memory-related issues such as: Buffer overflows, Use-after-free, Invalid memory access on the heap, stack, or globals
                CFLAGS += -fsanitize=address
                EXE_LDFLAGS += -fsanitize=address
                LIB_LDFLAGS += -fsanitize=address
                # UndefinedBehaviorSanitizer, catches undefined behavior like: Integer overflow, Division by zero, Misaligned memory accesses, Invalid type casts
                CFLAGS += -fsanitize=undefined
                EXE_LDFLAGS += -fsanitize=undefined
                LIB_LDFLAGS += -fsanitize=undefined
                # CLANG ONLY : MemorySanitizer, detects: Use of uninitialized memory. Does not work with -fsanitize=thread
                # CFLAGS += -fsanitize=memory
                # LDFLAGS += -fsanitize=memory
            endif
        endif
    endif
else 
    #
    # PRODUCTION FLAGS
    #
    ifneq ($(UNAME_S),SunOS)
        ifneq ($(OS),Windows_NT)
            CFLAGS+= -I/usr/include/pcre
        endif
        # Protect against control-flow hijacking (requires hardware support)
        CFLAGS += -fcf-protection=full
    endif
    # Optimize for speed without significantly increasing compile time
    CFLAGS += -O3
endif

SRC=n_common.c n_base64.c n_crypto.c n_exceptions.c n_hash.c n_list.c n_log.c n_network.c n_network_msg.c n_network_accept_pool.c n_nodup_log.c n_signals.c n_stack.c n_str.c n_thread_pool.c n_time.c n_zlib.c n_user.c n_files.c n_aabb.c n_trees.c n_trajectory.c n_dead_reckoning.c n_astar.c n_iso_engine.c n_clock_sync.c

OUTPUT=libnilorea
LIBNILOREA=-lnilorea
PROJECT_NAME= NILOREA_LIBRARY

# OS specific flags
ifeq ($(OS),Windows_NT)
    FORCE_NO_KAFKA=1
    CFLAGS+= -D__USE_MINGW_ANSI_STDIO
    RM=rm -f
    CC= gcc
    CLIBS= -pthread -lws2_32 -lm -lz -ldbghelp
    DEBUGLIB=-limagehlp
    ifeq ($(MSYSTEM),MINGW32)
        RM=rm -f
        CFLAGS+= -m32 -DARCH32BITS
        LIBNILOREA=-lnilorea32
        EXT=32.exe
        LIB_STATIC_EXT=32.a
        LIB_DYN_EXT=32.dll
    endif
    ifeq ($(MSYSTEM),MINGW64)
        RM=rm -f
        CFLAGS+= -DARCH64BITS
        LIBNILOREA=-lnilorea64
        EXT=64.exe
        LIB_STATIC_EXT=64.a
        LIB_DYN_EXT=64.dll
    endif
    HDR=$(SRC:%.c=%.h) nilorea.h
    OBJECTS=$(SRC:%.c=obj/%.o)
else
    RM=rm -f
    CC=gcc
    EXT=
    LIB_STATIC_EXT=.a
    LIB_DYN_EXT=.so
    HDR=$(SRC:%.c=%.h) nilorea.h
    OBJECTS=$(SRC:%.c=obj/%.o)
    ifeq ($(UNAME_S),Linux)
        # Mark the stack as non-executable, preventing execution of code injected into the stack
        EXE_LDFLAGS+= -Wl,-z,noexecstack
        LIB_LDFLAGS+= -Wl,-z,noexecstack
        CLIBS=-lpthread -lm -lz
        # Enable X/Open POSIX extensions (XPG6 and extended features)
        CFLAGS += -D_GNU_SOURCE
    endif
    ifeq ($(UNAME_S),SunOS)
        CFLAGS+= -I/usr/include/pcre
        CC=gcc
        CFLAGS+= -m64
        # Solaris needs -lpthread for threading, -lsocket -lnsl for networking
        CLIBS=-lpthread -lm -lsocket -lnsl -lrt -lz
        # Enable X/Open POSIX extensions (XPG6 and extended features)
        CFLAGS+= -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED -D__EXTENSIONS__
    endif
endif

# Check for required test files
TEST_FILES= test_allegro5.c test_kafka.c test_openssl.c test_cjson.c test_pcre.c test_pcre_const.c

$(foreach file,$(TEST_FILES), \
    $(if $(wildcard $(file)),, \
        $(warning Warning: Missing test file "$(file)". Dependency detection may be incomplete.) \
    ) \
)

# detect available deps
ifeq "$(shell $(CC) test_allegro5.c -Wall -o $(TMP_OBJ) -lallegro_acodec -lallegro_audio -lallegro_color -lallegro_image -lallegro_main -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_ALLEGRO=1
else
    HAVE_ALLEGRO=0
endif

KAFKA_CFLAGS=-I$(shell readlink -f ./external/librdkafka/src)
KAFKA_CLIBS= -lsasl2 -lssl -lcrypto -ldl -L$(shell readlink -f ./external/librdkafka/src) -lrdkafka
ifeq "$(shell $(CC) test_kafka.c -o $(TMP_OBJ) $(KAFKA_CFLAGS) $(KAFKA_CLIBS) 1>/dev/null 2>/dev/null && echo $$?)" "0"
    HAVE_KAFKA=1
else
    HAVE_KAFKA=0
    KAFKA_CFLAGS=
    KAFKA_CLIBS=
endif

ifeq "$(shell $(CC) $(CFLAGS) test_openssl.c -Wall -o $(TMP_OBJ) -lssl -lcrypto 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_OPENSSL=1
else
    HAVE_OPENSSL=0
endif

CJSON_CFLAGS=-I$(shell readlink -f ./external/cJSON/)

ifeq "$(shell $(CC) $(CJSON_CFLAGS) test_cjson.c external/cJSON/cJSON.c -Wall -o $(TMP_OBJ) 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_CJSON=1
else
    HAVE_CJSON=0
endif

ifeq "$(shell $(CC) $(CFLAGS) test_pcre.c -Wall -o $(TMP_OBJ) -lpcre2-8 1>/dev/null 2>/dev/null && echo $$? )" "0"
    HAVE_PCRE=1
    ifeq "$(shell $(CC) $(CFLAGS) test_pcre_const.c -Wall -Werror -o $(TMP_OBJ) -lpcre2-8 1>/dev/null 2>/dev/null && echo $$? )" "0"
        CFLAGS += -DPCRE2_LIST_FREE_CONST
        HAVE_PCRE_CONST=1
    else
        HAVE_PCRE_CONST=0
    endif
else
    HAVE_PCRE=0
endif

# Clean up the tmp probe file immediately after all detection is done
$(shell $(RM) $(TMP_OBJ) 2>/dev/null)

# rule to make objects of source files (for library)
obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) $(FPIC_FLAG) -c $< -o $@

# rule to make objects of source files (for examples binaries)
examples/%.o: examples/%.c
	$(CC) $(CFLAGS) $(FPIE_FLAG) -c $< -o $@

# $ \ is for concatenation without a new line
EXAMPLES=examples/ex_base64_encode$(EXT) $\
         examples/ex_crypto$(EXT) $\
         examples/ex_list$(EXT) $\
         examples/ex_nstr$(EXT) $\
         examples/ex_exceptions$(EXT) $\
         examples/ex_hash$(EXT) $\
         examples/ex_network$(EXT) $\
         examples/ex_threads$(EXT) $\
         examples/ex_log$(EXT) $\
         examples/ex_common$(EXT) $\
         examples/ex_stack$(EXT) $\
         examples/ex_trees$(EXT) $\
         examples/ex_signals$(EXT) $\
         examples/ex_iso_astar$(EXT) $\
         examples/ex_zlib$(EXT) $\
         examples/ex_file$(EXT) $\
         examples/ex_accept_pool_server$(EXT) $\
         examples/ex_accept_pool_client$(EXT) $\
         examples/ex_clock_sync$(EXT)

ifeq ($(HAVE_ALLEGRO),1)
    ifeq ($(FORCE_NO_ALLEGRO),0)
        SRC+= n_allegro5.c n_3d.c n_anim.c n_particles.c n_gui.c n_fluids.c
        EXAMPLES+= examples/ex_gui_particles$(EXT) $\
                  examples/ex_gui_dictionary$(EXT) $\
                  examples/ex_gui$(EXT) $\
                  examples/ex_gui_isometric$(EXT) $\
                  examples/ex_gui_network$(EXT) $\
                  examples/ex_trajectory$(EXT)
        ALLEGRO_CLIBS=-lallegro_acodec -lallegro_audio -lallegro_color -lallegro_image -lallegro_main -lallegro_primitives -lallegro_ttf -lallegro_font -lallegro
        ALLEGRO_CFLAGS=-DALLEGRO_UNSTABLE -DHAVE_ALLEGRO
        CFLAGS+= -DHAVE_ALLEGRO
    endif
endif

ifeq ($(HAVE_CJSON),1)
    ifeq ($(FORCE_NO_CJSON),0)
		CJSON_CFLAGS=-I$(shell readlink -f ./external/cJSON)
        CFLAGS+= -DHAVE_CJSON $(CJSON_CFLAGS)
        CJSON_OBJ=examples/cJSON.o
        SRC+= n_avro.c
        OBJECTS+=obj/cJSON.o
        EXAMPLES+= examples/ex_avro$(EXT)
        ifeq ($(HAVE_ALLEGRO),1) 
            ifeq ($(FORCE_NO_ALLEGRO),0)
                EXAMPLES+= examples/ex_fluid$(EXT) 
            endif
        endif
        ifeq ($(HAVE_PCRE),1)
            ifeq ($(FORCE_NO_PCRE),0)
                ifeq ($(HAVE_KAFKA),1)
                    ifeq ($(FORCE_NO_KAFKA),0)
                        SRC+= n_kafka.c
                        EXAMPLES+= examples/ex_kafka$(EXT)
                        CFLAGS+= $(CJSON_CFLAGS) $(KAFKA_CFLAGS) 
                    else
                        KAFKA_CFLAGS=
                        KAFKA_CLIBS=
                    endif
                endif
            endif
        endif
    endif
endif

ifeq ($(HAVE_OPENSSL),1)
    ifeq ($(FORCE_NO_OPENSSL),0)
        EXAMPLES+= examples/ex_network_ssl$(EXT)
        EXAMPLES+= examples/ex_network_ssl_hardened$(EXT)
        CFLAGS+= -DHAVE_OPENSSL
        OPENSSL_CLIBS= -lssl -lcrypto
    endif
endif

ifeq ($(HAVE_PCRE),1)
    ifeq ($(FORCE_NO_PCRE),0)
        SRC+= n_pcre.c n_config_file.c
        EXAMPLES+= examples/ex_pcre$(EXT) $\
                   examples/ex_configfile$(EXT)
        # PCRE1 is EOL; build against PCRE2 (8-bit)
        CFLAGS+= -DHAVE_PCRE -DPCRE2_CODE_UNIT_WIDTH=8
        PCRE_CLIBS= -lpcre2-8
    endif
endif


ifeq ($(HAVE_PCRE),1)
    ifeq ($(FORCE_NO_PCRE),0)
        ifeq ($(HAVE_OPENSSL),1)
            ifeq ($(FORCE_NO_OPENSSL),0)
                ifeq ($(HAVE_CJSON),1)
                    ifeq ($(FORCE_NO_CJSON),0)
                        ifeq ($(HAVE_KAFKA),1)
                            ifeq ($(FORCE_NO_KAFKA),0)
                                EXAMPLES+= examples/ex_monolith$(EXT)
                            endif
                        endif
                    endif
                endif
            endif
        endif
    endif
endif

EXAMPLE_OBJ=$(EXAMPLES:%$(EXT)=%.o)

CFLAGS+= $(NILOREA_CFLAGS) 

default: main
release: all
debug: all

# ensure obj directory exists (order-only prerequisite for obj/*.o targets)
obj:
	mkdir -p obj

all: main examples

show-detected-option:
	@echo "HAVE_ALLEGRO: $(HAVE_ALLEGRO), HAVE_KAFKA: $(HAVE_KAFKA), HAVE_OPENSSL: $(HAVE_OPENSSL), HAVE_CJSON: $(HAVE_CJSON), HAVE_PCRE: $(HAVE_PCRE), HAVE_PCRE_CONST: $(HAVE_PCRE_CONST)"
	@echo "FORCE_NO_ALLEGRO: $(FORCE_NO_ALLEGRO), FORCE_NO_KAFKA: $(FORCE_NO_KAFKA), FORCE_NO_OPENSSL: $(FORCE_NO_OPENSSL), FORCE_NO_CJSON: $(FORCE_NO_CJSON), FORCE_NO_PCRE: $(FORCE_NO_PCRE)"

doc:
	doxygen Doxyfile

copy-libs: 
	@cp ./external/librdkafka/src/librdkafka.so examples/librdkafka.so.1 2>/dev/null || echo "info: librdkafka not compiled, run  make integrate-deps if you need it"
	@cp ./external/cJSON/cJSON.c examples/ || true
	@cp ./external/cJSON/cJSON.h examples/ || true

build-deps:
	cd external/librdkafka && ./configure && make -j 8

update-deps:
	git subtree pull --prefix=external/cJSON https://github.com/DaveGamble/cJSON.git master --squash
	git subtree pull --prefix=external/librdkafka https://github.com/confluentinc/librdkafka.git 9416dd80fb0dba71ff73a8cb4d2b919f54651006 --squash

integrate-deps: build-deps copy-libs show-detected-option

main: show-detected-option $(OUTPUT)$(LIB_STATIC_EXT) $(OUTPUT)$(LIB_DYN_EXT) copy-libs
	@cp $(OUTPUT)$(LIB_DYN_EXT) examples/ || true

$(OUTPUT)$(LIB_STATIC_EXT): $(OBJECTS)
	$(AR) rcs $(OUTPUT)$(LIB_STATIC_EXT) $^

$(OUTPUT)$(LIB_DYN_EXT): $(OBJECTS)
	$(CC) $(LIB_LDFLAGS) -shared -o $(OUTPUT)$(LIB_DYN_EXT) $^ $(CLIBS) $(ALLEGRO_CLIBS) $(KAFKA_CLIBS) $(OPENSSL_CLIBS) $(PCRE_CLIBS)

clean:
	$(RM) obj/*.o examples/*.o

examples: main copy-libs $(EXAMPLES)

distclean: clean
	$(RM) *.a *.so *.dll
	$(RM) $(EXAMPLES) 
	cd examples && $(RM) *.a *.so *.dll

# examples rules
examples/cJSON.o: external/cJSON/cJSON.c
	$(CC) $(CFLAGS) $(FPIE_FLAG) -c $< -o $@

obj/cJSON.o: external/cJSON/cJSON.c | obj
	$(CC) $(CFLAGS) $(FPIC_FLAG) -c $< -o $@

examples/ex_common$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o examples/ex_common.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_stack$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_stack.o examples/ex_stack.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_trees$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_trees.o examples/ex_trees.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_nstr$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o examples/ex_nstr.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_exceptions$(EXT): obj/n_log.o obj/n_exceptions.o examples/ex_exceptions.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_base64_encode$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_base64.o examples/ex_base64.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_crypto$(EXT): obj/n_common.o obj/n_base64.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_time.o obj/n_thread_pool.o obj/n_crypto.o examples/ex_crypto.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_list$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o examples/ex_list.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_hash$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o examples/ex_hash.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_clock_sync$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o obj/n_time.o obj/n_thread_pool.o obj/n_network.o obj/n_network_msg.o obj/n_clock_sync.o examples/ex_clock_sync.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(EXE_LDFLAGS)

examples/ex_network$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_network.o examples/ex_network.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(EXE_LDFLAGS)

examples/ex_network_ssl$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_network.o examples/ex_network_ssl.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_network_ssl_hardened$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_network.o examples/ex_network_ssl_hardened.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_monolith$(EXT): examples/ex_monolith.o $(CJSON_OBJ) $(OUTPUT)$(LIB_STATIC_EXT)
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ examples/ex_monolith.o $(CJSON_OBJ) $(OUTPUT)$(LIB_STATIC_EXT) $(CLIBS) $(ALLEGRO_CLIBS) $(KAFKA_CLIBS) $(OPENSSL_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)
	
examples/ex_configfile$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o obj/n_pcre.o obj/n_config_file.o examples/ex_configfile.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_signals$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_signals.o examples/ex_signals.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(DEBUGLIB) $(EXE_LDFLAGS)

examples/ex_trajectory$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_trajectory.o examples/ex_trajectory.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(EXE_LDFLAGS)

examples/ex_gui$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o examples/cJSON.o obj/n_gui.o examples/ex_gui.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(EXE_LDFLAGS)

examples/ex_gui_isometric$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o examples/cJSON.o obj/n_gui.o obj/n_iso_engine.o obj/n_astar.o obj/n_dead_reckoning.o obj/n_trajectory.o examples/ex_gui_isometric.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(EXE_LDFLAGS)

examples/ex_gui_dictionary$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_particles.o obj/n_3d.o obj/n_allegro5.o obj/n_pcre.o examples/cJSON.o obj/n_gui.o examples/ex_gui_dictionary.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_gui_particles$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_particles.o obj/n_3d.o examples/cJSON.o obj/n_gui.o examples/ex_gui_particles.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(EXE_LDFLAGS)

examples/ex_gui_network$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_hash.o obj/n_time.o obj/n_network.o obj/n_network_msg.o obj/n_thread_pool.o examples/cJSON.o obj/n_gui.o examples/ex_gui_network.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(OPENSSL_CLIBS) $(EXE_LDFLAGS)

examples/ex_fluid$(EXT): obj/n_common.o obj/n_log.o obj/n_str.o obj/n_list.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_pcre.o obj/n_config_file.o examples/ex_fluid_config.o obj/n_fluids.o examples/cJSON.o examples/ex_fluid.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_pcre$(EXT): obj/n_log.o obj/n_pcre.o examples/ex_pcre.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_threads$(EXT): obj/n_log.o obj/n_list.o obj/n_time.o obj/n_thread_pool.o examples/ex_threads.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_log$(EXT): obj/n_str.o obj/n_hash.o obj/n_log.o obj/n_nodup_log.o obj/n_list.o obj/n_time.o examples/ex_log.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_kafka$(EXT): obj/n_common.o obj/n_list.o obj/n_log.o obj/n_str.o obj/n_hash.o obj/n_signals.o obj/n_zlib.o obj/n_time.o obj/n_network.o obj/n_network_msg.o obj/n_thread_pool.o obj/n_config_file.o examples/cJSON.o obj/n_base64.o obj/n_kafka.o obj/n_files.o obj/n_pcre.o examples/ex_kafka.o
	$(CC) $(CFLAGS) $(KAFKA_CFLAGS) -o $@ $^ $(CLIBS) $(KAFKA_CLIBS) $(PCRE_CLIBS) $(EXE_LDFLAGS)

examples/ex_iso_astar$(EXT): obj/n_log.o obj/n_list.o obj/n_str.o obj/n_common.o obj/n_astar.o obj/n_dead_reckoning.o obj/n_iso_engine.o obj/n_trajectory.o examples/ex_iso_astar.o
	$(CC) $(CFLAGS) $(ALLEGRO_CFLAGS) -o $@ $^ $(CLIBS) $(ALLEGRO_CLIBS) $(EXE_LDFLAGS)

examples/ex_zlib$(EXT): obj/n_common.o obj/n_log.o obj/n_str.o obj/n_list.o obj/n_zlib.o examples/ex_zlib.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_file$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_files.o examples/ex_file.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)

examples/ex_accept_pool_server$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_network.o obj/n_network_accept_pool.o examples/ex_accept_pool_server.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(EXE_LDFLAGS)

examples/ex_accept_pool_client$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_network_msg.o obj/n_time.o obj/n_thread_pool.o obj/n_hash.o obj/n_network.o examples/ex_accept_pool_client.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(OPENSSL_CLIBS) $(EXE_LDFLAGS)

examples/ex_avro$(EXT): obj/n_common.o obj/n_log.o obj/n_list.o obj/n_str.o obj/n_hash.o obj/n_avro.o examples/cJSON.o examples/ex_avro.o
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS) $(EXE_LDFLAGS)
