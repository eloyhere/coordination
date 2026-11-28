# @Native annotation semantics: new file/abi syntax, removed legacy form and
# the diagnostics that reject unsupported native declarations.
#
# Required cache variables:
#   COORC - path to coorc
#   WORK  - scratch directory

if(NOT DEFINED COORC OR NOT DEFINED WORK)
  message(FATAL_ERROR "native_annotations.cmake requires COORC and WORK")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(HEADER [=[
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

set(VALID_MANIFEST [=[
[package]
name = "native_annotations"
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
]=])

function(write_project directory source)
  file(MAKE_DIRECTORY "${directory}/src")
  file(MAKE_DIRECTORY "${directory}/native/include")
  file(MAKE_DIRECTORY "${directory}/native/build")
  file(WRITE "${directory}/native/include/sample.h" "${HEADER}")
  file(WRITE "${directory}/native/include/counter.h" [=[
#ifndef COUNTER_H
#define COUNTER_H
#include <cstdint>
class Counter {
 public:
  Counter();
  ~Counter();
  std::int64_t value();
 private:
  std::int64_t value_ = 0;
};
#endif
]=])
  file(WRITE "${directory}/native/build/sample.obj" "placeholder object")
  file(WRITE "${directory}/coordination.toml" "${VALID_MANIFEST}")
  file(WRITE "${directory}/src/main.coo" "${source}")
endfunction()

function(expect_failure label expected_code source)
  set(directory "${WORK}/${label}")
  write_project("${directory}" "${source}")
  execute_process(
    COMMAND "${COORC}" check "${directory}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label}: expected a failure\n${stdout}")
  endif()
  if(NOT "${stderr}${stdout}" MATCHES "${expected_code}")
    message(FATAL_ERROR "${label}: expected ${expected_code}\nstdout: ${stdout}\nstderr: ${stderr}")
  endif()
endfunction()

# 1. A single file cannot use @Native: there is no project configuration, and
#    the compiler is never guessed.
file(WRITE "${WORK}/single.coo" [=[
@Native(file: "native/include/sample.h", abi: "c", symbol: "sample_answer")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])
execute_process(
  COMMAND "${COORC}" check "${WORK}/single.coo"
  RESULT_VARIABLE single_result
  OUTPUT_VARIABLE single_out
  ERROR_VARIABLE single_err)
if(single_result EQUAL 0)
  message(FATAL_ERROR "single-file @Native must fail")
endif()
if(NOT "${single_err}${single_out}" MATCHES "E2112")
  message(FATAL_ERROR "single-file @Native should report E2112\n${single_err}${single_out}")
endif()

# 2. A project without native configuration cannot use @Native either.
set(plain "${WORK}/plain")
file(MAKE_DIRECTORY "${plain}/src")
file(WRITE "${plain}/coordination.toml" [=[
[package]
name = "plain"
version = "0.1.0"
source = "src"
]=])
file(WRITE "${plain}/src/main.coo" [=[
@Native(file: "native/include/sample.h", abi: "c", symbol: "sample_answer")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])
execute_process(
  COMMAND "${COORC}" check "${plain}"
  RESULT_VARIABLE plain_result
  OUTPUT_VARIABLE plain_out
  ERROR_VARIABLE plain_err)
if(plain_result EQUAL 0)
  message(FATAL_ERROR "project without native configuration must fail")
endif()
if(NOT "${plain_err}${plain_out}" MATCHES "E2112")
  message(FATAL_ERROR "project without native configuration should report E2112\n${plain_err}${plain_out}")
endif()

# 3. The removed @Native("symbol") form reports a migration diagnostic.
expect_failure(legacy [=[E2114]=] [=[
@Native("sample_answer")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])

# 4. Missing file: argument.
expect_failure(no_file [=[E2113]=] [=[
@Native(abi: "c")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])

# 5. Unknown argument name.
expect_failure(unknown_arg [=[E2113]=] [=[
@Native(file: "native/include/sample.h", library: "m")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])

# 6. Non-string argument value.
expect_failure(non_string [=[E2113]=] [=[
@Native(file: "native/include/sample.h", abi: 3)
integer sampleAnswer();

integer main() {
    return 0;
}
]=])

# 7. Invalid abi spelling.
expect_failure(bad_abi [=[E2113]=] [=[
@Native(file: "native/include/sample.h", abi: "rust")
integer sampleAnswer();

integer main() {
    return 0;
}
]=])

# 8. A native class requires abi: "c++".
expect_failure(class_c_abi [=[E2113]=] [=[
@Native(file: "native/include/counter.h", abi: "c")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}

integer main() {
    return 0;
}
]=])

# 9. symbol: is only valid on functions.
expect_failure(class_symbol [=[E2113]=] [=[
@Native(file: "native/include/counter.h", abi: "c++", symbol: "Counter")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}

integer main() {
    return 0;
}
]=])

# 10. A native class is an opaque handle: no fields.
expect_failure(class_field [=[E2113]=] [=[
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    integer value;
    Counter();
    ~Counter();
    integer get();
}

integer main() {
    return 0;
}
]=])

# 11. Native methods are body-less.
expect_failure(method_body [=[E2113]=] [=[
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value() {
        return 0;
    }
}

integer main() {
    return 0;
}
]=])

# 12. Native free functions are body-less.
expect_failure(function_body [=[E2078]=] [=[
@Native(file: "native/include/sample.h", abi: "c", symbol: "sample_answer")
integer sampleAnswer() {
    return 0;
}

integer main() {
    return 0;
}
]=])

# 13. A native class value is move-only: copying reports E2119.
expect_failure(copy_native [=[E2119]=] [=[
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}

@Unsafe
integer main() {
    Counter first = Counter();
    Counter second = first;
    return 0;
}
]=])

# 14. Assigning a native class value reports E2119.
expect_failure(assign_native [=[E2119]=] [=[
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    ~Counter();
    integer value();
}

@Unsafe
integer main() {
    Counter first = Counter();
    Counter second = Counter();
    second = first;
    return 0;
}
]=])

# 15. @Native methods must live inside an @Native class.
expect_failure(method_outside [=[E2113]=] [=[
class Holder {
public:
    @Native(file: "native/include/sample.h", abi: "c")
    integer value() {
        return 0;
    }
}

integer main() {
    return 0;
}
]=])

# 16. Unsupported native return types (a class value) report E2078.
expect_failure(unsupported_type [=[E2078]=] [=[
class Payload {
public:
    Payload() {}
}

@Native(file: "native/include/sample.h", abi: "c")
Payload payload();

integer main() {
    return 0;
}
]=])

# 17. 128-bit integers are outside the bridge ABI.
expect_failure(int128 [=[E2078]=] [=[
@Native(file: "native/include/sample.h", abi: "c")
@Length(128) integer wide();

integer main() {
    return 0;
}
]=])
