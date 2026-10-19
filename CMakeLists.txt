cmake_minimum_required(VERSION 3.16)

include(FetchContent)
# libas3, the ActionScript parser and VM, is developed alongside Phoenix:
# a checkout beside this one (../libas3), or the one PHOENIX_LIBAS3_SOURCE
# names, is built as it stands. Without one (CI, a release), the commit
# pinned here is fetched; move it forward on purpose when libas3 changes.
set(PHOENIX_LIBAS3_SOURCE "" CACHE PATH "A libas3 checkout to build instead of the pinned commit")
if(NOT PHOENIX_LIBAS3_SOURCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libas3/CMakeLists.txt")
    get_filename_component(PHOENIX_LIBAS3_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../libas3" ABSOLUTE)
endif()
if(PHOENIX_LIBAS3_SOURCE)
    message(STATUS "libas3: ${PHOENIX_LIBAS3_SOURCE}")
    set(FETCHCONTENT_SOURCE_DIR_LIBAS3 "${PHOENIX_LIBAS3_SOURCE}" CACHE PATH "" FORCE)
else()
    unset(FETCHCONTENT_SOURCE_DIR_LIBAS3 CACHE)
endif()
FetchContent_Declare(
    libas3
    GIT_REPOSITORY https://github.com/brendan-duncan/libas3
    GIT_TAG d81a73dc5eb9a906920bb541460e2657ebca547a
)
FetchContent_MakeAvailable(libas3)

add_library(libas3 INTERFACE)
target_link_libraries(libas3 INTERFACE as3_parser)

project(Phoenix VERSION 0.20.0 LANGUAGES C CXX)

# libas3 copies as3_parser.pdb from beside the library after a Debug build,
# but a single-configuration generator (Ninja) has MSVC write a static
# library's compile PDB in its object folder; put it where the copy looks.
if(MSVC AND NOT CMAKE_CONFIGURATION_TYPES AND TARGET as3_parser)
    get_target_property(AS3_PARSER_DIR as3_parser BINARY_DIR)
    set_target_properties(as3_parser PROPERTIES
        COMPILE_PDB_NAME as3_parser
        COMPILE_PDB_OUTPUT_DIRECTORY "${AS3_PARSER_DIR}")
endif()

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

# The user guide names the release it describes (docs/assets/site.js).
file(STRINGS "${CMAKE_SOURCE_DIR}/docs/assets/site.js" _phoenix_docs_version REGEX "^const VERSION = ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _phoenix_docs_version "${_phoenix_docs_version}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" _phoenix_release "${PHOENIX_VERSION}")
if(NOT _phoenix_docs_version STREQUAL _phoenix_release)
    message(WARNING "docs/assets/site.js says the guide describes ${_phoenix_docs_version}, but this is "
        "Phoenix ${PHOENIX_VERSION}: update VERSION there.")
endif()

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

# QuickJS (quickjs-ng), the JavaScript engine JSFL scripts run in (src/jsfl).
# Only its library is built; its own command-line tools are left out.
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs
    GIT_TAG v0.17.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(quickjs)

# Add tinyxml2 from third_party
set(tinyxml2_SHARED_LIBS OFF CACHE BOOL "Don't build tinyxml2 as a shared library")
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "Don't build tinyxml2 tests")
add_subdirectory(src/third_party/tinyxml2)
add_subdirectory(src/third_party/zstd)
add_subdirectory(src/third_party/libvterm)

set(HEADERS
    src/gui/main_window.h
    src/gui/properties_panel.h
    src/gui/library_panel.h
    src/gui/find_replace_dialog.h
    src/gui/toolbox.h
    src/gui/toolbox_flow.h
    src/gui/color_well.h
    src/gui/toolbar_editor.h
    src/gui/theme.h
    src/gui/theme_editor.h
    src/gui/actions_panel.h
    src/gui/claude_code.h
    src/gui/code_editor.h
    src/gui/script_window.h
    src/gui/cep_panel.h
    src/gui/node_graph_panel.h
    src/gui/node_graph_style.h
    src/gui/node_graph_view.h
    src/gui/node_search.h
    src/gui/claude_code_panel.h
    src/gui/pty_process.h
    src/gui/terminal_view.h
    src/gui/html_view.h
    src/gui/trace_bitmap_dialog.h
    src/gui/test_movie_window.h
    src/gui/movie_hooks.h
    src/gui/command_line.h
    src/gui/capture_steps.h
    src/gui/notes_panel.h
    src/gui/controllers_panel.h
    src/gui/palettes_panel.h
    src/gui/lip_sync_dialog.h
    src/gui/function_curves_panel.h
    src/gui/scene_panel.h
    src/gui/spell_checker.h
    src/gui/audio_engine.h
    src/gui/frame_picker_panel.h
    src/gui/swap_symbol_dialog.h
    src/gui/start_dialog.h
    src/gui/autosave.h
    src/gui/image_export.h
    src/gui/arrange_panels.h
    src/gui/motion_editor.h
    src/gui/motion_presets.h
    src/gui/xsheet_panel.h
    src/gui/layer_depth_panel.h
    src/gui/camera_tool.h
    src/gui/bone_tool.h
    src/gui/asset_warp_tool.h
    src/gui/three_d_tool.h
    src/gui/width_tool.h
    src/gui/preferences.h
    src/gui/shortcuts_dialog.h
    src/gui/movie_explorer.h
    src/gui/envelope_dialog.h
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
    src/gui/behavior_library.h
    src/gui/behaviors_panel.h
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
    src/gui/bone_corrector.h
    src/gui/movie_export.h
    src/gui/sprite_sheet_dialog.h
    src/gui/brush_properties_dialog.h
    src/gui/publish_dialog.h
    src/gui/paint_recorder.h
    src/gui/swf_export.h
    src/gui/render_queue.h
    src/gui/stage_controls.h
    src/gui/app_identity.h
    src/gui/tutorial_lesson.h
    src/gui/tutorial_runner.h
    src/gui/script_api.h
    src/gui/script_runner.h
    src/gui/output_panel.h
    src/jsfl/e4x.h
    src/jsfl/engine.h
    src/jsfl/behavior_engine.h
    src/jsfl/behavior_frames.h
    src/jsfl/panel_script.h
    src/jsfl/tool_script.h
    src/jsfl/flfile.h
    src/jsfl/host.h
    src/jsfl/model.h
    src/jsfl/object.h
    src/jsfl/session.h
    src/jsfl/xul.h
    src/gui/xul_dialog.h
    src/jsfl/extensions.h
    src/jsfl/mm_jsapi.h
    src/jsfl/native_extensions.h
    src/gui/mcp_server.h
    src/gui/stroke_style_dialog.h
    src/gui/brush_library_store.h
    src/gui/brush_library_dialog.h
    src/gui/document_brushes_dialog.h
    src/gui/safe_zones_dialog.h
    src/gui/selection_targets.h
    src/gui/navigation_tools.h
    src/gui/draw_placement.h
    src/gui/selection_tool.h
    src/gui/filters.h
    src/gui/display_painter.h
    src/gui/display_check.h
    src/gui/lines_to_fills.h
    src/gui/free_transform_tool.h
    src/gui/primitive_tool.h
    src/gui/script_tool.h
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
    src/parser/extension_package.h
    src/parser/fla_doctor.h
    src/parser/fla_parser.h
    src/parser/binary_fla.h
    src/parser/swf_import.h
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
    src/edit/guide_path.h
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
    src/edit/brush_presets.h
    src/edit/shortcut_sets.h
    src/edit/publish_settings.h
    src/edit/canvas_document.h
    src/edit/brush_texture.h
    src/edit/camera.h
    src/edit/motion_object.h
    src/edit/asset_warp.h
    src/edit/magic_wand.h
    src/edit/curve_fit.h
    src/edit/shape_recognition.h
    src/edit/path_tidy.h
    src/edit/shape_tween.h
    src/edit/phoenix_features.h
    src/edit/ik_pose.h
    src/edit/frame_commands.h
    src/edit/layer_commands.h
    src/edit/timeline_edit.h
    src/edit/library_commands.h
    src/edit/find_replace.h
    src/edit/library_media.h
    src/edit/frame_sound.h
    src/edit/frame_bake.h
    src/edit/shot_breakdown.h
    src/edit/linked_assets.h
    src/edit/review_notes.h
    src/edit/safe_zones.h
    src/edit/pegs.h
    src/edit/controllers.h
    src/edit/bone_corrections.h
    src/edit/palettes.h
    src/edit/color_art.h
    src/edit/layer_effects.h
    src/edit/lip_sync.h
    src/edit/frame_actions.h
    src/edit/function_curves.h
    src/edit/deformers.h
    src/edit/depth_sort.h
    src/edit/onion_shift.h
    src/edit/scene_commands.h
    src/edit/script_edit.h
    src/edit/document_settings.h
    src/edit/text_style.h
    src/edit/font_items.h
    src/edit/vector_import.h
    src/edit/paint_brushes.h
    src/edit/brush_library.h
    src/edit/pdf_objects.h
    src/edit/postscript_import.h
    src/edit/psd_import.h
    src/edit/three_d.h
    src/edit/behaviors.h
    src/edit/shape_operators.h
    src/edit/text_animators.h
    src/edit/adjustments.h
    src/edit/node_graph.h
    src/edit/templates.h
    src/edit/other_apps.h
    src/edit/filter_presets.h
    src/edit/library_transfer.h
    src/edit/document_session.h
    src/edit/trace_bitmap.h
    src/edit/edit_list.h
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
    src/geom/nine_slice.h
    src/edit/editable_path.h
    src/edit/path_bend.h
    src/writer/edge_writer.h
    src/writer/fla_writer.h
    src/writer/xfl_content.h
    src/writer/xfl_folder_writer.h
    src/writer/xfl_writer.h
    src/writer/document_diff.h
    src/phx/container.h
    src/phx/phx_codec.h
    src/phx/phx_document.h
    src/phx/phx_schema.h
    src/phx/wire.h
    src/util/zip_archive.h
    src/util/exr_writer.h
    src/util/zip_writer.h
    src/util/gif_writer.h
    src/util/png_writer.h
    src/util/sprite_sheet.h
    src/util/swf_writer.h
    src/util/lzma.h
    src/util/swf_sound.h
    src/edit/swf_event_sounds.h
)

set(SOURCES
    src/main.cpp
    src/gui/main_window.cpp
    src/gui/properties_panel.cpp
    src/gui/library_panel.cpp
    src/gui/find_replace_dialog.cpp
    src/gui/toolbox.cpp
    src/gui/color_well.cpp
    src/gui/toolbar_editor.cpp
    src/gui/theme.cpp
    src/gui/theme_editor.cpp
    src/gui/actions_panel.cpp
    src/gui/claude_code.cpp
    src/gui/code_editor.cpp
    src/gui/script_window.cpp
    src/gui/cep_panel.cpp
    src/gui/node_graph_panel.cpp
    src/gui/node_graph_style.cpp
    src/gui/node_graph_view.cpp
    src/gui/node_search.cpp
    src/gui/claude_code_panel.cpp
    src/gui/pty_process.cpp
    src/gui/terminal_view.cpp
    src/gui/html_view.cpp
    src/gui/trace_bitmap_dialog.cpp
    src/gui/test_movie_window.cpp
    src/gui/movie_hooks.cpp
    src/gui/command_line.cpp
    src/gui/capture_steps.cpp
    src/gui/notes_panel.cpp
    src/gui/controllers_panel.cpp
    src/gui/palettes_panel.cpp
    src/gui/lip_sync_dialog.cpp
    src/gui/function_curves_panel.cpp
    src/gui/scene_panel.cpp
    src/gui/spell_checker.cpp
    src/gui/frame_picker_panel.cpp
    src/gui/swap_symbol_dialog.cpp
    src/gui/start_dialog.cpp
    src/gui/autosave.cpp
    src/gui/image_export.cpp
    src/gui/arrange_panels.cpp
    src/gui/motion_editor.cpp
    src/gui/motion_presets.cpp
    src/gui/xsheet_panel.cpp
    src/gui/layer_depth_panel.cpp
    src/gui/camera_tool.cpp
    src/gui/bone_tool.cpp
    src/gui/asset_warp_tool.cpp
    src/gui/three_d_tool.cpp
    src/gui/width_tool.cpp
    src/gui/preferences.cpp
    src/gui/shortcuts_dialog.cpp
    src/gui/movie_explorer.cpp
    src/gui/envelope_dialog.cpp
    src/gui/ease_curve_dialog.cpp
    src/gui/file_association.cpp
    src/gui/crash_dump.cpp
    src/gui/color_panel.cpp
    src/gui/video_import.cpp
    src/gui/swatches_panel.cpp
    src/gui/media_import.cpp
    src/gui/timeline_audio.cpp
    src/gui/new_document_dialog.cpp
    src/gui/updater.cpp
    src/gui/hot_text.cpp
    src/gui/convert_to_symbol_dialog.cpp
    src/gui/phoenix_view.cpp
    src/gui/behavior_library.cpp
    src/gui/behaviors_panel.cpp
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
    src/gui/bone_corrector.cpp
    src/gui/movie_export.cpp
    src/gui/sprite_sheet_dialog.cpp
    src/gui/brush_properties_dialog.cpp
    src/gui/publish_dialog.cpp
    src/gui/html5_publish.cpp
    src/gui/paint_recorder.cpp
    src/gui/swf_export.cpp
    src/gui/render_queue.cpp
    src/gui/stage_controls.cpp
    src/gui/app_identity.cpp
    src/gui/tutorial_lesson.cpp
    src/gui/tutorial_runner.cpp
    src/gui/script_api.cpp
    src/gui/script_runner.cpp
    src/gui/output_panel.cpp
    src/gui/xul_dialog.cpp
    src/gui/mcp_server.cpp
    src/gui/stroke_style_dialog.cpp
    src/gui/brush_library_store.cpp
    src/gui/brush_library_dialog.cpp
    src/gui/document_brushes_dialog.cpp
    src/gui/safe_zones_dialog.cpp
    src/gui/navigation_tools.cpp
    src/gui/draw_placement.cpp
    src/gui/filters.cpp
    src/gui/display_painter.cpp
    src/gui/display_check.cpp
    src/gui/stroke_effects.cpp
    src/gui/lines_to_fills.cpp
    src/gui/shape_outline.cpp
    src/gui/fill_offset.cpp
    src/gui/free_transform_tool.cpp
    src/gui/primitive_tool.cpp
    src/gui/script_tool.cpp
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
)

# Python scripts (src/python) run with the person's own Python, loaded when
# the first one runs: building needs no Python, and Phoenix starts without
# one. OFF leaves Python scripting out.
option(PHOENIX_PYTHON "Run Python scripts with the user's own Python" ON)
if(PHOENIX_PYTHON)
    list(APPEND HEADERS src/python/python_api.h src/python/python_engine.h src/python/python_keywords.h)
endif()

# The Qt-free core, compiled once for the app and the tests.
include(cmake/PhoenixCore.cmake)
if(PHOENIX_PYTHON)
    phoenix_add_core(phoenix_core ${CMAKE_CURRENT_SOURCE_DIR}/src FILES JSFL PYTHON PLAY)
else()
    phoenix_add_core(phoenix_core ${CMAKE_CURRENT_SOURCE_DIR}/src FILES JSFL PLAY)
endif()

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

target_link_libraries(Phoenix PRIVATE phoenix_core Qt6::Widgets Qt6::Svg zlibstatic tinyxml2 libas3 vterm)
# The web for the movies Test Movie plays (URLLoader, Loader), when the Qt
# found has its network module.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Network)
if(TARGET Qt6::Network)
    target_link_libraries(Phoenix PRIVATE Qt6::Network)
    target_compile_definitions(Phoenix PRIVATE PHOENIX_NETWORK)
endif()
target_link_libraries(Phoenix PRIVATE qjs)
if(PHOENIX_PYTHON)
    target_compile_definitions(Phoenix PRIVATE PHOENIX_PYTHON)
    target_link_libraries(Phoenix PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    # Scripts run on the window's thread; room for deep recursion in them.
    target_link_options(Phoenix PRIVATE /STACK:8388608)
    # Symbols for a Release build too, beside it as Phoenix.pdb, so a crash
    # dump (crash_dump.cpp) reads as functions and lines. The code is
    # optimized just the same.
    target_compile_options(Phoenix PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_compile_options(phoenix_core PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Phoenix PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    target_link_libraries(Phoenix PRIVATE dbghelp)
    # main_window.cpp has more sections than an object file holds by
    # default once inlining adds its own (RelWithDebInfo).
    target_compile_options(Phoenix PRIVATE /bigobj)
endif()
if(WIN32)
    # The AI assistants' local socket (mcp_server.cpp).
    target_link_libraries(Phoenix PRIVATE ws2_32)
endif()

# The built-in lessons (Help > Tutorials), in the app as :/tutorials.
file(GLOB_RECURSE PHOENIX_TUTORIAL_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/tutorials/*")
if(PHOENIX_TUTORIAL_FILES)
    qt_add_resources(Phoenix "tutorials" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/resources"
        FILES ${PHOENIX_TUTORIAL_FILES})
endif()
# The CSInterface shim HTML extension panels start with, and the Node.js
# modules of those that ask for them (cep_panel.cpp).
qt_add_resources(Phoenix "cep" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/cep/adobe_cep.js" "${CMAKE_SOURCE_DIR}/resources/cep/node.js")
# The built-in ActionScript library (the fl.* components), as :/as3 (resources/as3/README.md).
file(GLOB_RECURSE PHOENIX_AS3_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/as3/*.as")
qt_add_resources(Phoenix "as3" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/resources" FILES ${PHOENIX_AS3_FILES})
# The HTML5 runtime Publish writes beside a page, as :/html5 (resources/html5/README.md).
file(GLOB PHOENIX_HTML5_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/html5/*.js")
qt_add_resources(Phoenix "html5" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/resources" FILES ${PHOENIX_HTML5_FILES})
target_compile_definitions(Phoenix PRIVATE PHOENIX_VERSION="${PHOENIX_VERSION}")
# HTML extension panels: see cmake/PhoenixWebView.cmake.
include(cmake/PhoenixWebView.cmake)
phoenix_add_web_view(Phoenix)

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

# The Claude Code panel's terminal: forkpty is in libutil on Linux.
if(UNIX AND NOT APPLE)
    target_link_libraries(Phoenix PRIVATE util)
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
elseif(UNIX AND NOT APPLE)
    # Flat on Linux too, as the AppImage Velopack makes runs it: phoenix at
    # the top, the Qt it needs in lib/ and plugins/ beside it, found from
    # where it is wherever the AppImage mounts.
    set(PHOENIX_INSTALL_BINDIR .)
    install(CODE "set(QT_DEPLOY_BIN_DIR .)" COMPONENT Phoenix)
    set_target_properties(Phoenix PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
endif()
install(TARGETS Phoenix
    RUNTIME DESTINATION ${PHOENIX_INSTALL_BINDIR} COMPONENT Phoenix
    BUNDLE DESTINATION . COMPONENT Phoenix)
set(PHOENIX_DEPLOY_OPTIONS)
if(WIN32)
    # Phoenix draws with QPainter, so neither the software OpenGL nor the
    # shader compilers are needed. Qt's networking ships when Test Movie's
    # movies load from the web with it.
    set(PHOENIX_DEPLOY_OPTIONS DEPLOY_TOOL_OPTIONS
        --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler)
    if(NOT TARGET Qt6::Network)
        list(APPEND PHOENIX_DEPLOY_OPTIONS --no-network)
    endif()
endif()
qt_generate_deploy_app_script(
    TARGET Phoenix
    OUTPUT_SCRIPT PHOENIX_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
    ${PHOENIX_DEPLOY_OPTIONS})
install(SCRIPT ${PHOENIX_DEPLOY_SCRIPT} COMPONENT Phoenix)
# The deploy script brings the platforms Phoenix shows windows with; the
# offscreen one, for renders from the command line, goes beside them.
if(TARGET Qt6::QOffscreenIntegrationPlugin AND NOT APPLE)
    install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
        DESTINATION ${PHOENIX_INSTALL_BINDIR}/plugins/platforms COMPONENT Phoenix)
elseif(TARGET Qt6::QOffscreenIntegrationPlugin)
    install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
        DESTINATION Phoenix.app/Contents/PlugIns/platforms COMPONENT Phoenix)
endif()

# The phoenix Python module (python/phoenix), for driving Phoenix from your
# own Python, ships beside it and finds the Phoenix it came with.
if(PHOENIX_PYTHON)
    if(APPLE)
        set(PHOENIX_PYTHON_MODULE_DIR Phoenix.app/Contents/Resources/python)
    else()
        set(PHOENIX_PYTHON_MODULE_DIR ${PHOENIX_INSTALL_BINDIR}/python)
    endif()
    install(DIRECTORY python/phoenix DESTINATION ${PHOENIX_PYTHON_MODULE_DIR} COMPONENT Phoenix
        PATTERN __pycache__ EXCLUDE)
endif()

# The ffmpeg Phoenix encodes and reads video with ships beside it: see
# cmake/PhoenixFfmpeg.cmake.
include(cmake/PhoenixFfmpeg.cmake)
phoenix_ship_ffmpeg()

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
    # windeployqt brings only the window platform; renders without a window
    # (QT_QPA_PLATFORM=offscreen: the command line, tests, render farms)
    # need the offscreen one too, or Qt stops with a "no platform plugin"
    # box on the screen.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
        add_custom_command(TARGET Phoenix POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Phoenix>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
                "$<TARGET_FILE_DIR:Phoenix>/platforms/"
            COMMENT "Deploying the offscreen platform plugin"
        )
    else()
        message(WARNING "Qt's offscreen platform plugin was not found; Phoenix's command-line renders will need "
            "QT_QPA_PLATFORM_PLUGIN_PATH pointed at Qt's plugins/platforms folder")
    endif()
endif()
