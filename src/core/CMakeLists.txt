add_library(savvy_core STATIC
    error.c
    clock.c
    lifecycle.c
    queue.c
    snapshot.c
)

target_include_directories(savvy_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(savvy_core PUBLIC Threads::Threads)
