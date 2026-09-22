file(GLOB sources CONFIGURE_DEPENDS "*.c")
add_library(modvm_util OBJECT ${sources})
modvm_module_headers(modvm_util util host)
