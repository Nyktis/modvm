# Give each compilation target only its declared public module headers. Keep the
# canonical headers in include/; generated views contain no forwarding wrappers.
function(modvm_module_headers target)
    set(view "${PROJECT_BINARY_DIR}/interfaces/${target}")
    set(selected)
    foreach(module IN LISTS ARGN)
        file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/modvm/${module}/*.h")
        list(APPEND selected ${headers})
    endforeach()
    list(APPEND selected "${PROJECT_SOURCE_DIR}/include/modvm/errno.h")
    set(outputs)
    foreach(header IN LISTS selected)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}/include" "${header}")
        configure_file("${header}" "${view}/${relative}" COPYONLY)
        list(APPEND outputs "${view}/${relative}")
    endforeach()
    # A removed or relocated header must not survive in an existing build tree.
    file(GLOB_RECURSE existing "${view}/*.h")
    foreach(header IN LISTS existing)
        if(NOT header IN_LIST outputs)
            file(REMOVE "${header}")
        endif()
    endforeach()
    target_include_directories(${target} PRIVATE "${view}")
endfunction()

foreach(module host util io core hw loader backend accel)
    add_subdirectory(${module})
    target_link_libraries(modvm_${module} PRIVATE modvm_host_options)
endforeach()

# Assemble the runtime here, without making common core code depend on concrete
# drivers. Include every registration object, even when only its constructor is used.
add_library(modvm_runtime INTERFACE)
target_include_directories(modvm_runtime INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(modvm_runtime INTERFACE
    $<TARGET_OBJECTS:modvm_core> $<TARGET_OBJECTS:modvm_io>
    $<TARGET_OBJECTS:modvm_util> $<TARGET_OBJECTS:modvm_hw> $<TARGET_OBJECTS:modvm_loader>
    $<TARGET_OBJECTS:modvm_backend> $<TARGET_OBJECTS:modvm_accel> modvm_host modvm_host_options)

add_executable(modvm main.c)
target_link_libraries(modvm PRIVATE modvm_runtime)
