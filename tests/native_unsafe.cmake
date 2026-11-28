# @Native calls stay behind @Unsafe/@Safe scopes after the project-config
# migration.
#
# Required cache variables:
#   COORC  - path to coorc
#   WORK   - scratch directory
#   CC     - compiler for the project's own native build
#   FLAVOR - msvc or gnu
# Optional:
#   OBJEXT - object file extension (.obj/.o)

if(NOT DEFINED COORC OR NOT DEFINED WORK OR NOT DEFINED CC)
  message(FATAL_ERROR "native_unsafe.cmake requires COORC, WORK and CC")
endif()

# The generated manifest below uses @NAME@ placeholders that string(REPLACE)
# substitutes afterwards. CMP0053 NEW keeps them literal in script mode.
cmake_policy(SET CMP0053 NEW)
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

# `cmake -P` does not populate CMAKE_EXECUTABLE_SUFFIX for the host.
if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
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

set(project "${WORK}/unsafe")
file(MAKE_DIRECTORY "${project}/src")
file(MAKE_DIRECTORY "${project}/native/include")
file(MAKE_DIRECTORY "${project}/native/build")

string(REPLACE "\\" "/" cc_toml "${CC}")
file(WRITE "${project}/coordination.toml" [=[
[package]
name = "native_unsafe"
version = "0.1.0"
source = "src"

[build]
target = "@HOST_TARGET@"

[toolchain.@HOST_TARGET@]
cc = "@CC@"
cxx = "@CC@"

[native]
include = ["native/include"]
objects = ["native/build/sample@OBJEXT@"]
]=])
file(READ "${project}/coordination.toml" manifest)
string(REPLACE "@HOST_TARGET@" "${HOST_TARGET}" manifest "${manifest}")
string(REPLACE "@CC@" "${cc_toml}" manifest "${manifest}")
string(REPLACE "@OBJEXT@" "${OBJEXT}" manifest "${manifest}")
file(WRITE "${project}/coordination.toml" "${manifest}")

file(WRITE "${project}/native/include/sample.h" [=[
#ifndef SAMPLE_H
#define SAMPLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t coo_sample_pid(void);
#ifdef __cplusplus
}
#endif
#endif
]=])
file(WRITE "${project}/native/sample.c" [=[
#include "sample.h"
int64_t coo_sample_pid(void) { return 4242; }
]=])

if(FLAVOR STREQUAL "msvc")
  execute_process(
    COMMAND "${CC}" /nologo /c /O2 "/I${project}/native/include"
            "/Fo${project}/native/build/sample.obj" "${project}/native/sample.c"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_out
    ERROR_VARIABLE native_err)
else()
  execute_process(
    COMMAND "${CC}" -c -O2 "-I${project}/native/include" -o
            "${project}/native/build/sample.o" "${project}/native/sample.c"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_out
    ERROR_VARIABLE native_err)
endif()
if(NOT native_result EQUAL 0)
  message(FATAL_ERROR "native build failed\n${native_out}\n${native_err}")
endif()

set(NATIVE_DECL [=[
@Native(file: "native/include/sample.h", abi: "c", symbol: "coo_sample_pid")
integer samplePid();
]=])

function(expect_error label expected_code source)
  file(WRITE "${project}/src/main.coo" "${source}")
  execute_process(
    COMMAND "${COORC}" check "${project}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label}: expected a failure\n${stdout}")
  endif()
  if(NOT "${stderr}${stdout}" MATCHES "${expected_code}")
    message(FATAL_ERROR "${label}: expected ${expected_code}\n${stdout}\n${stderr}")
  endif()
endfunction()

# 1. Calling a native function from a safe scope is rejected.
expect_error(safe_call [=[E2096]=] "${NATIVE_DECL}
integer main() {
    print(samplePid());
    return 0;
}
")

# 2. @Safe overrides an inherited @Unsafe scope; the native call is rejected.
file(WRITE "${project}/src/main.coo" "${NATIVE_DECL}
@Unsafe
class Worker {
public:
    @Safe
    integer pid() {
        return samplePid();
    }
}
integer main() {
    Worker worker;
    print(worker.pid());
    return 0;
}
")
execute_process(
  COMMAND "${COORC}" check "${project}"
  RESULT_VARIABLE override_result
  OUTPUT_VARIABLE override_out
  ERROR_VARIABLE override_err)
if(override_result EQUAL 0)
  message(FATAL_ERROR "@Safe override must not unlock native calls")
endif()
if(NOT "${override_err}${override_out}" MATCHES "E2096")
  message(FATAL_ERROR "@Safe override should report E2096\n${override_err}${override_out}")
endif()

# 3. An explicit @Unsafe { ... } block inside a safe function is enough.
file(WRITE "${project}/src/main.coo" "${NATIVE_DECL}
integer main() {
    @Unsafe {
        print(samplePid());
    }
    return 0;
}
")
execute_process(
  COMMAND "${COORC}" run "${project}" -o "${project}/app${COORD_EXE_SUFFIX}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "@Unsafe block build/run failed (${run_result})\n${run_out}\n${run_err}")
endif()
string(REPLACE "\r\n" "\n" run_actual "${run_out}")
string(STRIP "${run_actual}" run_actual)
if(NOT "${run_actual}" STREQUAL "4242")
  message(FATAL_ERROR "unexpected @Unsafe block output: '${run_actual}'")
endif()
