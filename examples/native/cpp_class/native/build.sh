#!/bin/sh
# The project's own native build: produces native/build/counter.o.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$here/build"
"${CXX:-c++}" -std=c++20 -O2 -c -I"$here/include" -o "$here/build/counter.o" "$here/counter.cpp"
