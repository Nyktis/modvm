# Selected explicitly by hosts reusing these POSIX implementations.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_sources(modvm_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/page.c
    ${CMAKE_CURRENT_SOURCE_DIR}/io.c
    ${CMAKE_CURRENT_SOURCE_DIR}/console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/error.c)
target_link_libraries(modvm_host PRIVATE Threads::Threads)

# Derive native mappings from the project enum, including aliases in declaration
# order. Independent checks accept equal native values; absent symbols are skipped.
set(errno_header "${PROJECT_SOURCE_DIR}/include/modvm/errno.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${errno_header}")
file(STRINGS "${errno_header}" errno_entries REGEX "^[ \t]*VM_E[A-Z0-9_]+[ \t]*=")
set(errno_map "/* Generated from modvm/errno.h; do not edit. */\n")
foreach(entry IN LISTS errno_entries)
    string(REGEX MATCH "VM_(E[A-Z0-9_]+)" symbol "${entry}")
    set(native "${CMAKE_MATCH_1}")
    string(APPEND errno_map "#ifdef ${native}\n\tif (error == ${native})\n\t\treturn ${symbol};\n#endif\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/errno_map.inc" CONTENT "${errno_map}")
target_include_directories(modvm_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
