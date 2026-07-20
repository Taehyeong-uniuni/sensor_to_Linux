add_library(cjson STATIC
    cJSON.c
)

target_include_directories(cjson PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Vendored, unmodified upstream source (see UPSTREAM.md) - do not hold it
# to this project's -Werror bar.
target_compile_options(cjson PRIVATE -Wno-error)
