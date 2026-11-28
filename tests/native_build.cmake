# End-to-end native bridge tests: C functions and a C++ class.
#
# The checked-in examples under examples/native are copied into WORK, the
# project manifest is rewritten for the host target/toolchain, the project's
# own native build produces the objects (native/build.cmd or native/build.sh
# equivalent - coorc never compiles user .c/.cpp), then coorc builds, links and
# runs the program.
#
# Required cache variables:
#   COORC    - path to coorc
#   WORK     - scratch directory
#   EXAMPLES - examples/native source directory
#   CC       - compiler used for the project's native C sources
#   CXX      - compiler used for the project's native C++ sources
#   FLAVOR   - msvc or gnu (argument style)
# Optional:
#   OBJEXT   - object file extension (.obj/.o)

if(NOT DEFINED COORC OR NOT DEFINED WORK OR NOT DEFINED EXAMPLES OR
   NOT DEFINED CC OR NOT DEFINED CXX)
  message(FATAL_ERROR "native_build.cmake requires COORC, WORK, EXAMPLES, CC and CXX")
endif()
if(NOT DEFINED FLAVOR)
  set(FLAVOR "gnu")
endif()
if(NOT DEFINED MODE)
  set(MODE "both")
endif()
if(NOT DEFINED OBJEXT)
  if(FLAVOR STREQUAL "msvc")
    set(OBJEXT ".obj")
  else()
    set(OBJEXT ".o")
  endif()
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# The host triple comes from coorc itself so the manifest always matches the
# compiler the test machine actually has.
execute_process(
  COMMAND "${COORC}" platform
  RESULT_VARIABLE platform_result
  OUTPUT_VARIABLE platform_out
  ERROR_VARIABLE platform_err)
if(NOT platform_result EQUAL 0)
  message(FATAL_ERROR "coorc platform failed\n${platform_out}\n${platform_err}")
endif()
if(NOT "${platform_out}" MATCHES "target triple: ([A-Za-z0-9_.-]+)")
  message(FATAL_ERROR "cannot parse the host target triple from:\n${platform_out}")
endif()
set(HOST_TARGET "${CMAKE_MATCH_1}")

function(rewrite_manifest path object_name)
  # TOML basic strings treat '\' as an escape; compiler paths are emitted with
  # forward slashes on every host.
  string(REPLACE "\\" "/" cc_toml "${CC}")
  string(REPLACE "\\" "/" cxx_toml "${CXX}")
  file(READ "${path}" content)
  string(REPLACE "target = \"x86_64-pc-windows-msvc\""
                 "target = \"${HOST_TARGET}\"" content "${content}")
  string(REPLACE "[toolchain.x86_64-pc-windows-msvc]"
                 "[toolchain.${HOST_TARGET}]" content "${content}")
  string(REPLACE "cc = \"cl\"" "cc = \"${cc_toml}\"" content "${content}")
  string(REPLACE "cxx = \"cl\"" "cxx = \"${cxx_toml}\"" content "${content}")
  string(REPLACE "native/build/${object_name}.obj"
                 "native/build/${object_name}${OBJEXT}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()

function(build_native source object include_dir flavor)
  if(flavor STREQUAL "msvc")
    execute_process(
      COMMAND "${CC}" /nologo /c /O2 "/I${include_dir}" "/Fo${object}" "${source}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  else()
    execute_process(
      COMMAND "${CC}" -c -O2 "-I${include_dir}" -o "${object}" "${source}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  endif()
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "native build failed for ${source}\n${out}\n${err}")
  endif()
endfunction()

function(build_native_cxx source object include_dir flavor)
  if(flavor STREQUAL "msvc")
    execute_process(
      COMMAND "${CXX}" /nologo /std:c++20 /EHsc /c /O2 "/I${include_dir}"
              "/Fo${object}" "${source}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  else()
    execute_process(
      COMMAND "${CXX}" -std=c++20 -O2 -c "-I${include_dir}" -o "${object}" "${source}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  endif()
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "native build failed for ${source}\n${out}\n${err}")
  endif()
endfunction()

function(run_project project expected)
  execute_process(
    COMMAND "${COORC}" run "${project}" -o "${project}/app${CMAKE_EXECUTABLE_SUFFIX}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "coorc run failed for ${project} (${result})\n${stdout}\n${stderr}")
  endif()
  # coorc prints nothing on success; only the program's stdout is compared.
  string(REPLACE "\r\n" "\n" actual "${stdout}")
  string(STRIP "${actual}" actual)
  string(REPLACE "\n" "|" actual "${actual}")
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "unexpected program output for ${project}\nexpected: ${expected}\nactual:   ${actual}")
  endif()
endfunction()

# --- C functions -----------------------------------------------------------
if(MODE STREQUAL "c" OR MODE STREQUAL "both")
  file(COPY "${EXAMPLES}/c_functions" DESTINATION "${WORK}")
  set(c_project "${WORK}/c_functions")
  rewrite_manifest("${c_project}/coordination.toml" "sample")
  file(MAKE_DIRECTORY "${c_project}/native/build")
  build_native("${c_project}/native/sample.c"
               "${c_project}/native/build/sample${OBJEXT}"
               "${c_project}/native/include" "${FLAVOR}")
  run_project("${c_project}" "5|hi")

  # Configured cxxflags must reach the compiler that builds the bridge and the
  # program; --verbose prints the exact command lines.
  if(FLAVOR STREQUAL "msvc")
    set(native_flag "/DCOO_NATIVE_FLAG=1")
  else()
    set(native_flag "-DCOO_NATIVE_FLAG=1")
  endif()
  file(READ "${c_project}/coordination.toml" manifest_content)
  string(REPLACE "cxxflags = []" "cxxflags = [\"${native_flag}\"]"
                 manifest_content "${manifest_content}")
  file(WRITE "${c_project}/coordination.toml" "${manifest_content}")
  execute_process(
    COMMAND "${COORC}" build "${c_project}" --verbose -o
            "${c_project}/verbose${CMAKE_EXECUTABLE_SUFFIX}"
    RESULT_VARIABLE verbose_result
    OUTPUT_VARIABLE verbose_out
    ERROR_VARIABLE verbose_err)
  if(NOT verbose_result EQUAL 0)
    message(FATAL_ERROR "verbose native build failed (${verbose_result})\n${verbose_out}\n${verbose_err}")
  endif()
  if(NOT "${verbose_out}" MATCHES "COO_NATIVE_FLAG")
    message(FATAL_ERROR "configured cxxflags were not forwarded to the compiler\n${verbose_out}")
  endif()
  if(NOT "${verbose_out}" MATCHES "C\\+\\+ compilation")
    message(FATAL_ERROR "--verbose did not print the compilation command\n${verbose_out}")
  endif()
endif()

# --- C++ class (constructor/method/destructor through an opaque handle) -----
if(MODE STREQUAL "class" OR MODE STREQUAL "both")
  file(COPY "${EXAMPLES}/cpp_class" DESTINATION "${WORK}")
  set(cpp_project "${WORK}/cpp_class")
  rewrite_manifest("${cpp_project}/coordination.toml" "counter")
  file(MAKE_DIRECTORY "${cpp_project}/native/build")
  build_native_cxx("${cpp_project}/native/counter.cpp"
                   "${cpp_project}/native/build/counter${OBJEXT}"
                   "${cpp_project}/native/include" "${FLAVOR}")
  run_project("${cpp_project}" "11|16|counter|~Counter")
endif()
