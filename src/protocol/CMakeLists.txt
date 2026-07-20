add_library(savvy_protocol STATIC
    packet/packet_codec.c
    packet/stream_parser.c
    json/json_codec.c
    json/field_table.c
    json/config_codec.c
    json/device_codec.c
    json/data_result_codec.c
    ipc/ipc_envelope_codec.c
    ipc/ipc_action_catalog.c
)

target_include_directories(savvy_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(savvy_protocol PUBLIC savvy_core cjson)

# json_codec.c's savvy_json_number_to_int32 calls floor()/isfinite(). glibc
# ships these in a separate libm that must be linked explicitly; macOS folds
# libm into libSystem, which is why this only fails to link on Linux.
if(UNIX)
    target_link_libraries(savvy_protocol PUBLIC m)
endif()
