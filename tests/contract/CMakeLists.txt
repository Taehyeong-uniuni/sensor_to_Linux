add_executable(test_packet test_packet.c)
target_link_libraries(test_packet PRIVATE savvy_protocol savvy_core)

add_test(NAME CT-PKT-001 COMMAND test_packet 001)
add_test(NAME CT-PKT-002 COMMAND test_packet 002)
add_test(NAME CT-PKT-003 COMMAND test_packet 003)

add_executable(test_json test_json.c)
target_link_libraries(test_json PRIVATE savvy_protocol savvy_core)

add_test(NAME CT-JSON-001 COMMAND test_json 001)
add_test(NAME CT-JSON-002 COMMAND test_json 002)

# Real AF_UNIX SOCK_SEQPACKET transport - Docker Ubuntu 22.04 only (Darwin
# does not support SOCK_SEQPACKET for AF_UNIX).
if(SAVVY_IPC_REAL_TRANSPORT)
    add_executable(test_ipc test_ipc.c)
    target_link_libraries(test_ipc PRIVATE savvy_platform_ipc savvy_platform_interfaces savvy_protocol savvy_core)

    add_test(NAME CT-IPC-001 COMMAND test_ipc 001)
    add_test(NAME CT-IPC-002 COMMAND test_ipc 002)
    add_test(NAME CT-IPC-003 COMMAND test_ipc 003)
    # Supplementary - V0B-H-02 strict cancellable I/O. Deliberately NOT
    # named CT-IPC-004: this is additional coverage beyond CC-FOUNDATION's
    # required CT-IPC-001~003, not a renumbering of them.
    add_test(NAME CT-IPC-CANCEL COMMAND test_ipc 004)
endif()
