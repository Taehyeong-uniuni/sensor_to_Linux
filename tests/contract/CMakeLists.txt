add_executable(test_packet test_packet.c)
target_link_libraries(test_packet PRIVATE savvy_protocol savvy_core)

add_test(NAME CT-PKT-001 COMMAND test_packet 001)
add_test(NAME CT-PKT-002 COMMAND test_packet 002)
add_test(NAME CT-PKT-003 COMMAND test_packet 003)

# Real AF_UNIX SOCK_SEQPACKET transport - Docker Ubuntu 22.04 only (Darwin
# does not support SOCK_SEQPACKET for AF_UNIX). CT-IPC-001 (full Android
# action/key catalog conformance) is not yet implemented - pending the
# Android source action-catalog research - so it is intentionally not
# registered here (no stub/fake pass).
if(SAVVY_IPC_REAL_TRANSPORT)
    add_executable(test_ipc test_ipc.c)
    target_link_libraries(test_ipc PRIVATE savvy_platform_ipc savvy_protocol savvy_core)

    add_test(NAME CT-IPC-002 COMMAND test_ipc 002)
    add_test(NAME CT-IPC-003 COMMAND test_ipc 003)
endif()
