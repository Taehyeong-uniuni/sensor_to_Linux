add_library(savvy_platform_interfaces STATIC
    ipc_reconnect.c
)

target_include_directories(savvy_platform_interfaces PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
