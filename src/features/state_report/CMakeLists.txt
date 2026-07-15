# CC-SENSOR-CORE (SNS-CORE-003b): standalone feature-local subproject.
# Pure in-memory dedup tracker + typed payload structs, no JSON/IPC/snapshot
# need, so unlike sibling features this has zero Foundation dependency.
cmake_minimum_required(VERSION 3.20)
project(sensor_core_state_report C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_state_report tests" ON)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_library(sensor_core_state_report STATIC
    state_report.c
)
target_include_directories(sensor_core_state_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(SAVVY_BUILD_TESTS)
    enable_testing()
    add_executable(test_sensor_core_state_report
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/state_report/test_state_report.c
    )
    target_link_libraries(test_sensor_core_state_report PRIVATE sensor_core_state_report)
    add_test(NAME SNS-CORE-001-state-report COMMAND test_sensor_core_state_report 001)
    add_test(NAME SNS-CORE-003b COMMAND test_sensor_core_state_report 003b)
endif()
