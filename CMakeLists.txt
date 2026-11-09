# coordination：跨平台 C++20 头文件库与示例程序。
# 同一份脚本适用于 Windows / Linux / macOS 上的 MSVC / GCC / Clang。
cmake_minimum_required(VERSION 3.21...4.0)

# 保留 Visual Studio 模板的 MSVC 调试信息行为（“热重载” / EditAndContinue）。
# CMP0141 在 CMake 3.25+ 中默认 NEW；显式设置同时兼容 3.21 ~ 3.24。
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

project(coordination VERSION 0.1.0 LANGUAGES C CXX)

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
# 命令行工具：目标名 coordination_cli，产物名 coordination(.exe)
# ---------------------------------------------------------------------------
add_executable(coordination_cli tools/CoordinationMain.cpp)
set_target_properties(coordination_cli PROPERTIES OUTPUT_NAME coordination)
target_link_libraries(coordination_cli PRIVATE coordination)
coordination_set_warnings(coordination_cli)

if(BUILD_TESTING)
  add_test(NAME cli_version COMMAND coordination_cli version)
  add_test(NAME cli_ast_hello COMMAND coordination_cli ast "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.cn")
  add_test(NAME cli_check_hello COMMAND coordination_cli check "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.cn")
  add_test(NAME cli_check_broken COMMAND coordination_cli check "${CMAKE_CURRENT_SOURCE_DIR}/examples/broken.cn")
  set_tests_properties(cli_check_broken PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_check_cycle COMMAND coordination_cli check "${CMAKE_CURRENT_SOURCE_DIR}/examples/cycle.cn")
  set_tests_properties(cli_check_cycle PROPERTIES WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------------------
# 端到端示例：.cn -> C 源码 -> 可执行文件 -> 运行（build 时生成 C，CTest 跑 exe）
# ---------------------------------------------------------------------------
set(EXAMPLE_C ${CMAKE_CURRENT_BINARY_DIR}/generated/example_main.c)
add_custom_command(
  OUTPUT ${EXAMPLE_C}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND coordination_cli build ${CMAKE_CURRENT_SOURCE_DIR}/examples/main.cn -o ${EXAMPLE_C}
  DEPENDS coordination_cli ${CMAKE_CURRENT_SOURCE_DIR}/examples/main.cn
  COMMENT "生成 examples/main.cn 的 C 源码")
set_source_files_properties(${EXAMPLE_C} PROPERTIES LANGUAGE C)
add_executable(example_main ${EXAMPLE_C})
coordination_set_warnings(example_main)

if(BUILD_TESTING)
  add_test(NAME example_run COMMAND coordination_cli run $<TARGET_FILE:example_main>)
  set_tests_properties(example_run PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 42")
endif()

# ---------------------------------------------------------------------------
# 包级示例：import 图合并构建（examples/package/main.cn -> math/util.cn）
# ---------------------------------------------------------------------------
set(EXAMPLE_PKG_C ${CMAKE_CURRENT_BINARY_DIR}/generated/example_pkg.c)
add_custom_command(
  OUTPUT ${EXAMPLE_PKG_C}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND coordination_cli build ${CMAKE_CURRENT_SOURCE_DIR}/examples/package/main.cn -o ${EXAMPLE_PKG_C}
  DEPENDS coordination_cli ${CMAKE_CURRENT_SOURCE_DIR}/examples/package/main.cn ${CMAKE_CURRENT_SOURCE_DIR}/examples/package/math/util.cn
  COMMENT "生成 examples/package/main.cn 的 C 源码")
set_source_files_properties(${EXAMPLE_PKG_C} PROPERTIES LANGUAGE C)
add_executable(example_pkg ${EXAMPLE_PKG_C})
coordination_set_warnings(example_pkg)

if(BUILD_TESTING)
  add_test(NAME cli_check_package COMMAND coordination_cli check "${CMAKE_CURRENT_SOURCE_DIR}/examples/package/main.cn")
  add_test(NAME example_pkg_run COMMAND coordination_cli run $<TARGET_FILE:example_pkg>)
  set_tests_properties(example_pkg_run PROPERTIES PASS_REGULAR_EXPRESSION "exit code: 42")
endif()

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

install(DIRECTORY structure syntax compiler charset stdlib
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coordination
        FILES_MATCHING PATTERN "*.h")

install(EXPORT coordinationTargets
        FILE coordinationTargets.cmake
        NAMESPACE coordination::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coordination)
