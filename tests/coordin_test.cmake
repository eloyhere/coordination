if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "test.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" test "${SRC}" -o "${WORK}/tests.exe"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc test failed (${result})\n${stdout}\n${stderr}")
endif()

if(NOT stdout MATCHES "\\[test\\] addition")
  message(FATAL_ERROR "missing addition test output:\n${stdout}")
endif()
if(NOT stdout MATCHES "\\[test\\] comparison")
  message(FATAL_ERROR "missing comparison test output:\n${stdout}")
endif()
