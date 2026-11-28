if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "incremental.cmake requires COORC, SRC and WORK")
endif()

if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()
set(APP "${WORK}/app${COORD_EXE_SUFFIX}")

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${APP}" --incremental
  RESULT_VARIABLE first
  OUTPUT_VARIABLE firstOut
  ERROR_VARIABLE firstErr)
if(NOT first EQUAL 0)
  message(FATAL_ERROR "first incremental build failed\n${firstOut}\n${firstErr}")
endif()

execute_process(
  COMMAND "${COORC}" build "${SRC}" -o "${APP}" --incremental
  RESULT_VARIABLE second
  OUTPUT_VARIABLE secondOut
  ERROR_VARIABLE secondErr)
if(NOT second EQUAL 0)
  message(FATAL_ERROR "second incremental build failed\n${secondOut}\n${secondErr}")
endif()

if(NOT "${secondOut}" MATCHES "up to date")
  message(FATAL_ERROR "expected incremental skip, got:\n${secondOut}\n${secondErr}")
endif()
