# Tests for Phoenix's non-Qt core. This can be configured standalone (handy when
# only a C++ compiler is around) or pulled in from the top-level CMakeLists.
#
# Most of what is tested needs neither Qt nor a file reader, so it builds either
# way. The parts that read and write documents need tinyxml2 and zlib, which the
# top-level build provides; standalone those targets are absent and their tests
# are left out rather than failing to configure.
#
# Keep the two lists in step with what the tests actually touch. A source in the
# wrong list shows up as a link error in the standalone build only, which is easy
# to miss when working from the top-level one.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PhoenixTests LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PHOENIX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Free of Qt and of the file readers.
set(TEST_CORE_SOURCES
    ${PHOENIX_SRC}/data/bitmap_decoder.cpp
    ${PHOENIX_SRC}/util/gif_writer.cpp
    ${PHOENIX_SRC}/data/dom_element.cpp
    ${PHOENIX_SRC}/data/document.cpp
    ${PHOENIX_SRC}/data/edge.cpp
    ${PHOENIX_SRC}/data/fla_document.cpp
    ${PHOENIX_SRC}/data/element.cpp
    ${PHOENIX_SRC}/data/fill_style.cpp
    ${PHOENIX_SRC}/data/frame.cpp
    ${PHOENIX_SRC}/data/group.cpp
    ${PHOENIX_SRC}/data/layer.cpp
    ${PHOENIX_SRC}/data/linear_gradient.cpp
    ${PHOENIX_SRC}/data/oval_primitive.cpp
    ${PHOENIX_SRC}/data/radial_gradient.cpp
    ${PHOENIX_SRC}/data/rect.cpp
    ${PHOENIX_SRC}/data/rectangle_primitive.cpp
    ${PHOENIX_SRC}/data/shape.cpp
    ${PHOENIX_SRC}/data/symbol.cpp
    ${PHOENIX_SRC}/data/symbol_instance.cpp
    ${PHOENIX_SRC}/data/symbol_list.cpp
    ${PHOENIX_SRC}/data/stroke_style.cpp
    ${PHOENIX_SRC}/data/timeline.cpp
    ${PHOENIX_SRC}/data/transform.cpp
    ${PHOENIX_SRC}/edit/command_stack.cpp
    ${PHOENIX_SRC}/edit/brush_stroke.cpp
    ${PHOENIX_SRC}/edit/motion_object.cpp
    ${PHOENIX_SRC}/edit/brush_texture.cpp
    ${PHOENIX_SRC}/edit/camera.cpp
    ${PHOENIX_SRC}/edit/guide_path.cpp
    ${PHOENIX_SRC}/edit/asset_warp.cpp
    ${PHOENIX_SRC}/edit/magic_wand.cpp
    ${PHOENIX_SRC}/edit/curve_fit.cpp
    ${PHOENIX_SRC}/edit/drawing_style.cpp
    ${PHOENIX_SRC}/edit/editable_path.cpp
    ${PHOENIX_SRC}/edit/edit_context.cpp
    ${PHOENIX_SRC}/edit/element_commands.cpp
    ${PHOENIX_SRC}/edit/element_clone.cpp
    ${PHOENIX_SRC}/edit/new_document.cpp
    ${PHOENIX_SRC}/edit/selection.cpp
    ${PHOENIX_SRC}/edit/shape_merge.cpp
    ${PHOENIX_SRC}/edit/shape_transform.cpp
    ${PHOENIX_SRC}/edit/transform_blend.cpp
    ${PHOENIX_SRC}/edit/tween_ease.cpp
    ${PHOENIX_SRC}/edit/image_blur.cpp
    ${PHOENIX_SRC}/edit/shape_recognition.cpp
    ${PHOENIX_SRC}/edit/path_tidy.cpp
    ${PHOENIX_SRC}/edit/shape_tween.cpp
    ${PHOENIX_SRC}/edit/ik_pose.cpp
    ${PHOENIX_SRC}/edit/frame_commands.cpp
    ${PHOENIX_SRC}/edit/layer_commands.cpp
    ${PHOENIX_SRC}/edit/timeline_edit.cpp
    ${PHOENIX_SRC}/edit/library_commands.cpp
    ${PHOENIX_SRC}/edit/find_replace.cpp
    ${PHOENIX_SRC}/edit/library_media.cpp
    ${PHOENIX_SRC}/edit/frame_sound.cpp
    ${PHOENIX_SRC}/edit/frame_bake.cpp
    ${PHOENIX_SRC}/edit/shot_breakdown.cpp
    ${PHOENIX_SRC}/edit/linked_assets.cpp
    ${PHOENIX_SRC}/edit/library_transfer.cpp
    ${PHOENIX_SRC}/edit/review_notes.cpp
    ${PHOENIX_SRC}/edit/pegs.cpp
    ${PHOENIX_SRC}/edit/controllers.cpp
    ${PHOENIX_SRC}/edit/palettes.cpp
    ${PHOENIX_SRC}/edit/color_art.cpp
    ${PHOENIX_SRC}/edit/layer_effects.cpp
    ${PHOENIX_SRC}/edit/lip_sync.cpp
    ${PHOENIX_SRC}/edit/function_curves.cpp
    ${PHOENIX_SRC}/edit/deformers.cpp
    ${PHOENIX_SRC}/edit/scene_commands.cpp
    ${PHOENIX_SRC}/edit/document_settings.cpp
    ${PHOENIX_SRC}/edit/text_style.cpp
    ${PHOENIX_SRC}/edit/psd_import.cpp
    ${PHOENIX_SRC}/edit/three_d.cpp
    ${PHOENIX_SRC}/edit/filter_presets.cpp
    ${PHOENIX_SRC}/edit/combine_objects.cpp
    ${PHOENIX_SRC}/edit/swatches.cpp
    ${PHOENIX_SRC}/gui/audio_engine.cpp
    ${PHOENIX_SRC}/gui/miniaudio_impl.c
    ${PHOENIX_SRC}/edit/snapping.cpp
    ${PHOENIX_SRC}/edit/region_split.cpp
    ${PHOENIX_SRC}/geom/curve.cpp
    ${PHOENIX_SRC}/geom/fill_attribution.cpp
    ${PHOENIX_SRC}/geom/intersect.cpp
    ${PHOENIX_SRC}/geom/planar_map.cpp
    ${PHOENIX_SRC}/geom/shape_geometry.cpp
    ${PHOENIX_SRC}/geom/fill_tracing.cpp
    ${PHOENIX_SRC}/parser/path_parser.cpp
    ${PHOENIX_SRC}/writer/edge_writer.cpp
)

