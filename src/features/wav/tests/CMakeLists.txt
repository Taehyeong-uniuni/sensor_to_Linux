add_executable(test_wav test_wav.c)
target_link_libraries(test_wav PRIVATE sensor_stream_wav)

add_test(NAME SNS-STR-004-WAV-001 COMMAND test_wav 001)
add_test(NAME SNS-STR-004-WAV-002 COMMAND test_wav 002)
add_test(NAME SNS-STR-004-WAV-003 COMMAND test_wav 003)
add_test(NAME SNS-STR-004-WAV-004 COMMAND test_wav 004)
