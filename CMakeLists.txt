cmake_minimum_required(VERSION 3.20)
project(sensor_to_linux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

# ON for the host-linux preset (real AF_UNIX SOCK_SEQPACKET, CT-IPC-00x);
# OFF for host-mac, where the mock/in-memory transport is used instead
# because Darwin does not support AF_UNIX SOCK_SEQPACKET.
option(SAVVY_IPC_REAL_TRANSPORT "Build the real Linux AF_UNIX SOCK_SEQPACKET transport" OFF)
option(SAVVY_BUILD_TESTS "Build contract tests" ON)

add_subdirectory(third_party/cJSON)
add_subdirectory(src/core)
add_subdirectory(src/protocol)
if(SAVVY_IPC_REAL_TRANSPORT)
    add_subdirectory(src/platform/linux/ipc)
endif()

if(SAVVY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/contract)
endif()
