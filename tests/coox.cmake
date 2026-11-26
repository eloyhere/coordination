if(NOT DEFINED COORC OR NOT DEFINED SRC OR NOT DEFINED WORK)
  message(FATAL_ERROR "coox.cmake requires COORC, SRC and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
execute_process(
  COMMAND "${COORC}" check "${SRC}" --emit-coox "${WORK}/interface.coox"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "coorc check --emit-coox failed\n${stdout}\n${stderr}")
endif()

if(NOT EXISTS "${WORK}/interface.coox")
  message(FATAL_ERROR "interface file was not written")
endif()

file(READ "${WORK}/interface.coox" content)
if(NOT content MATCHES "class File")
  message(FATAL_ERROR "interface is missing class File:\n${content}")
endif()
if(NOT content MATCHES "integer size\\(\\) const;")
  message(FATAL_ERROR "interface is missing method signature:\n${content}")
endif()
