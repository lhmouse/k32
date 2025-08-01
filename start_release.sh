#!/bin/bash -e

meson setup -Ddebug=true -Doptimization=3 build_release
meson compile -Cbuild_release

export LD_LIBRARY_PATH=$(realpath -e build_release)
gdb -ex run --args poseidon $* ./etc
