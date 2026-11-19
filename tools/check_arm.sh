#!/bin/sh
# Builds the core natively and for aarch64, runs the tests on both (aarch64
# under qemu), and checks that both give identical census lines.
# Needs: aarch64-linux-gnu-gcc and qemu-user (pacman -S aarch64-linux-gnu-gcc qemu-user).
set -eu
cd "$(dirname "$0")/.."
cmake -S . -B build-native -DPETRIHD_GUI=OFF >/dev/null
cmake --build build-native -j"$(nproc)" >/dev/null
cmake -S . -B build-arm -DPETRIHD_GUI=OFF -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake >/dev/null
cmake --build build-arm -j"$(nproc)" >/dev/null
ctest --test-dir build-native --output-on-failure
ctest --test-dir build-arm --output-on-failure
run="seed=7 ticks=4800 every=480"
./build-native/petrihd-run $run | sed 's/ tick_us=.*//' > build-native/census.txt
qemu-aarch64 -L /usr/aarch64-linux-gnu ./build-arm/petrihd-run $run | sed 's/ tick_us=.*//' > build-arm/census.txt
if cmp -s build-native/census.txt build-arm/census.txt; then
    echo "x86 and aarch64 fingerprints match ($(wc -l < build-native/census.txt) census lines)"
else
    diff build-native/census.txt build-arm/census.txt
    exit 1
fi
