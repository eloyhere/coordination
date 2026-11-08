# 每个 tests/test_*.cpp 自动生成一个同名可执行目标与同名 CTest 测试。
# CONFIGURE_DEPENDS 使新增/删除测试文件后重新构建时自动重新配置。

file(GLOB coordination_test_sources CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

if(coordination_test_sources)
  foreach(test_source IN LISTS coordination_test_sources)
    get_filename_component(test_name "${test_source}" NAME_WE)

    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE coordination)

    if(COMMAND coordination_set_warnings)
      coordination_set_warnings(${test_name})
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      TIMEOUT 120
    )
  endforeach()
else()
  message(STATUS "coordination: tests/test_*.cpp 尚不存在，暂不生成测试目标")
endif()
