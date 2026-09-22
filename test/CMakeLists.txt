function(modvm_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE modvm_runtime)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

modvm_add_test(test_virtio_lifetime)
modvm_add_test(test_io_receiver)
include(${MODVM_OS}.cmake)

add_test(NAME test_module_headers COMMAND ${CMAKE_COMMAND}
    -DCC=${CMAKE_C_COMPILER} -DBUILD=${PROJECT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_module_headers.cmake)
