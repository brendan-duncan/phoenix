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

# Where to find Qt, and how to fetch a trimmed one when there is none: see
# cmake/PhoenixQt.cmake.
include(cmake/PhoenixQt.cmake)
phoenix_find_qt()

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
    src/gui/properties_panel.h
    src/gui/library_panel.h
    src/gui/hot_text.h
    src/gui/convert_to_symbol_dialog.h
    src/gui/phoenix_view.h
    src/gui/tool.h
    src/gui/tool_icons.h
    src/gui/cursors.h
    src/gui/edge_bend.h
    src/gui/paint_tools.h
    src/gui/brush_tools.h
    src/gui/lasso_tool.h
    src/gui/selection_targets.h
    src/gui/navigation_tools.h
    src/gui/draw_placement.h
    src/gui/selection_tool.h
    src/gui/filters.h
    src/gui/lines_to_fills.h
    src/gui/free_transform_tool.h
    src/gui/primitive_tool.h
    src/gui/pen_tool.h
    src/gui/pencil_tool.h
    src/gui/text_tool.h
    src/gui/text_layout.h
    src/gui/subselection_tool.h
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
    src/data/bitmap_decoder.h
    src/data/color_transform.h
    src/data/document.h
    src/data/dom_element.h
    src/data/edge.h
    src/data/element.h
    src/data/fill_style.h
    src/data/fla_document.h
    src/data/folder.h
    src/data/filter.h
    src/data/frame.h
    src/data/group.h
    src/data/layer.h
    src/data/linear_gradient.h
    src/data/bitmap_fill.h
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
    src/edit/command.h
    src/edit/command_stack.h
    src/edit/edit_context.h
    src/edit/new_document.h
    src/edit/shape_transform.h
    src/edit/transform_blend.h
    src/edit/tween_ease.h
    src/edit/image_blur.h
    src/edit/element_commands.h
    src/edit/element_clone.h
    src/edit/selection.h
    src/edit/shape_merge.h
    src/edit/snapping.h
    src/edit/region_split.h
    src/edit/drawing_style.h
    src/edit/curve_fit.h
    src/edit/shape_recognition.h
    src/edit/path_tidy.h
    src/edit/shape_tween.h
    src/edit/frame_commands.h
    src/edit/layer_commands.h
    src/edit/timeline_edit.h
    src/edit/library_commands.h
    src/edit/property_command.h
    src/geom/curve.h
    src/geom/intersect.h
    src/geom/fill_attribution.h
    src/geom/planar_map.h
    src/geom/shape_geometry.h
    src/edit/editable_path.h
    src/writer/edge_writer.h
    src/writer/fla_writer.h
    src/writer/xfl_content.h
    src/writer/xfl_folder_writer.h
    src/writer/xfl_writer.h
    src/util/zip_archive.h
    src/util/zip_writer.h
)

set(SOURCES
    src/main.cpp
    src/gui/main_window.cpp
    src/gui/properties_panel.cpp
    src/gui/library_panel.cpp
    src/gui/hot_text.cpp
    src/gui/convert_to_symbol_dialog.cpp
    src/gui/phoenix_view.cpp
    src/gui/selection_tool.cpp
    src/gui/tool_icons.cpp
    src/gui/cursors.cpp
    src/gui/edge_bend.cpp
    src/gui/paint_tools.cpp
    src/gui/brush_tools.cpp
    src/gui/lasso_tool.cpp
    src/gui/navigation_tools.cpp
    src/gui/draw_placement.cpp
    src/gui/filters.cpp
    src/gui/stroke_effects.cpp
    src/gui/lines_to_fills.cpp
    src/gui/shape_outline.cpp
    src/gui/fill_offset.cpp
    src/gui/free_transform_tool.cpp
    src/gui/primitive_tool.cpp
    src/gui/pen_tool.cpp
    src/gui/pencil_tool.cpp
    src/gui/text_tool.cpp
    src/gui/text_layout.cpp
    src/gui/subselection_tool.cpp
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
    src/data/bitmap_decoder.cpp
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
    src/edit/command_stack.cpp
    src/edit/edit_context.cpp
    src/edit/new_document.cpp
    src/edit/shape_transform.cpp
    src/edit/transform_blend.cpp
    src/edit/tween_ease.cpp
    src/edit/image_blur.cpp
    src/edit/element_commands.cpp
    src/edit/element_clone.cpp
    src/edit/selection.cpp
    src/edit/shape_merge.cpp
    src/edit/snapping.cpp
    src/edit/region_split.cpp
    src/edit/drawing_style.cpp
    src/edit/curve_fit.cpp
    src/edit/shape_recognition.cpp
    src/edit/path_tidy.cpp
    src/edit/shape_tween.cpp
    src/edit/frame_commands.cpp
    src/edit/layer_commands.cpp
    src/edit/timeline_edit.cpp
    src/edit/library_commands.cpp
    src/geom/curve.cpp
    src/geom/intersect.cpp
    src/geom/fill_attribution.cpp
    src/geom/planar_map.cpp
    src/geom/shape_geometry.cpp
    src/edit/editable_path.cpp
    src/writer/edge_writer.cpp
    src/writer/fla_writer.cpp
    src/writer/xfl_content.cpp
    src/writer/xfl_folder_writer.cpp
    src/writer/xfl_writer.cpp
    src/util/zip_archive.cpp
    src/util/zip_writer.cpp
)

qt6_standard_project_setup()

qt6_add_executable(Phoenix MANUAL_FINALIZATION ${SOURCES} ${HEADERS})

target_link_libraries(Phoenix PRIVATE Qt6::Widgets Qt6::Svg zlibstatic tinyxml2 libas3)

if(TRUE)
    qt_finalize_executable(Phoenix)
endif()

# Tests for the non-Qt core. Also configurable standalone from tests/.
enable_testing()
add_subdirectory(tests)

# Copy Qt DLLs to build directory
if(WIN32)
    # windeployqt lives with the build tools, which for a cross kit (arm64
    # built on x64) is the host kit; told where the target kit's qtpaths is,
    # it deploys that kit's libraries and plugins rather than its own.
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_EXECUTABLE ${QT_QMAKE_EXECUTABLE} PATH)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    set(PHOENIX_WINDEPLOYQT_ARGS --no-translations)
    if(QT6_INSTALL_PREFIX AND EXISTS "${QT6_INSTALL_PREFIX}/bin/qtpaths.exe")
        list(APPEND PHOENIX_WINDEPLOYQT_ARGS --qtpaths "${QT6_INSTALL_PREFIX}/bin/qtpaths.exe")
    endif()

    # Add custom target to deploy Qt DLLs
    add_custom_command(TARGET Phoenix POST_BUILD
        COMMAND ${QT_WINDEPLOYQT_EXECUTABLE} ${PHOENIX_WINDEPLOYQT_ARGS} $<TARGET_FILE:Phoenix>
        COMMENT "Deploying Qt libraries"
    )
endif()
