#include "tool_hints.h"

#include <map>
#include <vector>

namespace fla::hints {

namespace {

enum : int
{
    Shift = 1,
    Alt = 2,
    Ctrl = 4
};

/// What one combination of modifier keys does with a tool.
struct Entry
{
    int keys;
    const char* text;
};

struct Hint
{
    /// What a click or a drag does with no keys held.
    const char* base;
    std::vector<Entry> entries;
    /// Ctrl is the tool's own; otherwise holding it calls up the Selection
    /// tool, as it does for every tool.
    bool ownsCtrl = false;
};

const char* const kDot = " \xC2\xB7 ";
const char* const kCtrlArrow = "Ctrl: Selection tool while held";

const std::map<std::string, Hint>& table()
{
    static const std::map<std::string, Hint> hints = {
        {"Selection",
            {"Click to select, drag to move or to draw a marquee, double-click to go inside",
                {{Shift, "Shift: add to or take from the selection, keep a move straight"},
                    {Alt, "Alt: drag a copy"}, {Ctrl, "Ctrl: drag an outline to add a corner"}},
                true}},
        {"Subselection",
            {"Drag points and handles, or a marquee around points",
                {{Shift, "Shift: add or remove points"},
                    {Alt, "Alt: click a point to make it a corner, drag to pull out handles or move one on its own"}}}},
        {"Free Transform",
            {"Drag inside to move, a handle to scale, outside a corner to rotate, an edge to skew",
                {{Shift, "Shift: keep proportions, rotate in 45\xC2\xB0 steps"}, {Alt, "Alt: scale about the pivot"},
                    {Ctrl, "Ctrl: drag a corner to distort a shape"},
                    {Ctrl | Shift, "Ctrl+Shift: drag a corner to taper a shape"}},
                true}},
        {"Gradient Transform", {"Click a gradient or bitmap fill, then drag its handles", {}}},
        {"Lasso", {"Drag a loop around what to select", {{Shift, "Shift: add to the selection"}}}},
        {"Polygon",
            {"Click to place corners; click the first, double-click or press Enter to close",
                {{Shift, "Shift: add to the selection"}}}},
        {"Magic Wand", {"Click a bitmap fill to select a region of its color", {{Shift, "Shift: add to the selection"}}}},
        {"Bone", {"Drag from a symbol, a shape or a joint to add a bone; click a bone for its limits", {}, true}},
        {"Bind",
            {"Click a bone, then click points to bind them to it",
                {{Shift, "Shift: click a point to bind or unbind it"}, {Alt, "Alt: click a point to give the bone more say"},
                    {Alt | Shift, "Alt+Shift: click a point to give the bone less say"},
                    {Ctrl, "Ctrl: click a point to unbind it"}},
                true}},
        {"Asset Warp",
            {"Click art to add a pin, drag pins to bend it",
                {{Shift, "Shift: drag a pin to turn it"}, {Alt, "Alt: click a pin to remove it"},
                    {Ctrl, "Ctrl: click a pin to join it to the selected one with a bone, or part them"}},
                true}},
        {"3D Rotation", {"Drag a movie clip to turn it in 3D", {{Alt, "Alt: click a clip to set its 3D turn back"}}}},
        {"3D Translation", {"Drag a movie clip up or down to move it nearer or further", {}}},
        {"Pen",
            {"Click to add corners, drag to pull out curves; double-click or press Enter to finish",
                {{Shift, "Shift: keep to 45\xC2\xB0 steps"}, {Alt, "Alt: move one handle on its own"},
                    {Ctrl, "Ctrl: click to finish the path open (before one starts: Selection tool)"}},
                true}},
        {"Add Anchor Point", {"Click a path to add a point", {}}},
        {"Delete Anchor Point", {"Click a point to remove it", {}}},
        {"Convert Anchor Point", {"Click a smooth point to make it a corner, drag one to pull out handles", {}}},
        {"Text",
            {"Click to type, drag to make a box of a set width",
                {{Shift, "Shift: click or use the arrow keys to extend the selected text"}}}},
        {"Line", {"Drag to draw a line", {{Shift, "Shift: keep to 45\xC2\xB0 steps"}}}},
        {"Rectangle",
            {"Drag to draw a rectangle; Up and Down while dragging round its corners",
                {{Shift, "Shift: a square"}, {Alt, "Alt: draw from the center, or click to type a size"}}}},
        {"Oval",
            {"Drag to draw an oval",
                {{Shift, "Shift: a circle"}, {Alt, "Alt: draw from the center, or click to type a size"}}}},
        {"Rectangle Primitive",
            {"Drag to draw a rectangle you can change later; Up and Down while dragging round its corners",
                {{Shift, "Shift: a square"}, {Alt, "Alt: draw from the center, or click to type a size"}}}},
        {"Oval Primitive",
            {"Drag to draw an oval you can change later",
                {{Shift, "Shift: a circle"}, {Alt, "Alt: draw from the center, or click to type a size"}}}},
        {"PolyStar", {"Drag to draw a polygon or a star", {{Shift, "Shift: turn it in 45\xC2\xB0 steps"}}}},
        {"Width",
            {"Drag on a line to add or move a width point; Delete removes it", {{Alt, "Alt: change one side only"}}}},
        {"Pencil", {"Drag to draw a line", {{Shift, "Shift: straight across or down"}}}},
        {"Paint Brush", {"Drag to paint a line with the brush", {{Shift, "Shift: straight across or down"}}}},
        {"Brush", {"Drag to paint", {{Shift, "Shift: straight across or down"}}}},
        {"Nib Brush", {"Drag to paint", {{Shift, "Shift: straight across or down"}}}},
        {"Ink Brush", {"Drag to ink a steady line", {{Shift, "Shift: straight across or down"}}}},
        {"Eraser", {"Drag to erase", {}}},
        {"Paint Bucket", {"Click to fill, drag to set a gradient's direction", {}}},
        {"Ink Bottle", {"Click a line to restyle it, or a fill to outline it", {}}},
        {"Eyedropper", {"Click a fill or a line to take its color and style", {}}},
        {"Hand", {"Drag to pan the stage", {}}},
        {"Rotation",
            {"Click to set the pivot, drag to turn the view, double-click to set it straight",
                {{Shift, "Shift: 15\xC2\xB0 steps"}}}},
        {"Time Scrub", {"Drag left or right to move through the frames", {}}},
        {"Camera", {"Drag to pan the camera", {{Shift, "Shift: drag to zoom"}, {Alt, "Alt: drag to turn"}}}},
        {"Zoom", {"Click to zoom in, drag to zoom to an area", {{Alt, "Alt: click to zoom out"}}}},
        {"Drawing Guides",
            {"Drag a guide or its handles, click empty stage to add the guide chosen; Delete removes it",
                {{Shift, "Shift: keep a handle to 15\xC2\xB0 steps"}}}},
    };
    return hints;
}

int keysOf(const Modifiers& held)
{
    return (held.shift ? Shift : 0) | (held.alt ? Alt : 0) | (held.ctrl ? Ctrl : 0);
}

} // namespace

bool knows(const std::string& tool)
{
    return table().count(tool) != 0;
}

std::string hintFor(const std::string& tool, const Modifiers& held, const Context& context)
{
    const auto found = table().find(tool);
    if (found == table().end())
        return std::string();
    Hint hint = found->second;
    if (tool == "Eraser" && context.faucet)
        hint.base = "Click a fill or a line to remove it whole";
    std::vector<Entry> entries = hint.entries;
    if (!hint.ownsCtrl)
        entries.push_back({Ctrl, kCtrlArrow});

    const int keys = keysOf(held);
    if (keys != 0)
    {
        for (const Entry& entry : entries)
        {
            if (entry.keys == keys)
                return entry.text;
        }
        // Ctrl with other keys still calls up the Selection tool, whose own
        // keys then apply.
        if (!hint.ownsCtrl && (keys & Ctrl))
            return kCtrlArrow;
    }

    std::string text = hint.base;
    for (const Entry& entry : entries)
    {
        text += kDot;
        text += entry.text;
    }
    return text;
}

} // namespace fla::hints
