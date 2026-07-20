cmake_minimum_required(VERSION 3.20)
project(sensor_stream_result_policy C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Feature-local standalone build - see src/platform/linux/tcp_8141/CMakeLists.txt
# for the rationale (no repo-wide feature-registration extension point).
get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_subdirectory("${SAVVY_REPO_ROOT}/third_party/cJSON" "${CMAKE_CURRENT_BINARY_DIR}/_cjson")
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
add_subdirectory("${SAVVY_REPO_ROOT}/src/protocol" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_protocol")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")
target_include_directories(savvy_protocol PUBLIC "${SAVVY_REPO_ROOT}/include")

add_library(sensor_stream_result_policy STATIC
    result_policy.c
)
target_include_directories(sensor_stream_result_policy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sensor_stream_result_policy PUBLIC savvy_core savvy_protocol)

option(SAVVY_SENSOR_STREAM_BUILD_TESTS "Build sensor_stream_result_policy tests" ON)
if(SAVVY_SENSOR_STREAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${SAVVY_REPO_ROOT}/tests/unit/sensor_stream/result_policy" "${CMAKE_CURRENT_BINARY_DIR}/_tests_result_policy")
endif()
