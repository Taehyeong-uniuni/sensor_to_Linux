# CC-SENSOR-CORE (SNC-02): standalone feature-local subproject.
#
# Foundation exposes no feature auto-registration extension point and no
# find_package()/installed-config consumption path (verified 2026-07-15):
# every savvy_* target's own CMakeLists.txt hardcodes
# target_include_directories(<tgt> PUBLIC ${CMAKE_SOURCE_DIR}/include), and
# CMAKE_SOURCE_DIR is fixed to whatever directory `cmake -S` was pointed at
# for the WHOLE configure run - it does not shift back to the repo root just
# because a nested add_subdirectory() re-enters src/core or src/protocol.
# Configuring this feature standalone via `cmake -S src/features/config`
# would therefore make Foundation's own add_subdirectory() wiring resolve
# ${CMAKE_SOURCE_DIR}/include to a nonexistent path.
#
# Fix: vendor Foundation's .c sources directly into locally-defined targets
# here, with include dirs pointed explicitly at the real repo root. This
# reads Foundation's frozen source files but never modifies them, and never
# touches the forbidden root CMakeLists.txt/cmake/** to add a subdirectory
# entry. third_party/cJSON is the one exception pulled in via
# add_subdirectory(), since its own CMakeLists.txt uses
# CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) and is safe under any
# top-level project.
cmake_minimum_required(VERSION 3.20)
project(sensor_core_config C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_config tests" ON)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_subdirectory(${SAVVY_REPO_ROOT}/third_party/cJSON ${CMAKE_CURRENT_BINARY_DIR}/_vendor/cjson)

add_library(savvy_core STATIC
    ${SAVVY_REPO_ROOT}/src/core/error.c
    ${SAVVY_REPO_ROOT}/src/core/clock.c
    ${SAVVY_REPO_ROOT}/src/core/lifecycle.c
    ${SAVVY_REPO_ROOT}/src/core/queue.c
    ${SAVVY_REPO_ROOT}/src/core/snapshot.c
)
target_include_directories(savvy_core PUBLIC ${SAVVY_REPO_ROOT}/include)
find_package(Threads REQUIRED)
target_link_libraries(savvy_core PUBLIC Threads::Threads)

add_library(savvy_protocol STATIC
    ${SAVVY_REPO_ROOT}/src/protocol/packet/packet_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/packet/stream_parser.c
    ${SAVVY_REPO_ROOT}/src/protocol/json/json_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/json/field_table.c
    ${SAVVY_REPO_ROOT}/src/protocol/json/config_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/json/device_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/json/data_result_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/ipc/ipc_envelope_codec.c
    ${SAVVY_REPO_ROOT}/src/protocol/ipc/ipc_action_catalog.c
)
target_include_directories(savvy_protocol PUBLIC ${SAVVY_REPO_ROOT}/include)
target_link_libraries(savvy_protocol PUBLIC savvy_core cjson)
if(UNIX)
    target_link_libraries(savvy_protocol PUBLIC m)
endif()

add_library(sensor_core_config STATIC
    config_store.c
    device_store.c
)
target_include_directories(sensor_core_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sensor_core_config PUBLIC savvy_protocol savvy_core)

if(SAVVY_BUILD_TESTS)
    enable_testing()

    add_executable(test_sensor_core_config
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/config/test_config_store.c
    )
    target_link_libraries(test_sensor_core_config PRIVATE sensor_core_config)
    add_test(NAME SNS-CORE-001-config COMMAND test_sensor_core_config 001)
    add_test(NAME SNS-CORE-002 COMMAND test_sensor_core_config 002)
    add_test(NAME SNS-CORE-005-config COMMAND test_sensor_core_config 005)
endif()
