cmake_minimum_required(VERSION 3.20)
project(sensor_stream_mock_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Feature-local standalone build - see src/platform/linux/tcp_8141/CMakeLists.txt
# for the rationale. This tool is a TEST DOUBLE ONLY: it is not linked
# into, and has no dependents in, any production target - it only depends
# on savvy_protocol for wire encoding/decoding, matching real traffic.
get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)

add_subdirectory("${SAVVY_REPO_ROOT}/third_party/cJSON" "${CMAKE_CURRENT_BINARY_DIR}/_cjson")
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
add_subdirectory("${SAVVY_REPO_ROOT}/src/protocol" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_protocol")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")
target_include_directories(savvy_protocol PUBLIC "${SAVVY_REPO_ROOT}/include")

add_executable(mock_streaming_server mock_streaming_server.c)
target_link_libraries(mock_streaming_server PRIVATE savvy_protocol savvy_core)
