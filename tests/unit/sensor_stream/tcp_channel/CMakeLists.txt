add_executable(test_tcp_channel test_tcp_channel.c)
target_link_libraries(test_tcp_channel PRIVATE sensor_tcp_8141 savvy_protocol savvy_core)

add_test(NAME SNS-STR-006-queue-overflow COMMAND test_tcp_channel 006)
add_test(NAME SNS-STR-007-lifecycle COMMAND test_tcp_channel 007)
add_test(NAME SNS-STR-008-fd-leak COMMAND test_tcp_channel 008)
add_test(NAME SNS-STR-009-channel-isolation COMMAND test_tcp_channel 009)
add_test(NAME SNS-STR-010-stale-response COMMAND test_tcp_channel 010)
