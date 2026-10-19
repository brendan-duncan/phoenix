#pragma once

#include <string>

namespace fla {

/// The short hints the status bar shows for the active tool: what a click
/// or a drag does and what each modifier key adds, and, while modifier keys
/// are held, just what they do. Every hint is written from the tool's own
/// key handling, so a modifier is only named where it does something. Free
/// of Qt.
namespace hints {

/// The modifier keys held.
struct Modifiers
{
    bool shift = false;
    bool alt = false;
    bool ctrl = false;

    bool any() const { return shift || alt || ctrl; }
};

/// What else the hint depends on.
struct Context
{
    /// The eraser's Faucet is on: a click takes a whole fill or stroke.
    bool faucet = false;
};

/// The hint for the tool named \a tool (as the toolbox names it: "Brush",
/// "Free Transform") with \a held down. With nothing held it is the whole
/// hint, "Drag to draw · Shift: straight across or down"; with keys held,
/// what those keys do, "Shift: straight across or down", or the whole hint
/// again when they do nothing with this tool. Empty for a tool it does not
/// know, such as a script's.
std::string hintFor(const std::string& tool, const Modifiers& held, const Context& context = Context());

/// Whether a hint is written for \a tool.
bool knows(const std::string& tool);

} // namespace hints
} // namespace fla
