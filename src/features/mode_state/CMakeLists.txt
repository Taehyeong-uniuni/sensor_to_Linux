# CC-SENSOR-CORE (SNS-CORE-002a/SNS-CORE-003): standalone feature-local
# subproject. Pure integer/boolean raw->runtime mode derivation - no
# JSON/IPC/snapshot need, so unlike sibling features this pulls in zero
# Foundation headers/libraries (plain C11 stdint.h/stdbool.h only).
cmake_minimum_required(VERSION 3.20)
project(sensor_core_mode_state C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_mode_state tests" ON)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_library(sensor_core_mode_state STATIC
    mode_state.c
)
target_include_directories(sensor_core_mode_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(SAVVY_BUILD_TESTS)
    enable_testing()
    add_executable(test_sensor_core_mode_state
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/mode_state/test_mode_state.c
    )
    target_link_libraries(test_sensor_core_mode_state PRIVATE sensor_core_mode_state)
    add_test(NAME SNS-CORE-002a COMMAND test_sensor_core_mode_state 002a)
    add_test(NAME SNS-CORE-003-mode-state COMMAND test_sensor_core_mode_state 003)
endif()
