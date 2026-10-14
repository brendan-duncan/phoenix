#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;

/// Safe zones and aspect-ratio guides: where the edges of the screens a shot
/// is made for fall on the stage. Title safe and action safe are rectangles
/// inset from the stage (or from one of the aspect-ratio frames), and each
/// aspect-ratio frame is the largest rectangle of its shape that fits on the
/// stage, centered or against one edge.
///
/// The settings belong to the document, so a show's template carries them:
/// they are kept in one Phoenix attribute on the document (FLA keeps it and
/// Animate ignores it). Whether the overlay is shown at all is the viewer's
/// choice, not the document's. Nothing here is ever drawn into the art or
/// exported unless an export asks for it. Free of Qt.
namespace safezones {

/// Where an aspect-ratio frame sits on the stage when it is narrower or
/// shorter than the stage.
enum class Anchor
{
    Center,
    Left,
    Right,
    Top,
    Bottom
};

/// One aspect-ratio frame: "16:9" is 16 wide for 9 high.
struct AspectFrame
{
    double width = 16.0;
    double height = 9.0;
    bool on = false;
    /// RGBA.
    uint8_t color[4] = {255, 160, 0, 255};
    Anchor anchor = Anchor::Center;
    /// Darken the stage outside the frame, to preview the crop.
    bool shade = false;

    /// "16:9", "1.85:1".
    std::string name() const;
    bool operator==(const AspectFrame& other) const;
};

struct Settings
{
    /// Title safe and action safe, as percentages of the frame they are in.
    bool titleSafe = true;
    double titlePercent = 90.0;
    bool actionSafe = true;
    double actionPercent = 93.0;
    /// A small cross at the center, and lines dividing the frame in thirds.
    bool centerCross = true;
    bool thirds = false;
    /// The aspect-ratio frame the safe zones, cross and thirds are measured
    /// in, as an index into frames; -1 for the stage itself.
    int follow = -1;
    std::vector<AspectFrame> frames;

    bool operator==(const Settings& other) const;
    bool operator!=(const Settings& other) const { return !(*this == other); }
};

/// The settings a document has before anyone changes them: 90% title safe,
/// 93% action safe, a center cross, and the usual frames (16:9, 4:3, 1.85:1,
/// 2.39:1, 1:1, 4:5, 9:16), all off.
Settings defaults();

/// A frame of \a width : \a height, off, in a color of its own.
AspectFrame frameOf(double width, double height);

/// Reads "16:9", "1.85:1" or "2.39" (meaning 2.39:1); false for anything
/// else.
bool parseRatio(const std::string& text, double& width, double& height);

/// The text one Settings is kept as (in the document and in presets).
std::string toText(const Settings& settings);
/// Reads that text; what it does not say keeps its default.
Settings fromText(const std::string& text);

/// The attribute on the document that holds them.
extern const char* const kAttribute;

/// The document's settings, or the defaults when it has none.
Settings of(const Document& document);
/// The document's attributes with \a settings in them; the attribute is left
/// out when the settings are the defaults, so a document that never used
/// them stays as it was.
std::vector<std::pair<std::string, std::string>> withSettings(const Document& document, const Settings& settings);

struct Rect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

/// The largest rectangle of \a frame's shape inside a stage of \a stageWidth
/// by \a stageHeight, placed by its anchor.
Rect frameRect(double stageWidth, double stageHeight, const AspectFrame& frame);
/// \a area inset to \a percent of its width and height, about its center.
Rect inset(const Rect& area, double percent);
/// What the safe zones are measured in: the stage, or the frame they follow
/// when that frame is on.
Rect zoneArea(double stageWidth, double stageHeight, const Settings& settings);

/// The edges the tools snap to while the overlay is shown: the safe zones'
/// and every frame's that is on, as x positions and y positions on the
/// stage.
void snapLines(double stageWidth, double stageHeight, const Settings& settings, std::vector<double>& xs,
    std::vector<double>& ys);

/// Built-in presets, by name: "HD Broadcast", "Cinema", "Social" and
/// "Clear" (no frames).
std::vector<std::pair<std::string, Settings>> presets();

} // namespace safezones

} // namespace fla
