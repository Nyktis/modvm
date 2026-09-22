target_sources(modvm_backend PRIVATE block/linux_file.c char/posix_stdio.c net/linux_tap.c)
target_include_directories(modvm_backend PRIVATE "${PROJECT_SOURCE_DIR}/src/host/include")
