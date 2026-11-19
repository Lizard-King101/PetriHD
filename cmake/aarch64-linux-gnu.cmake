# Cross-compiles for the Pi 3 B+ (Cortex-A53) and runs tests under qemu.
#   cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake -DPETRIHD_GUI=OFF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a53")
set(PETRIHD_SYSROOT "/usr/aarch64-linux-gnu" CACHE PATH "aarch64 libraries for qemu")
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64 -L ${PETRIHD_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${PETRIHD_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
