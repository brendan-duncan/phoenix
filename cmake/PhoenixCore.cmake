# phoenix_core: the parts of Phoenix free of Qt (the document model, the
# readers and writers, the editing commands and geometry, scripting), compiled
# once for both the app and the tests. This is the one list of those sources;
# the app's own (Qt) sources are listed in the top-level CMakeLists.
#
# The groups follow what they need beyond a C++ compiler; the first two go
# together (the model and the commands use the file code), and the tests can
# be configured on their own (tests/CMakeLists.txt) with just those:
#   PHOENIX_CORE_SOURCES         the model, the commands, the geometry
#   PHOENIX_CORE_FILE_SOURCES    tinyxml2 and zlib, both kept in src/third_party
#   PHOENIX_CORE_JSFL_SOURCES    QuickJS (JSFL scripts)
#   PHOENIX_CORE_PYTHON_SOURCES  nothing at build time (Python is loaded when
#                                a script runs); left out with PHOENIX_PYTHON off
#
# Paths are relative to src/. It is an OBJECT library, so every object goes
# into both programs: nothing is dropped by the linker as it could be from a
# static library.

set(PHOENIX_CORE_SOURCES
    data/action_script.cpp
    data/bitmap.cpp
    data/document.cpp
    data/dom_element.cpp
    data/edge.cpp
    data/element.cpp
    data/fill_style.cpp
    data/fla_document.cpp
    data/folder.cpp
    data/frame.cpp
    data/group.cpp
    data/layer.cpp
    data/linear_gradient.cpp
    data/oval_primitive.cpp
    data/point.cpp
    data/printer_settings.cpp
    data/publish_history.cpp
    data/radial_gradient.cpp
    data/rect.cpp
    data/rectangle_primitive.cpp
    data/resource.cpp
    data/script_list.cpp
    data/shape.cpp
    data/solid_color.cpp
    data/static_text.cpp
    data/stroke_style.cpp
    data/swatch.cpp
    data/symbol.cpp
    data/symbol_instance.cpp
    data/symbol_list.cpp
    data/timeline.cpp
    data/transform.cpp
    edit/asset_warp.cpp
    edit/brush_presets.cpp
    edit/brush_stroke.cpp
    edit/bone_corrections.cpp
    edit/brush_texture.cpp
    edit/camera.cpp
    edit/color_art.cpp
    edit/combine_objects.cpp
    edit/feather.cpp
    edit/slices.cpp
    edit/command_stack.cpp
    edit/controllers.cpp
    edit/curve_fit.cpp
    edit/deformers.cpp
    edit/depth_sort.cpp
    edit/document_session.cpp
    edit/document_settings.cpp
    edit/drawing_style.cpp
    edit/edit_context.cpp
    edit/editable_path.cpp
    edit/path_bend.cpp
    edit/element_clone.cpp
    edit/element_commands.cpp
    edit/filter_presets.cpp
    edit/find_replace.cpp
    edit/frame_actions.cpp
    edit/frame_bake.cpp
    edit/inbetween.cpp
    edit/frame_commands.cpp
    edit/frame_sound.cpp
    edit/function_curves.cpp
    edit/guide_path.cpp
    edit/image_blur.cpp
    edit/layer_commands.cpp
    edit/layer_effects.cpp
    edit/library_commands.cpp
    edit/library_media.cpp
    edit/library_transfer.cpp
    edit/linked_assets.cpp
    edit/lip_sync.cpp
    edit/magic_wand.cpp
    edit/new_document.cpp
    edit/palettes.cpp
    edit/path_tidy.cpp
    edit/pegs.cpp
    edit/phoenix_features.cpp
    edit/region_split.cpp
    edit/region_fill.cpp
    edit/review_notes.cpp
    edit/onion_shift.cpp
    edit/safe_zones.cpp
    edit/reference_board.cpp
    edit/stage_view.cpp
    edit/drawing_aids.cpp
    edit/tool_hints.cpp
    edit/scene_commands.cpp
    edit/script_edit.cpp
    edit/selection.cpp
    edit/shape_merge.cpp
    edit/shape_recognition.cpp
    edit/shape_transform.cpp
    edit/shape_tween.cpp
    edit/shot_breakdown.cpp
    edit/snapping.cpp
    edit/state_graph.cpp
    edit/swatches.cpp
    edit/color_history.cpp
    edit/first_use_hints.cpp
    edit/text_style.cpp
    edit/three_d.cpp
    edit/behaviors.cpp
    edit/shape_operators.cpp
    edit/text_animators.cpp
    edit/adjustments.cpp
    edit/templates.cpp
    edit/motion_blur.cpp
    edit/adjustment_effects.cpp
    edit/audio_analysis.cpp
    edit/other_apps.cpp
    edit/timeline_edit.cpp
    edit/trace_bitmap.cpp
    edit/paper_cleanup.cpp
    edit/transform_blend.cpp
    edit/tween_ease.cpp
    geom/curve.cpp
    geom/fill_attribution.cpp
    geom/fill_tracing.cpp
    geom/intersect.cpp
    geom/nine_slice.cpp
    geom/planar_map.cpp
    geom/shape_geometry.cpp
    gui/audio_engine.cpp
    gui/miniaudio_impl.c
    parser/path_parser.cpp
    util/apng_writer.cpp
    util/gif_writer.cpp
    util/share_formats.cpp
    util/parallel.cpp
    util/sprite_sheet.cpp
    writer/edge_writer.cpp
)