set(TEST_SOURCES
    test_main.cpp
    test_command_stack.cpp
    test_curve_fit.cpp
    test_edge_writer.cpp
    test_edit_context.cpp
    test_editable_path.cpp
    test_element_commands.cpp
    test_fill_attribution.cpp
    test_bitmap_decoder.cpp
    test_geometry.cpp
    test_intersect.cpp
    test_new_document.cpp
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
    test_review_notes.cpp
    test_pegs.cpp
    test_controllers.cpp
    test_palettes.cpp
    test_color_art.cpp
    test_layer_effects.cpp
    test_lip_sync.cpp
    test_function_curves.cpp
    test_deformers.cpp
    test_scene_commands.cpp
    test_document_settings.cpp
    test_text_style.cpp
    test_psd_import.cpp
    test_three_d.cpp
    test_filter_presets.cpp
    test_stroke_width.cpp
    test_swatches.cpp
    test_find_replace.cpp
    test_audio_decode.cpp
    test_brush_stroke.cpp
    test_magic_wand.cpp
    test_ik_tree.cpp
    test_ik_pose.cpp
    test_fill_tracing.cpp
    test_binary_fla.cpp
    test_motion_object.cpp
    test_asset_warp.cpp
    test_property_command.cpp
    test_tween_ease.cpp
    test_shape_recognition.cpp
    test_path_tidy.cpp
    test_snapping.cpp
)

if(TARGET tinyxml2 AND TARGET zlibstatic)
    list(APPEND TEST_CORE_SOURCES
        ${PHOENIX_SRC}/data/swatch_list.cpp
        ${PHOENIX_SRC}/parser/document_parser.cpp
        ${PHOENIX_SRC}/parser/ik_tree.cpp
        ${PHOENIX_SRC}/parser/fla_parser.cpp
        ${PHOENIX_SRC}/parser/binary_fla.cpp
        ${PHOENIX_SRC}/parser/compound_file.cpp
        ${PHOENIX_SRC}/parser/zip_reader.cpp
        ${PHOENIX_SRC}/parser/fla_doctor.cpp
        ${PHOENIX_SRC}/writer/document_diff.cpp
        ${PHOENIX_SRC}/util/zip_archive.cpp
        ${PHOENIX_SRC}/util/exr_writer.cpp
        ${PHOENIX_SRC}/edit/edit_list.cpp
        ${PHOENIX_SRC}/edit/font_items.cpp
        ${PHOENIX_SRC}/edit/vector_import.cpp
        ${PHOENIX_SRC}/util/zip_writer.cpp
        ${PHOENIX_SRC}/writer/fla_writer.cpp
        ${PHOENIX_SRC}/writer/xfl_content.cpp
        ${PHOENIX_SRC}/writer/xfl_folder_writer.cpp
        ${PHOENIX_SRC}/writer/xfl_writer.cpp
        ${PHOENIX_SRC}/phx/container.cpp
        ${PHOENIX_SRC}/phx/phx_dump.cpp
        ${PHOENIX_SRC}/phx/phx_read.cpp
        ${PHOENIX_SRC}/phx/phx_write.cpp
    )
    list(APPEND TEST_SOURCES
        test_fla_writer.cpp
        test_font_items.cpp
        test_vector_import.cpp
        test_fla_doctor.cpp
        test_exr_writer.cpp
        test_edit_list.cpp
        test_phx.cpp
        test_shape_geometry.cpp
        test_xfl_writer.cpp
    )
    set(PHOENIX_TESTS_HAVE_PARSER TRUE)
else()
    message(STATUS
        "phoenix_tests: building without the file readers (tinyxml2/zlib targets "
        "not available). Configure from the top-level CMakeLists to include the "
        "XFL round-trip tests as well.")
endif()

add_executable(phoenix_tests ${TEST_SOURCES} ${TEST_CORE_SOURCES})

if(PHOENIX_TESTS_HAVE_PARSER)
    target_link_libraries(phoenix_tests PRIVATE tinyxml2 zlibstatic)
    if(UNIX AND NOT APPLE)
        target_link_libraries(phoenix_tests PRIVATE ${CMAKE_DL_LIBS} pthread m)
    elseif(APPLE)
        target_link_libraries(phoenix_tests PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
    endif()
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
