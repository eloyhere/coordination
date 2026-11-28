# Runtime-failure end-to-end test: coorc build -> executable -> run.
#
# Required cache variables:
#   COORC    - path to the coorc executable
#   SRC      - .coo source file
#   WORK     - scratch directory
# Optional:
#   EXPECT_STDERR - regex the generated program's stderr must match
#
# The generated program must build, then exit non-zero (a checked fix overflow
# or division by zero aborts); the harness never compares the exact abort code
# because it differs between Windows and POSIX hosts. Only the executable name
# is platform-specific.

if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "runtime_fail.cmake requires COORC, SRC and WORK")
endif()

if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()
set(APP "${WORK}/app${COORD_EXE_SUFFIX}")

file(MAKE_DIRECTORY "${WORK}")

execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${APP}"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr)

if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "coorc build failed (${build_result})\n${build_stdout}\n${build_stderr}")
endif()

execute_process(
  COMMAND "${APP}"
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
