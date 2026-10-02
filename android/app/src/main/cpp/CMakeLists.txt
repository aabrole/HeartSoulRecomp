cmake_minimum_required(VERSION 3.22)
project(heartsoul_android C)

# The game is compiled here with the NDK. Its data (maps, scripts, graphics
# tables, music) is assembled beforehand by port/android-data.sh, which needs
# the GNU assembler and so runs in the Docker image. That script also runs the
# asset tools, so it has to be run once before the first Gradle build and
# again whenever data or assets change.
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
set(GAME_DATA_OBJECT "${ROOT}/build/android/game_data.o")
if(NOT EXISTS "${GAME_DATA_OBJECT}")
    message(FATAL_ERROR "build/android/game_data.o is missing. Run port/android-data.sh first.")
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
    -DPLATFORM_SDL2 -DRENDERER_EASY_DRAW -DUBFIX)
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
    # ARM mode keeps functions 4-byte aligned, which the script engine's
    # effect tag (bit 1 of a function pointer) depends on.
    -marm
    -fno-builtin
    -fno-strict-aliasing
    -fwrapv
    -Wno-everything)
# The game has thousands of global names. Binding them inside the library
# keeps a same-named function in a system library from being used instead.
target_link_options(main PRIVATE -Wl,-Bsymbolic)
target_link_libraries(main PRIVATE SDL2::SDL2 m log)
