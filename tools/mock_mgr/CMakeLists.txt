# CC-SENSOR-CORE test tool: standalone subproject (same rationale as
# src/features/config/CMakeLists.txt for vendoring Foundation's .c
# sources rather than add_subdirectory()). Uses real AF_UNIX
# SOCK_SEQPACKET server calls directly (socket/bind/listen/accept) since
# Foundation ships no server-role helper in this repo (MGR=server is
# mgr_to_Linux's own concern) - only functionally usable on Linux; on
# macOS, socket(AF_UNIX, SOCK_SEQPACKET, ...) compiles but fails at
# runtime with EPROTONOSUPPORT (Darwin does not support SOCK_SEQPACKET for
# AF_UNIX), matching why src/features/mgr_ipc's real transport is also
# Linux-only.
cmake_minimum_required(VERSION 3.20)
project(mock_mgr C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

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

add_executable(mock_mgr mock_mgr.c)
target_link_libraries(mock_mgr PRIVATE savvy_protocol savvy_core)
