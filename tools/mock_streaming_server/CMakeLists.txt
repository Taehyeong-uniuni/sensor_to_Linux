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
find_package(Threads REQUIRED)

add_executable(mock_streaming_server mock_streaming_server.c)
target_link_libraries(mock_streaming_server PRIVATE savvy_protocol savvy_core)

option(SAVVY_SENSOR_STREAM_MOCK_BUILD_TESTS "Build mock response integration tests" ON)
if(SAVVY_SENSOR_STREAM_MOCK_BUILD_TESTS)
    enable_testing()

    # Compile the production TCP channel into a test-local library. The
    # dependency direction stays test -> production; no production target
    # links this mock server or its harness.
    add_library(sensor_tcp_8141_for_mock_tests STATIC
        "${SAVVY_REPO_ROOT}/src/platform/linux/tcp_8141/tcp_channel.c"
    )
    target_include_directories(sensor_tcp_8141_for_mock_tests PUBLIC
        "${SAVVY_REPO_ROOT}/src/platform/linux/tcp_8141/include"
    )
    target_link_libraries(sensor_tcp_8141_for_mock_tests PUBLIC
        savvy_core
        savvy_protocol
        Threads::Threads
    )

    add_executable(test_mock_response test_mock_response.c)
    target_link_libraries(test_mock_response PRIVATE
        sensor_tcp_8141_for_mock_tests
        savvy_protocol
        savvy_core
        Threads::Threads
    )
    add_dependencies(test_mock_response mock_streaming_server)

    add_test(NAME MOCK-response-header-split
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> header)
    add_test(NAME MOCK-response-body-split
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> body)
    add_test(NAME MOCK-response-delay
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> delay)
    add_test(NAME MOCK-response-timeout
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> timeout)
    add_test(NAME MOCK-late-response-after-timeout
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> late)
    add_test(NAME MOCK-disconnect-before-response-header
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> disconnect-header)
    add_test(NAME MOCK-disconnect-during-response-body
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> disconnect-body)
    add_test(NAME MOCK-stream-failure-voice-healthy
             COMMAND test_mock_response $<TARGET_FILE:mock_streaming_server> isolation)

    set_tests_properties(
        MOCK-response-header-split
        MOCK-response-body-split
        MOCK-response-delay
        MOCK-response-timeout
        MOCK-late-response-after-timeout
        MOCK-disconnect-before-response-header
        MOCK-disconnect-during-response-body
        MOCK-stream-failure-voice-healthy
        PROPERTIES TIMEOUT 15
    )
endif()
