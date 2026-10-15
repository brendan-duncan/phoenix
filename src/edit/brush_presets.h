#pragma once

#include "brush_stroke.h"

#include <string>
#include <vector>

namespace fla {

class DrawingStyle;

/// Brush presets: a named brush a tool's list holds,
/// with what it paints with besides its tip (the texture, the bitmap
/// brush's flow and tip, Auto Fill). Each brush has its own list: the Flow,
/// Nib and Ink brushes, and the bitmap brush. Free of Qt.
namespace brushpresets {

/// The lists, by the key each is kept under.
constexpr const char* kFlow = "fluid";
constexpr const char* kNib = "classic";
constexpr const char* kInk = "ink";
constexpr const char* kBitmap = "bitmap";

struct Preset
{
    std::string name;
    /// The brush's settings (toText) and the style's painting options, one
    /// "key=value" a line; the style's keys start "style.".
    std::string text;
};

/// The style's painting options a preset carries, as "style." lines.
std::string styleText(const DrawingStyle& style);

/// A preset of the brush and the style as they are.
std::string presetText(const BrushSettings& brush, const DrawingStyle& style);

/// Sets \a brush and the style's painting options from a preset's text;
/// what it does not mention is left as it was.
void apply(const std::string& text, BrushSettings& brush, DrawingStyle& style);

/// A list as one text, to keep or to share: each preset a "[name]" line
/// and its lines.
std::string toSetText(const std::vector<Preset>& presets);
std::vector<Preset> fromSetText(const std::string& text);

/// The presets a list starts with.
std::vector<Preset> builtIns(const std::string& list);

} // namespace brushpresets

} // namespace fla
