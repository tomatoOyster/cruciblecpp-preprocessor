cmake_minimum_required(VERSION 3.20)

project(cruciblecpp_preprocessor VERSION 3.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cpp_preproc STATIC
    src/Token.cpp
    src/Sha256.cpp
    src/GccHasTables.cpp
    src/Charset.cpp
    src/FileReader.cpp
    src/GccDiscovery.cpp
    src/Tokenizer.cpp
    src/MacroRegistry.cpp
    src/MacroExpander.cpp
    src/ConditionalEngine.cpp
    src/IncludeHandler.cpp
    src/SpecialOperator.cpp
    src/OutputWriter.cpp
    src/PchCache.cpp
    src/Preprocessor.cpp
)

target_include_directories(cpp_preproc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(cpp_preproc PRIVATE /W4 /permissive-)
else()
    target_compile_options(cpp_preproc PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cruciblecpp-preprocessor src/main.cpp)
target_link_libraries(cruciblecpp-preprocessor PRIVATE cpp_preproc)
if(WIN32)
    target_link_libraries(cruciblecpp-preprocessor PRIVATE shell32)
endif()
if(NOT MSVC)
    target_compile_options(cruciblecpp-preprocessor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Native Windows GUI. It links the same cpp_preproc library as the CLI and
# deliberately uses only the Windows SDK, never a third-party UI toolkit.
if(WIN32)
    find_package(Threads REQUIRED)
    add_executable(cruciblecpp-gui WIN32
        gui/main.cpp
        gui/resources.rc
    )
    target_link_libraries(cruciblecpp-gui PRIVATE
        cpp_preproc
        comctl32
        comdlg32
        shell32
        ole32
        Threads::Threads
    )
    target_compile_definitions(cruciblecpp-gui PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
    if(NOT MSVC)
        target_compile_options(cruciblecpp-gui PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

enable_testing()
add_subdirectory(tests)

