# CC-SENSOR-CORE (SNC-03): standalone feature-local subproject.
# See src/features/config/CMakeLists.txt for the full rationale on
# vendoring Foundation's .c sources directly instead of add_subdirectory()
# (CMAKE_SOURCE_DIR mismatch under a nested top-level project()).
#
# SENSOR_MGR_IPC_REAL_TRANSPORT mirrors Foundation's own
# SAVVY_IPC_REAL_TRANSPORT option: real AF_UNIX SOCK_SEQPACKET is
# Linux-only in this repo (host-mac's CMakePresets.json entry always sets
# the Foundation equivalent OFF), so it defaults OFF here too. With it
# OFF, this feature still builds and its full connect/recv/reconnect/
# cancel-destroy state machine is still fully unit-tested, using a fake
# connector (tests/unit/sensor_core/mgr_ipc/fake_transport.*) standing in
# for Foundation's real savvy_ipc_client_connect_cancelable(). Only the
# genuine AF_UNIX SOCK_SEQPACKET behavior itself needs
# SENSOR_MGR_IPC_REAL_TRANSPORT=ON, verified separately on Ubuntu 22.04
# arm64 Docker against tools/mock_mgr.
cmake_minimum_required(VERSION 3.20)
project(sensor_core_mgr_ipc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(SAVVY_BUILD_TESTS "Build sensor_core_mgr_ipc tests" ON)
option(SENSOR_MGR_IPC_REAL_TRANSPORT "Build the real Linux AF_UNIX SOCK_SEQPACKET connector" OFF)

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

add_library(savvy_platform_interfaces STATIC
    ${SAVVY_REPO_ROOT}/src/platform/interfaces/ipc_reconnect.c
)
target_include_directories(savvy_platform_interfaces PUBLIC ${SAVVY_REPO_ROOT}/include)

add_library(sensor_core_mgr_ipc STATIC
    mgr_ipc_client.c
)
target_include_directories(sensor_core_mgr_ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sensor_core_mgr_ipc PUBLIC savvy_protocol savvy_core savvy_platform_interfaces)

if(SENSOR_MGR_IPC_REAL_TRANSPORT)
    add_library(savvy_platform_ipc STATIC
        ${SAVVY_REPO_ROOT}/src/platform/linux/ipc/ipc_transport_common.c
        ${SAVVY_REPO_ROOT}/src/platform/linux/ipc/ipc_client.c
    )
    target_include_directories(savvy_platform_ipc PUBLIC ${SAVVY_REPO_ROOT}/include)
    target_link_libraries(savvy_platform_ipc PUBLIC savvy_core savvy_protocol)

    # ipc_transport_common.c (above) already defines
    # savvy_ipc_cancel_source_*, so cancel_source_portable.c must NOT
    # also be compiled into this build - two definitions of the same
    # symbols would fail to link.
    target_sources(sensor_core_mgr_ipc PRIVATE real_connector.c)
    target_compile_definitions(sensor_core_mgr_ipc PUBLIC SENSOR_MGR_IPC_REAL_TRANSPORT=1)
    target_link_libraries(sensor_core_mgr_ipc PUBLIC savvy_platform_ipc)
else()
    # Foundation's own savvy_ipc_cancel_source_* implementation lives
    # inside src/platform/linux/ipc/ipc_transport_common.c, bundled in the
    # same translation unit as Linux-only send()/MSG_NOSIGNAL socket code
    # that will not compile on macOS - see cancel_source_portable.c's own
    # header comment for the full rationale. This client still needs a
    # real cancel source (used for both the connector wait and, via a
    # fake connector, its own tests) regardless of whether the real
    # transport is being built, so a portable reimplementation of just
    # those 4 pipe()-based functions is vendored here instead.
    target_sources(sensor_core_mgr_ipc PRIVATE cancel_source_portable.c)
endif()

if(SAVVY_BUILD_TESTS)
    enable_testing()

    add_executable(test_sensor_core_mgr_ipc
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/mgr_ipc/test_mgr_ipc_client.c
        ${SAVVY_REPO_ROOT}/tests/unit/sensor_core/mgr_ipc/fake_transport.c
    )
    target_link_libraries(test_sensor_core_mgr_ipc PRIVATE sensor_core_mgr_ipc)
    add_test(NAME SNS-CORE-003a COMMAND test_sensor_core_mgr_ipc 003a)
    add_test(NAME SNS-CORE-003b-mgr-ipc COMMAND test_sensor_core_mgr_ipc 003b)
    add_test(NAME CT-IPC-002 COMMAND test_sensor_core_mgr_ipc ct-ipc-002)
    add_test(NAME SNS-CORE-006 COMMAND test_sensor_core_mgr_ipc 006)
    add_test(NAME SNS-CORE-007-mgr-ipc COMMAND test_sensor_core_mgr_ipc 007)

    # Real-transport integration test (Linux Docker only): needs a
    # pre-built tools/mock_mgr binary, since that tool is its own
    # standalone subproject and add_subdirectory()-ing it here would
    # collide with the savvy_core/savvy_protocol/cjson targets already
    # defined above. Build tools/mock_mgr separately first, then
    # reconfigure this feature with -DMOCK_MGR_BINARY=<path to it>.
    if(SENSOR_MGR_IPC_REAL_TRANSPORT)
        set(MOCK_MGR_BINARY "" CACHE FILEPATH "Path to a pre-built tools/mock_mgr binary")
        if(MOCK_MGR_BINARY)
            add_executable(test_mgr_ipc_real_integration
                ${SAVVY_REPO_ROOT}/tests/integration/sensor_core/mgr_ipc/test_real_transport_integration.c
            )
            target_link_libraries(test_mgr_ipc_real_integration PRIVATE sensor_core_mgr_ipc)
            target_compile_definitions(test_mgr_ipc_real_integration PRIVATE MOCK_MGR_BINARY_PATH="${MOCK_MGR_BINARY}")
            add_test(NAME CT-IPC-002-real COMMAND test_mgr_ipc_real_integration)
        else()
            message(STATUS "mgr_ipc: SENSOR_MGR_IPC_REAL_TRANSPORT is ON but MOCK_MGR_BINARY is not set - skipping the real-transport integration test. Build tools/mock_mgr separately, then reconfigure with -DMOCK_MGR_BINARY=<path>.")
        endif()
    endif()
endif()
