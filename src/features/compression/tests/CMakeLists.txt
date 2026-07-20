# SNS-STR-004's BZip-half requirement composes with Part 1's WAV feature
# (sensor_wav_wrap): compress/decompress actual WAV-wrapped bytes and
# verify byte-for-byte equality with the original. wav_encoder.c is
# compiled directly as an extra source of this test executable (rather
# than add_subdirectory-ing ../../wav's own standalone CMake project),
# deliberately avoiding pulling in - and thereby double-registering -
# the wav feature's own ctest suite inside this feature's build tree;
# each feature's tests stay registered only in its own build/<feature>.
add_executable(test_bzip
    test_bzip.c
    "${SAVVY_REPO_ROOT}/src/features/wav/wav_encoder.c"
)
target_include_directories(test_bzip PRIVATE "${SAVVY_REPO_ROOT}/src/features/wav/include")
target_link_libraries(test_bzip PRIVATE sensor_stream_compression savvy_core)

add_test(NAME SNS-STR-005-BZIP-001-ROUNDTRIP COMMAND test_bzip 001)
add_test(NAME SNS-STR-005-BZIP-002-LARGER-OUTPUT COMMAND test_bzip 002)
add_test(NAME SNS-STR-005-BZIP-003-CORRUPT-INPUT COMMAND test_bzip 003)
add_test(NAME SNS-STR-005-BZIP-004-EMPTY-INPUT COMMAND test_bzip 004)
add_test(NAME SNS-STR-004-BZIP-005-WAV-ROUNDTRIP COMMAND test_bzip 005)
add_test(NAME SNS-STR-005-BZIP-006-LEAK-LOOP COMMAND test_bzip 006)
