if(NOT DEFINED COORC OR NOT DEFINED SRC)
  message(FATAL_ERROR "outline.cmake requires COORC and SRC")
endif()

execute_process(
  COMMAND "${COORC}" outline ${EXTRA} "${SRC}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc outline failed\n${stdout}\n${stderr}")
endif()

if(NOT DEFINED NEEDLE AND NOT stdout MATCHES "File \\[class\\]")
  message(FATAL_ERROR "outline is missing class File:\n${stdout}")
endif()
if(NOT DEFINED NEEDLE AND NOT stdout MATCHES "\\[method\\]")
  message(FATAL_ERROR "outline is missing method nodes:\n${stdout}")
endif()

if(DEFINED NEEDLE AND NOT stdout MATCHES "")
  message(FATAL_ERROR "outline is missing '':
")
endif()
