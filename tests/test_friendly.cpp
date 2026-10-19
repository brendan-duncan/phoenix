#include "test_util.h"

#include "../src/edit/color_history.h"
#include "../src/edit/first_use_hints.h"
#include "../src/gui/simple_workspace.h"

#include <algorithm>
#include <cmath>
#include <set>

using fla::ColorHistory;

namespace {

bool near(double a, double b)
{
    return std::fabs(a - b) < 1e-6;
}

} // namespace

TEST(color_history_newest_first_no_repeats_and_limit)
{
    ColorHistory history(3);
    CHECK(history.add({255, 0, 0, 255}));
    CHECK(history.add({0, 255, 0, 255}));
    // The same color again first changes nothing.
    CHECK(!history.add({0, 255, 0, 255}));
    CHECK(history.add({0, 0, 255, 255}));
    CHECK(history.colors().size() == 3 && history.colors()[0][2] == 255 && history.colors()[2][0] == 255);
    // Used again, red moves to the front rather than showing twice.
    CHECK(history.add({255, 0, 0, 255}));
    CHECK(history.colors().size() == 3 && history.colors()[0][0] == 255 && history.colors()[1][2] == 255);
    // Past the limit, the oldest drops off.
    history.add({10, 20, 30, 255});
    CHECK(history.colors().size() == 3 && history.colors()[0][0] == 10);
    CHECK(std::find(history.colors().begin(), history.colors().end(), ColorHistory::Color{0, 255, 0, 255}) ==
          history.colors().end());
    // Alpha makes a color its own.
    history.add({10, 20, 30, 128});
    CHECK(history.colors()[0][3] == 128 && history.colors()[1][3] == 255);
    history.setLimit(1);
    CHECK(history.colors().size() == 1 && history.colors()[0][3] == 128);
}

TEST(color_history_text_round_trip)
{
    ColorHistory history(16);
    history.add({1, 2, 3, 255});
    history.add({255, 128, 0, 64});
    const std::string text = history.toText();
    CHECK(text == "#FF800040 #010203FF");
    const ColorHistory back = ColorHistory::fromText(text);
    CHECK(back.colors() == history.colors());
    // Opaque without alpha; junk and repeats passed over; the limit kept.
    const ColorHistory read = ColorHistory::fromText("#00ff00 nonsense #12345 #00FF00FF #0000ff", 2);
    CHECK(read.colors().size() == 2);
    CHECK((read.colors()[0] == ColorHistory::Color{0, 255, 0, 255}));
    CHECK((read.colors()[1] == ColorHistory::Color{0, 0, 255, 255}));
    CHECK(ColorHistory::fromText("").colors().empty());
}

