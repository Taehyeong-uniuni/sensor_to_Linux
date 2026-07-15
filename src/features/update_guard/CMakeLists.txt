# CC-SENSOR-CORE (SNC-04): standalone feature-local subproject, no
# Foundation dependency - a mutex-protected bool needs nothing else.
cmake_minimum_required(VERSION 3.20)
project(sensor_core_update_guard C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_update_guard tests" ON)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

find_package(Threads REQUIRED)

add_library(sensor_core_update_guard STATIC
    update_guard.c
)
target_include_directories(sensor_core_update_guard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sensor_core_update_guard PUBLIC Threads::Threads)

if(SAVVY_BUILD_TESTS)
    enable_testing()
    add_executable(test_sensor_core_update_guard
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/update_guard/test_update_guard.c
    )
    target_link_libraries(test_sensor_core_update_guard PRIVATE sensor_core_update_guard)
    add_test(NAME SNS-CORE-004 COMMAND test_sensor_core_update_guard 004)
endif()
