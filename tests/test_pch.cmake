if(MSVC)
    return()
endif()

foreach(required PREPROCESSOR CXX SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing ${required}")
    endif()
endforeach()

set(work "${BINARY_DIR}/pch-work")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
file(COPY
    "${SOURCE_DIR}/pch.h"
    "${SOURCE_DIR}/use_pch.cpp"
    "${SOURCE_DIR}/missing_pch.cpp"
    "${SOURCE_DIR}/forced_include.cpp"
    DESTINATION "${work}"
)

execute_process(
    COMMAND "${CXX}" -std=c++20 -x c++-header
            "${work}/pch.h" -o "${work}/pch.h.gch"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot create test PCH:\n${output}\n${error}")
endif()

function(run_success)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "command failed (${result}): ${ARGN}\n${output}\n${error}")
    endif()
    set(LAST_OUTPUT "${output}" PARENT_SCOPE)
    set(LAST_ERROR "${error}" PARENT_SCOPE)
endfunction()

function(run_failure)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "command unexpectedly succeeded: ${ARGN}")
    endif()
    set(LAST_ERROR "${error}" PARENT_SCOPE)
endfunction()

# A .gch next to a header must be ignored by default, just like g++ -E.
run_success("${PREPROCESSOR}" -P -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/use_pch.i")
if(NOT "${LAST_ERROR}" STREQUAL "")
    message(FATAL_ERROR "default PCH policy wrote stderr: ${LAST_ERROR}")
endif()
file(READ "${work}/use_pch.i" output)
if(NOT output MATCHES "42")
    message(FATAL_ERROR "PCH header source was not preprocessed:\n${output}")
endif()

# -Winvalid-pch is accepted as a build-system compatibility no-op.
run_success("${PREPROCESSOR}" -P -Winvalid-pch -Wno-invalid-pch -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/warnings.i")

# warn reports the ignored PCH once and continues.
run_success("${PREPROCESSOR}" -P --pch=warn -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/warn.i")
if(NOT LAST_ERROR MATCHES "ignoring precompiled header")
    message(FATAL_ERROR "missing PCH warning: ${LAST_ERROR}")
endif()

# error refuses to continue.
run_failure("${PREPROCESSOR}" -P --pch=error -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/error.i")
if(NOT LAST_ERROR MATCHES "refusing to use it")
    message(FATAL_ERROR "missing PCH error: ${LAST_ERROR}")
endif()

# report records ignored PCH files in analysis JSON.
run_success("${PREPROCESSOR}" -P --pch=report --analysis-json
            "${work}/pch.json" -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/report.i")
file(READ "${work}/pch.json" output)
if(NOT output MATCHES "\"pch\"" OR NOT output MATCHES "pch.h.gch")
    message(FATAL_ERROR "PCH metadata missing from analysis JSON:\n${output}")
endif()

# The depfile must contain the header source, never the .gch.
run_success("${PREPROCESSOR}" -P -MD -MF "${work}/pch.d" -I "${work}"
            "${work}/use_pch.cpp" -o "${work}/dep.i")
file(READ "${work}/pch.d" output)
if(output MATCHES "\\.gch")
    message(FATAL_ERROR ".gch leaked into depfile:\n${output}")
endif()

# A header source missing, with only its .gch, must fail like g++ -E.
set(missing "${work}/missing")
file(MAKE_DIRECTORY "${missing}")
file(COPY "${work}/pch.h.gch" "${work}/missing_pch.cpp" DESTINATION "${missing}")
run_failure("${PREPROCESSOR}" -P -I "${missing}"
            "${missing}/missing_pch.cpp" -o "${missing}/missing.i")
if(NOT LAST_ERROR MATCHES "found only precompiled header")
    message(FATAL_ERROR "missing-source PCH error is unclear: ${LAST_ERROR}")
endif()

# CMake PCH-style forced includes are processed from source too.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy
                        "${work}/pch.h" "${work}/cmake_pch.hxx")
execute_process(
    COMMAND "${CXX}" -std=c++20 -x c++-header
            "${work}/cmake_pch.hxx" -o "${work}/cmake_pch.hxx.gch"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot create forced-include PCH:\n${output}\n${error}")
endif()
run_success("${PREPROCESSOR}" -P --pch=warn -I "${work}"
            -include "${work}/cmake_pch.hxx"
            "${work}/forced_include.cpp" -o "${work}/forced.i")
if(NOT LAST_ERROR MATCHES "ignoring precompiled header")
    message(FATAL_ERROR "forced include did not report PCH: ${LAST_ERROR}")
endif()
file(READ "${work}/forced.i" output)
if(NOT output MATCHES "42")
    message(FATAL_ERROR "forced PCH header was not preprocessed:\n${output}")
endif()

# A forced include with only its .gch must also fail with a clear message.
file(REMOVE "${work}/cmake_pch.hxx")
run_failure("${PREPROCESSOR}" -P -I "${work}"
            -include "${work}/cmake_pch.hxx"
            "${work}/forced_include.cpp" -o "${work}/forced-missing.i")
if(NOT LAST_ERROR MATCHES "found only precompiled header")
    message(FATAL_ERROR "forced missing-source PCH error is unclear: ${LAST_ERROR}")
endif()
