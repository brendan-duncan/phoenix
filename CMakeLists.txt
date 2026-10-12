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

project(Phoenix VERSION 0.3.0 LANGUAGES C CXX)

# The version shown in the app and given to the installer. A release build
# passes the tag's version (-DPHOENIX_VERSION=1.2.3).
if(NOT PHOENIX_VERSION)
    set(PHOENIX_VERSION "${PROJECT_VERSION}")
endif()
if(NOT PHOENIX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "PHOENIX_VERSION must start with major.minor.patch, not '${PHOENIX_VERSION}'")
endif()
# Read by tools/release/pack.py, to check that it packs the version it says.
file(WRITE "${CMAKE_BINARY_DIR}/phoenix_version.txt" "${PHOENIX_VERSION}")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where to find Qt, and how to fetch a trimmed one when there is none: see
# cmake/PhoenixQt.cmake.
include(cmake/PhoenixQt.cmake)
phoenix_find_qt()

# The auto-updater: see cmake/PhoenixVelopack.cmake.
include(cmake/PhoenixVelopack.cmake)
phoenix_add_velopack()

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
    src/gui/audio_engine.h
    src/gui/frame_picker_panel.h
    src/gui/swap_symbol_dialog.h
    src/gui/start_dialog.h
    src/gui/autosave.h
    src/gui/image_export.h
    src/gui/arrange_panels.h
    src/gui/ease_curve_dialog.h
    src/gui/file_association.h
    src/gui/color_panel.h
    src/gui/video_import.h
    src/gui/swatches_panel.h
    src/gui/media_import.h
    src/gui/timeline_audio.h
    src/gui/new_document_dialog.h
    src/gui/updater.h
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
    src/gui/magic_wand_tool.h
    src/gui/gradient_transform_tool.h
    src/gui/anchor_point_tool.h
    src/gui/ik_poser.h
    src/gui/movie_export.h
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
    src/parser/ik_tree.h
    src/parser/zip_reader.h
    src/parser/fla_parser.h
    src/parser/binary_fla.h
    src/parser/compound_file.h
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
    src/data/lazy_bytes.h
    src/data/linear_gradient.h
    src/data/bitmap_fill.h
    src/data/oval_primitive.h
    src/data/point.h
    src/data/printer_settings.h
    src/data/publish_history.h
    src/data/radial_gradient.h
    src/data/rect.h
    src/data/raw_element.h
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
    src/edit/guide_command.h
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
    src/edit/brush_stroke.h
    src/edit/motion_object.h
    src/edit/asset_warp.h
    src/edit/magic_wand.h
    src/edit/curve_fit.h
    src/edit/shape_recognition.h
    src/edit/path_tidy.h
    src/edit/shape_tween.h
    src/edit/ik_pose.h
    src/edit/frame_commands.h
    src/edit/layer_commands.h
    src/edit/timeline_edit.h
    src/edit/library_commands.h
    src/edit/library_media.h
    src/edit/frame_sound.h
    src/edit/frame_bake.h
    src/edit/combine_objects.h
    src/edit/transform_parts.h
    src/edit/swatches.h
    src/edit/property_command.h
    src/geom/curve.h
    src/geom/intersect.h
    src/geom/fill_attribution.h
    src/geom/planar_map.h
    src/geom/shape_geometry.h
    src/geom/fill_tracing.h
    src/edit/editable_path.h
    src/writer/edge_writer.h
    src/writer/fla_writer.h
    src/writer/xfl_content.h
    src/writer/xfl_folder_writer.h
    src/writer/xfl_writer.h
    src/phx/container.h
    src/phx/phx_codec.h
    src/phx/phx_document.h
    src/phx/phx_schema.h
    src/phx/wire.h
    src/util/zip_archive.h
    src/util/zip_writer.h
    src/util/gif_writer.h
)

set(SOURCES
    src/main.cpp
    src/gui/main_window.cpp
    src/gui/properties_panel.cpp
    src/gui/library_panel.cpp
    src/gui/audio_engine.cpp
    src/gui/frame_picker_panel.cpp
    src/gui/swap_symbol_dialog.cpp
    src/gui/start_dialog.cpp
    src/gui/autosave.cpp
    src/gui/image_export.cpp
    src/gui/arrange_panels.cpp
    src/gui/ease_curve_dialog.cpp
    src/gui/file_association.cpp
    src/gui/color_panel.cpp
    src/gui/video_import.cpp
    src/gui/swatches_panel.cpp
    src/gui/media_import.cpp
    src/gui/timeline_audio.cpp
    src/gui/miniaudio_impl.c
    src/gui/new_document_dialog.cpp
    src/gui/updater.cpp
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
    src/gui/magic_wand_tool.cpp
    src/gui/gradient_transform_tool.cpp
    src/gui/anchor_point_tool.cpp
    src/gui/ik_poser.cpp
    src/gui/movie_export.cpp
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
    src/parser/ik_tree.cpp
    src/parser/zip_reader.cpp
    src/parser/fla_parser.cpp
    src/parser/binary_fla.cpp
    src/parser/compound_file.cpp
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
    src/edit/brush_stroke.cpp
    src/edit/motion_object.cpp
    src/edit/asset_warp.cpp
    src/edit/magic_wand.cpp
    src/edit/curve_fit.cpp
    src/edit/shape_recognition.cpp
    src/edit/path_tidy.cpp
    src/edit/shape_tween.cpp
    src/edit/ik_pose.cpp
    src/edit/frame_commands.cpp
    src/edit/layer_commands.cpp
    src/edit/timeline_edit.cpp
    src/edit/library_commands.cpp
    src/edit/library_media.cpp
    src/edit/frame_sound.cpp
    src/edit/frame_bake.cpp
    src/edit/combine_objects.cpp
    src/edit/swatches.cpp
    src/geom/curve.cpp
    src/geom/intersect.cpp
    src/geom/fill_attribution.cpp
    src/geom/planar_map.cpp
    src/geom/shape_geometry.cpp
    src/geom/fill_tracing.cpp
    src/edit/editable_path.cpp
    src/writer/edge_writer.cpp
    src/writer/fla_writer.cpp
    src/writer/xfl_content.cpp
    src/writer/xfl_folder_writer.cpp
    src/writer/xfl_writer.cpp
    src/phx/container.cpp
    src/phx/phx_dump.cpp
    src/phx/phx_read.cpp
    src/phx/phx_write.cpp
    src/util/zip_archive.cpp
    src/util/zip_writer.cpp
    src/util/gif_writer.cpp
)

