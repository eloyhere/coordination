add_executable(coord_tests unit/main.cpp)
target_link_libraries(coord_tests PRIVATE coorc_core)
if(MSVC)
  target_compile_options(coord_tests PRIVATE /EHsc /utf-8)
endif()
add_test(NAME coord_unit COMMAND coord_tests)

# Package manager end-to-end coverage. This test only needs `coorc` itself
# (local file registries, no network, no host compiler) so it runs on every
# platform.
add_test(NAME coord_e2e_packages
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_packages
    -P ${CMAKE_CURRENT_SOURCE_DIR}/package_manager.cmake)

# End-to-end: `coorc build` lowers to C++, invokes the host MSVC, links an
# executable, runs it, and compares the observable destruction order.
if(MSVC)
  add_test(NAME coord_e2e_raii
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/raii.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_raii
      -DEXPECTED=scope1|~B|~A|scope2|~B|~A|done
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_comptime
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/consts.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_consts
      -DEXPECTED=55
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_enum
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/enum.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_enum
      -DEXPECTED=3
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_hello
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_hello
      -DEXPECTED="hello, coordination|0"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_interface
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/interface.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_interface
      -DEXPECTED=point
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_super
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/super.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_super
      -DEXPECTED=animal|dog|4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_polymorphism
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/polymorphism.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_polymorphism
      -DEXPECTED=dog|dog
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_enum_methods
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/enum_methods.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_enum_methods
      -DEXPECTED=color|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional
      -DEXPECTED=empty|also-empty
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_conversion
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/conversion.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_conversion
      -DEXPECTED=1|ok
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_result_error
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/result_error.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_result_error
      -DEXPECTED=before
      -DEXPECTED_EXIT=1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_lengths
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lengths.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lengths
      -DEXPECTED=65|-2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_arrays
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/arrays.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_arrays
      -DEXPECTED=6
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_expect_unwrap
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/expect_unwrap.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_expect_unwrap
      -DEXPECTED=7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_if_let
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/if_let.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_if_let
      -DEXPECTED=7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_imports
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/modules/main.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_imports
      -DEXPECTED=5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_line
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/line_directive.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_line
      -DEXPECTED=ok
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_platform
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/platform.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_platform
      -DEXPECTED=true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true|true
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_coox
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_coox
      -P ${CMAKE_CURRENT_SOURCE_DIR}/coox.cmake)

  add_test(NAME coord_e2e_outline
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello.coo
      -P ${CMAKE_CURRENT_SOURCE_DIR}/outline.cmake)

  add_test(NAME coord_e2e_outline_docs_full
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/docs_outline.coo
      -DEXTRA=--docs
      -DNEEDLE=@return
      -P ${CMAKE_CURRENT_SOURCE_DIR}/outline.cmake)
  add_test(NAME coord_e2e_outline_docs
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/docs_outline.coo
      -DNEEDLE=/// 
      -P ${CMAKE_CURRENT_SOURCE_DIR}/outline.cmake)
  add_test(NAME coord_e2e_project
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/project
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_project
      -DEXPECTED=5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_methods
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_methods.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_methods
      -DEXPECTED=empty|false|5|5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_test_mode
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/test_sample.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_test_mode
      -P ${CMAKE_CURRENT_SOURCE_DIR}/coordin_test.cmake)

  add_test(NAME coord_e2e_deps
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/../examples/deps/coordination.toml
      -P ${CMAKE_CURRENT_SOURCE_DIR}/deps.cmake)

  add_test(NAME coord_e2e_queue
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/queue.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_queue
      -DEXPECTED=3|false|0|1|2|0
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_queue_index
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/queue_index.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_queue_index
      -DEXPECTED=7|2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_queue_for
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/queue_for.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_queue_for
      -DEXPECTED=6
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_queue_literal
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/queue_literal.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_queue_literal
      -DEXPECTED=3|1|3
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_array_view
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/array_view.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_array_view
      -DEXPECTED=3|2|false
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_map
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/map.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_map
      -DEXPECTED=2|true|30|false|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_set
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/set.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_set
      -DEXPECTED=2|true|false|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_unordered
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/unordered.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_unordered
      -DEXPECTED=2|30|2|true
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_length128
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/length128.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_length128
      -DEXPECTED=65|66|132
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_test_filter
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/test_sample.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_test_filter
      -P ${CMAKE_CURRENT_SOURCE_DIR}/filter.cmake
  )

  add_test(NAME coord_e2e_invocation
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation
      -DEXPECTED=before|work|after|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_invocation_multi
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_multi.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_multi
      -DEXPECTED=first-before|second-before|3|second-after|first-after|4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_generated
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/generated.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_generated
      -DEXPECTED=42
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_serializable
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/serializable.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_serializable
      -DEXPECTED=2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_call
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_call.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_call
      -DEXPECTED=trap|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_fields
      -DEXPECTED=set|get|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_trap_args
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_trap_args.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_trap_args
      -DEXPECTED=7|value|7|run|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_invocation_method
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_method.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_method
      -DEXPECTED=before|work|after|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_invocation_method_multi
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_method_multi.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_method_multi
      -DEXPECTED=first-before|second-before|3|second-after|first-after|4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_invocation_virtual
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_virtual.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_virtual
      -DEXPECTED=before|child|after|2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_invocation_values
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_values.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_values
      -DEXPECTED=8
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake
  )

  add_test(NAME coord_e2e_invocation_method_values
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/invocation_method_values.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_invocation_method_values
      -DEXPECTED=8
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_reflection
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/reflection.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_reflection
      -DEXPECTED=Data|class|2|1|first|integer|run|void|2|integer|string|1|Marker|true|false
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership
      -DEXPECTED=7|7|9|8
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership_move
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership_move.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership_move
      -DEXPECTED=1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_ownership
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_ownership.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_ownership
      -DEXPECTED=false|true|7|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_determinism
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/serializable.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_determinism
      -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism.cmake)

  add_test(NAME coord_e2e_two_packages
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_packages/app
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_two_packages
      -DEXPECTED=42
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_two_packages_deps
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_packages/app
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_two_packages_deps
      -DEXPECTED=lib|app
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_deps.cmake)

  add_test(NAME coord_e2e_packages_build
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_packages_build
      -DBUILD=1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/package_manager.cmake)

  add_test(NAME coord_e2e_cycle
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cycle/a
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_cycle
      -DEXPECT_SUBSTRING=cyclic
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_incremental
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/hello.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_incremental
      -P ${CMAKE_CURRENT_SOURCE_DIR}/incremental.cmake)

  add_test(NAME coord_e2e_incremental_coox
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_incremental_coox
      -P ${CMAKE_CURRENT_SOURCE_DIR}/incremental_coox.cmake)

  add_test(NAME coord_e2e_import_selective
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/selective/main.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_import_selective
      -DEXPECTED=4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_import_selective_negative
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/selective/bad.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_import_selective_negative
      -DEXPECT_SUBSTRING=E2004
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_import_alias
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/selective/alias.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_import_alias
      -DEXPECTED=4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_import_alias_type
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/selective/alias_type.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_import_alias_type
      -DEXPECTED=7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_main_args
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/main_args.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_main_args
      -DEXPECTED=1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_main_argc
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/main_argc.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_main_argc
      -DEXPECTED=1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_js_import
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/js_modules/main.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_js_import
      -DEXPECTED=4
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_old_import_removed
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/js_modules/old_import.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_old_import_removed
      -DEXPECT_SUBSTRING=E1101
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_references
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/references.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_references
      -DEXPECTED=6|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_operators_incdec
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/operators_incdec.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_operators_incdec
      -DEXPECTED=10|11|12|12|11|10
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_access
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_access.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_access
      -DEXPECTED=2|missing|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_operators_shift
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/operators_shift.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_operators_shift
      -DEXPECTED=8|0|4|24
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_switch
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch
      -DEXPECTED=two|fallback|green|b
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_destructure
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/destructure.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_destructure
      -DEXPECTED=1|a|get|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_type_aliases
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/type_aliases.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_type_aliases
      -DEXPECTED=3|coordination|1.5|2.5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_std_io
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/std_io.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_std_io
      -DEXPECTED=written|exists|hello|true|true
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_get_class
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/get_class.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_get_class
      -DEXPECTED=Point|Point|Point|integer
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_match
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/match.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_match
      -DEXPECTED=green
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_get_super_class
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/get_super_class.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_get_super_class
      -DEXPECTED=integer|Object|Object
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_reflect_types
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/reflect_types.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_reflect_types
      -DEXPECTED=x|1|1|2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_descriptor_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/descriptor_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_descriptor_fields
      -DEXPECTED=Point|1|Base|2|2|y|add|PointDocs.|integer|integer|LengthDocs.|2|x|y|length|true|false|true|1|-1|0|x|0|integer|Point|1|1|length|integer|Point|0|1|LengthDocs.|2|integer,integer|0
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_float
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/float.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_float
      -DEXPECTED=1.5|3.25|3.25
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_decimal_fixed
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/decimal_fixed.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_decimal_fixed
      -DEXPECTED=1.75|1.25|0.375|6|true|0.868928|3.131072|2.262144|6|1.5|1.5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_determinism_reflection
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/reflection.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_determinism_reflection
      -P ${CMAKE_CURRENT_SOURCE_DIR}/determinism.cmake)

  add_test(NAME coord_e2e_proxy_annotation
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_annotation.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_annotation
      -DEXPECTED=get|value|7|call|run|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_runtime_method
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_runtime_method.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_runtime_method
      -DEXPECTED=trap|work|work
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_runtime_class
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_runtime_class.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_runtime_class
      -DEXPECTED=trap|work|work
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_runtime_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_runtime_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_runtime_fields
      -DEXPECTED=set|7|get|value|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_retention_state
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_retention_state.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_retention_state
      -DEXPECTED=1|run|2|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_per_object
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_per_object.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_per_object
      -DEXPECTED=1|run|2|run|1|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_object_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_object_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_object_fields
      -DEXPECTED=1|2|1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_cleanup
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_cleanup.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_cleanup
      -DEXPECTED=1|run|2|run|1|run|2|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_runtime_reflector_consistency
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/runtime_reflector_consistency.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_runtime_reflector_consistency
      -DEXPECTED=2|2|first|first|1|1|run|run
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership_fields
      -DEXPECTED=16
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership_return
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership_return.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership_return
      -DEXPECTED=7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership_param
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership_param.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership_param
      -DEXPECTED=7|9
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_ownership_initializer
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/ownership_initializer.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_ownership_initializer
      -DEXPECTED=7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_runtime_retention
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/runtime_retention.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_runtime_retention
      -DEXPECTED=true|false
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_runtime_annotation_value
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/runtime_annotation_value.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_runtime_annotation_value
      -DEXPECTED=true|prod,1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_runtime_reflection
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/runtime_reflection.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_runtime_reflection
      -DEXPECTED=2|baseField|childField|2|baseMethod|Base|true|false|integer|void
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_policies
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_policies.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_policies
      -DEXPECTED=false|0|7|42|0|7|8
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_error
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_error.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_error
      -DEXPECT_SUBSTRING=E2048
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_optional_implicit
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_implicit.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_implicit
      -DEXPECTED=9|5|9
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_constructure
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/constructure.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_constructure
      -DEXPECTED=3|hi|6|ok|9|named
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_constructure_error
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/constructure_error.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_constructure_error
      -DEXPECT_SUBSTRING=E2076
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_destructure_annotation
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/destructure_annotation.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_destructure_annotation
      -DEXPECTED=2|a!
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_proxy_construct_destructure
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/proxy_construct_destructure.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_proxy_construct_destructure
      -DEXPECTED="construct|5|destructure value|5"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_retention_block
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/retention_block.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_retention_block
      -DCOORC_FLAGS=--print-consts
      -DEXPECTED=compile-time|runtime
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_radix
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/radix.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_radix
      -DEXPECTED=658
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_radix_error
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/radix_error.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_radix_error
      -DEXPECT_SUBSTRING=E2066
      -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

  add_test(NAME coord_e2e_default_init
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/default_init.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_default_init
      -DEXPECTED=0|true|false|0|true
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_weak
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/weak.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_weak
      -DEXPECTED=false|true|7|true|7|true|false
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_bounds
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/bounds.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_bounds
      -DEXPECTED=20|5
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_params
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_params.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_params
      -DEXPECTED=0|42|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_division
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/division.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_division
      -DEXPECTED=3|1|-3|-1
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_overflow
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/overflow.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_overflow
      -DEXPECTED=9223372036854775807|9223372036854775807|9223372036854775807
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_fields
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_fields.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_fields
      -DEXPECTED=false|0|0|5|7|7
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_optional_return
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/optional_return.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_optional_return
      -DEXPECTED=false|9|0|0|42
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_compound
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/compound.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_compound
      -DEXPECTED=7|21|21|22|23|11|3|2
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_shift
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/shift.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_shift
      -DEXPECTED=8|16|12|18446744073709551616
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_share_cycle
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/share_cycle.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_share_cycle
      -DEXPECTED=done
      -DEXPECT_STDERR=W0005
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_share_cycle_ctor
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/share_cycle_ctor.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_share_cycle_ctor
      -DEXPECTED=done
      -DEXPECT_STDERR=W0005
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  if(WIN32)
    add_test(NAME coord_e2e_native_ffi
      COMMAND ${CMAKE_COMMAND}
        -DCOORC=$<TARGET_FILE:coorc>
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native_ffi_windows.coo
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_ffi
        -DEXPECTED=ok
        -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
  else()
    add_test(NAME coord_e2e_native_ffi
      COMMAND ${CMAKE_COMMAND}
        -DCOORC=$<TARGET_FILE:coorc>
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native_ffi_posix.coo
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_ffi
        -DEXPECTED=ok
        -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
  endif()

  add_test(NAME coord_e2e_native_ffi_string
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native_ffi_string.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_ffi_string
      -DEXPECTED=5|ok
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

  add_test(NAME coord_e2e_lambda
    COMMAND ${CMAKE_COMMAND}
      -DCOORC=$<TARGET_FILE:coorc>
      -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda.coo
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda
      -DEXPECTED=42|15|42
      -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
endif()
