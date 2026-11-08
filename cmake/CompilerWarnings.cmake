include_guard(GLOBAL)

# 为项目内目标开启统一的编译告警。
# 只收紧告警级别，不把告警升级为错误（不使用 -Werror / /WX）。
function(coordination_set_warnings target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "coordination_set_warnings(): 目标 '${target}' 不存在")
  endif()

  if(MSVC)
    target_compile_options("${target}" PRIVATE /W4 /permissive-)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