set(PHOENIX_CORE_FILE_SOURCES
    edit/node_graph.cpp
    edit/psd_import.cpp
    edit/motion_object.cpp
    edit/ik_pose.cpp
    data/bitmap_decoder.cpp
    data/swatch_list.cpp
    parser/document_parser.cpp
    parser/ik_tree.cpp
    parser/fla_parser.cpp
    parser/binary_fla.cpp
    parser/swf_import.cpp
    parser/compound_file.cpp
    parser/zip_reader.cpp
    parser/extension_package.cpp
    util/cep_native.cpp
    edit/document_import.cpp
    util/json.cpp
    parser/fla_doctor.cpp
    writer/document_diff.cpp
    util/zip_archive.cpp
    util/exr_writer.cpp
    edit/edit_list.cpp
    edit/font_items.cpp
    edit/vector_import.cpp
    edit/paint_brushes.cpp
    edit/brush_library.cpp
    edit/phoenix_brushes.cpp
    edit/pdf_objects.cpp
    edit/postscript_import.cpp
    edit/postscript_interpreter.cpp
    edit/shortcut_sets.cpp
    edit/canvas_document.cpp
    edit/publish_settings.cpp
    util/swf_writer.cpp
    util/lzma.cpp
    util/swf_sound.cpp
    edit/swf_event_sounds.cpp
    util/zip_writer.cpp
    util/png_writer.cpp
    writer/fla_writer.cpp
    writer/xfl_content.cpp
    writer/xfl_folder_writer.cpp
    writer/xfl_writer.cpp
    phx/container.cpp
    phx/phx_dump.cpp
    phx/phx_read.cpp
    phx/phx_write.cpp
)

set(PHOENIX_CORE_JSFL_SOURCES
    jsfl/e4x.cpp
    jsfl/behavior_builtins.cpp
    jsfl/behavior_engine.cpp
    jsfl/behavior_frames.cpp
    jsfl/engine.cpp
    jsfl/flfile.cpp
    jsfl/model_document.cpp
    jsfl/model_element.cpp
    jsfl/model_flash.cpp
    jsfl/model_library.cpp
    jsfl/model_data.cpp
    jsfl/model_layers.cpp
    jsfl/model_publish.cpp
    jsfl/model_shape.cpp
    jsfl/model_commands.cpp
    jsfl/model_import.cpp
    jsfl/model_profiles.cpp
    jsfl/model_style.cpp
    jsfl/model_view.cpp
    jsfl/model_timeline.cpp
    jsfl/object.cpp
    jsfl/session.cpp
    jsfl/xul.cpp
    jsfl/model_xul.cpp
    jsfl/native_extensions.cpp
)

set(PHOENIX_CORE_PYTHON_SOURCES
    python/python_api.cpp
    python/python_engine.cpp
    python/python_keywords.cpp
)

