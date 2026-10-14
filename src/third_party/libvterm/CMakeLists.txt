# libvterm 0.3.3 (MIT, Paul Evans), from https://launchpad.net/libvterm: the
# terminal emulator behind the Claude Code panel (src/gui/terminal_view.cpp).
# Kept as released, with the encoding tables the release generated.
add_library(vterm STATIC
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)
target_include_directories(vterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(vterm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # Its own code, as released: quiet about what MSVC thinks unsafe.
    target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(vterm PRIVATE /W1)
endif()
