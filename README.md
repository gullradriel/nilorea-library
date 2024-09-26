# Nilorea C Library

## Features
- various helping macros and typedefs
-  logging system
- strings
- hash tables
- linked lists
- thread pools
- network engine (with or without SSL)
- Kafka consumer / producer wrappers 
- simple 2D/3D physics, particles,
- generic game/user managing system
- examples for each modules 
- usable as a monolith library or a collection of files

## Dependencies
Most of the core modules can be compiled without additional libraries. That said, you'll need the following dependencies installed if you want all the modules to compile:
- libpcre (install from your package manager) 
- allegro5 (https://github.com/liballeg/allegro5) (install from your package manager)
- cjson (https://github.com/DaveGamble/cJSON) (included in submodules as we only need to get the .h and .c  from it)
- librdkafka (https://github.com/confluentinc/librdkafka)  (included in submodules as the system version may be too old in a lots of cases)
-  nuklear (https://github.com/Immediate-Mode-UI/Nuklear) (WIP dialog manager, not used ATM, header only library)

## Building

### Supported platforms
- Linux
- Solaris
- Windows

### Prerequisites
- gcc, make 
- doxygen and graphviz (if you want to regenerate the documentation)

### Instructions
- ```cd my_prog_dir```
- ```git clone --recurse-submodule git@github.com:gullradriel/nilorea-library.git```
- ```cd nilorea-library```
- ```make ; make examples``` or ```make all```
### Regenerating the documentation
From the root directory:
- ```make doc```
or 
- ```doxygen Doxyfile```

### Compiling with extra dependencies
From the root directory:
- ```make download-dep``` => will launch a submodule update, compile latest librdkafka version
- ```make clean ; make all``` => restart a whole fresh compilation (lib + examples) 
