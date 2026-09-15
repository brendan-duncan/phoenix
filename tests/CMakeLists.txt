# Tests for Phoenix's non-Qt core. This can be configured standalone (handy
# when Qt is not installed) or pulled in from the top-level CMakeLists.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PhoenixTests LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PHOENIX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Core sources under test. Must stay free of Qt.
set(TEST_CORE_SOURCES
    ${PHOENIX_SRC}/data/rect.cpp
    ${PHOENIX_SRC}/data/transform.cpp
    ${PHOENIX_SRC}/edit/command_stack.cpp
    ${PHOENIX_SRC}/edit/edit_context.cpp
)

set(TEST_SOURCES
    test_main.cpp
    test_geometry.cpp
    test_command_stack.cpp
    test_edit_context.cpp
)

add_executable(phoenix_tests ${TEST_SOURCES} ${TEST_CORE_SOURCES})

if(MSVC)
    target_compile_options(phoenix_tests PRIVATE /W3)
else()
    target_compile_options(phoenix_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME phoenix_tests COMMAND phoenix_tests)
