cmake_minimum_required(VERSION 3.24)
project(OpenSheep VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# The portable core: everything except the host backends. The native build has no platform library at
# all (tests and tools only); the game itself is the gasm build of the same file list.
file(GLOB CORE_SRC src/*.c src/game/*.c)
set(HOST_SRC src/vfs_host.c src/storage_file.c src/input_none.c)
foreach(f ${HOST_SRC} src/platform_gasm.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform: native (headless tests and tools, against the host filesystem) or gasm (opensheep.wasm):
#   tools/fetch-gasm-sdk.sh
#   cmake -S . -B build-gasm -DOPENSHEEP_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
set(OPENSHEEP_PLATFORM native CACHE STRING "Platform: native (tests) or gasm (the game)")
set_property(CACHE OPENSHEEP_PLATFORM PROPERTY STRINGS native gasm)
set(WARN -Wall -Wextra -Wno-unused-parameter)
if(OPENSHEEP_PLATFORM STREQUAL "gasm")
  include(Gasm)
  gasm_add_game(opensheep ${CORE_SRC} src/platform_gasm.c)
  set_target_properties(opensheep PROPERTIES OUTPUT_NAME opensheep)
  target_compile_options(opensheep PRIVATE ${WARN} -O2)
  target_link_options(opensheep PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

if(NOT APPLE)
  link_libraries(m)
endif()

# The whole core compiles natively too (no platform dependency can creep in).
add_library(opensheep_core STATIC ${CORE_SRC} ${HOST_SRC})
target_compile_options(opensheep_core PRIVATE ${WARN})
target_include_directories(opensheep_core PUBLIC src)

# Tests read the data through the file layer (./cd, or OPENSHEEP_DATA=<dir|image>).
foreach(t xp_test play_test)
  add_executable(${t} tests/${t}.c)
  target_link_libraries(${t} opensheep_core)
  target_compile_options(${t} PRIVATE ${WARN})
endforeach()

# Development tools on the same core.
foreach(t overview bik)
  add_executable(${t} tools/${t}.c)
  target_link_libraries(${t} opensheep_core)
  target_compile_options(${t} PRIVATE ${WARN})
endforeach()
