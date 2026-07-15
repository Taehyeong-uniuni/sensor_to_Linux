# CC-SENSOR-CORE (SNC-01/SNC-04): standalone feature-local subproject.
# See src/features/config/CMakeLists.txt for the full rationale on why
# Foundation's .c sources are vendored directly rather than pulled in via
# add_subdirectory() (CMAKE_SOURCE_DIR mismatch under a nested top-level
# project()). This feature only needs savvy_core (lifecycle/error), no
# JSON/IPC, so it skips cJSON/savvy_protocol entirely.
cmake_minimum_required(VERSION 3.20)
project(sensor_core_health C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_health tests" ON)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

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

add_library(sensor_core_health STATIC
    sensor_lifecycle.c
    sensor_health.c
)
target_include_directories(sensor_core_health PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sensor_core_health PUBLIC savvy_core)

if(SAVVY_BUILD_TESTS)
    enable_testing()

    add_executable(test_sensor_core_health
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/health/test_lifecycle.c
    )
    target_link_libraries(test_sensor_core_health PRIVATE sensor_core_health)
    add_test(NAME SNS-CORE-007-health COMMAND test_sensor_core_health 007)
    add_test(NAME SENSOR-CORE-LIFECYCLE-IDEMPOTENT COMMAND test_sensor_core_health idempotent)
endif()
