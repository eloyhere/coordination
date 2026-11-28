# Fail-closed native toolchain configuration.
#
# Every native project must declare [build] target and a matching
# [toolchain.<target>] with cc and cxx, plus the [native] objects/libraries that
# the project's own build produced. Nothing falls back to the host default
# compiler, PATH lookup or a downloaded toolchain.
#
# Required cache variables:
#   COORC - path to coorc
#   WORK  - scratch directory
#
# This test never invokes a C++ compiler: every case must fail during
# configuration validation.

if(NOT DEFINED COORC OR NOT DEFINED WORK)
  message(FATAL_ERROR "native_config.cmake requires COORC and WORK")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(TARGET "x86_64-unknown-linux-gnu")

# A valid project used as the starting point of most assertions.
function(write_open_project directory manifest_text main_text)
  file(MAKE_DIRECTORY "${directory}/src")
  file(MAKE_DIRECTORY "${directory}/native/include")
  file(MAKE_DIRECTORY "${directory}/native/build")
  file(WRITE "${directory}/native/include/sample.h" [=[
#ifndef SAMPLE_H
#define SAMPLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t sample_answer(void);
#ifdef __cplusplus
}
#endif
#endif
]=])
  # A placeholder object is enough: configuration validation only checks that
  # the declared file exists.
  file(WRITE "${directory}/native/build/sample.obj" "placeholder object")
  file(WRITE "${directory}/src/main.coo" "${main_text}")
  file(WRITE "${directory}/coordination.toml" "${manifest_text}")
endfunction()

set(MAIN_NATIVE [=[
@Native(file: "native/include/sample.h", abi: "c", symbol: "sample_answer")
integer sampleAnswer();

@Unsafe
integer main() {
    print(sampleAnswer());
    return 0;
}
]=])

function(expect_error label expected_code)
  execute_process(
    COMMAND "${COORC}" build "${directory}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label}: expected a failure but coorc succeeded\n${stdout}")
  endif()
  if(NOT "${stderr}${stdout}" MATCHES "${expected_code}")
    message(FATAL_ERROR "${label}: expected ${expected_code}\nstdout: ${stdout}\nstderr: ${stderr}")
  endif()
endfunction()

