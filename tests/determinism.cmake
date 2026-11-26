if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "determinism.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
foreach(name first second)
  execute_process(
    COMMAND "${COORC}" --emit-cpp "${WORK}/${name}.cpp" "${SRC}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "emit-cpp failed\n${stdout}\n${stderr}")
  endif()
endforeach()

file(SHA256 "${WORK}/first.cpp" first_hash)
file(SHA256 "${WORK}/second.cpp" second_hash)
if(NOT first_hash STREQUAL second_hash)
  message(FATAL_ERROR "generated C++ is not deterministic")
endif()
