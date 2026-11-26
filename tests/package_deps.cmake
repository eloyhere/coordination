if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "package_deps.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" deps "${SRC}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc deps failed (${result})\n${stdout}\n${stderr}")
endif()

string(REPLACE "\r\n" "\n" actual "${stdout}")
string(STRIP "${actual}" actual)
string(REPLACE "\n" "|" actual "${actual}")
if(NOT actual STREQUAL EXPECTED)
  message(FATAL_ERROR "deps mismatch\nexpected: ${EXPECTED}\nactual:   ${actual}")
endif()
