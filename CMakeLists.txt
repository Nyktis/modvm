cmake_minimum_required(VERSION 3.15)

project(modvm
    VERSION 0.1.0
    DESCRIPTION "A Modular and Multi-arch Virtual Machine Monitor"
    LANGUAGES C
)

# Enforce C11 standard and disable compiler-specific extensions (e.g., GNU extensions)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable strict compiler warnings for quality assurance
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    add_compile_options(-Wall -Wextra -Werror -Wno-unused-value)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    message(STATUS "Clang detected: Enabling Static Thread Safety Analysis")
    add_compile_options(-Wthread-safety)
    add_compile_definitions(WARN_CONTEXT_ANALYSIS)
endif()

option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" ON)

if(ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    message(STATUS "Sanitizers (ASAN & UBSAN) are ENABLED")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(ENABLE_HARDENING "Enable security hardening flags (PIE, RELRO, Stack Protector)" ON)

if(ENABLE_HARDENING)
    message(STATUS "Security Hardening is ENABLED")

    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        add_compile_options(-fstack-protector-strong -O2)
    endif()
endif()

# Detect host CPU architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    set(MODVM_HOST_ARCH "x86_64")
else()
    message(FATAL_ERROR "Unsupported host architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Detect host Operating System
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MODVM_OS "linux")
else()
    message(FATAL_ERROR "Unsupported OS: ${CMAKE_SYSTEM_NAME}")
endif()

set(MODVM_GUEST_ARCH "${MODVM_HOST_ARCH}" CACHE STRING "Guest architecture")
message(STATUS "Configuring ModVM host=${MODVM_OS}-${MODVM_HOST_ARCH} guest=${MODVM_GUEST_ARCH}")
include(CTest)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
