# Terra Nova: Freefall -- standalone build of the decompiled Terra Nova: Strike Force Centauri.
#
# 32-bit only for now: the decompiled C keeps the original's struct layouts, which assume 32-bit
# pointers.  Needs a GNU-compatible C compiler (MinGW-w64 GCC or Clang) and Python 3.
#
#   Windows (MSYS2 "MINGW32" shell):  cmake -B build -G Ninja && cmake --build build
#   Cross-compiling from Linux:       cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-i686.cmake
#   Linux 32-bit (for testing):       cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i386.cmake
cmake_minimum_required(VERSION 3.16)
project(TerraNovaFreefall VERSION 1.2.0 LANGUAGES C ASM)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Terra Nova: Freefall builds as a 32-bit program for now "
                      "(use a 32-bit compiler, e.g. MSYS2's MINGW32 shell or cmake/mingw-w64-i686.cmake).")
endif()
if(MSVC)
  message(FATAL_ERROR "The MSVC compiler can't build this code yet (it uses GNU C extensions and GNU "
                      "assembler syntax). Use MinGW-w64 (MSYS2) or Clang.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(FREEFALL_WARNINGS "Show compiler warnings of the decompiled code" OFF)
option(FREEFALL_SYSTEM_SDL "Use an installed SDL3 (find_package) instead of building it from source" OFF)
if(WIN32)
  set(rife_default ON)
else()
  set(rife_default OFF)
endif()
option(FREEFALL_RIFE "Build tnfree-rife.dll, the optional video smoothing helper (C++, Vulkan; rife/)" ${rife_default})

# ---- SDL3 (window, input, sound, timers).  By default its source is downloaded and built as a
# static library, so tnfree.exe stays a single file.  For an offline build, point
# FETCHCONTENT_SOURCE_DIR_SDL3 at an unpacked SDL3 source folder.
if(FREEFALL_SYSTEM_SDL)
  find_package(SDL3 REQUIRED CONFIG)
  set(FREEFALL_SDL_TARGET SDL3::SDL3)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
  FetchContent_MakeAvailable(SDL3)
  set(FREEFALL_SDL_TARGET SDL3::SDL3-static)
endif()
# ---- FluidSynth 2.6.1 (LGPL-2.1), the SoundFont music (port/audio/sfsynth.c): downloaded and built as a
# static library without its audio drivers, threads or other libraries.  For an offline build,
# point FETCHCONTENT_SOURCE_DIR_FLUIDSYNTH at its source folder (with the gcem submodule).
option(FREEFALL_FLUIDSYNTH "Build the SoundFont music (FluidSynth, C++)" ON)
if(FREEFALL_FLUIDSYNTH)
  enable_language(CXX)
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(osal embedded CACHE STRING "" FORCE)
  foreach(opt alsa aufile dbus ipv6 jack ladspa libsndfile midishare network oss dsound wasapi waveout
          winmidi sdl3 pulseaudio pipewire readline threads openmp signalsmith systemd unicode opensles
          oboe coreaudio coremidi framework portaudio dart kai)
    set(enable-${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(enable-native-dls ON CACHE BOOL "" FORCE)     # Windows' gm.dls; needs C++17
  set(enable-floats ON CACHE BOOL "" FORCE)
  FetchContent_Declare(fluidsynth
    GIT_REPOSITORY https://github.com/FluidSynth/fluidsynth.git
    GIT_TAG 71e85b2ca6bf48641ba7e2261d8f2640ae473773       # v2.6.1
    GIT_SUBMODULES gcem)
  FetchContent_GetProperties(fluidsynth)
  if(NOT fluidsynth_POPULATED)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)     # FetchContent_Populate: only its library, not its programs
    endif()
    FetchContent_Populate(fluidsynth)
    add_subdirectory(${fluidsynth_SOURCE_DIR} ${fluidsynth_BINARY_DIR} EXCLUDE_FROM_ALL)
  endif()
endif()
set(FREEFALL_GAME_DIR "" CACHE PATH "The game's TNOVA folder (with __FF.EXE), for 'ctest'")

# ---- generated glue: the original's data layout, function map, stubs (tools/gen_port.py)
if(WIN32)
  set(SYM_PREFIX --prefix _)       # 32-bit Windows puts '_' in front of C symbols
else()
  set(SYM_PREFIX)
endif()
file(GLOB SYMBOL_CSVS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/symbols/*.csv ${CMAKE_SOURCE_DIR}/port/symbols-port.csv)
file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/*.c)
set(GEN ${CMAKE_BINARY_DIR}/gen)
add_custom_command(
  OUTPUT ${GEN}/tn_image.S ${GEN}/tn_funcmap.c ${GEN}/tn_stubs.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_port.py ${GEN} ${SYM_PREFIX}
          --port-dir ${CMAKE_SOURCE_DIR}/port
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_port.py ${SYMBOL_CSVS} ${PORT_SOURCES}
  COMMENT "Generating the original data layout and function map")

# ---- the decompiled game (shared with the verification harness in verify/)
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)

# ---- version: FREEFALL_RELEASE_LABEL ("pre-release", "test build", empty for a release) and the git commit
set(FREEFALL_RELEASE_LABEL "" CACHE STRING "Text after the version number (empty for a release)")
set(FREEFALL_COMMIT "$ENV{GITHUB_SHA}")
if(NOT FREEFALL_COMMIT)
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE FREEFALL_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(FREEFALL_COMMIT)
  string(SUBSTRING "${FREEFALL_COMMIT}" 0 7 FREEFALL_COMMIT)
else()
  set(FREEFALL_COMMIT "unknown")
endif()
# the version as the player sees it (window title, credits, options screen, log, zip): two
# numbers, the third only when it is not 0 (owner)
if(PROJECT_VERSION_PATCH EQUAL 0)
  set(FREEFALL_VERSION_SHORT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
  set(FREEFALL_VERSION_SHORT "${PROJECT_VERSION}")
endif()
string(STRIP "${FREEFALL_VERSION_SHORT} ${FREEFALL_RELEASE_LABEL}" FREEFALL_VERSION_SHOWN)
# the same in tnfree.log, the exe's file properties, README.txt and the zip's name (owner: 1.2, not 1.2.0)
set(FREEFALL_VERSION_FULL "${FREEFALL_VERSION_SHOWN}")
if(FREEFALL_RELEASE_LABEL)
  set(FREEFALL_RC_FLAGS "VS_FF_PRERELEASE")
else()
  set(FREEFALL_RC_FLAGS "0x0L")
endif()
configure_file(port/res/version.h.in ${GEN}/version.h @ONLY)

# ---- the help screen's fonts (port/ui/help.c), built into tnfree
set(FONT_DIR ${CMAKE_SOURCE_DIR}/third_party/liberation-fonts)
add_custom_command(
  OUTPUT ${GEN}/fonts.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/embed.py ${GEN}/fonts.c
          port_font_sans=${FONT_DIR}/LiberationSans-Regular.ttf
          port_font_sans_bold=${FONT_DIR}/LiberationSans-Bold.ttf
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed.py ${FONT_DIR}/LiberationSans-Regular.ttf
          ${FONT_DIR}/LiberationSans-Bold.ttf
  COMMENT "Embedding the help screen's fonts")

# ---- the Freefall logo line (the help screen), the logo without its star line (the options
# screen and the first-start question) and the wide main menu's sides (owner's art, made by
# tools/menu-wide.py), built into tnfree (port/ui/options.c, port/ui/help.c, port/ui/menuwide.c)
add_custom_command(
  OUTPUT ${GEN}/logo.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/embed.py ${GEN}/logo.c
          port_logo_line=${CMAKE_SOURCE_DIR}/port/res/logo-line.png
          port_logo_nostar=${CMAKE_SOURCE_DIR}/port/res/logo-nostar.png
          port_menu_wide=${CMAKE_SOURCE_DIR}/port/res/menu-wide.png
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed.py ${CMAKE_SOURCE_DIR}/port/res/logo-line.png
          ${CMAKE_SOURCE_DIR}/port/res/logo-nostar.png ${CMAKE_SOURCE_DIR}/port/res/menu-wide.png
  COMMENT "Embedding the Freefall logo and the wide main menu")

# ---- "Visor damage": its pictures (made by tools/visor-damage.py), built into tnfree
# (port/mods/visor.c)
set(VISOR_PICS)
foreach(kind shrapnel laser cracks)
  foreach(style a b c)
    list(APPEND VISOR_PICS ${kind}-${style}-light ${kind}-${style}-heavy)
  endforeach()
endforeach()
set(VISOR_ARGS)
set(VISOR_DEPS)
foreach(pic ${VISOR_PICS})
  string(REPLACE "-" "_" sym ${pic})
  list(APPEND VISOR_ARGS port_visor_${sym}=${CMAKE_SOURCE_DIR}/port/res/visor/${pic}.png)
  list(APPEND VISOR_DEPS ${CMAKE_SOURCE_DIR}/port/res/visor/${pic}.png)
endforeach()
add_custom_command(
  OUTPUT ${GEN}/visor.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/embed.py ${GEN}/visor.c ${VISOR_ARGS}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed.py ${VISOR_DEPS}
  COMMENT "Embedding the visor damage pictures")

# ---- the fan-made extra missions' patches (what their authors changed in the game's files,
# made by tools/mission-patch.py), built into tnfree (port/mods/extra.c)
add_custom_command(
  OUTPUT ${GEN}/missions.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/embed.py ${GEN}/missions.c
          port_patch_stone_henge=${CMAKE_SOURCE_DIR}/port/res/stone-henge.ffmp
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed.py ${CMAKE_SOURCE_DIR}/port/res/stone-henge.ffmp
  COMMENT "Embedding the fan missions' patches")

# ---- the options screen's preview pictures (owner's stills of the game, dist/Previews), built
# into tnfree (port/ui/options.c; a Previews folder next to tnfree.exe goes over them)
file(GLOB PREVIEW_PICS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/dist/Previews/*.jpg
     ${CMAKE_SOURCE_DIR}/dist/Previews/*.png)
add_custom_command(
  OUTPUT ${GEN}/previews.S ${GEN}/previews.c
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/embed_previews.py ${GEN} ${SYM_PREFIX}
          ${PREVIEW_PICS}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed_previews.py ${PREVIEW_PICS}
  COMMENT "Embedding the options screen's preview pictures")

add_executable(freefall ${GAME_SOURCES} ${PORT_SOURCES}
               third_party/nuked-opl3/opl3.c
               ${GEN}/tn_image.S ${GEN}/tn_funcmap.c ${GEN}/tn_stubs.c ${GEN}/fonts.c ${GEN}/logo.c ${GEN}/missions.c ${GEN}/visor.c
               ${GEN}/previews.S ${GEN}/previews.c)
target_include_directories(freefall PRIVATE include port/include third_party ${GEN})
if(WIN32)
  enable_language(RC)
  target_sources(freefall PRIVATE port/res/tnfree.rc)
  set_source_files_properties(port/res/tnfree.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/port/res/tnfree.manifest)
endif()
target_link_libraries(freefall PRIVATE ${FREEFALL_SDL_TARGET})
if(FREEFALL_FLUIDSYNTH)
  target_link_libraries(freefall PRIVATE libfluidsynth)
  target_compile_definitions(freefall PRIVATE FREEFALL_FLUIDSYNTH FLUIDSYNTH_NOT_A_DLL)
  set_property(TARGET freefall PROPERTY LINKER_LANGUAGE CXX)
endif()
if(NOT WIN32)
  # frame comparison runs poison the heap like the emulator does (port/test/lockstep.c)
  target_link_options(freefall PRIVATE -Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free)
endif()
# the decompiled C's file and exit calls go to the port (port/include/port_libc.h)
set_source_files_properties(${GAME_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/port/include/port_libc.h")
# TN_LINK_ORIGINAL_DATA: globals live at their original offsets (inside tn_image.S)
target_compile_definitions(freefall PRIVATE TN_LINK_ORIGINAL_DATA)
set_property(TARGET freefall PROPERTY OUTPUT_NAME tnfree)   # tnfree.exe
set_property(TARGET freefall PROPERTY C_STANDARD 11)
set_property(TARGET freefall PROPERTY C_EXTENSIONS ON)
# Watcom C semantics the decompiled code relies on: signed char, wrapping integer overflow,
# type-punned pointers.
target_compile_options(freefall PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-fsigned-char -fwrapv -fno-strict-aliasing>)
if(FREEFALL_WARNINGS)
  target_compile_options(freefall PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall>)
else()
  target_compile_options(freefall PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
endif()
if(WIN32)
  # one self-contained .exe (no MinGW runtime DLLs to ship); a window program, but the log
  # still goes to tnfree.log
  target_link_options(freefall PRIVATE -static -mwindows)
  # always loaded at 0x400000, above the original program's code (0x244000-0x354940): the
  # 3D library's register calls name routines by their original address (port/core/regcall.c),
  # and Windows' random placement put our routines there too (P40)
  target_link_options(freefall PRIVATE -Wl,--image-base=0x400000,--disable-dynamicbase,--disable-reloc-section)
  target_link_libraries(freefall PRIVATE winmm)
endif()

if(FREEFALL_RIFE)
  add_subdirectory(rife)
endif()

# ---- smoke test: loads the original data from the player's game folder
enable_testing()
if(FREEFALL_GAME_DIR)
  add_test(NAME load-original-data COMMAND freefall --check --game ${FREEFALL_GAME_DIR})
endif()
