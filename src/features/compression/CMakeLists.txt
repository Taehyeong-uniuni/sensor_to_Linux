cmake_minimum_required(VERSION 3.20)
project(sensor_stream_compression C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Werror)

# Opt-in ASan+UBSan build for verification (never on by default, so the
# normal -Wall -Wextra -Werror build/test flow this feature is primarily
# validated with is never affected): cmake -S src/features/compression
# -B build/compression-asan -DSENSOR_STREAM_COMPRESSION_SANITIZE=ON.
# Applied at this top-level scope (before the add_subdirectory calls
# below) so savvy_core is instrumented too, giving full coverage across
# the dependency chain actually exercised by the tests.
option(SENSOR_STREAM_COMPRESSION_SANITIZE "Build with ASan+UBSan (verification only)" OFF)
if(SENSOR_STREAM_COMPRESSION_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# Only savvy_core is needed here (savvy_status_t / SAVVY_* error codes) -
# no JSON/wire-protocol dependency in this feature, so cJSON/savvy_protocol
# are deliberately not pulled in, to keep this standalone build lean.
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")

find_package(BZip2 REQUIRED)

add_library(sensor_stream_compression STATIC bzip_codec.c)
target_include_directories(sensor_stream_compression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sensor_stream_compression PUBLIC savvy_core BZip2::BZip2)

option(SAVVY_SENSOR_STREAM_COMPRESSION_BUILD_TESTS "Build sensor_stream_compression tests" ON)
if(SAVVY_SENSOR_STREAM_COMPRESSION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
