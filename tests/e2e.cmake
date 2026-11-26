# End-to-end test: coorc build -> executable -> run -> compare stdout.
#
# Required cache variables:
#   COORC    - path to coorc.exe
#   SRC      - .coo source file
#   WORK     - scratch directory
#   EXPECTED - expected stdout lines joined with '|'

if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "e2e.cmake requires COORC, SRC and WORK")
endif()

if(NOT DEFINED EXPECTED_EXIT)
  set(EXPECTED_EXIT 0)
endif()

if(NOT DEFINED COORC_FLAGS)
  set(COORC_FLAGS "")
endif()

# CTest may preserve the quotes of an argument containing spaces.
if(EXPECTED MATCHES "^\"(.*)\"$")
  set(EXPECTED "${CMAKE_MATCH_1}")
endif()

file(MAKE_DIRECTORY "${WORK}")

execute_process(
  COMMAND "${COORC}" ${COORC_FLAGS} build "${SRC}" -o "${WORK}/app.exe"
  RESULT_VARIABLE coord_result
  OUTPUT_VARIABLE coord_stdout
  ERROR_VARIABLE coord_stderr)

if(NOT coord_result EQUAL 0)
  message(FATAL_ERROR "coorc build failed (${coord_result})\n${coord_stdout}\n${coord_stderr}")
endif()

execute_process(
  COMMAND "${WORK}/app.exe"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr)

# Compile-time output (for example @Retention(Compile) blocks under
# --print-consts) precedes the generated program's stdout.
set(run_stdout "${coord_stdout}${run_stdout}")

if(NOT run_result EQUAL EXPECTED_EXIT)
  message(FATAL_ERROR "generated program exit ${run_result}, expected ${EXPECTED_EXIT}\n${run_stderr}")
endif()

string(REPLACE "\r\n" "\n" actual "${run_stdout}")
string(STRIP "${actual}" actual)
string(REPLACE "\n" "|" actual "${actual}")

if(NOT actual STREQUAL EXPECTED)
  message(FATAL_ERROR "stdout mismatch\nexpected: ${EXPECTED}\nactual:   ${actual}")
endif()
