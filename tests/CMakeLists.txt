include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# On Windows: keep gtest's CRT choice in sync with the rest of the build.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lexer_tests lexer_tests.cpp)
target_link_libraries(lexer_tests PRIVATE nurt_core nurt_warnings GTest::gtest_main)

add_executable(parser_tests parser_tests.cpp)
target_link_libraries(parser_tests PRIVATE nurt_core nurt_warnings GTest::gtest_main)

add_executable(semantic_tests semantic_tests.cpp)
target_link_libraries(semantic_tests PRIVATE nurt_core nurt_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lexer_tests)
gtest_discover_tests(parser_tests)
gtest_discover_tests(semantic_tests)
