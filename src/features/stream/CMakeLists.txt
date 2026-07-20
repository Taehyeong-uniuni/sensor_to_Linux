cmake_minimum_required(VERSION 3.20)
project(sensor_stream_feature C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Feature-local standalone build. IMPORTANT: this feature composes FOUR
# sibling features (tcp_8141 transport, result_policy, wav, compression),
# each of which ALSO has its own independent standalone CMakeLists.txt
# (project() + its own add_subdirectory of Foundation) for its own
# isolated `cmake -S <dir> -B build/<dir>` build. Do NOT add_subdirectory
# those sibling CMakeLists.txt files here - each independently pulls in
# Foundation (src/core, src/protocol, third_party/cJSON), and doing that
# four times within this ONE configure run would redefine the savvy_core/
# savvy_protocol/cjson targets multiple times (a CMake error - target
# names must be unique within one build tree). Instead: bring in
# Foundation exactly once here, then compile each sibling's .c sources
# directly into new targets scoped to THIS build tree. This is deliberate
# duplication across independent build trees (this one vs. each sibling's
# own standalone build), not a shared/cached dependency - acceptable and
# expected given there is no repo-wide feature-registration/root-CMake
# orchestration available (see src/platform/linux/tcp_8141/CMakeLists.txt
# for the same rationale in more detail).
get_filename_component(SAVVY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

add_subdirectory("${SAVVY_REPO_ROOT}/third_party/cJSON" "${CMAKE_CURRENT_BINARY_DIR}/_cjson")
add_subdirectory("${SAVVY_REPO_ROOT}/src/core" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_core")
add_subdirectory("${SAVVY_REPO_ROOT}/src/protocol" "${CMAKE_CURRENT_BINARY_DIR}/_savvy_protocol")
target_include_directories(savvy_core PUBLIC "${SAVVY_REPO_ROOT}/include")
target_include_directories(savvy_protocol PUBLIC "${SAVVY_REPO_ROOT}/include")

find_package(Threads REQUIRED)
find_package(BZip2 REQUIRED)

# --- sibling feature sources, compiled directly (see rationale above) ---
add_library(sensor_tcp_8141 STATIC
    "${SAVVY_REPO_ROOT}/src/platform/linux/tcp_8141/tcp_channel.c"
)
target_include_directories(sensor_tcp_8141 PUBLIC
    "${SAVVY_REPO_ROOT}/src/platform/linux/tcp_8141/include"
)
target_link_libraries(sensor_tcp_8141 PUBLIC savvy_core savvy_protocol Threads::Threads)

add_library(sensor_stream_result_policy STATIC
    "${SAVVY_REPO_ROOT}/src/features/result_policy/result_policy.c"
)
target_include_directories(sensor_stream_result_policy PUBLIC
    "${SAVVY_REPO_ROOT}/src/features/result_policy/include"
)
target_link_libraries(sensor_stream_result_policy PUBLIC savvy_core savvy_protocol)

add_library(sensor_stream_wav STATIC
    "${SAVVY_REPO_ROOT}/src/features/wav/wav_encoder.c"
)
target_include_directories(sensor_stream_wav PUBLIC
    "${SAVVY_REPO_ROOT}/src/features/wav/include"
)
target_link_libraries(sensor_stream_wav PUBLIC savvy_core)

add_library(sensor_stream_compression STATIC
    "${SAVVY_REPO_ROOT}/src/features/compression/bzip_codec.c"
)
target_include_directories(sensor_stream_compression PUBLIC
    "${SAVVY_REPO_ROOT}/src/features/compression/include"
)
target_link_libraries(sensor_stream_compression PUBLIC savvy_core BZip2::BZip2)

# --- this feature itself (SNS-01 session lifecycle + SNS-02 wire commands) ---
add_library(sensor_stream_session STATIC
    session.c
)
target_include_directories(sensor_stream_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sensor_stream_session PUBLIC
    savvy_core
    savvy_protocol
    sensor_tcp_8141
    sensor_stream_result_policy
    sensor_stream_wav
    sensor_stream_compression
)

option(SAVVY_SENSOR_STREAM_BUILD_TESTS "Build sensor_stream_session tests" ON)
if(SAVVY_SENSOR_STREAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${SAVVY_REPO_ROOT}/tests/integration/sensor_stream/session" "${CMAKE_CURRENT_BINARY_DIR}/_tests_session")
endif()
