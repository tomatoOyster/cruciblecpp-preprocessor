set(PREPROCESSOR $<TARGET_FILE:cruciblecpp-preprocessor>)
set(TEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test_include)
if(MSVC)
    set(TEST_COMPILE_FLAGS /std:c++20 /Zs)
else()
    set(TEST_COMPILE_FLAGS -std=c++20 -fsyntax-only)
endif()

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
add_compile_case(standard_headers test_standard_headers.cpp ${GCC_IMPLICIT_INCLUDE_FLAGS})
add_compile_case(standard_headers_auto test_standard_headers.cpp)
add_compile_case(windows_header test_windows_header.cpp ${GCC_IMPLICIT_INCLUDE_FLAGS})
add_compile_case(angle_header_name test_angle_header_name.cpp
                 -I ${CMAKE_CURRENT_SOURCE_DIR})

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
add_negative_case(bad_redefinition bad_redefinition.cpp)
add_negative_case(missing_include missing_include.cpp)
add_negative_case(error_directive error_directive.cpp)
add_negative_case(unterminated_string unterminated_string.cpp)
add_negative_case(missing_angle_include missing_angle_include.cpp)
add_negative_case(nostdinc_angle nostdinc_angle.cpp -nostdinc)
