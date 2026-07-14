add_executable(test_packet test_packet.c)
target_link_libraries(test_packet PRIVATE savvy_protocol savvy_core)

add_test(NAME CT-PKT-001 COMMAND test_packet 001)
add_test(NAME CT-PKT-002 COMMAND test_packet 002)
add_test(NAME CT-PKT-003 COMMAND test_packet 003)
