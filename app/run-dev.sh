#!/usr/bin/env bash
# Launch simple_world with the Metal validation layer ON (metal-cpp-discipline
# Rule 4). Build first if needed. Pass --selftest to run the headless eye instead
# of opening the window.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -d build ]; then
  cmake -S . -B build -G "Unix Makefiles"
fi
cmake --build build -j

export MTL_DEBUG_LAYER=1
export MTL_SHADER_VALIDATION=1
export MTL_DEBUG_LAYER_ERROR_MODE=assert
export ASAN_OPTIONS=detect_leaks=0   # Cocoa/AppKit hold benign at-exit allocations

exec ./build/simple_world "$@"
