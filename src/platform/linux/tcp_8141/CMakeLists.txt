cmake_minimum_required(VERSION 3.20)
project(sensor_tcp_8141 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Feature-local standalone build: this feature has its own top-level
# CMakeLists.txt (no repo-wide feature-registration extension point exists
# - confirmed by inspecting the root CMakeLists.txt - and the root file is
# a forbidden path for this session). Foundation is pulled in by relative
# path and re-pointed at the real repo-root include/ directory, since
# savvy_core/savvy_protocol's own CMakeLists.txt use ${CMAKE_SOURCE_DIR}/include,
# which resolves to *this* project's source dir (not the repo root) when
# built standalone like this.
get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

add_subdirectory("${SAVVY_REPO_ROOT}/third_party/cJSON" "${CMAKE_CURRENT_BINARY_DIR}/_cjson")
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
add_subdirectory("${SAVVY_REPO_ROOT}/src/protocol" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_protocol")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")
target_include_directories(savvy_protocol PUBLIC "${SAVVY_REPO_ROOT}/include")

add_library(sensor_tcp_8141 STATIC
    tcp_channel.c
)
target_include_directories(sensor_tcp_8141 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sensor_tcp_8141 PUBLIC savvy_core savvy_protocol)

find_package(Threads REQUIRED)
target_link_libraries(sensor_tcp_8141 PUBLIC Threads::Threads)

option(SAVVY_SENSOR_STREAM_BUILD_TESTS "Build sensor_tcp_8141 tests" ON)
if(SAVVY_SENSOR_STREAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${SAVVY_REPO_ROOT}/tests/unit/sensor_stream/tcp_channel" "${CMAKE_CURRENT_BINARY_DIR}/_tests_tcp_8141")
endif()
