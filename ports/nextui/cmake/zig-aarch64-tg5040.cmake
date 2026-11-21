# Cross toolchain for the TrimUI Brick / Smart Pro (tg5040): Zig's clang + libc++ targeting
# aarch64 glibc 2.23 (the TrimUI rootfs; its SDK sysroot ships libpthread-2.23), linking against the TrimUI SDK sysroot for SDL2.
#
#   cmake -S ports/nextui -B build-tg5040 -DCMAKE_TOOLCHAIN_FILE=ports/nextui/cmake/zig-aarch64-tg5040.cmake \
#         -DTG5040_SYSROOT=/path/to/sysroot
#
# The firmware is C++23; the stock tg5040 GCC 8.3 cannot build it, so only its sysroot is used.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(ZIG_TARGET "aarch64-linux-gnu.2.23" CACHE STRING "Zig target triple (glibc version of the device)")
set(ZIG_CPU "cortex_a53" CACHE STRING "Zig -mcpu (Allwinner A133P)")

set(_wrappers "${CMAKE_CURRENT_LIST_DIR}/zig")
set(CMAKE_C_COMPILER "${_wrappers}/zig-cc")
set(CMAKE_CXX_COMPILER "${_wrappers}/zig-c++")
set(CMAKE_AR "${_wrappers}/zig-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_wrappers}/zig-ranlib" CACHE FILEPATH "" FORCE)
set(ENV{ZIG_TARGET} "${ZIG_TARGET}")
set(ENV{ZIG_CPU} "${ZIG_CPU}")

if(NOT TG5040_SYSROOT AND DEFINED ENV{TG5040_SYSROOT})
    set(TG5040_SYSROOT "$ENV{TG5040_SYSROOT}")
endif()
set(ENV{TG5040_SYSROOT} "${TG5040_SYSROOT}") # visible to try_compile projects
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TG5040_SYSROOT)
if(NOT TG5040_SYSROOT)
    message(FATAL_ERROR "Set TG5040_SYSROOT to the TrimUI SDK sysroot (contains usr/include/SDL2)")
endif()

# Only SDL2 comes from the SDK: Zig provides its own glibc 2.23 headers and stubs.
set(RSVP_SDL2_INCLUDE_DIR "${TG5040_SYSROOT}/usr/include/SDL2" CACHE PATH "")
set(RSVP_SDL2_LIBRARY "${TG5040_SYSROOT}/usr/lib/libSDL2-2.0.so.0" CACHE FILEPATH "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
