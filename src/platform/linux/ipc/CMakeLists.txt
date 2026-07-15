add_library(savvy_platform_ipc STATIC
    ipc_transport_common.c
    ipc_client.c
)

target_include_directories(savvy_platform_ipc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(savvy_platform_ipc PUBLIC savvy_core savvy_protocol)

# FG-M-01: the scripted-poll() fault-injection seam (TC-H-01) exists ONLY
# in test builds. PRIVATE so it affects only how ipc_transport_common.c
# itself is compiled here - it never leaks into this target's PUBLIC
# include path or into any consumer's own compile definitions. A
# SAVVY_BUILD_TESTS=OFF build produces a libsavvy_platform_ipc.a with no
# savvy_test_poll_override symbol at all (verify via nm -g) and a
# savvy_do_poll() that never touches a function pointer.
if(SAVVY_BUILD_TESTS)
    target_compile_definitions(savvy_platform_ipc PRIVATE SAVVY_IPC_TEST_HOOKS=1)
endif()
