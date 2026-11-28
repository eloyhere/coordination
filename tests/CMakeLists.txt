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

# --- Native bridge (project-configured C/C++ interop) ----------------------
# The native tests read the toolchain from coordination.toml; the scripts never
# rely on coorc guessing a compiler.
if(MSVC)
  set(COORD_NATIVE_FLAVOR msvc)
  set(COORD_NATIVE_OBJEXT ".obj")
else()
  set(COORD_NATIVE_FLAVOR gnu)
  set(COORD_NATIVE_OBJEXT ".o")
endif()

# Fail-closed configuration: target/toolchain/cc/cxx/objects are mandatory and
# are never defaulted to the host compiler.
add_test(NAME coord_e2e_native_config
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_config
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_config.cmake)

# Annotation syntax: @Native(file:, abi:, symbol:), the removed legacy form and
# the move-only native class diagnostics.
add_test(NAME coord_e2e_native_annotations
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_annotations
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_annotations.cmake)

# C functions through the generated bridge: the project's own build produces the
# object, coorc compiles only bridge + main.
add_test(NAME coord_e2e_native_c_functions
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_c_functions
    -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native
    -DMODE=c
    -DCC=${CMAKE_CXX_COMPILER}
    -DCXX=${CMAKE_CXX_COMPILER}
    -DFLAVOR=${COORD_NATIVE_FLAVOR}
    -DOBJEXT=${COORD_NATIVE_OBJEXT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_build.cmake)

# C++ class through the generated bridge: opaque handle, external new/delete,
# constructor/method overloads and destructor ordering.
add_test(NAME coord_e2e_native_cpp_class
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_cpp_class
    -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native
    -DMODE=class
    -DCC=${CMAKE_CXX_COMPILER}
    -DCXX=${CMAKE_CXX_COMPILER}
    -DFLAVOR=${COORD_NATIVE_FLAVOR}
    -DOBJEXT=${COORD_NATIVE_OBJEXT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_build.cmake)

# Cross-target compile-only: the front end and bridge follow [build].target and
# never fall back to the host compiler.
add_test(NAME coord_e2e_native_cross
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_cross
    -DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/../examples/native
    -DCC=${CMAKE_CXX_COMPILER}
    -DCXX=${CMAKE_CXX_COMPILER}
    -DFLAVOR=${COORD_NATIVE_FLAVOR}
    -DOBJEXT=${COORD_NATIVE_OBJEXT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_cross.cmake)

# @Unsafe/@Safe gating of the migrated native calls.
add_test(NAME coord_e2e_native_unsafe
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_native_unsafe
    -DCC=${CMAKE_CXX_COMPILER}
    -DFLAVOR=${COORD_NATIVE_FLAVOR}
    -DOBJEXT=${COORD_NATIVE_OBJEXT}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/native_unsafe.cmake)

# End-to-end: `coorc build` lowers to C++, compiles and links with the host
# compiler, runs the executable and compares the observable destruction order.
# --- Host-compiler end-to-end suite -----------------------------------------
# Everything below drives coorc only. `check`/`--emit-cpp` stay in the front
# end; `build`/`run`/`test` invoke the host C++ compiler that the driver
# selects (MSVC on Windows, c++/g++ on POSIX). Both the front end and the
# generated C++ are platform-neutral, so these tests are registered on every
# platform. A genuinely OS-bound case must carry its own if(WIN32)/if(UNIX)
# guard with a comment; never wrap this whole section in a compiler guard.
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

# Annotation processors: annotation + ProxyHandler hierarchy, and the Sema
# rejections for non-annotation implementers, missing process overrides and
# invalid process signatures.
add_test(NAME coord_e2e_annotation_processor
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/annotation_processor.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_annotation_processor
    -DEXPECTED=1|get|value|7
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_annotation_processor_class_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/annotation_processor_class_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_annotation_processor_class_error
    -DEXPECT_SUBSTRING=E2060
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_annotation_processor_missing_process_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/annotation_processor_missing_process_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_annotation_processor_missing_process_error
    -DEXPECT_SUBSTRING=E2060
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_annotation_processor_signature_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/annotation_processor_signature_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_annotation_processor_signature_error
    -DEXPECT_SUBSTRING=E2058
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

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

add_test(NAME coord_e2e_switch_old_syntax_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch_old_syntax_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch_old_syntax_error
    "-DEXPECT_SUBSTRING=after 'case'"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_switch_missing_paren_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch_missing_paren_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch_missing_paren_error
    "-DEXPECT_SUBSTRING=after 'case'"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_switch_missing_brace_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch_missing_brace_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch_missing_brace_error
    -DEXPECT_SUBSTRING=must be a block
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_switch_multi_value
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch_multi_value.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch_multi_value
    -DEXPECTED=small
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_switch_multi_value_type_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/switch_multi_value_type_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_switch_multi_value_type_error
    -DEXPECT_SUBSTRING=E2005
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

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

add_test(NAME coord_e2e_fix
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix
    -DEXPECTED=19.99|20|19.98|true|true|2.5|21.49|true|19.99|1.5|3|3
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_radix
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_radix.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_radix
    -DEXPECTED=10|0.1|0.5|0.12|12
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_power_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_power_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_power_error
    -DEXPECT_SUBSTRING=E2103
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_fix_conversion
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_conversion.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_conversion
    -DEXPECTED=1.5|1.1|1.5|2
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_operators
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_operators.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_operators
    -DEXPECTED=1.9|1.1|0.6|3.75|0.3|-1.5|true|0|1.9|1.9|-1.51|3|0.75|2.5|3.5|2.5
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_division_target
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_division_target.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_division_target
    -DEXPECTED=0.33|0.3333
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_lengths
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_lengths.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_lengths
    -DEXPECTED=2|3|1.25
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_wide_product
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_wide_product.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_wide_product
    -DEXPECTED=2.25
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_conversion_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_conversion_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_conversion_error
    -DEXPECT_SUBSTRING=E2109
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_fix_bitwise_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_bitwise_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_bitwise_error
    -DEXPECT_SUBSTRING=E2111
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_fix_scale_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_scale_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_scale_error
    -DEXPECT_SUBSTRING=E2102
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_fix128_operators
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_operators.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_operators
    -DEXPECTED=1.5|0.4|1.9|1.1|0.6|0.6|3.75|0.3|-1.5|true|0|1.9|1.9|-1.51|3|0.75|2.5|3.5|2.5
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix128_integer
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_integer.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_integer
    -DEXPECTED=170141183460469231731687303715884105727|170141183460469231731687303715884105726|56713727820156410577229101238628035242|1|1267650600228229401496703205376|-1|1000000000000000000000000000000000000|100000000000000000000000000000000000000|11111111|fff
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix128_conversion
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_conversion.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_conversion
    -DEXPECTED=1.5|1.1|1.5|1.5|1000000000000000000|11|0.5
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix128_overflow
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_overflow.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_overflow
    -DEXPECT_STDERR=fix overflow
    -P ${CMAKE_CURRENT_SOURCE_DIR}/runtime_fail.cmake)

add_test(NAME coord_e2e_fix128_divzero
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_divzero.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_divzero
    -DEXPECT_STDERR=fix division by zero
    -P ${CMAKE_CURRENT_SOURCE_DIR}/runtime_fail.cmake)

add_test(NAME coord_e2e_fix128_literal_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix128_literal_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix128_literal_error
    -DEXPECT_SUBSTRING=E2105
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_fix_copy
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_copy.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_copy
    -DEXPECTED=1.5|1.5|1.5|1.5|1.5|3|3
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_fix_container_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/fix_container_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_fix_container_error
    -DEXPECT_SUBSTRING=E2109
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

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
    -DEXPECT_SUBSTRING=E2100
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_radix_zero_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/radix_zero_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_radix_zero_error
    -DEXPECT_SUBSTRING=E2100
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

add_test(NAME coord_e2e_lambda
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda
    -DEXPECTED=42|15|16|21|20|10
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_lambda_capture
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_capture.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_capture
    -DEXPECTED=5|12|30|101|4|0|15|15
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_lambda_this
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_this.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_this
    -DEXPECT_SUBSTRING=E2087
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lambda_interface
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_interface.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_interface
    -DEXPECTED=42|10|noisy
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_match_patterns
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/match_patterns.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_match_patterns
    -DEXPECTED=small|b|green|positive|outside|on
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_if_expression
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/if_expression.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_if_expression
    -DEXPECTED=9|big|1|yes
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_match_missing
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/match_missing.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_match_missing
    -DEXPECT_SUBSTRING=E2077
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_match_no_wildcard
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/match_no_wildcard.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_match_no_wildcard
    -DEXPECT_SUBSTRING=E2077
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_match_bad_pattern
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/match_bad_pattern.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_match_bad_pattern
    -DEXPECT_SUBSTRING=E2024
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_if_expression_type_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/if_expression_type_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_if_expression_type_error
    -DEXPECT_SUBSTRING=E2005
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_method_ref
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/method_ref.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_method_ref
    -DEXPECTED=42|15|11|25|17
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_method_ref_escape
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/method_ref_escape.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_method_ref_escape
    -DEXPECT_SUBSTRING=E2039
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_method_ref_unbound
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/method_ref_unbound.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_method_ref_unbound
    -DEXPECT_SUBSTRING=E2005
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_functional_interface
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/functional_interface.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_functional_interface
    -DEXPECTED=42|20|10|9|functional|true|false|9|5|action|else
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_functional_interface_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/functional_interface_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_functional_interface_error
    -DEXPECT_SUBSTRING=E2004
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_functional_interface_arity_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/functional_interface_arity_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_functional_interface_arity_error
    -DEXPECT_SUBSTRING=E2097
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_functional_interface_methods_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/functional_interface_methods_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_functional_interface_methods_error
    -DEXPECT_SUBSTRING=E2079
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_thread
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/thread.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_thread
    -DEXPECTED=true|worker|false|true|42|false
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_thread_capture_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/thread_capture_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_thread_capture_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_thread_this_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/thread_this_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_thread_this_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_thread_copy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/thread_copy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_thread_copy_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_mutex
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/mutex.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_mutex
    -DEXPECTED=2
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_mutex_copy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/mutex_copy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_mutex_copy_error
    -DEXPECT_SUBSTRING=E2091
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_mutex_assign_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/mutex_assign_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_mutex_assign_error
    -DEXPECT_SUBSTRING=E2091
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_atomic
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/atomic.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_atomic
    -DEXPECTED=2|5|5|7
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_scopedlock_copy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scopedlock_copy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scopedlock_copy_error
    -DEXPECT_SUBSTRING=E2092
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_atomic_copy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/atomic_copy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_atomic_copy_error
    -DEXPECT_SUBSTRING=E2092
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_atomic_assign_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/atomic_assign_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_atomic_assign_error
    -DEXPECT_SUBSTRING=E2092
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_annotation
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_annotation.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_annotation
    -DEXPECTED=2
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_lock_annotation_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_annotation_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_annotation_error
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture
    -DEXPECTED=0|9|2|5
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_lock_capture_uncaptured_write
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_uncaptured_write.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_uncaptured_write
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_const_write
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_const_write.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_const_write
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_readonly_method
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_readonly_method.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_readonly_method
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_value_class
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_value_class.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_value_class
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_escape
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_escape.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_escape
    -DEXPECT_SUBSTRING=E2039
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_legacy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_legacy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_legacy_error
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_nested_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_nested_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_nested_error
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_uncaptured_call
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_uncaptured_call.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_uncaptured_call
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lock_capture_value_return
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_value_return.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_value_return
    -DEXPECTED=3
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_copy_deep
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_deep.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_deep
    -DEXPECTED=1|9
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_copy_deep_unique
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_deep_unique.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_deep_unique
    -DEXPECTED=7|9
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_copy_shallow
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_shallow.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_shallow
    -DEXPECTED=0|2|5
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_copy_deep_share_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_deep_share_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_deep_share_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_copy_shallow_unique_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_shallow_unique_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_shallow_unique_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_copy_explicit_required_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_explicit_required_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_explicit_required_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_copy_bad_mode_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_bad_mode_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_bad_mode_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_copy_deep_unique_chain
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_deep_unique_chain.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_deep_unique_chain
    -DEXPECTED=1|9
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_copy_deep_nested_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_deep_nested_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_deep_nested_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_copy_explicit_shell_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/copy_explicit_shell_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_copy_explicit_shell_error
    -DEXPECT_SUBSTRING=E2095
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_unsafe_pointer_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/unsafe_pointer_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_unsafe_pointer_error
    -DEXPECT_SUBSTRING=E2096
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)


add_test(NAME coord_e2e_lock_capture_deep
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_deep.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_deep
    -DEXPECTED=5|1
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_lock_capture_shallow_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lock_capture_shallow_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lock_capture_shallow_error
    -DEXPECT_SUBSTRING=E2094
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_scoped_thread
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scoped_thread.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scoped_thread
    -DEXPECTED=true|1
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_atomic_ops
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/atomic_ops.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_atomic_ops
    -DEXPECTED=10|10|20|true|30|false|30|30|25
    -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)

add_test(NAME coord_e2e_scopedthread_copy_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scopedthread_copy_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scopedthread_copy_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_scopedthread_assign_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scopedthread_assign_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scopedthread_assign_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_scopedthread_capture_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scopedthread_capture_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scopedthread_capture_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_scopedthread_this_error
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/scopedthread_this_error.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_scopedthread_this_error
    -DEXPECT_SUBSTRING=E2090
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lambda_escape
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_escape.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_escape
    -DEXPECT_SUBSTRING=E2039
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lambda_escape_scope
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_escape_scope.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_escape_scope
    -DEXPECT_SUBSTRING=E2039
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lambda_unlisted
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_unlisted.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_unlisted
    -DEXPECT_SUBSTRING=E2087
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)

add_test(NAME coord_e2e_lambda_readonly
  COMMAND ${CMAKE_COMMAND}
    -DCOORC=$<TARGET_FILE:coorc>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/../examples/lambda_readonly.coo
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e_lambda_readonly
    -DEXPECT_SUBSTRING=E2088
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fail.cmake)
