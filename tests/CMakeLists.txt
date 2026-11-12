set(PREPROCESSOR $<TARGET_FILE:cruciblecpp-preprocessor>)
set(TEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_include)
if(MSVC)
    set(TEST_COMPILE_FLAGS /std:c++20 /Zs)
else()
    set(TEST_COMPILE_FLAGS -std=c++20 -fsyntax-only)
endif()

# Every positive test must use this helper (or add a matching compile_* test)
# so the preprocessed .i is accepted by GCC, not merely produced by CrucibleCpp.
function(add_compile_case name input)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/${name}.i)
    add_test(NAME preprocess_${name}
        COMMAND ${PREPROCESSOR} -P -I ${TEST_INCLUDE_DIR} ${ARGN}
                ${CMAKE_CURRENT_SOURCE_DIR}/${input}
                -o ${output})
    add_test(NAME compile_${name}
        COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${output})
    set_tests_properties(compile_${name} PROPERTIES DEPENDS preprocess_${name})
endfunction()

# GCC's own implicit include directories, passed explicitly as -isystem.
set(GCC_IMPLICIT_INCLUDE_FLAGS)
foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    list(APPEND GCC_IMPLICIT_INCLUDE_FLAGS -isystem "${dir}")
endforeach()

add_compile_case(simple test_simple.cpp)
add_compile_case(function_macro test_function_macro.cpp)
add_compile_case(conditional test_conditional.cpp)
add_compile_case(include test_include/main.h)
add_compile_case(pound_ops test_pound_ops.cpp)
add_compile_case(gcc_profile test_gcc_profile.cpp)
add_compile_case(gcc_has test_gcc_has.cpp)
add_compile_case(character_constants test_character_constants.cpp)
add_compile_case(redefinition test_redefinition.cpp)
add_compile_case(gcc_pragmas diagnostics/gcc_pragmas.cpp)
add_compile_case(trigraph test_trigraph.cpp -trigraphs)
add_compile_case(charset_latin1 test_charset_latin1.cpp
                 -finput-charset=ISO-8859-1)
add_compile_case(charset_gbk test_charset_gbk.cpp -finput-charset=GBK)
add_compile_case(charset_exec_gbk test_charset_exec_gbk.cpp
                 -fexec-charset=GBK)
add_compile_case(charset_unsigned test_charset_unsigned.cpp
                 -funsigned-char)

# -newyear prints one integer and ignores every other argument.
add_test(NAME newyear COMMAND ${PREPROCESSOR} -newyear)
set_tests_properties(newyear PROPERTIES PASS_REGULAR_EXPRESSION "^[0-9]+")
add_test(NAME newyear_ignores_arguments
    COMMAND ${PREPROCESSOR} -newyear --not-an-option missing.cpp -o missing.i)
set_tests_properties(newyear_ignores_arguments
    PROPERTIES PASS_REGULAR_EXPRESSION "^[0-9]+")
add_compile_case(standard_headers test_standard_headers.cpp ${GCC_IMPLICIT_INCLUDE_FLAGS})
add_compile_case(standard_headers_auto test_standard_headers.cpp)
add_compile_case(windows_header test_windows_header.cpp ${GCC_IMPLICIT_INCLUDE_FLAGS})

# GCC .gch files are ignored like g++ -E; this test also covers CMake PCH
# forced includes and the --pch diagnostics policy.
add_test(NAME pch_policy
    COMMAND ${CMAKE_COMMAND}
            -DPREPROCESSOR=$<TARGET_FILE:cruciblecpp-preprocessor>
            -DCXX=${CMAKE_CXX_COMPILER}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/pch
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_pch.cmake)

add_executable(pch_corrupt_helper pch_corrupt_helper.cpp)
if(WIN32)
    target_link_libraries(pch_corrupt_helper PRIVATE shell32)
endif()
add_test(NAME pch_cache
    COMMAND ${CMAKE_COMMAND}
            -DPREPROCESSOR=$<TARGET_FILE:cruciblecpp-preprocessor>
            -DCXX=${CMAKE_CXX_COMPILER}
            -DCORRUPT_HELPER=$<TARGET_FILE:pch_corrupt_helper>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/pch
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_pch_cache.cmake)
add_compile_case(angle_header_name test_angle_header_name.cpp
                 -I ${CMAKE_CURRENT_SOURCE_DIR})

