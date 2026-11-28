if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "coordin_test.cmake requires COORC, SRC and WORK")
endif()

if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" test "${SRC}" -o "${WORK}/tests${COORD_EXE_SUFFIX}"
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
