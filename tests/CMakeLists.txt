# Tests for Phoenix's Qt-free core. Configured from the top-level
# CMakeLists, they link the core the app is built from (phoenix_core, listed
# in cmake/PhoenixCore.cmake), so each source compiles once. This file can
# also be configured on its own, with no Qt: it then builds the core from the
# same list with the zlib and tinyxml2 kept in src/third_party, and leaves out
# JSFL and Python, whose engine the top-level build downloads.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PhoenixTests LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PHOENIX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set(TEST_SOURCES
    test_main.cpp
    test_command_stack.cpp
    test_curve_fit.cpp
    test_edge_writer.cpp
    test_edit_context.cpp
    test_editable_path.cpp
    test_element_commands.cpp
    test_fill_attribution.cpp
    test_geometry.cpp
    test_intersect.cpp
    test_new_document.cpp
    test_nine_slice.cpp
    test_planar_map.cpp
    test_selection.cpp
    test_region_split.cpp
    test_shape_merge.cpp
    test_shape_transform.cpp
    test_shape_tween.cpp
    test_frame_commands.cpp
    test_layer_commands.cpp
    test_timeline_edit.cpp
    test_library_commands.cpp
    test_shot_breakdown.cpp
    test_linked_assets.cpp
    test_library_transfer.cpp
    test_document_tabs.cpp
    test_review_notes.cpp
    test_safe_zones.cpp
    test_pegs.cpp
    test_controllers.cpp
    test_palettes.cpp
    test_color_art.cpp
    test_layer_effects.cpp
    test_lip_sync.cpp
    test_frame_actions.cpp
    test_function_curves.cpp
    test_deformers.cpp
    test_scene_commands.cpp
    test_script_edit.cpp
    test_document_settings.cpp
    test_text_style.cpp
    test_three_d.cpp
    test_filter_presets.cpp
    test_trace_bitmap.cpp
    test_sprite_sheet.cpp
    test_stroke_width.cpp
    test_swatches.cpp
    test_find_replace.cpp
    test_audio_decode.cpp
    test_brush_stroke.cpp
    test_magic_wand.cpp
    test_ik_tree.cpp
    test_fill_tracing.cpp
    test_binary_fla.cpp
    test_asset_warp.cpp
    test_property_command.cpp
    test_tween_ease.cpp
    test_shape_recognition.cpp
    test_path_tidy.cpp
    test_snapping.cpp
)

if(TARGET phoenix_core)
    set(TEST_CORE phoenix_core)
    set(PHOENIX_TESTS_HAVE_PARSER TRUE)
    if(TARGET qjs)
        set(PHOENIX_TESTS_HAVE_JSFL TRUE)
    endif()
    if(PHOENIX_PYTHON)
        set(PHOENIX_TESTS_HAVE_PYTHON TRUE)
    endif()
else()
    # On its own: the libraries kept in the tree, as the top-level adds them.
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${PHOENIX_SRC}/third_party/zlib ${CMAKE_BINARY_DIR}/src/third_party/zlib)
    set(tinyxml2_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(${PHOENIX_SRC}/third_party/tinyxml2 ${CMAKE_BINARY_DIR}/src/third_party/tinyxml2)
    add_subdirectory(${PHOENIX_SRC}/third_party/zstd ${CMAKE_BINARY_DIR}/src/third_party/zstd)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/PhoenixCore.cmake)
    phoenix_add_core(phoenix_test_core ${PHOENIX_SRC} FILES)
    set(TEST_CORE phoenix_test_core)
    set(PHOENIX_TESTS_HAVE_PARSER TRUE)
    message(STATUS "phoenix_tests: building without JSFL and Python; configure from the "
                   "top-level CMakeLists to include their tests as well.")
endif()

if(PHOENIX_TESTS_HAVE_PARSER)
    list(APPEND TEST_SOURCES
        test_motion_object.cpp
        test_psd_import.cpp
        test_ik_pose.cpp
        test_bitmap_decoder.cpp
        test_fla_writer.cpp
        test_font_items.cpp
        test_vector_import.cpp
        test_shortcut_sets.cpp
        test_publish_settings.cpp
        test_swf_writer.cpp
        test_extension_package.cpp
        test_behaviors.cpp
        test_parallel.cpp
        test_lzma.cpp
        test_swf_sound.cpp
        test_swf_import.cpp
        test_postscript_import.cpp
        test_fla_doctor.cpp
        test_exr_writer.cpp
        test_edit_list.cpp
        test_phx.cpp
        test_shape_geometry.cpp
        test_xfl_writer.cpp
        test_zip_archive.cpp
        test_phoenix_features.cpp
        test_other_apps.cpp
        test_node_graph.cpp
    )
endif()

if(PHOENIX_TESTS_HAVE_JSFL)
    list(APPEND TEST_SOURCES test_jsfl.cpp test_jsfl_e4x.cpp test_jsfl_xul.cpp test_jsfl_extensions.cpp test_jsfl_document.cpp
        test_jsfl_native.cpp)
endif()

# Playing movies with their scripts, with libas3 in the build.
if(TARGET phoenix_core AND TARGET as3_parser)
    list(APPEND TEST_SOURCES test_movie_runtime.cpp test_display.cpp test_as3_bindings.cpp test_canvas_scripts.cpp)
endif()

# Python scripts over the same object model; the tests skip themselves where
# no Python is installed.
if(PHOENIX_TESTS_HAVE_PYTHON)
    list(APPEND TEST_SOURCES test_python.cpp)
endif()

add_executable(phoenix_tests ${TEST_SOURCES})
target_link_libraries(phoenix_tests PRIVATE ${TEST_CORE})
if(PHOENIX_TESTS_HAVE_JSFL)
    target_compile_definitions(phoenix_tests PRIVATE PHOENIX_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    if(MSVC)
        target_link_options(phoenix_tests PRIVATE /STACK:8388608)
    endif()

    # Native extension libraries the tests put in an External Libraries
    # folder: one written against mm_jsapi.h, one with no entry point.
    add_library(test_jsapi_extension SHARED native_extension/test_extension.c)
    target_include_directories(test_jsapi_extension PRIVATE ${PHOENIX_SRC}/jsfl)
    add_library(test_jsapi_no_init SHARED native_extension/no_init.c)
    foreach(library test_jsapi_extension test_jsapi_no_init)
        set_target_properties(${library} PROPERTIES
            AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/native_extension
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/native_extension)
    endforeach()
    # JS_ObjectToValue casts a JSObject* to a long jsval, which 64-bit
    # Windows warns of; Phoenix's object handles are small enough to fit.
    if(MSVC)
        target_compile_options(test_jsapi_extension PRIVATE /wd4311)
    endif()
    add_dependencies(phoenix_tests test_jsapi_extension test_jsapi_no_init)
    target_compile_definitions(phoenix_tests PRIVATE
        PHOENIX_TEST_EXTENSION="$<TARGET_FILE:test_jsapi_extension>"
        PHOENIX_TEST_NO_INIT="$<TARGET_FILE:test_jsapi_no_init>")
endif()

if(MSVC)
    target_compile_options(phoenix_tests PRIVATE /W3)
else()
    target_compile_options(phoenix_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME phoenix_tests COMMAND phoenix_tests)
