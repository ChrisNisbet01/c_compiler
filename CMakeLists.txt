cmake_minimum_required(VERSION 3.16)
project(c_compiler LANGUAGES C)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimization settings
add_compile_options(-O3)

# LTO settings
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported)

if(lto_supported)
    message(STATUS "LTO (Interprocedural Optimization) is supported by the toolchain.")
endif()

# --- LLVM Integration ---
# Specify LLVM installation prefix based on llvm-config-20 output
set(LLVM_INSTALL_DIR "/usr/lib/llvm-20")
set(LLVM_INCLUDE_DIRS "${LLVM_INSTALL_DIR}/include")
set(LLVM_LIBRARY_DIRS "${LLVM_INSTALL_DIR}/lib")

# Find LLVM with specific components for X86 support
find_package(LLVM REQUIRED COMPONENTS Support Core IRReader X86 TargetMachine)

# Check if LLVM was found and display its version
if(LLVM_FOUND)
    message(STATUS "Found LLVM version ${LLVM_VERSION}")
    # Pass LLVM include directories to the project for all targets
    include_directories(${LLVM_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "LLVM not found. Please ensure LLVM 20.1.8 is installed and CMake can find it.")
endif()
# --- End LLVM Integration ---


# Set path to easy_pc (since it's a sibling)
set(easy_pc_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../easy_pc")

# Include the macros from easy_pc
include("${easy_pc_SOURCE_DIR}/cmake/EasyPcMacros.cmake")

# Add easy_pc as a subdirectory.
add_subdirectory("${easy_pc_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/easy_pc" EXCLUDE_FROM_ALL)

# Add our source
add_subdirectory(src)
