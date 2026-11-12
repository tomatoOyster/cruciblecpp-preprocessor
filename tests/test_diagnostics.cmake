foreach(required PREPROCESSOR SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing ${required}")
    endif()
endforeach()

set(work "${BINARY_DIR}/diagnostics")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

execute_process(
    COMMAND "${PREPROCESSOR}" -P -I "${SOURCE_DIR}/diagnostics"
            "${SOURCE_DIR}/diagnostics/main.cpp"
            -o "${work}/diag.i"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "diagnostic fixture unexpectedly succeeded")
endif()

foreach(pattern
        "In file included from"
        "warning: 'VALUE' redefined"
        "note: this is the location of the previous definition"
        "warning: #warning intentional warning"
        "error: #error intentional failure"
        "unterminated #if")
    if(NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "missing diagnostic '${pattern}':\n${error}")
    endif()
endforeach()
if(NOT error MATCHES "\\^")
    message(FATAL_ERROR "missing source caret:\n${error}")
endif()
if(NOT error MATCHES "\\| #error intentional failure")
    message(FATAL_ERROR "missing source context line:\n${error}")
endif()

# A changed redefinition is a warning in GCC; the new definition must win.
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${SOURCE_DIR}/test_redefinition.cpp"
            -o "${work}/redefinition.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "redefinition should not fail:\n${error}")
endif()
if(NOT error MATCHES "warning: 'VALUE' redefined")
    message(FATAL_ERROR "missing redefinition warning:\n${error}")
endif()

# Missing includes are fatal and use GCC's wording.
file(WRITE "${work}/missing.cpp" "#include \"no_such_12345.h\"\n")
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${work}/missing.cpp" -o "${work}/missing.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "missing include should fail")
endif()
if(NOT error MATCHES "fatal error: no_such_12345\\.h: No such file or directory")
    message(FATAL_ERROR "wrong missing-include diagnostic:\n${error}")
endif()

# Caret rendering can be disabled without changing the diagnostic itself.
execute_process(
    COMMAND "${PREPROCESSOR}" -P -fno-diagnostics-show-caret
            -I "${SOURCE_DIR}/diagnostics"
            "${SOURCE_DIR}/diagnostics/main.cpp"
            -o "${work}/nocaret.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT error MATCHES "error: #error intentional failure")
    message(FATAL_ERROR "missing diagnostic without caret:\n${error}")
endif()
if(error MATCHES "\\^")
    message(FATAL_ERROR "caret was not disabled:\n${error}")
endif()
