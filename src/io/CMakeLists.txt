file(GLOB sources CONFIGURE_DEPENDS "*.c")
add_library(modvm_io OBJECT ${sources})
modvm_module_headers(modvm_io io host util)
