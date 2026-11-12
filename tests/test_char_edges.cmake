foreach(required PREPROCESSOR SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing ${required}")
    endif()
endforeach()

set(work "${BINARY_DIR}/char-edges")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
file(COPY "${SOURCE_DIR}/test_char_edges.cpp" DESTINATION "${work}")

execute_process(
    COMMAND "${PREPROCESSOR}" -P "${work}/test_char_edges.cpp"
            -o "${work}/edges.i"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
file(READ "${work}/edges.i" output)
if(result EQUAL 0)
    message(FATAL_ERROR "char-edge preprocessing unexpectedly succeeded")
endif()
foreach(marker U8_VALUE_OK L_VALUE_OK U_VALUE_OK U32_VALUE_OK
               HEX_ORD_OK HEX_L_OK HEX_U_OK HEX_U32_OK)
    if(NOT output MATCHES "${marker}")
        message(FATAL_ERROR "missing ${marker} in preprocessed output:\n${output}")
    endif()
endforeach()
if(NOT error MATCHES "character not encodable in a single code unit")
    message(FATAL_ERROR "missing non-BMP/multi-byte diagnostic:\n${error}")
endif()
if(NOT error MATCHES "hex escape sequence out of range")
    message(FATAL_ERROR "missing hex overflow diagnostic:\n${error}")
endif()

# L'non-BMP' is a warning, not an error; the condition is still true.
file(WRITE "${work}/wide.cpp"
     "#if L'😀' == 0xDE00\nWIDE_OK\n#endif\nint main(){return 0;}\n")
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${work}/wide.cpp" -o "${work}/wide.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${work}/wide.i" output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "wide non-BMP should only warn:\n${error}")
endif()
if(NOT output MATCHES "WIDE_OK")
    message(FATAL_ERROR "wide non-BMP value is wrong:\n${output}")
endif()
if(NOT error MATCHES "warning: character not encodable in a single code unit")
    message(FATAL_ERROR "wide non-BMP warning missing:\n${error}")
endif()

# u8 multi-byte is an error but still evaluates its last UTF-8 code unit.
file(WRITE "${work}/u8.cpp"
     "#if u8'中' == 0xAD\nU8_ONLY_OK\n#endif\nint main(){return 0;}\n")
execute_process(
    COMMAND "${PREPROCESSOR}" -P "${work}/u8.cpp" -o "${work}/u8.i"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(READ "${work}/u8.i" output)
if(result EQUAL 0)
    message(FATAL_ERROR "u8 multi-byte should report an error")
endif()
if(NOT output MATCHES "U8_ONLY_OK")
    message(FATAL_ERROR "u8 multi-byte value is wrong:\n${output}")
endif()
if(NOT error MATCHES "error: character not encodable in a single code unit")
    message(FATAL_ERROR "u8 multi-byte error missing:\n${error}")
endif()
