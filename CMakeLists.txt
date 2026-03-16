cmake_minimum_required(VERSION 3.16)
project(c_compiler LANGUAGES C)

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

# Set path to easy_pc (since it's a sibling)
set(easy_pc_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../easy_pc")

# Include the macros from easy_pc
include("${easy_pc_SOURCE_DIR}/cmake/EasyPcMacros.cmake")

# Add easy_pc as a subdirectory.
# We use EXCLUDE_FROM_ALL to avoid building all examples and tests of easy_pc if we don't want to.
add_subdirectory("${easy_pc_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/easy_pc" EXCLUDE_FROM_ALL)

# Add our source
add_subdirectory(src)
