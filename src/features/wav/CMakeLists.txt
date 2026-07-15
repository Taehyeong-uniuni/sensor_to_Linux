cmake_minimum_required(VERSION 3.20)
project(sensor_stream_wav C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Werror)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# Only savvy_core is needed here (savvy_status_t / SAVVY_* error codes) -
# no JSON/wire-protocol dependency in this feature, so cJSON/savvy_protocol
# are deliberately not pulled in, to keep this standalone build lean.
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")

add_library(sensor_stream_wav STATIC wav_encoder.c)
target_include_directories(sensor_stream_wav PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sensor_stream_wav PUBLIC savvy_core)

option(SAVVY_SENSOR_STREAM_WAV_BUILD_TESTS "Build sensor_stream_wav tests" ON)
if(SAVVY_SENSOR_STREAM_WAV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