# 1. A complete configuration is accepted by `coorc check` (no compiler runs).
set(directory "${WORK}/valid")
write_open_project("${directory}" [=[
[package]
name = "valid"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
include = ["native/include"]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
execute_process(
  COMMAND "${COORC}" check "${directory}"
  RESULT_VARIABLE check_result
  OUTPUT_VARIABLE check_out
  ERROR_VARIABLE check_err)
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "valid native project should pass `coorc check`\n${check_out}\n${check_err}")
endif()

# 2. Missing [build].target.
set(directory "${WORK}/no_target")
write_open_project("${directory}" [=[
[package]
name = "no_target"
version = "0.1.0"
source = "src"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing [build].target" "E2115")

# 3. Unknown target triple.
set(directory "${WORK}/bad_target")
write_open_project("${directory}" [=[
[package]
name = "bad_target"
version = "0.1.0"
source = "src"

[build]
target = "not-a-real-triple"

[toolchain.not-a-real-triple]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("unknown target triple" "E2115")

# 4. [toolchain.<target>] is missing for the configured target.
set(directory "${WORK}/no_toolchain")
write_open_project("${directory}" [=[
[package]
name = "no_toolchain"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.aarch64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing toolchain table" "E2116")

# 5. [toolchain] without cxx.
set(directory "${WORK}/no_cxx")
write_open_project("${directory}" [=[
[package]
name = "no_cxx"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing cxx" "E2116")

# 6. [toolchain] without cc.
set(directory "${WORK}/no_cc")
write_open_project("${directory}" [=[
[package]
name = "no_cc"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing cc" "E2116")

# 7. A toolchain path that does not exist is rejected instead of searching PATH.
set(directory "${WORK}/bad_cxx")
write_open_project("${directory}" [=[
[package]
name = "bad_cxx"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "native/toolchain/does-not-exist-clang++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing compiler file" "E2116")

# 8. --target contradicting [build].target.
set(directory "${WORK}/target_mismatch")
write_open_project("${directory}" [=[
[package]
name = "target_mismatch"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
execute_process(
  COMMAND "${COORC}" build "${directory}" --target aarch64-unknown-linux-gnu
  RESULT_VARIABLE mismatch_result
  OUTPUT_VARIABLE mismatch_out
  ERROR_VARIABLE mismatch_err)
if(mismatch_result EQUAL 0)
  message(FATAL_ERROR "target mismatch must fail")
endif()
if(NOT "${mismatch_err}${mismatch_out}" MATCHES "E2117")
  message(FATAL_ERROR "target mismatch should report E2117\n${mismatch_err}${mismatch_out}")
endif()

# 9. A declared native object that the project build never produced.
set(directory "${WORK}/missing_object")
write_open_project("${directory}" [=[
[package]
name = "missing_object"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/never-built.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing native object" "E2118")

# 10. A [native] include directory that does not exist.
set(directory "${WORK}/missing_include")
write_open_project("${directory}" [=[
[package]
name = "missing_include"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
include = ["native/headers"]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
expect_error("missing include directory" "E2118")

# 11. A header declared by @Native that does not exist.
set(directory "${WORK}/missing_header")
write_open_project("${directory}" [=[
[package]
name = "missing_header"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
include = ["native/include"]
objects = ["native/build/sample.obj"]
]=] [=[
@Native(file: "native/include/nope.h", abi: "c", symbol: "sample_answer")
integer sampleAnswer();

@Unsafe
integer main() {
    print(sampleAnswer());
    return 0;
}
]=])
expect_error("missing native header" "E2118")

# 12. The legacy coord.pkg line format supports the same native fields.
set(directory "${WORK}/legacy_pkg")
write_open_project("${directory}" [=[
name legacy_pkg
version 0.1.0
source src
target x86_64-unknown-linux-gnu
cc cc
cxx c++
native-include native/include
native-object native/build/sample.obj
]=] "${MAIN_NATIVE}")
file(REMOVE "${directory}/coordination.toml")
file(WRITE "${directory}/coord.pkg" [=[
name legacy_pkg
version 0.1.0
source src
target x86_64-unknown-linux-gnu
cc cc
cxx c++
native-include native/include
native-object native/build/sample.obj
]=])
execute_process(
  COMMAND "${COORC}" check "${directory}"
  RESULT_VARIABLE legacy_result
  OUTPUT_VARIABLE legacy_out
  ERROR_VARIABLE legacy_err)
if(NOT legacy_result EQUAL 0)
  message(FATAL_ERROR "coord.pkg native configuration should pass `coorc check`\n${legacy_out}\n${legacy_err}")
endif()

# 13. coord.pkg toolchain lines without a target are rejected.
set(directory "${WORK}/legacy_no_target")
write_open_project("${directory}" "" "${MAIN_NATIVE}")
file(REMOVE "${directory}/coordination.toml")
file(WRITE "${directory}/coord.pkg" [=[
name legacy_no_target
version 0.1.0
source src
cc cc
cxx c++
native-object native/build/sample.obj
]=])
execute_process(
  COMMAND "${COORC}" build "${directory}"
  RESULT_VARIABLE legacy_missing_result
  OUTPUT_VARIABLE legacy_missing_out
  ERROR_VARIABLE legacy_missing_err)
if(legacy_missing_result EQUAL 0)
  message(FATAL_ERROR "coord.pkg cc/cxx without target must fail")
endif()
if(NOT "${legacy_missing_err}${legacy_missing_out}" MATCHES "require a 'target' field")
  message(FATAL_ERROR "coord.pkg without target should explain the requirement\n${legacy_missing_err}${legacy_missing_out}")
endif()

# 14. Unknown [build] fields are rejected instead of silently ignored.
set(directory "${WORK}/unknown_build_field")
write_open_project("${directory}" [=[
[package]
name = "unknown_build_field"
version = "0.1.0"
source = "src"

[build]
target = "x86_64-unknown-linux-gnu"
cxx = "c++"

[toolchain.x86_64-unknown-linux-gnu]
cc = "cc"
cxx = "c++"

[native]
objects = ["native/build/sample.obj"]
]=] "${MAIN_NATIVE}")
execute_process(
  COMMAND "${COORC}" build "${directory}"
  RESULT_VARIABLE field_result
  OUTPUT_VARIABLE field_out
  ERROR_VARIABLE field_err)
if(field_result EQUAL 0)
  message(FATAL_ERROR "unknown [build] field must fail")
endif()
if(NOT "${field_err}${field_out}" MATCHES "unknown \\[build\\] field")
  message(FATAL_ERROR "unknown [build] field should be reported\n${field_err}${field_out}")
endif()

