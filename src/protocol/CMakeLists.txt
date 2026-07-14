add_library(savvy_protocol STATIC
    packet/packet_codec.c
    packet/stream_parser.c
)

target_include_directories(savvy_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(savvy_protocol PUBLIC savvy_core cjson)
