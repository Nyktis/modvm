add_library(modvm_backend OBJECT)
modvm_module_headers(modvm_backend io host util)
include(${MODVM_OS}.cmake)
