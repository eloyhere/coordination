if(NOT DEFINED COORC OR NOT DEFINED MANIFEST)
  message(FATAL_ERROR "deps.cmake requires COORC and MANIFEST")
endif()

execute_process(
  COMMAND "${COORC}" deps "${MANIFEST}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc deps failed\n${stdout}\n${stderr}")
endif()

string(REPLACE "\r\n" "\n" stdout "${stdout}")
string(STRIP "${stdout}" stdout)
if(NOT stdout STREQUAL "beta\nalpha")
  message(FATAL_ERROR "dependency order mismatch:\n${stdout}")
endif()
