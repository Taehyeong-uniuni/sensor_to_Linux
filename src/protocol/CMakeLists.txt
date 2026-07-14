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
