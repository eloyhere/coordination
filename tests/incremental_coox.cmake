if(NOT DEFINED COORC OR NOT DEFINED WORK)
  message(FATAL_ERROR "incremental_coox.cmake requires COORC and WORK")
endif()

if(WIN32)
  set(COORD_EXE_SUFFIX ".exe")
else()
  set(COORD_EXE_SUFFIX "")
endif()
set(APP "${WORK}/app${COORD_EXE_SUFFIX}")

file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/main.coo" "integer main() {\n  print(\"coox\");\n  return 0;\n}\n")

execute_process(
  COMMAND "${COORC}" build "${WORK}/main.coo" -o "${APP}" --incremental
  RESULT_VARIABLE first
  OUTPUT_VARIABLE firstOut
  ERROR_VARIABLE firstErr)
if(NOT first EQUAL 0)
  message(FATAL_ERROR "first coox build failed\n${firstOut}\n${firstErr}")
endif()

file(WRITE "${WORK}/summary.coox" "// interface summary\n")

execute_process(
  COMMAND "${COORC}" build "${WORK}/main.coo" -o "${APP}" --incremental
  RESULT_VARIABLE second
  OUTPUT_VARIABLE secondOut
  ERROR_VARIABLE secondErr)
if(NOT second EQUAL 0)
  message(FATAL_ERROR "second coox build failed\n${secondOut}\n${secondErr}")
endif()
if("${secondOut}" MATCHES "up to date")
  message(FATAL_ERROR "expected .coox change to trigger rebuild")
endif()

execute_process(
  COMMAND "${COORC}" build "${WORK}/main.coo" -o "${APP}" --incremental
  RESULT_VARIABLE third
  OUTPUT_VARIABLE thirdOut
  ERROR_VARIABLE thirdErr)
if(NOT third EQUAL 0)
  message(FATAL_ERROR "third coox build failed\n${thirdOut}\n${thirdErr}")
endif()
if(NOT "${thirdOut}" MATCHES "up to date")
  message(FATAL_ERROR "expected unchanged coox to skip\n${thirdOut}")
endif()
