cmake_minimum_required(VERSION 3.20)

project(cruciblecpp_preprocessor VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cpp_preproc STATIC
    src/Token.cpp
    src/FileReader.cpp
    src/Tokenizer.cpp
    src/MacroRegistry.cpp
    src/MacroExpander.cpp
    src/ConditionalEngine.cpp
    src/IncludeHandler.cpp
    src/SpecialOperator.cpp
    src/OutputWriter.cpp
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

enable_testing()
add_subdirectory(tests)
