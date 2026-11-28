# Cross-target compile-only native build.
#
# A native project whose [build].target is a foreign triple must be accepted by
# the front end (main translation unit plus generated bridge) without invoking
# the host compiler. This proves the target/toolchain configuration is really
# the one driving lowering; an actual cross link needs a cross toolchain that
# this machine may not have.
#
# Required cache variables:
#   COORC    - path to coorc
#   WORK     - scratch directory
#   EXAMPLES - examples/native source directory
#   CC, CXX  - configured toolchain entries (must exist; not invoked)
#   FLAVOR   - msvc or gnu

if(NOT DEFINED COORC OR NOT DEFINED WORK OR NOT DEFINED EXAMPLES)
  message(FATAL_ERROR "native_cross.cmake requires COORC, WORK and EXAMPLES")
endif()
if(NOT DEFINED FLAVOR)
  set(FLAVOR "gnu")
endif()
if(NOT DEFINED OBJEXT)
  if(FLAVOR STREQUAL "msvc")
    set(OBJEXT ".obj")
  else()
    set(OBJEXT ".o")
  endif()
endif()
if(NOT DEFINED CC)
  set(CC "cc")
endif()
if(NOT DEFINED CXX)
  set(CXX "c++")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
  COMMAND "${COORC}" platform
  RESULT_VARIABLE platform_result
  OUTPUT_VARIABLE platform_out)
if(NOT platform_result EQUAL 0)
  message(FATAL_ERROR "coorc platform failed")
endif()
if(NOT "${platform_out}" MATCHES "target triple: ([A-Za-z0-9_.-]+)")
  message(FATAL_ERROR "cannot parse the host target triple:\n${platform_out}")
endif()
set(HOST_TARGET "${CMAKE_MATCH_1}")

if(HOST_TARGET MATCHES "windows")
  set(CROSS_TARGET "x86_64-unknown-linux-gnu")
else()
  set(CROSS_TARGET "x86_64-pc-windows-msvc")
endif()

file(COPY "${EXAMPLES}/c_functions" DESTINATION "${WORK}")
set(project "${WORK}/c_functions")
string(REPLACE "\\" "/" cc_toml "${CC}")
string(REPLACE "\\" "/" cxx_toml "${CXX}")
file(READ "${project}/coordination.toml" content)
string(REPLACE "target = \"x86_64-pc-windows-msvc\""
               "target = \"${CROSS_TARGET}\"" content "${content}")
string(REPLACE "[toolchain.x86_64-pc-windows-msvc]"
               "[toolchain.${CROSS_TARGET}]" content "${content}")
string(REPLACE "cc = \"cl\"" "cc = \"${cc_toml}\"" content "${content}")
string(REPLACE "cxx = \"cl\"" "cxx = \"${cxx_toml}\"" content "${content}")
string(REPLACE "native/build/sample.obj"
               "native/build/sample${OBJEXT}" content "${content}")
file(WRITE "${project}/coordination.toml" "${content}")
# The object itself is never compiled here (compile-only test); the file has to
# exist because a native project must point at the project build's output.
file(MAKE_DIRECTORY "${project}/native/build")
file(WRITE "${project}/native/build/sample${OBJEXT}" "placeholder object")

execute_process(
  COMMAND "${COORC}" --emit-cpp "${project}/out.cpp" "${project}"
  RESULT_VARIABLE emit_result
  OUTPUT_VARIABLE emit_out
  ERROR_VARIABLE emit_err)
if(NOT emit_result EQUAL 0)
  message(FATAL_ERROR "cross-target --emit-cpp failed (${emit_result})\n${emit_out}\n${emit_err}")
endif()

file(READ "${project}/out.cpp" generated)
if(NOT "${generated}" MATCHES "extern \"C\"")
  message(FATAL_ERROR "--emit-cpp did not emit the native bridge declarations")
endif()
if(NOT "${generated}" MATCHES "coo_native_fn_")
  message(FATAL_ERROR "--emit-cpp did not reference generated bridge symbols")
endif()
if(NOT EXISTS "${project}/out.native.cpp")
  message(FATAL_ERROR "--emit-cpp did not emit the bridge translation unit")
endif()
file(READ "${project}/out.native.cpp" bridge)
if(NOT "${bridge}" MATCHES "native/include/sample.h")
  message(FATAL_ERROR "bridge does not include the @Native header")
endif()
if(NOT "${bridge}" MATCHES "coo_sample_add")
  message(FATAL_ERROR "bridge does not call the configured C symbol")
endif()

# A target the configured compiler cannot produce must fail closed instead of
# silently falling back to the host compiler.
if(FLAVOR STREQUAL "msvc")
  execute_process(
    COMMAND "${COORC}" build "${project}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE build_err)
  if(build_result EQUAL 0)
    message(FATAL_ERROR "cross build with MSVC must fail closed")
  endif()
  if(NOT "${build_err}${build_out}" MATCHES "cross-compile")
    message(FATAL_ERROR "cross build should explain the MSVC limitation\n${build_err}${build_out}")
  endif()
endif()
