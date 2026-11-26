# Coordination language toolchain.
#
# Layout:
#   libs/coord   - coorc_core static library (front end, comptime evaluator, C++ backend)
#   tools/coorc  - command line driver
#   tests        - unit + end-to-end tests
#
cmake_minimum_required(VERSION 3.20)

project(coord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COORD_BUILD_TESTS "Build the coord test suite" ON)
option(COORD_BUILD_LEGACY_DEMO "Build the original CMake template demo" OFF)

add_subdirectory(libs/coord)
add_subdirectory(tools/coorc)

if(COORD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# The Visual Studio CMake template artifacts are kept on disk but are not part
# of the language toolchain. Enable this option if you want the old demo target.
if(COORD_BUILD_LEGACY_DEMO)
  add_executable(coordination coordination.cpp coordination.h)
  if(CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET coordination PROPERTY CXX_STANDARD 20)
  endif()
endif()
