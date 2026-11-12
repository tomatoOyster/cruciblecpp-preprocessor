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
        "In file included from .*main\\.cpp:1:"
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

# pragma GCC diagnostic push/ignored/pop/error
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${SOURCE_DIR}/diagnostics/pragma.cpp"
            -o "${work}/pragma.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "pragma diagnostic error was not honored:\n${error}")
endif()
if(error MATCHES "hidden")
    message(FATAL_ERROR "ignored #warning was emitted:\n${error}")
endif()
if(NOT error MATCHES "warning: #warning visible")
    message(FATAL_ERROR "visible #warning missing:\n${error}")
endif()
if(NOT error MATCHES "error: #warning now_error")
    message(FATAL_ERROR "diagnostic error override missing:\n${error}")
endif()

# Headers included through -isystem hide warnings by default.
execute_process(
    COMMAND "${PREPROCESSOR}" -P -isystem "${SOURCE_DIR}/diagnostics/system"
            "${SOURCE_DIR}/diagnostics/system_header.cpp"
            -o "${work}/system.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "system header preprocessing failed:\n${error}")
endif()
if(error MATCHES "system header warning")
    message(FATAL_ERROR "system-header warning was not suppressed:\n${error}")
endif()

execute_process(
    COMMAND "${PREPROCESSOR}" -P -Wsystem-headers
            -isystem "${SOURCE_DIR}/diagnostics/system"
            "${SOURCE_DIR}/diagnostics/system_header.cpp"
            -o "${work}/system_warnings.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT error MATCHES "warning: #warning system header warning")
    message(FATAL_ERROR "-Wsystem-headers did not enable the warning:\n${error}")
endif()

# #pragma GCC system_header marks a quoted header too.
execute_process(
    COMMAND "${PREPROCESSOR}" -P -I "${SOURCE_DIR}/diagnostics"
            "${SOURCE_DIR}/diagnostics/system_pragma.cpp"
            -o "${work}/system_pragma.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "pragma system header failed:\n${error}")
endif()
if(error MATCHES "pragma system header warning")
    message(FATAL_ERROR "pragma system_header did not suppress:\n${error}")
endif()

# Command-line warning control for #warning.
file(WRITE "${work}/warn.cpp" "#warning command line controlled\n")
execute_process(
    COMMAND "${PREPROCESSOR}" -P -Wno-cpp "${work}/warn.cpp"
            -o "${work}/warn.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "warning suppression changed exit status:\n${error}")
endif()
if(error MATCHES "command line controlled")
    message(FATAL_ERROR "-Wno-cpp did not suppress #warning:\n${error}")
endif()

execute_process(
    COMMAND "${PREPROCESSOR}" -P -Werror=cpp "${work}/warn.cpp"
            -o "${work}/warn_error.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "-Werror=cpp did not fail:\n${error}")
endif()
if(NOT error MATCHES "error: #warning command line controlled")
    message(FATAL_ERROR "-Werror=cpp produced the wrong diagnostic:\n${error}")
endif()

execute_process(
    COMMAND "${PREPROCESSOR}" -P -Werror -Wno-error=cpp "${work}/warn.cpp"
            -o "${work}/warn_no_error.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "-Wno-error=cpp did not override -Werror:\n${error}")
endif()

# GCC poison.
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${SOURCE_DIR}/diagnostics/poison.cpp"
            -o "${work}/poison.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "poisoned identifier was accepted:\n${error}")
endif()
if(NOT error MATCHES "attempt to use poisoned 'POISONED_NAME'")
    message(FATAL_ERROR "missing poison diagnostic:\n${error}")
endif()
if(NOT error MATCHES "poisoned here")
    message(FATAL_ERROR "missing poison note:\n${error}")
endif()

# GCC dependency missing file is fatal.
execute_process(
    COMMAND "${PREPROCESSOR}" -P
            "${SOURCE_DIR}/diagnostics/dependency_missing.cpp"
            -o "${work}/dependency.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "missing dependency was accepted:\n${error}")
endif()
if(NOT error MATCHES "fatal error: does_not_exist_98765\\.h: No such file or directory")
    message(FATAL_ERROR "wrong dependency diagnostic:\n${error}")
endif()

# Known GCC compiler pragmas are passed through without unknown-pragma noise.
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${SOURCE_DIR}/diagnostics/gcc_pragmas.cpp"
            -o "${work}/gcc_pragmas.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "known GCC pragmas failed:\n${error}")
endif()
if(error MATCHES "unknown-pragma|ignoring #pragma GCC")
    message(FATAL_ERROR "known GCC pragma was misdiagnosed:\n${error}")
endif()
