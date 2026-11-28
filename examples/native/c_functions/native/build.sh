#!/bin/sh
# The project's own native build: produces native/build/sample.o.
# coorc only compiles the generated bridge and links this object.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$here/build"
"${CC:-cc}" -c -O2 -I"$here/include" -o "$here/build/sample.o" "$here/sample.c"
