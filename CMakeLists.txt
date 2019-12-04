
# CMakeLists.txt for the Nilorea library

cmake_minimum_required(VERSION 3.0)


project(Nilorea)

set(A5_DEP_SEARCH_PATH "../Allegro525" CACHE PATH "Where to find allegro 5")

set(ALLEGRO5_LIB_DIR "$CACHE{A5_DEP_SEARCH_PATH}/lib")

set(NILOREA_INCLUDE_DIRS "include" "$CACHE{A5_DEP_SEARCH_PATH}/include/allegro5" "$CACHE{A5_DEP_SEARCH_PATH}/include")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
   find_library(A5_MONOLITH NAMES liballegro_monolith-debug-static.a PATHS ${ALLEGRO5_LIB_DIR})
   find_library(A5_ADDONS NAMES
         liballegro_color-debug.a
         liballegro_image-debug.a
         liballegro_memfile-debug.a
         liballegro_ttf-debug.a
         liballegro_font-debug.a
         liballegro_physfs-debug.a
         liballegro_dialog-debug.a
         liballegro_video-debug.a
         liballegro_acodec-debug.a
         liballegro_main-debug.a
         liballegro_primitives-debug.a
         liballegro_audio-debug.a
         liballegro-debug.a
         PATHS ${ALLEGRO5_LIB_DIR})
else()
   find_library(A5_MONOLITH NAMES liballegro_monolith-static.a PATHS ${ALLEGRO5_LIB_DIR})
   find_library(A5_ADDONS NAMES
         liballegro_color.a
         liballegro_image.a
         liballegro_memfile.a
         liballegro_ttf.a
         liballegro_font.a
         liballegro_physfs.a
         liballegro_dialog.a
         liballegro_video.a
         liballegro_acodec.a
         liballegro_main.a
         liballegro_primitives.a
         liballegro_audio.a
         liballegro.a
         PATHS ${ALLEGRO5_LIB_DIR})
endif("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")


set(ALLEGRO_FOUND 1)

if (${A5_MONOLITH} STRGREATER "")
   set(A5_LINK_LIBS ${A5_MONOLITH} CACHE STRING "Allegro 5 Link libraries")
elseif(${A5_ADDONS} STRGREATER "")
   set(A5_LINK_LIBS ${A5_ADDONS} CACHE STRING "Allegro 5 Link libraries")
else()
   set(ALLEGRO_FOUND 0)
endif(${A5_MONOLITH} STRGREATER "")

set (NILOREASRCS1
         n_3d.c
         n_anim.c
         n_common.c
         n_config_file.c
         n_debug_mem.c
         n_exceptions.c
         n_file.c
         n_hash.c
         n_list.c
         n_log.c
         n_network.c
         n_network_msg.c
         n_nodup_log.c
         n_stack.c
         n_str.c
         n_thread_pool.c
         n_time.c
         n_zlib.c)

set(NILOREASRCS2
      n_resources.c n_particles.c n_gui.c
   )

set(NILOREASRCS ${NILOREASRCS1})
if (ALLEGRO_FOUND)
   list(APPEND NILOREASRCS ${NILOREASRCS2})
endif (ALLEGRO_FOUND)


list(TRANSFORM NILOREASRCS PREPEND "src/")

foreach(src ${NILOREASRCS})
   message(${src})
endforeach()

add_library(NiloreaObjects OBJECT ${NILOREASRCS})

target_include_directories(NiloreaObjects PRIVATE ${NILOREA_INCLUDE_DIRS})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
   target_compile_options(NiloreaObjects PRIVATE "-O0" "-ggdb")
else()
   target_compile_options(NiloreaObjects PRIVATE "-O2")
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

target_compile_options(NiloreaObjects PRIVATE "-std=c11")

add_library(Nilorea STATIC $<TARGET_OBJECTS:NiloreaObjects>)

target_link_libraries(Nilorea ${ALLEGRO_LINK_LIBS})


