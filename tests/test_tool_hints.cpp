#include "test_util.h"

#include "../src/edit/tool_hints.h"

#include <string>

namespace hints = fla::hints;

namespace {

hints::Modifiers keys(bool shift, bool alt, bool ctrl)
{
    hints::Modifiers held;
    held.shift = shift;
    held.alt = alt;
    held.ctrl = ctrl;
    return held;
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(tool_hints_name_the_modifiers_with_nothing_held)
{
    const std::string brush = hints::hintFor("Brush", keys(false, false, false));
    CHECK(contains(brush, "Drag to paint"));
    CHECK(contains(brush, "Shift: straight across or down"));
    // Ctrl calls up the Selection tool for a tool that has no use for it.
    CHECK(contains(brush, "Ctrl: Selection tool while held"));
    // The brush does nothing with Alt, so the hint does not say it does.
    CHECK(!contains(brush, "Alt"));
}

TEST(tool_hints_show_only_what_the_held_keys_do)
{
    CHECK(hints::hintFor("Pencil", keys(true, false, false)) == "Shift: straight across or down");
    CHECK(hints::hintFor("Zoom", keys(false, true, false)) == "Alt: click to zoom out");
    CHECK(hints::hintFor("Free Transform", keys(true, false, true)) == "Ctrl+Shift: drag a corner to taper a shape");
    CHECK(hints::hintFor("Bind", keys(true, true, false)) == "Alt+Shift: click a point to give the bone less say");
    // Ctrl is the free transform tool's own, not the Selection tool's.
    CHECK(hints::hintFor("Free Transform", keys(false, false, true)) == "Ctrl: drag a corner to distort a shape");
    CHECK(hints::hintFor("Oval", keys(false, false, true)) == "Ctrl: Selection tool while held");
    // Keys that do nothing with the tool leave the whole hint.
    CHECK(hints::hintFor("Eraser", keys(true, false, false)) == hints::hintFor("Eraser", keys(false, false, false)));
}

TEST(tool_hints_follow_the_faucet)
{
    hints::Context faucet;
    faucet.faucet = true;
    CHECK(contains(hints::hintFor("Eraser", keys(false, false, false)), "Drag to erase"));
    CHECK(contains(hints::hintFor("Eraser", keys(false, false, false), faucet), "remove it whole"));
}

TEST(every_toolbox_tool_has_a_hint)
{
    for (const char* tool : {"Selection", "Subselection", "Free Transform", "Gradient Transform", "Lasso", "Polygon",
             "Magic Wand", "Bone", "Bind", "Asset Warp", "3D Rotation", "3D Translation", "Pen", "Add Anchor Point",
             "Delete Anchor Point", "Convert Anchor Point", "Text", "Line", "Rectangle", "Oval", "Rectangle Primitive",
             "Oval Primitive", "PolyStar", "Width", "Pencil", "Brush", "Nib Brush", "Ink Brush", "Paint Brush",
             "Paint Bucket", "Ink Bottle", "Eyedropper", "Eraser", "Hand", "Rotation", "Time Scrub", "Camera", "Zoom",
             "Drawing Guides"})
    {
        if (!hints::knows(tool))
            std::printf("    no hint: %s\n", tool);
        CHECK(hints::knows(tool));
        CHECK(!hints::hintFor(tool, keys(false, false, false)).empty());
    }
    CHECK(hints::hintFor("A Script Tool", keys(false, false, false)).empty());
}
