#!/bin/sh
find include src examples -iname '*.h' -o -iname '*.hpp' -o -iname '*.cpp' -o -iname '*.c' | xargs clang-format -style=file -i
