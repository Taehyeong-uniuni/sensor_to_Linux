add_library(savvy_platform_ipc STATIC
    ipc_transport_common.c
    ipc_client.c
)

target_include_directories(savvy_platform_ipc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(savvy_platform_ipc PUBLIC savvy_core savvy_protocol)