qt6_standard_project_setup()

# The version without a pre-release suffix, for the places that only take
# numbers: the Windows version resource and the macOS bundle version.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${PHOENIX_VERSION}")
set(PHOENIX_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PHOENIX_VERSION_MINOR ${CMAKE_MATCH_2})
set(PHOENIX_VERSION_PATCH ${CMAKE_MATCH_3})
set(PHOENIX_VERSION_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

# The icon and version stamped into Phoenix.exe.
if(WIN32)
    configure_file(src/phoenix.rc.in ${CMAKE_CURRENT_BINARY_DIR}/phoenix.rc @ONLY)
    list(APPEND SOURCES ${CMAKE_CURRENT_BINARY_DIR}/phoenix.rc)
endif()

qt6_add_executable(Phoenix MANUAL_FINALIZATION ${SOURCES} ${HEADERS})

# On macOS, Phoenix.app with its icon. The bundle identifier is how macOS
# knows the app, so it must not change.
if(APPLE)
    set(PHOENIX_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/src/phoenix.icns)
    target_sources(Phoenix PRIVATE ${PHOENIX_ICNS})
    set_source_files_properties(${PHOENIX_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Phoenix PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME Phoenix
        MACOSX_BUNDLE_GUI_IDENTIFIER com.brendanduncan.phoenix
        MACOSX_BUNDLE_ICON_FILE phoenix.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PHOENIX_VERSION_NUMERIC}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PHOENIX_VERSION_NUMERIC}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
endif()

target_link_libraries(Phoenix PRIVATE Qt6::Widgets Qt6::Svg zlibstatic tinyxml2 libas3)
target_compile_definitions(Phoenix PRIVATE PHOENIX_VERSION="${PHOENIX_VERSION}")
if(PHOENIX_UPDATER)
    target_link_libraries(Phoenix PRIVATE velopack)
    target_compile_definitions(Phoenix PRIVATE PHOENIX_UPDATER)
endif()

# Audio output through miniaudio (vendored in src/third_party/miniaudio):
# it loads the platform audio libraries at run time on Windows and macOS,
# and needs dl, pthread and libm on Linux.
if(APPLE)
    target_link_libraries(Phoenix PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(Phoenix PRIVATE ${CMAKE_DL_LIBS} pthread m)
endif()

if(TRUE)
    qt_finalize_executable(Phoenix)
endif()

# What a release ships: `cmake --install build --config Release --component
# Phoenix --prefix <dir>` puts Phoenix and the Qt it needs in <dir>, which
# tools/release/pack.py hands to Velopack. The component keeps out what
# zlib, tinyxml2 and libas3 would install.
include(GNUInstallDirs)
set(PHOENIX_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    # Flat, as Windows apps are: the exe, its DLLs and plugins/ side by side.
    set(PHOENIX_INSTALL_BINDIR .)
    install(CODE "set(QT_DEPLOY_BIN_DIR .)" COMPONENT Phoenix)
endif()
install(TARGETS Phoenix
    RUNTIME DESTINATION ${PHOENIX_INSTALL_BINDIR} COMPONENT Phoenix
    BUNDLE DESTINATION . COMPONENT Phoenix)
set(PHOENIX_DEPLOY_OPTIONS)
if(WIN32)
    # Phoenix draws with QPainter, so neither the software OpenGL nor the
    # shader compilers are needed, and it does no networking of Qt's (the
    # updater has its own).
    set(PHOENIX_DEPLOY_OPTIONS DEPLOY_TOOL_OPTIONS
        --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler
        --no-network)
endif()
qt_generate_deploy_app_script(
    TARGET Phoenix
    OUTPUT_SCRIPT PHOENIX_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${PHOENIX_DEPLOY_OPTIONS})
install(SCRIPT ${PHOENIX_DEPLOY_SCRIPT} COMPONENT Phoenix)

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
