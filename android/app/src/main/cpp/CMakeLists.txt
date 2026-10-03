cmake_minimum_required(VERSION 3.22)
project(heartsoul_android C)

# The game is compiled here with the NDK. Its data (maps, scripts, graphics
# tables, music) is assembled beforehand by port/android-data.sh, which needs
# the GNU assembler and so runs in the Docker image. That script also runs the
# asset tools, so it has to be run once before the first Gradle build and
# again whenever data or assets change.
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
# Gradle configures this once per ABI. Each ABI links the data assembled for
# its pointer size: 4-byte pointers for armeabi-v7a, 8-byte (VER_64BIT) for
# arm64-v8a.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(GAME_DATA_NAME game_data64.o)
    set(ABI_DEFINES -DVER_64BIT)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(GAME_DATA_NAME game_data.o)
    set(ABI_DEFINES)
else()
    message(FATAL_ERROR "Unsupported ABI ${ANDROID_ABI}")
endif()
set(GAME_DATA_OBJECT "${ROOT}/build/android/${GAME_DATA_NAME}")
if(NOT EXISTS "${GAME_DATA_OBJECT}")
    message(FATAL_ERROR "build/android/${GAME_DATA_NAME} is missing. Run port/android-data.sh first.")
endif()

set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/android/SDL2" "${CMAKE_BINARY_DIR}/SDL2")

# preproc turns the game's _("text") strings and INCBINs into C. It runs on
# the build machine, so it is built with the host compiler.
set(PREPROC "${CMAKE_BINARY_DIR}/host/preproc")
file(GLOB PREPROC_SOURCES "${ROOT}/tools/preproc/*.cpp")
add_custom_command(
    OUTPUT "${PREPROC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/host"
    COMMAND c++ -std=c++11 -O2 -w ${PREPROC_SOURCES} -o "${PREPROC}"
    DEPENDS ${PREPROC_SOURCES}
    VERBATIM)

set(GAME_DEFINES
    -DMODERN=1 -DTESTING=0 -DPOKEMON_HNS -DNONMATCHING -DPORTABLE
    -DPLATFORM_SDL2 -DRENDERER_EASY_DRAW -DUBFIX ${ABI_DEFINES})
string(REPLACE ";" " " GAME_DEFINES_STR "${GAME_DEFINES}")

file(GLOB_RECURSE GAME_C CONFIGURE_DEPENDS "${ROOT}/src/*.c")
set(GENERATED_C)
foreach(SOURCE IN LISTS GAME_C)
    file(RELATIVE_PATH RELATIVE_SOURCE "${ROOT}" "${SOURCE}")
    set(OUTPUT "${CMAKE_BINARY_DIR}/generated/${RELATIVE_SOURCE}")
    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    # The depfile makes a header or asset change re-run the preprocessing.
    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIR}"
        COMMAND /bin/sh -c "cd '${ROOT}' && '${CMAKE_C_COMPILER}' --target=${CMAKE_C_COMPILER_TARGET} --sysroot='${CMAKE_SYSROOT}' -E -std=gnu17 -Wno-trigraphs -iquote include -I android/SDL2/include ${GAME_DEFINES_STR} -MD -MT '${OUTPUT}' -MF '${OUTPUT}.d' '${RELATIVE_SOURCE}' -o '${OUTPUT}.i' && '${PREPROC}' -i '${RELATIVE_SOURCE}' charmap.txt < '${OUTPUT}.i' > '${OUTPUT}'"
        DEPENDS "${SOURCE}" "${PREPROC}" "${ROOT}/charmap.txt"
        DEPFILE "${OUTPUT}.d"
        VERBATIM)
    list(APPEND GENERATED_C "${OUTPUT}")
endforeach()

set_source_files_properties("${GAME_DATA_OBJECT}" PROPERTIES EXTERNAL_OBJECT TRUE)

add_library(main SHARED ${GENERATED_C} "${GAME_DATA_OBJECT}")
target_compile_options(main PRIVATE
    -std=gnu17
    -O2
    -fno-builtin
    -fno-strict-aliasing
    -fwrapv
    -Wno-everything)
# The script engine's effect tag is bit 1 of a function pointer, so functions
# must be 4-byte aligned. AArch64 code always is; 32-bit ARM needs ARM mode,
# since Thumb functions are only 2-byte aligned.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_options(main PRIVATE -marm)
endif()
# The game has thousands of global names. Binding them inside the library
# keeps a same-named function in a system library from being used instead.
target_link_options(main PRIVATE -Wl,-Bsymbolic)
target_link_libraries(main PRIVATE SDL2::SDL2 m log)

option(HNS_ASAN "Build the game with AddressSanitizer" OFF)
if(HNS_ASAN)
    # Recover: report every error and keep running, so one harmless read does
    # not stop a session that is hunting for something else.
    target_compile_options(main PRIVATE -fsanitize=address -fsanitize-recover=address -fno-omit-frame-pointer)
    target_link_options(main PRIVATE -fsanitize=address)
endif()
