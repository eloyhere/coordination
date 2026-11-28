# Runtime-failure end-to-end test: coorc build -> executable -> run.
#
# Required cache variables:
#   COORC    - path to coorc.exe
#   SRC      - .coo source file
#   WORK     - scratch directory
# Optional:
#   EXPECT_STDERR - regex the generated program's stderr must match
#
# The generated program must build, then exit non-zero (a checked fix overflow
# or division by zero aborts); the harness never compares the exact abort code
# because it differs between Windows and POSIX hosts.

if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "runtime_fail.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")

execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${WORK}/app.exe"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr)

if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "coorc build failed (${build_result})\n${build_stdout}\n${build_stderr}")
endif()

execute_process(
  COMMAND "${WORK}/app.exe"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr)

if(run_result EQUAL 0)
  message(FATAL_ERROR "expected the generated program to fail, but it exited 0\n${run_stdout}")
endif()

if(DEFINED EXPECT_STDERR AND NOT "${run_stderr}" MATCHES "${EXPECT_STDERR}")
  message(FATAL_ERROR "stderr does not contain '${EXPECT_STDERR}'\n${run_stderr}")
endif()
