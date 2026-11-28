if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "fail.cmake requires COORC, SRC and WORK")
endif()

# Negative test: the front end must reject the source before the host toolchain
# runs. The output path only has to be platform-correct.
if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${WORK}/app${COORD_EXE_SUFFIX}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(result EQUAL 0)
  message(FATAL_ERROR "expected coorc build to fail, but it succeeded")
endif()

if(DEFINED EXPECT_SUBSTRING AND NOT "${stdout}${stderr}" MATCHES "${EXPECT_SUBSTRING}")
  message(FATAL_ERROR "expected error containing '${EXPECT_SUBSTRING}'\n${stdout}\n${stderr}")
endif()
