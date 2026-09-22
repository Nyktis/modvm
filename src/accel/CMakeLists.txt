add_library(modvm_accel OBJECT)
modvm_module_headers(modvm_accel core io host util arch)
include(${MODVM_OS}.cmake)