# Playing movies with their scripts (Control > Test Movie), and publishing
# them with their ActionScript compiled (SWF): libas3 for ActionScript,
# QuickJS for HTML5 Canvas JavaScript.
set(PHOENIX_CORE_PLAY_SOURCES
    play/movie_runtime.cpp
    play/avm1_player.cpp
    play/as2_bitmap.cpp
    play/as2_filters.cpp
    play/as2_library.cpp
    play/as2_shared_object.cpp
    play/as2_xml.cpp
    play/css.cpp
    play/legacy_xml.cpp
    play/canvas/createjs.cpp
    play/canvas/scripts.cpp
    play/display/bitmap_data.cpp
    play/display/bitmap_filter.cpp
    play/display/blend.cpp
    play/display/display_object.cpp
    play/display/events.cpp
    play/display/geom.cpp
    play/display/graphics.cpp
    play/display/stage.cpp
    play/display/text_field.cpp
    play/script/bind_bitmap.cpp
    play/script/bind_display.cpp
    play/script/bind_events.cpp
    play/script/bind_filters.cpp
    play/script/bind_geom.cpp
    play/script/bind_net.cpp
    play/script/bind_system.cpp
    play/script/bind_text.cpp
    play/script/amf.cpp
    play/script/bind_utils.cpp
    play/script/bindings.cpp
    play/script/class_path.cpp
    play/script/png_reader.cpp
    edit/swf_actionscript3.cpp
    edit/swf_movie.cpp
    # Publishing to HTML5, for the JavaScript runtime in resources/html5;
    # it shares the filters' color math with the display list.
    edit/html5_export.cpp
    edit/html5_actionscript2.cpp
)

# phoenix_add_core(<target> <src dir> [FILES] [JSFL] [PYTHON] [PLAY]): the core
# as an object library with the groups asked for, carrying what they link
# against.
function(phoenix_add_core target src)
    cmake_parse_arguments(CORE "FILES;JSFL;PYTHON;PLAY" "" "" ${ARGN})
    set(groups PHOENIX_CORE_SOURCES)
    if(CORE_FILES)
        list(APPEND groups PHOENIX_CORE_FILE_SOURCES)
    endif()
    if(CORE_JSFL)
        list(APPEND groups PHOENIX_CORE_JSFL_SOURCES)
    endif()
    if(CORE_PYTHON)
        list(APPEND groups PHOENIX_CORE_PYTHON_SOURCES)
    endif()
    if(CORE_PLAY)
        list(APPEND groups PHOENIX_CORE_PLAY_SOURCES)
    endif()
    set(sources)
    foreach(group ${groups})
        foreach(file ${${group}})
            list(APPEND sources ${src}/${file})
        endforeach()
    endforeach()

    add_library(${target} OBJECT ${sources})
    # No Qt here: keep Qt's code generators off it.
    set_target_properties(${target} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(${target} PUBLIC cxx_std_20)
    # C++20 with char as UTF-8, as before it: path::u8string() and u8""
    # stay std::string rather than char8_t, which nothing here uses.
    if(MSVC)
        target_compile_options(${target} PUBLIC /Zc:char8_t-)
    else()
        target_compile_options(${target} PUBLIC -fno-char8_t)
    endif()
    # As the app compiled these sources before they moved here, with Qt's
    # settings: UTF-8 source text (strings with ° or × in them), the Unicode
    # Windows API, and Qt's aligned-storage and __cplusplus settings.
    if(MSVC)
        target_compile_options(${target} PUBLIC /utf-8 /Zc:__cplusplus)
        target_compile_definitions(${target} PUBLIC UNICODE _UNICODE _ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()
    if(CORE_FILES)
        target_link_libraries(${target} PUBLIC tinyxml2 zlibstatic zstd_decompress)
        target_include_directories(${target} PUBLIC
            ${src}/third_party/zlib
            ${CMAKE_BINARY_DIR}/src/third_party/zlib)
    endif()
    if(CORE_JSFL)
        # dl for native extension libraries (jsfl/native_extensions.cpp).
        target_link_libraries(${target} PUBLIC qjs ${CMAKE_DL_LIBS})
    endif()
    if(CORE_PYTHON)
        target_link_libraries(${target} PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(CORE_PLAY)
        target_link_libraries(${target} PUBLIC as3_parser qjs)
        target_compile_definitions(${target} PUBLIC PHOENIX_PLAY)
        # ActionScript 2's library classes (resources/as2), built in.
        file(GLOB as2_library CONFIGURE_DEPENDS "${src}/../resources/as2/*.as")
        list(SORT as2_library)
        set(as2_out "${CMAKE_CURRENT_BINARY_DIR}/generated/as2_library.inc")
        add_custom_command(OUTPUT ${as2_out}
            COMMAND ${CMAKE_COMMAND} "-DINPUTS=${as2_library}" "-DOUTPUT=${as2_out}"
                    -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedText.cmake
            DEPENDS ${as2_library} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedText.cmake
            VERBATIM)
        target_sources(${target} PRIVATE ${as2_out})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    endif()
    # The audio engine (miniaudio) needs the system's audio and threads.
    if(APPLE)
        target_link_libraries(${target} PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
    elseif(UNIX)
        target_link_libraries(${target} PUBLIC ${CMAKE_DL_LIBS} pthread m)
    endif()
endfunction()
