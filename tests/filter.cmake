if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "filter.cmake requires COORC, SRC and WORK")
endif()

if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" test "${SRC}" --filter comparison
          -o "${WORK}/filtered${COORD_EXE_SUFFIX}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc test --filter failed\n${stdout}\n${stderr}")
endif()

if(stdout MATCHES "\\[test\\] addition")
  message(FATAL_ERROR "filtered test should not run:\n${stdout}")
endif()
if(NOT stdout MATCHES "\\[test\\] comparison")
  message(FATAL_ERROR "matching test did not run:\n${stdout}")
endif()
