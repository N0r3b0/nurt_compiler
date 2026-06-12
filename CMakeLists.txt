cmake_minimum_required(VERSION 3.20)

project(nurt
    VERSION 0.1.0
    DESCRIPTION "Nurt - a pedagogical programming language compiled to x86_64 NASM"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NURT_BUILD_TESTS "Build the Nurt test suite" ON)

# Strict warnings applied to all first-party targets (not to fetched dependencies).
add_library(nurt_warnings INTERFACE)
if(MSVC)
    target_compile_options(nurt_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(nurt_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(nurt_core STATIC
    src/common/diagnostics.cpp
    src/lexer/lexer.cpp
    src/parser/ast.cpp
    src/parser/parser.cpp
    src/semantic/analyzer.cpp
    src/codegen/codegen.cpp)
target_include_directories(nurt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nurt_core PRIVATE nurt_warnings)

add_executable(nurtc src/main.cpp)
target_link_libraries(nurtc PRIVATE nurt_core nurt_warnings)

if(NURT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
