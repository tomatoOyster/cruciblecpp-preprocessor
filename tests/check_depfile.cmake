if(NOT DEFINED DEPFILE)
    message(FATAL_ERROR "DEPFILE was not provided")
endif()

if(NOT EXISTS "${DEPFILE}")
    message(FATAL_ERROR "dependency file does not exist: ${DEPFILE}")
endif()

file(READ "${DEPFILE}" content)

if(NOT content MATCHES "depfile\\.i:")
    message(FATAL_ERROR "dependency target 'depfile.i:' missing:\n${content}")
endif()

if(NOT content MATCHES "main\\.h")
    message(FATAL_ERROR "main.h missing from depfile:\n${content}")
endif()

if(NOT content MATCHES "sub\\.h")
    message(FATAL_ERROR "sub.h missing from depfile:\n${content}")
endif()
