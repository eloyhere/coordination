# coordination：跨平台 C++20 头文件库与示例程序。
# 同一份脚本适用于 Windows / Linux / macOS 上的 MSVC / GCC / Clang。
cmake_minimum_required(VERSION 3.21...4.0)

# 保留 Visual Studio 模板的 MSVC 调试信息行为（“热重载” / EditAndContinue）。
# CMP0141 在 CMake 3.25+ 中默认 NEW；显式设置同时兼容 3.21 ~ 3.24。
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

project(coordination VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
  if(POLICY CMP0141)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>")
  endif()
endif()

include(GNUInstallDirs)
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# ---------------------------------------------------------------------------
# 头文件库
#   include 源根目录，使用者可直接写：
#       #include "structure/tree/AVLTree.h"
#       #include "syntax/SyntaxNodes.h"
# ---------------------------------------------------------------------------
add_library(coordination INTERFACE)
add_library(coordination::coordination ALIAS coordination)

target_include_directories(coordination
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/coordination>
)

target_compile_features(coordination INTERFACE cxx_std_20)

# 源码与头文件含中文注释，MSVC 统一按 UTF-8 解析；通过 INTERFACE 传给所有使用者。
if(MSVC)
  target_compile_options(coordination INTERFACE /utf-8)
endif()

# ---------------------------------------------------------------------------
# 示例程序：目标名 coordination_demo，产物名仍为 coordination(.exe)
# ---------------------------------------------------------------------------
add_executable(coordination_demo coordination.cpp)
set_target_properties(coordination_demo PROPERTIES OUTPUT_NAME coordination)
target_link_libraries(coordination_demo PRIVATE coordination)
coordination_set_warnings(coordination_demo)

# ---------------------------------------------------------------------------
# 测试：tests/CMakeLists.txt 自动收集 tests/test_*.cpp
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# 安装：头文件库、头文件与 CMake 导出目标
# ---------------------------------------------------------------------------
install(TARGETS coordination EXPORT coordinationTargets)

install(DIRECTORY structure syntax
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coordination
        FILES_MATCHING PATTERN "*.h")

install(EXPORT coordinationTargets
        FILE coordinationTargets.cmake
        NAMESPACE coordination::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coordination)
