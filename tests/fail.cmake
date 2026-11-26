if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "fail.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${WORK}/app.exe"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(result EQUAL 0)
  message(FATAL_ERROR "expected coorc build to fail, but it succeeded")
endif()

if(DEFINED EXPECT_SUBSTRING AND NOT "${stdout}${stderr}" MATCHES "${EXPECT_SUBSTRING}")
  message(FATAL_ERROR "expected error containing '${EXPECT_SUBSTRING}'\n${stdout}\n${stderr}")
endif()