TEST(hsv_conversions)
{
    const fla::Hsv red = fla::hsvOf(255, 0, 0);
    CHECK(near(red.h, 0.0) && near(red.s, 1.0) && near(red.v, 1.0));
    const fla::Hsv green = fla::hsvOf(0, 255, 0);
    CHECK(near(green.h, 120.0));
    const fla::Hsv blue = fla::hsvOf(0, 0, 255);
    CHECK(near(blue.h, 240.0));
    const fla::Hsv magenta = fla::hsvOf(255, 0, 255);
    CHECK(near(magenta.h, 300.0));
    const fla::Hsv gray = fla::hsvOf(128, 128, 128);
    CHECK(near(gray.s, 0.0) && near(gray.h, 0.0) && near(gray.v, 128.0 / 255.0));
    // Every color makes it there and back.
    bool all = true;
    for (int r = 0; r < 256; r += 17)
        for (int g = 0; g < 256; g += 15)
            for (int b = 0; b < 256; b += 51)
            {
                const fla::Hsv hsv = fla::hsvOf(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
                const auto rgb = fla::rgbOf(hsv, 200);
                all = all && rgb[0] == r && rgb[1] == g && rgb[2] == b && rgb[3] == 200;
            }
    CHECK(all);
    // The hue wraps, the rest are kept in range.
    const auto wrapped = fla::rgbOf({480.0, 2.0, 1.0});
    CHECK(wrapped[0] == 0 && wrapped[1] == 255 && wrapped[2] == 0);
    const auto under = fla::rgbOf({-60.0, 1.0, 1.0});
    CHECK(under[0] == 255 && under[1] == 0 && under[2] == 255);
}

TEST(first_use_hints_show_once)
{
    using Event = fla::FirstUseHints::Event;
    fla::FirstUseHints hints;
    // Drawing on a later frame earns nothing; on frame 1, the F6 hint.
    fla::FirstUseHints::Context later;
    later.frame = 5;
    CHECK(!hints.on(Event::Drew, later));
    const auto drew = hints.on(Event::Drew);
    CHECK(drew && drew->id == "first-drawing" && drew->text.find("F6") != std::string::npos);
    CHECK(!hints.on(Event::Drew));
    const auto key = hints.on(Event::Keyframe);
    CHECK(key && key->text.find("Onion Skin") != std::string::npos && key->text.find("Enter") != std::string::npos);
    const auto symbol = hints.on(Event::Symbol);
    CHECK(symbol && symbol->id == "first-symbol" && symbol->text.find("Double-click") != std::string::npos);
    fla::FirstUseHints::Context motion;
    motion.tween = "motion";
    const auto tween = hints.on(Event::Tween, motion);
    CHECK(tween && tween->text.find("playhead") != std::string::npos);
    CHECK(!hints.on(Event::Tween));

    // Kept as text and read back; unknown names passed over.
    fla::FirstUseHints again;
    again.setShownText(hints.shownText() + " made-up");
    CHECK(again.wasShown("first-drawing") && again.wasShown("first-tween") && !again.wasShown("made-up"));
    CHECK(!again.on(Event::Symbol));
    again.reset();
    CHECK(again.on(Event::Symbol).has_value());

    // Off, none show and none count as shown.
    fla::FirstUseHints off;
    off.setEnabled(false);
    CHECK(!off.on(Event::Drew) && !off.wasShown("first-drawing"));
    // A classic or shape tween is told about the next keyframe.
    fla::FirstUseHints classic;
    fla::FirstUseHints::Context context;
    context.tween = "classic";
    const auto told = classic.on(Event::Tween, context);
    CHECK(told && told->text.find("next keyframe") != std::string::npos);
}

TEST(simple_workspace_definition)
{
    // The panels it keeps: the tools, timeline, Properties and Color.
    const std::vector<std::string>& panels = simpleworkspace::panels();
    for (const char* wanted : {"toolsDock", "timelineDock", "propertiesDock", "colorDock"})
        CHECK(std::find(panels.begin(), panels.end(), wanted) != panels.end());
    for (const char* advanced : {"nodeGraphDock", "actionsDock", "motionEditorDock", "xsheetDock", "outlinerDock"})
        CHECK(std::find(panels.begin(), panels.end(), advanced) == panels.end());
    // The tools: the common ones, each once, no empty slot, lines between.
    std::set<std::string> seen;
    bool unique = true;
    for (const auto& slot : simpleworkspace::toolSlots())
    {
        CHECK(!slot.empty());
        for (const std::string& tool : slot)
            if (tool != "|")
                unique = seen.insert(tool).second && unique;
    }
    CHECK(unique);
    for (const char* wanted : {"Selection", "Brush", "Pencil", "Eraser", "Paint Bucket", "Rectangle", "Oval", "Text"})
        CHECK(seen.count(wanted) == 1);
    CHECK(!seen.count("Bone") && !seen.count("Asset Warp") && !seen.count("3D Rotation"));
    CHECK(std::string(simpleworkspace::name()) == "Simple" && std::string(simpleworkspace::fullName()) == "Essentials");
}
