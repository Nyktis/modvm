set(POSIX_TESTS test_driver_registration test_hardening test_loader_regions test_vm_lifecycle test_io_backpressure test_binding test_device_lifetime
    test_io_lifetime test_io_ctx test_io_reentry test_infra
    test_stdio test_stdio_notifications test_virtio_contracts test_virtio_net)
set(LINUX_TESTS test_char_contracts test_ownership test_resource_contracts test_host_service test_host_identity test_io_failures test_host_errors test_board test_io_contracts test_kvm_topology test_linux_boot
    test_net_errors test_pc_mptable test_pci_resources test_raw_boot
    test_runtime_failures test_signal_mask test_virtio_pci_queues)

find_package(Threads REQUIRED)
list(APPEND LINUX_TESTS test_memory_ownership)
foreach(name IN LISTS POSIX_TESTS LINUX_TESTS)
    modvm_add_test(${name})
    target_link_libraries(${name} PRIVATE Threads::Threads)
endforeach()
foreach(name host_errors host_service host_identity io_failures io_contracts)
    target_include_directories(test_${name} PRIVATE "${PROJECT_SOURCE_DIR}/src/host/include")
endforeach()
target_include_directories(test_hardening PRIVATE "${PROJECT_SOURCE_DIR}/src/loader")

target_link_options(test_runtime_failures PRIVATE
        -Wl,--wrap=host_thread_join -Wl,--wrap=calloc -Wl,--wrap=pthread_create -Wl,--wrap=poll -Wl,--wrap=__poll_chk)
target_link_options(test_resource_contracts PRIVATE -Wl,--wrap=calloc)
target_link_options(test_ownership PRIVATE -Wl,--wrap=calloc)
target_link_options(test_io_contracts PRIVATE -Wl,--wrap=poll -Wl,--wrap=__poll_chk)
target_link_options(test_net_errors PRIVATE -Wl,--wrap=open -Wl,--wrap=ioctl -Wl,--wrap=read -Wl,--wrap=__read_chk)
target_link_options(test_io_failures PRIVATE -Wl,--wrap=calloc -Wl,--wrap=malloc -Wl,--wrap=pthread_create)
target_link_options(test_host_errors PRIVATE -Wl,--wrap=host_posix_write_nosigpipe)
target_link_options(test_memory_ownership PRIVATE -Wl,--wrap=calloc -Wl,--wrap=host_page_alloc -Wl,--wrap=host_page_free)