# Verify multiple inputs and matching outputs.
set(MULTI_A ${CMAKE_CURRENT_BINARY_DIR}/multi_a.i)
set(MULTI_B ${CMAKE_CURRENT_BINARY_DIR}/multi_b.i)
set(MULTI_C ${CMAKE_CURRENT_BINARY_DIR}/multi_c.i)
add_test(NAME preprocess_multi
    COMMAND ${PREPROCESSOR} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/test_simple.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_function_macro.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_conditional.cpp
            -o ${MULTI_A} ${MULTI_B} ${MULTI_C})
add_test(NAME compile_multi_a
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${MULTI_A})
add_test(NAME compile_multi_b
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${MULTI_B})
add_test(NAME compile_multi_c
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${MULTI_C})
set_tests_properties(compile_multi_a compile_multi_b compile_multi_c
    PROPERTIES DEPENDS preprocess_multi)

# Verify CMake/Ninja depfile generation for quoted and angled includes.
set(DEPFILE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/depfile.d)
add_test(NAME preprocess_depfile
    COMMAND ${PREPROCESSOR} -P -I ${TEST_INCLUDE_DIR}
            -MD -MF ${DEPFILE_OUTPUT} -MT depfile.i
            ${CMAKE_CURRENT_SOURCE_DIR}/test_include/main.h
            -o ${CMAKE_CURRENT_BINARY_DIR}/depfile.i)
add_test(NAME check_depfile
    COMMAND ${CMAKE_COMMAND} -DDEPFILE=${DEPFILE_OUTPUT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_depfile.cmake)
set_tests_properties(check_depfile PROPERTIES DEPENDS preprocess_depfile)
add_test(NAME compile_depfile
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS}
            ${CMAKE_CURRENT_BINARY_DIR}/depfile.i)
set_tests_properties(compile_depfile PROPERTIES DEPENDS preprocess_depfile)

# Verify that emitted line markers are accepted by the compiler.
set(LINE_MARKER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/line_markers.i)
add_test(NAME preprocess_line_markers
    COMMAND ${PREPROCESSOR} -I ${TEST_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_simple.cpp
            -o ${LINE_MARKER_OUTPUT})
add_test(NAME compile_line_markers
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${LINE_MARKER_OUTPUT})
set_tests_properties(compile_line_markers
    PROPERTIES DEPENDS preprocess_line_markers)

# Verify that -C keeps comments and the result still compiles.
set(COMMENTS_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/comments.i)
add_test(NAME preprocess_comments
    COMMAND ${PREPROCESSOR} -C -P -I ${TEST_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_simple.cpp
            -o ${COMMENTS_OUTPUT})
add_test(NAME compile_comments
    COMMAND ${CMAKE_CXX_COMPILER} ${TEST_COMPILE_FLAGS} ${COMMENTS_OUTPUT})
set_tests_properties(compile_comments
    PROPERTIES DEPENDS preprocess_comments)

function(add_negative_case name input)
    set(output ${CMAKE_CURRENT_BINARY_DIR}/negative_${name}.i)
    add_test(NAME negative_${name}
        COMMAND ${PREPROCESSOR} -P ${ARGN}
                ${CMAKE_CURRENT_SOURCE_DIR}/negative/${input}
                -o ${output})
    set_tests_properties(negative_${name} PROPERTIES WILL_FAIL TRUE)
endfunction()

add_negative_case(unclosed_if unclosed_if.cpp)
add_negative_case(unterminated_macro unterminated_macro.cpp)
add_negative_case(missing_include missing_include.cpp)
add_negative_case(error_directive error_directive.cpp)
add_negative_case(unterminated_string unterminated_string.cpp)
add_negative_case(missing_angle_include missing_angle_include.cpp)
add_negative_case(nostdinc_angle nostdinc_angle.cpp -nostdinc)

add_test(NAME diagnostics
    COMMAND ${CMAKE_COMMAND}
            -DPREPROCESSOR=$<TARGET_FILE:cruciblecpp-preprocessor>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_diagnostics.cmake)

add_test(NAME char_edge_values
    COMMAND ${CMAKE_COMMAND}
            -DPREPROCESSOR=$<TARGET_FILE:cruciblecpp-preprocessor>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_char_edges.cmake)
