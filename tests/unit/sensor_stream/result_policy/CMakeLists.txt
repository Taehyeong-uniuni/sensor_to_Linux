add_executable(test_result_policy test_result_policy.c)
target_link_libraries(test_result_policy PRIVATE sensor_stream_result_policy savvy_protocol savvy_core)

add_test(NAME SNS-STR-002-quoted-matrix COMMAND test_result_policy 001)
add_test(NAME SNS-STR-002-unquoted-wire-quirk COMMAND test_result_policy 002)
add_test(NAME SNS-STR-002-stream-threshold COMMAND test_result_policy 003)
add_test(NAME SNS-STR-002-voice-no-threshold COMMAND test_result_policy 004)
add_test(NAME SNS-STR-002-parse-error-noop COMMAND test_result_policy 005)
add_test(NAME SNS-STR-002-reset COMMAND test_result_policy 006)
