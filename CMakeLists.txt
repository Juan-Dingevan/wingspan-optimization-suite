# File taken from https://github.com/jvstech/vs-windows-llvm, under MIT License.
cmake_minimum_required(VERSION 3.23)

project(wingspan)

# Added by me; manually point CMake to LLVM's install directory.
set(LLVM_DIR "C:/Users/Juani/LLVM/src/llvm/build/bootstrap/install/lib/cmake/llvm")

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_INSTALL_PREFIX}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(AddLLVM)

# Plugins have to be explicitly enabled for Windows.
set(LLVM_ENABLE_PLUGINS ON)

# Disable RTTI if LLVM was build without it.
if (NOT LLVM_ENABLE_RTTI)
  if (MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /GR-")
  else()
    string(APPEND CMAKE_CXX_FLAGS " -fno-rtti")
  endif()
endif()

include_directories(PUBLIC ${LLVM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)

message(STATUS "Included dirs: ${LLVM_INCLUDE_DIRS}")
message(STATUS "Included dirs: ${CMAKE_SOURCE_DIR}/include")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_subdirectory(lib)