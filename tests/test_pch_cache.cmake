if(MSVC)
    return()
endif()

foreach(required PREPROCESSOR CXX CORRUPT_HELPER SOURCE_DIR BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing ${required}")
    endif()
endforeach()

set(work "${BINARY_DIR}/pch-cache-work")
set(cache "${work}/cache")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
file(COPY "${SOURCE_DIR}/pch.h" "${SOURCE_DIR}/use_pch.cpp"
     DESTINATION "${work}")

execute_process(
    COMMAND "${CXX}" -std=c++20 -x c++-header
            "${work}/pch.h" -o "${work}/pch.h.gch"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot create cache test PCH:\n${output}\n${error}")
endif()

function(run_ok)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "command failed (${result}): ${ARGN}\n${output}\n${error}")
    endif()
    set(LAST_ERROR "${error}" PARENT_SCOPE)
endfunction()

function(assert_stats path field value)
    file(READ "${path}" stats)
    if(NOT stats MATCHES "\"${field}\":${value}")
        message(FATAL_ERROR
            "expected ${field}=${value} in ${stats}")
    endif()
endfunction()

# First run populates; second run hits. The cache must contain an artifact,
# manifest, dependency file, and options stamp.
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s1.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/one.i")
assert_stats("${work}/s1.json" lookups 1)
assert_stats("${work}/s1.json" misses 1)
assert_stats("${work}/s1.json" created 1)
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s2.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/two.i")
assert_stats("${work}/s2.json" hits 1)
assert_stats("${work}/s2.json" hitRate 1.000000)

file(GLOB cache_artifacts "${cache}/artifacts/*.gch")
file(GLOB cache_manifests "${cache}/manifests/*.meta")
file(GLOB cache_deps "${cache}/deps/*.d")
file(GLOB cache_stamps "${cache}/stamps/*.options")
if(NOT cache_artifacts OR NOT cache_manifests OR NOT cache_deps OR
   NOT cache_stamps)
    message(FATAL_ERROR "PCH cache did not create all artifact directories")
endif()
file(READ "${cache_deps}" dependency)
string(REPLACE "\\" "/" work_fs "${work}")
if(NOT dependency MATCHES "${work_fs}/pch\\.h" OR
   NOT dependency MATCHES "g\\+\\+\\.exe" OR
   NOT dependency MATCHES "\\.options" OR
   NOT dependency MATCHES "\\.meta")
    message(FATAL_ERROR "PCH dependency file is incomplete:\n${dependency}")
endif()

# Reproducible metadata: rebuilding the same entry produces the same manifest.
file(READ "${cache_manifests}" manifest_before)
file(REMOVE_RECURSE "${cache}")
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s3.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/three.i")
file(GLOB cache_manifests "${cache}/manifests/*.meta")
file(READ "${cache_manifests}" manifest_after)
if(NOT manifest_before STREQUAL manifest_after)
    message(FATAL_ERROR "PCH cache manifest is not reproducible")
endif()

# A header newer than its PCH is reported stale.
file(TOUCH "${work}/pch.h")
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s4.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/four.i")
assert_stats("${work}/s4.json" stale 1)
assert_stats("${work}/s4.json" hits 0)

# Regenerating the PCH refreshes the cache, then it hits again.
execute_process(
    COMMAND "${CXX}" -std=c++20 -x c++-header
            "${work}/pch.h" -o "${work}/pch.h.gch"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot regenerate PCH:\n${output}\n${error}")
endif()
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s5.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/five.i")
assert_stats("${work}/s5.json" updated 1)
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s6.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/six.i")
assert_stats("${work}/s6.json" hits 1)

# A preprocessing option change invalidates the entry.
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s7.json" -DEXTRA_CACHE_TEST=1
       -I "${work}" "${work}/use_pch.cpp" -o "${work}/seven.i")
assert_stats("${work}/s7.json" stale 1)

# Cache size policy evicts entries after population.
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-max-size 1 --pch-cache-stats "${work}/s8.json"
       -I "${work}" "${work}/use_pch.cpp" -o "${work}/eight.i")
assert_stats("${work}/s8.json" entries 0)
assert_stats("${work}/s8.json" evicted 1)

# Populate a fresh entry for lock and corruption tests.
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s9.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/nine.i")
file(GLOB cache_manifest "${cache}/manifests/*.meta")
get_filename_component(manifest_name "${cache_manifest}" NAME_WE)
file(MAKE_DIRECTORY "${cache}/locks/${manifest_name}.lock")
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-lock-timeout 100
       --pch-cache-stats "${work}/s10.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/ten.i")
assert_stats("${work}/s10.json" lockTimeouts 1)
file(REMOVE_RECURSE "${cache}/locks/${manifest_name}.lock")

# Corrupt one byte after the magic while preserving size/mtime. This must be
# caught by the content hash, not merely by the "file changed" fast path.
execute_process(
    COMMAND "${CORRUPT_HELPER}" "${work}/pch.h.gch"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "corruption helper failed:\n${output}\n${error}")
endif()
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s11.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/eleven.i")
assert_stats("${work}/s11.json" corrupt 1)
assert_stats("${work}/s11.json" hits 0)

# A valid rebuilt artifact refreshes the cache again.
execute_process(
    COMMAND "${CXX}" -std=c++20 -x c++-header
            "${work}/pch.h" -o "${work}/pch.h.gch"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cannot regenerate PCH after corruption:\n${output}\n${error}")
endif()
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s12.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/twelve.i")
assert_stats("${work}/s12.json" updated 1)

# Invalid GCC PCH magic is detected as corruption, not a cache hit.
file(WRITE "${work}/pch.h.gch" "not-a-gcc-pch")
run_ok("${PREPROCESSOR}" -P --pch-cache "${cache}"
       --pch-cache-stats "${work}/s13.json" -I "${work}"
       "${work}/use_pch.cpp" -o "${work}/thirteen.i")
assert_stats("${work}/s13.json" corrupt 1)
assert_stats("${work}/s13.json" hits 0)
