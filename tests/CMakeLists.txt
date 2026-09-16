# Tests for Phoenix's non-Qt core. This can be configured standalone (handy when
# only a C++ compiler is around) or pulled in from the top-level CMakeLists.
#
# Tests that need the parser also need tinyxml2 and zlib, which the top-level
# build provides. Standalone, those targets are absent and the parser tests are
# left out rather than failing to configure.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PhoenixTests LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PHOENIX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Core sources under test. Must stay free of Qt.
set(TEST_CORE_SOURCES
    ${PHOENIX_SRC}/data/dom_element.cpp
    ${PHOENIX_SRC}/data/edge.cpp
    ${PHOENIX_SRC}/data/rect.cpp
    ${PHOENIX_SRC}/data/transform.cpp
    ${PHOENIX_SRC}/edit/command_stack.cpp
    ${PHOENIX_SRC}/edit/edit_context.cpp
    ${PHOENIX_SRC}/edit/curve_fit.cpp
    ${PHOENIX_SRC}/geom/curve.cpp
    ${PHOENIX_SRC}/geom/intersect.cpp
    ${PHOENIX_SRC}/edit/editable_path.cpp
    ${PHOENIX_SRC}/edit/element_commands.cpp
    ${PHOENIX_SRC}/edit/selection.cpp
    ${PHOENIX_SRC}/edit/snapping.cpp
    ${PHOENIX_SRC}/parser/path_parser.cpp
    ${PHOENIX_SRC}/writer/edge_writer.cpp
)

set(TEST_SOURCES
    test_main.cpp
    test_geometry.cpp
    test_command_stack.cpp
    test_edit_context.cpp
    test_edge_writer.cpp
    test_selection.cpp
    test_element_commands.cpp
    test_snapping.cpp
    test_editable_path.cpp
    test_curve_fit.cpp
    test_intersect.cpp
)

if(TARGET tinyxml2 AND TARGET zlibstatic)
    list(APPEND TEST_CORE_SOURCES
        ${PHOENIX_SRC}/data/document.cpp
        ${PHOENIX_SRC}/data/element.cpp
        ${PHOENIX_SRC}/data/fill_style.cpp
        ${PHOENIX_SRC}/data/fla_document.cpp
        ${PHOENIX_SRC}/data/frame.cpp
        ${PHOENIX_SRC}/data/group.cpp
        ${PHOENIX_SRC}/data/layer.cpp
        ${PHOENIX_SRC}/data/linear_gradient.cpp
        ${PHOENIX_SRC}/data/oval_primitive.cpp
        ${PHOENIX_SRC}/data/radial_gradient.cpp
        ${PHOENIX_SRC}/data/rectangle_primitive.cpp
        ${PHOENIX_SRC}/data/shape.cpp
        ${PHOENIX_SRC}/data/stroke_style.cpp
        ${PHOENIX_SRC}/data/swatch_list.cpp
        ${PHOENIX_SRC}/data/symbol.cpp
        ${PHOENIX_SRC}/data/symbol_list.cpp
        ${PHOENIX_SRC}/data/timeline.cpp
        ${PHOENIX_SRC}/parser/document_parser.cpp
        ${PHOENIX_SRC}/parser/fla_parser.cpp
        ${PHOENIX_SRC}/parser/zip_reader.cpp
        ${PHOENIX_SRC}/util/zip_archive.cpp
        ${PHOENIX_SRC}/util/zip_writer.cpp
        ${PHOENIX_SRC}/writer/fla_writer.cpp
        ${PHOENIX_SRC}/writer/xfl_content.cpp
        ${PHOENIX_SRC}/writer/xfl_folder_writer.cpp
        ${PHOENIX_SRC}/writer/xfl_writer.cpp
    )
    list(APPEND TEST_SOURCES test_xfl_writer.cpp test_fla_writer.cpp)
    set(PHOENIX_TESTS_HAVE_PARSER TRUE)
else()
    message(STATUS
        "phoenix_tests: building without the parser (tinyxml2/zlib targets not "
        "available). Configure from the top-level CMakeLists to include the XFL "
        "writer round-trip tests.")
endif()

add_executable(phoenix_tests ${TEST_SOURCES} ${TEST_CORE_SOURCES})

if(PHOENIX_TESTS_HAVE_PARSER)
    target_link_libraries(phoenix_tests PRIVATE tinyxml2 zlibstatic)
    target_include_directories(phoenix_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/third_party/zlib
        ${CMAKE_BINARY_DIR}/src/third_party/zlib
    )
endif()

if(MSVC)
    target_compile_options(phoenix_tests PRIVATE /W3)
else()
    target_compile_options(phoenix_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME phoenix_tests COMMAND phoenix_tests)
