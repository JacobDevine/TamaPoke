cmake_minimum_required(VERSION 3.18)
project(tamapoke CXX C)

# Paths: this file lives in android/app/src/main/cpp.
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(EMU "${ROOT}/tools/emu")
set(SDL_DIR "${ROOT}/android/third_party/SDL2")

if(NOT EXISTS "${SDL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "SDL2 source missing at ${SDL_DIR} -- run: bash android/fetch_sdl.sh")
endif()

# 16 KB page alignment for every .so we build. Android 15+ devices may run
# with 16 KB pages and refuse (or warn about) libraries aligned to 4 KB; NDK r27
# does this by default, r26 needs it said. Set BEFORE add_subdirectory so SDL2
# inherits it too.
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-z,max-page-size=16384")

# --- SDL2, built from source as a shared lib (SDLActivity loads libSDL2.so) ---
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_DIR}" SDL2-build EXCLUDE_FROM_ALL)

# --- The firmware source list is READ from tools/emu/build.sh, never restated.
# A second list here is the "helper that keeps its own copy" trap from
# CLAUDE.md: the next .cpp added to the firmware would build on the desktop and
# fail to link here, or worse, link a stale stub. build.sh is the one list.
file(READ "${EMU}/build.sh" BUILD_SH)
string(REGEX MATCHALL "\\$ROOT/[A-Za-z0-9_]+\\.cpp" FW_REFS "${BUILD_SH}")
if(NOT FW_REFS)
  message(FATAL_ERROR "found no firmware sources in tools/emu/build.sh -- did its g++ line change shape?")
endif()
set(FW_SOURCES "")
foreach(ref ${FW_REFS})
  string(REPLACE "$ROOT" "${ROOT}" path "${ref}")
  list(APPEND FW_SOURCES "${path}")
endforeach()
list(REMOVE_DUPLICATES FW_SOURCES)

# The emulator's own host files the same way, minus its desktop main.
string(REGEX MATCH "\n  sketch\\.cpp[^\n]*\n  [^\n]*" EMU_LINES "${BUILD_SH}")
string(REGEX MATCHALL "[ \t\n]([a-z_]+\\.cpp)" EMU_REFS "${EMU_LINES}")
set(EMU_SOURCES "")
foreach(ref ${EMU_REFS})
  string(STRIP "${ref}" name)
  if(NOT name STREQUAL "sketch.cpp" AND NOT name STREQUAL "main_sdl.cpp")
    list(APPEND EMU_SOURCES "${EMU}/${name}")
  endif()
endforeach()
if(NOT EMU_SOURCES MATCHES "host_impl\\.cpp")
  message(FATAL_ERROR "could not find host_impl.cpp in tools/emu/build.sh")
endif()

# --- The sketch: Arduino generates prototypes for a .ino; we use the
# emulator's genproto.py, regenerated every build so it can never go stale.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN}")
add_custom_command(
  OUTPUT "${GEN}/sketch.cpp" "${GEN}/proto.h"
  COMMAND "${Python3_EXECUTABLE}" "${EMU}/genproto.py" "${ROOT}/TamaPoke.ino"
  COMMAND "${CMAKE_COMMAND}" -DINO=${ROOT}/TamaPoke.ino -DOUT=${GEN}/sketch.cpp
          -P "${CMAKE_CURRENT_SOURCE_DIR}/gen_sketch.cmake"
  WORKING_DIRECTORY "${GEN}"
  DEPENDS "${ROOT}/TamaPoke.ino" "${EMU}/genproto.py" "${CMAKE_CURRENT_SOURCE_DIR}/gen_sketch.cmake"
  COMMENT "Generating sketch.cpp + proto.h from TamaPoke.ino")

# SDLActivity loads "SDL2" then "main".
add_library(main SHARED
  "${GEN}/sketch.cpp"
  ${FW_SOURCES}
  ${EMU_SOURCES}
  "${ROOT}/audio.cpp"     # the board's real audio task (see below)
  phone_save.cpp          # the save backup's .tpsave (tools/emu/tests/phone_test.cpp)
  phone_remind.cpp        # the hunger reminder's forecast (the same test)
  android_main.cpp)

# Stubs FIRST, as build.sh does with -I. -I$ROOT: Arduino.h, Preferences.h and
# friends must shadow anything of the same name.
target_include_directories(main PRIVATE "${GEN}" "${EMU}" "${ROOT}")
# audio.cpp's <ESP_I2S.h>: the FreeRTOS queue and task, and an I2S write that
# feeds SDL's audio queue. Only audio.cpp includes it.
set_source_files_properties("${ROOT}/audio.cpp" PROPERTIES
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/audio_host")
target_compile_features(main PRIVATE cxx_std_17)
target_compile_options(main PRIVATE -w -O2)
target_compile_definitions(main PRIVATE
  TAMA_HOST_FILES          # sprites come from android_main.cpp's .pak index
  TAMA_HOST_WALLCLOCK      # the phone's real clock is the RTC
  TAMA_HOST_BATTERY        # and its battery is the PMU's
  SPRITE_DIR="mons"
  TAMA_REAL_AUDIO)         # host_impl.cpp's silent stubs step aside for audio.cpp
target_link_libraries(main PRIVATE SDL2::SDL2 android log)
