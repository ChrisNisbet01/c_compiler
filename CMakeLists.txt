cmake_minimum_required(VERSION 3.16)
project(c_compiler LANGUAGES C)

# Generate compile_commands.json for LSP support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimization settings
add_compile_options(-O3)

# LTO settings
option(ENABLE_LTO "Enable Link Time Optimization" OFF)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT error)
    if(lto_supported)
        message(STATUS "LTO (Interprocedural Optimization) is supported by the toolchain.")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "LTO NOT supported: ${error}")
    endif()
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

# --- easy_pc Integration ---
include(FetchContent)

set(EASY_PC_LOCAL_PATH "" CACHE PATH "Path to local easy_pc repository")

if(DEFINED EASY_PC_LOCAL_PATH AND EXISTS ${EASY_PC_LOCAL_PATH})
  if(NOT IS_DIRECTORY ${EASY_PC_LOCAL_PATH})
    message(FATAL_ERROR "EASY_PC_LOCAL_PATH is not a valid path: ${EASY_PC_LOCAL_PATH}")
  endif()
   

  message(STATUS "Using local easy_pc repository at: ${EASY_PC_LOCAL_PATH}")
  add_subdirectory(${EASY_PC_LOCAL_PATH} easy_pc-build)
else()
  set(EASY_PC_GIT_REPO "git@github.com:ChrisNisbet01/easy_pc.git")
  set(EASY_PC_REPO_TAG "0.4.1")
  message(STATUS "Using github easy_pc repository at: ${EASY_PC_GIT_REPO}:${EASY_PC_REPO_TAG}")
  FetchContent_Declare(
    easy_pc
    GIT_REPOSITORY ${EASY_PC_GIT_REPO}
    GIT_TAG ${EASY_PC_REPO_TAG}
  )
  FetchContent_MakeAvailable(easy_pc)
  
  # Create a dummy Doxyfile if it doesn't exist (to prevent docs target failure)
  if(NOT EXISTS "${easy_pc_SOURCE_DIR}/Doxyfile")
    file(WRITE "${easy_pc_SOURCE_DIR}/Doxyfile" "# Dummy Doxyfile\nPROJECT_NAME = easy_pc\n")
endif()
endif()
   


# --- End easy_pc Integration ---

# Add our source
add_subdirectory(src)
