add_executable(test_session test_session.c)
target_link_libraries(test_session PRIVATE sensor_stream_session savvy_protocol savvy_core)

add_test(NAME SNS-STR-001-stream-full-flow COMMAND test_session 001)
add_test(NAME SNS-STR-003-voice-full-flow COMMAND test_session 003)
add_test(NAME SNS-STR-003-voice-wav-wrap COMMAND test_session 003wav)
add_test(NAME SNS-STR-009-session-isolation COMMAND test_session isolation)
