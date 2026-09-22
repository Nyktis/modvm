file(GLOB sources CONFIGURE_DEPENDS "*/*.c" "board/${MODVM_GUEST_ARCH}/*.c")
add_library(modvm_hw OBJECT ${sources})
modvm_module_headers(modvm_hw hw core io host util arch)
