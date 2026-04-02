cmake_minimum_required(VERSION 3.16)

include(FetchContent)
FetchContent_Declare(
    libas3
    GIT_REPOSITORY https://github.com/brendan-duncan/libas3
    GIT_TAG main
)
FetchContent_MakeAvailable(libas3)

add_library(libas3 INTERFACE)
add_dependencies(libas3 as3_parser)

project(Phoenix VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Qt path
# TODO: this isn't right...how should this be set up so that it's not hard-coded?
set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64" ${CMAKE_PREFIX_PATH})

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Add zlib from third_party
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build zlib examples")
add_subdirectory(src/third_party/zlib)

# Add zlib include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/zlib)

# Remove debug postfix from zlibstatic to avoid library name conflicts
set_target_properties(zlibstatic PROPERTIES
    DEBUG_POSTFIX ""
    OUTPUT_NAME "zlibstatic"
)
set_target_properties(zlib PROPERTIES DEBUG_POSTFIX "")

# Set ZLIB variables for compatibility with find_package
set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/zlib;${CMAKE_CURRENT_BINARY_DIR}/src/third_party/zlib" CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR}" CACHE STRING "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)
set(ZLIB_VERSION_STRING "1.3.1" CACHE STRING "" FORCE)

# Add tinyxml2 from third_party
set(tinyxml2_SHARED_LIBS OFF CACHE BOOL "Don't build tinyxml2 as a shared library")
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "Don't build tinyxml2 tests")
add_subdirectory(src/third_party/tinyxml2)

set(HEADERS
    src/gui/main_window.h
    src/gui/phoenix_view.h
    src/gui/document_view.h
    src/gui/timeline_view.h
    src/gui/layers_panel.h
    src/gui/timelines_panel.h
    src/gui/player.h
    src/parser/document_parser.h
    src/parser/zip_reader.h
    src/parser/fla_parser.h
    src/parser/path_parser.h
    src/data/action_script.h
    src/data/bitmap.h
    src/data/color_transform.h
    src/data/document.h
    src/data/dom_element.h
    src/data/edge.h
    src/data/element.h
    src/data/fill_style.h
    src/data/fla_document.h
    src/data/folder.h
    src/data/frame.h
    src/data/group.h
    src/data/layer.h
    src/data/linear_gradient.h
    src/data/oval_primitive.h
    src/data/point.h
    src/data/printer_settings.h
    src/data/publish_history.h
    src/data/radial_gradient.h
    src/data/rect.h
    src/data/rectangle_primitive.h
    src/data/resource.h
    src/data/script_list.h
    src/data/shape.h
    src/data/solid_color.h
    src/data/static_text.h
    src/data/stroke_style.h
    src/data/swatch.h
    src/data/swatch_list.h
    src/data/symbol_instance.h
    src/data/symbol_list.h
    src/data/symbol.h
    src/data/timeline.h
    src/data/transform.h
    src/util/zip_archive.h
)

set(SOURCES
    src/main.cpp
    src/gui/main_window.cpp
    src/gui/phoenix_view.cpp
    src/gui/document_view.cpp
    src/gui/timeline_view.cpp
    src/gui/layers_panel.cpp
    src/gui/timelines_panel.cpp
    src/gui/player.cpp
    src/parser/document_parser.cpp
    src/parser/zip_reader.cpp
    src/parser/fla_parser.cpp
    src/parser/path_parser.cpp
    src/data/action_script.cpp
    src/data/bitmap.cpp
    src/data/document.cpp
    src/data/dom_element.cpp
    src/data/edge.cpp
    src/data/element.cpp
    src/data/fill_style.cpp
    src/data/fla_document.cpp
    src/data/folder.cpp
    src/data/frame.cpp
    src/data/group.cpp
    src/data/layer.cpp
    src/data/linear_gradient.cpp
    src/data/oval_primitive.cpp
    src/data/point.cpp
    src/data/printer_settings.cpp
    src/data/publish_history.cpp    
    src/data/radial_gradient.cpp
    src/data/rect.cpp
    src/data/rectangle_primitive.cpp
    src/data/resource.cpp
    src/data/script_list.cpp
    src/data/shape.cpp
    src/data/solid_color.cpp
    src/data/static_text.cpp
    src/data/stroke_style.cpp
    src/data/swatch.cpp
    src/data/swatch_list.cpp
    src/data/symbol_instance.cpp
    src/data/symbol.cpp
    src/data/symbol_list.cpp
    src/data/timeline.cpp
    src/data/transform.cpp
    src/util/zip_archive.cpp
)

qt6_standard_project_setup()

qt6_add_executable(Phoenix MANUAL_FINALIZATION ${SOURCES} ${HEADERS})

target_link_libraries(Phoenix PRIVATE Qt6::Widgets zlibstatic tinyxml2 libas3)

if(TRUE)
    qt_finalize_executable(Phoenix)
endif()

# Copy Qt DLLs to build directory
if(WIN32)
    # Get Qt installation path
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_EXECUTABLE ${QT_QMAKE_EXECUTABLE} PATH)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    # Add custom target to deploy Qt DLLs
    add_custom_command(TARGET Phoenix POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:Phoenix>
        COMMENT "Deploying Qt libraries"
    )
endif()
