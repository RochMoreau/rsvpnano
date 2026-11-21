cmake_minimum_required(VERSION 3.20)
project(rsvp_nano_nextui C CXX)

# Host build of RSVP Nano for Linux handhelds (TrimUI Brick / NextUI) and desktop testing.
# The firmware sources under ../../src are compiled unchanged against a small Arduino/ESP-IDF
# compatibility layer (shim/ + runtime/). Hardware features with no Linux equivalent
# (USB mass storage, OTA, serial companion) are replaced by stubs/; the Wi-Fi companion runs on
# runtime/HttpServer.cpp with stubs/NextUiCompanion.cpp in place of CompanionApi.cpp.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

get_filename_component(RSVP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(RSVP_SRC "${RSVP_ROOT}/src")
set(RSVP_LIB "${RSVP_ROOT}/lib")
set(PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(RSVP_DEPS_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps" CACHE PATH "Where third-party sources are fetched")
set(ARDUINO_GFX_REV dd977b70828545c23d2b73140ac02d716a11bdd3)
set(GLAZE_REV v7.9.1)
set(IMPROV_REV 17898613a1c17062ca5af295ceb639b16b4930bf)

# Logical UI resolution; the host scales it to the screen (TrimUI Brick: 1024x768).
set(RSVP_PANEL_WIDTH 640 CACHE STRING "Logical panel width")
set(RSVP_PANEL_HEIGHT 480 CACHE STRING "Logical panel height")

find_package(Git REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(fetch_git name url rev)
    set(dir "${RSVP_DEPS_DIR}/${name}")
    if(NOT EXISTS "${dir}/.fetched-${rev}")
        file(REMOVE_RECURSE "${dir}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" init -q "${dir}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" fetch -q --depth=1 "${url}" "${rev}"
                        COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" checkout -q --detach FETCH_HEAD
                        COMMAND_ERROR_IS_FATAL ANY)
        file(TOUCH "${dir}/.fetched-${rev}")
    endif()
    set(${name}_DIR "${dir}" PARENT_SCOPE)
endfunction()

fetch_git(arduino_gfx https://github.com/moononournation/Arduino_GFX.git ${ARDUINO_GFX_REV})
fetch_git(glaze https://github.com/stephenberry/glaze.git ${GLAZE_REV})
fetch_git(improv https://github.com/improv-wifi/sdk-cpp.git ${IMPROV_REV})

# Same signed-coordinate fix the firmware build applies (tools/pio_gfx.py).
set(GFX_PATCHED "${CMAKE_CURRENT_BINARY_DIR}/generated/Arduino_GFX.cpp")
add_custom_command(
    OUTPUT "${GFX_PATCHED}"
    COMMAND "${Python3_EXECUTABLE}" "${PORT_DIR}/tools/patch_gfx.py"
            "${arduino_gfx_DIR}/src/Arduino_GFX.cpp" "${GFX_PATCHED}"
    DEPENDS "${arduino_gfx_DIR}/src/Arduino_GFX.cpp" "${RSVP_ROOT}/tools/pio_gfx.py" "${PORT_DIR}/tools/patch_gfx.py")

# Firmware version header (same shape as tools/pio_set_version.py).
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${RSVP_ROOT}" describe --tags --always --dirty
                OUTPUT_VARIABLE RSVP_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT RSVP_GIT_DESCRIBE)
    set(RSVP_GIT_DESCRIBE "dev")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/FirmwareVersion.generated.h"
     CONTENT "#pragma once\n\ninline constexpr char kFirmwareVersion[] = \"${RSVP_GIT_DESCRIBE}-nextui\";\n")

# ---------------------------------------------------------------------------------------------
# SDL2
# ---------------------------------------------------------------------------------------------
if(RSVP_SDL2_INCLUDE_DIR)
    add_library(rsvp_sdl2 INTERFACE)
    target_include_directories(rsvp_sdl2 INTERFACE "${RSVP_SDL2_INCLUDE_DIR}")
    target_link_libraries(rsvp_sdl2 INTERFACE ${RSVP_SDL2_LIBRARY})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    add_library(rsvp_sdl2 INTERFACE)
    target_link_libraries(rsvp_sdl2 INTERFACE PkgConfig::SDL2)
endif()

# ---------------------------------------------------------------------------------------------
# Vendored C libraries (same selection as lib/*/library.json)
# ---------------------------------------------------------------------------------------------
add_library(rsvp_zlib STATIC
    ${RSVP_LIB}/zlib/upstream/adler32.c ${RSVP_LIB}/zlib/upstream/crc32.c ${RSVP_LIB}/zlib/upstream/deflate.c
    ${RSVP_LIB}/zlib/upstream/inffast.c ${RSVP_LIB}/zlib/upstream/inflate.c ${RSVP_LIB}/zlib/upstream/inftrees.c
    ${RSVP_LIB}/zlib/upstream/trees.c ${RSVP_LIB}/zlib/upstream/zutil.c)
target_include_directories(rsvp_zlib PUBLIC ${RSVP_LIB}/zlib/upstream)

add_library(rsvp_sheenbidi STATIC ${RSVP_LIB}/SheenBidi/upstream/Source/SheenBidi.c)
target_include_directories(rsvp_sheenbidi PUBLIC ${RSVP_LIB}/SheenBidi/upstream/Headers)
target_include_directories(rsvp_sheenbidi PRIVATE ${RSVP_LIB}/SheenBidi/upstream/Source)
target_compile_definitions(rsvp_sheenbidi PRIVATE SB_CONFIG_UNITY)
target_compile_definitions(rsvp_sheenbidi PUBLIC SB_CONFIG_DISABLE_SCRATCH_MEMORY)

add_library(rsvp_harfbuzz STATIC ${RSVP_LIB}/HarfBuzz/upstream/src/harfbuzz.cc)
target_include_directories(rsvp_harfbuzz PUBLIC ${RSVP_LIB}/HarfBuzz/upstream/src)
target_compile_definitions(rsvp_harfbuzz PUBLIC HB_TINY)
target_compile_options(rsvp_harfbuzz PRIVATE -Os -w)

set(PDFIO_FILES aes array common content crypto dict file md5 object page rc4 sha256 stream string token value)
set(PDFIO_SOURCES ${RSVP_LIB}/PDFio/upstream/ttf.c)
foreach(f ${PDFIO_FILES})
    list(APPEND PDFIO_SOURCES ${RSVP_LIB}/PDFio/upstream/pdfio-${f}.c)
endforeach()
add_library(rsvp_pdfio STATIC ${PDFIO_SOURCES})
target_include_directories(rsvp_pdfio PUBLIC ${RSVP_LIB}/PDFio/upstream)
target_compile_options(rsvp_pdfio PRIVATE -w "-include${PORT_DIR}/shim/rsvp_pdfio_prelude.h")
target_link_libraries(rsvp_pdfio PUBLIC rsvp_zlib m)

# ---------------------------------------------------------------------------------------------
# Firmware sources
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE FIRMWARE_SOURCES CONFIGURE_DEPENDS ${RSVP_SRC}/*.cpp)
# Same exclusions as [common] base_src_filter, plus the modules replaced by stubs/.
list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX "/src/(ui/screens/watch|benchmark|companion_api_test|platforms|drivers)/")
list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX "/src/(usb|network|sleep|logging/Logger\\.cpp|companion/http/CompanionApi\\.cpp|companion/serial/CompanionSerial\\.cpp|update/OtaUpdater|settings/NvsSecurity|feeds/RssFeeds)")

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${RSVP_SRC}/platforms/nextui/*.cpp)
file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS ${PORT_DIR}/runtime/*.cpp ${PORT_DIR}/stubs/*.cpp)

add_executable(rsvpnano
    ${FIRMWARE_SOURCES}
    ${PLATFORM_SOURCES}
    ${RUNTIME_SOURCES}
    ${GFX_PATCHED}
    ${arduino_gfx_DIR}/src/Arduino_G.cpp
    ${arduino_gfx_DIR}/src/canvas/Arduino_Canvas.cpp)

target_include_directories(rsvpnano PRIVATE
    ${PORT_DIR}/shim
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${RSVP_SRC}
    ${RSVP_ROOT}
    ${arduino_gfx_DIR}/src
    ${glaze_DIR}/include
    ${improv_DIR}/src)

target_compile_definitions(rsvpnano PRIVATE
    RSVP_NEXTUI=1
    ARDUINO=10819
    CORE_DEBUG_LEVEL=3
    RSVP_ON_DEVICE_DOCUMENT_CONVERSION=1
    RSVP_USB_TRANSFER_ENABLED=0
    U8G2_WITH_UNICODE
    GLZ_DEFAULT_OPTIMIZATION_SIZE
    GLZ_DISABLE_ALWAYS_INLINE
    RSVP_BOARD_CONFIG_HEADER="platforms/nextui/BoardConfig.h"
    RSVP_NEXTUI_PANEL_WIDTH=${RSVP_PANEL_WIDTH}
    RSVP_NEXTUI_PANEL_HEIGHT=${RSVP_PANEL_HEIGHT}
    RSVP_WIDE_LAYOUT_MAX_HEIGHT=${RSVP_PANEL_HEIGHT})

# ESP32 toolchains use unsigned plain char; Arduino_GFX's UTF-8 decoder relies on it.
target_compile_options(rsvpnano PRIVATE -funsigned-char -fno-exceptions -fno-rtti
    -Wno-deprecated-declarations -Wno-psabi)
set_source_files_properties(${RSVP_SRC}/conversion/pdf/PdfTextExtractor.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${PORT_DIR}/shim/rsvp_pdfio_paths.h")

target_link_libraries(rsvpnano PRIVATE rsvp_sdl2 rsvp_pdfio rsvp_harfbuzz rsvp_sheenbidi rsvp_zlib pthread)

install(TARGETS rsvpnano RUNTIME DESTINATION .)
