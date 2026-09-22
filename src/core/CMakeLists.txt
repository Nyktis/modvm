file(GLOB sources CONFIGURE_DEPENDS "*.c")
add_library(modvm_core OBJECT ${sources})
modvm_module_headers(modvm_core core io host util)
