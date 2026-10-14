#include "test_util.h"

#include "../src/data/document.h"
#include "../src/edit/safe_zones.h"

#include <cmath>

namespace sz = fla::safezones;

namespace {

bool near(double a, double b)
{
    return std::fabs(a - b) < 1.0e-6;
}

} // namespace

TEST(safe_zone_frames_fit_the_stage_by_their_anchor)
{
    // 4:3 on a 1920x1080 stage: full height, 1440 wide, centered.
    sz::AspectFrame fourThree = sz::frameOf(4, 3);
    sz::Rect r = sz::frameRect(1920, 1080, fourThree);
    CHECK(near(r.width, 1440) && near(r.height, 1080) && near(r.x, 240) && near(r.y, 0));
    fourThree.anchor = sz::Anchor::Left;
    CHECK(near(sz::frameRect(1920, 1080, fourThree).x, 0));
    fourThree.anchor = sz::Anchor::Right;
    CHECK(near(sz::frameRect(1920, 1080, fourThree).x, 480));

    // 2.39:1 on the same stage: full width, letterboxed.
    sz::AspectFrame scope = sz::frameOf(2.39, 1);
    r = sz::frameRect(1920, 1080, scope);
    CHECK(near(r.width, 1920) && near(r.height, 1920 / 2.39) && near(r.y, (1080 - 1920 / 2.39) / 2));
    scope.anchor = sz::Anchor::Bottom;
    CHECK(near(sz::frameRect(1920, 1080, scope).y + sz::frameRect(1920, 1080, scope).height, 1080));

    // 9:16 on a 16:9 stage, and the same shape as the stage.
    r = sz::frameRect(1920, 1080, sz::frameOf(9, 16));
    CHECK(near(r.height, 1080) && near(r.width, 1080 * 9.0 / 16.0));
    r = sz::frameRect(1920, 1080, sz::frameOf(16, 9));
    CHECK(near(r.width, 1920) && near(r.height, 1080) && near(r.x, 0) && near(r.y, 0));

    // A degenerate stage or frame gives an empty rectangle, never NaN.
    CHECK(sz::frameRect(0, 1080, sz::frameOf(16, 9)).width == 0.0);
    CHECK(sz::frameRect(1920, 1080, sz::frameOf(0, 9)).width == 0.0);
}

TEST(safe_zones_inset_about_the_center_of_what_they_follow)
{
    const sz::Rect title = sz::inset(sz::Rect{0, 0, 1920, 1080}, 90);
    CHECK(near(title.width, 1728) && near(title.height, 972) && near(title.x, 96) && near(title.y, 54));

    sz::Settings settings = sz::defaults();
    // Following a frame that is off measures in the stage.
    settings.follow = 1;
    sz::Rect area = sz::zoneArea(1920, 1080, settings);
    CHECK(near(area.width, 1920));
    settings.frames[1].on = true;   // 4:3
    area = sz::zoneArea(1920, 1080, settings);
    CHECK(near(area.width, 1440) && near(area.x, 240));

    std::vector<double> xs;
    std::vector<double> ys;
    sz::snapLines(1920, 1080, settings, xs, ys);
    // Title and action edges, the center, and the 4:3 frame's sides.
    CHECK(std::find(xs.begin(), xs.end(), 240.0) != xs.end());
    CHECK(std::find(xs.begin(), xs.end(), 960.0) != xs.end());
    CHECK(std::find(ys.begin(), ys.end(), 540.0) != ys.end());
}

TEST(safe_zone_ratios_parse_and_settings_round_trip)
{
    double w = 0;
    double h = 0;
    CHECK(sz::parseRatio("16:9", w, h) && w == 16 && h == 9);
    CHECK(sz::parseRatio(" 2.39 : 1 ", w, h) && near(w, 2.39) && h == 1);
    CHECK(sz::parseRatio("1.85", w, h) && near(w, 1.85) && h == 1);
    CHECK(sz::parseRatio("4x5", w, h) && w == 4 && h == 5);
    CHECK(!sz::parseRatio("", w, h));
    CHECK(!sz::parseRatio("wide", w, h));
    CHECK(!sz::parseRatio("16:0", w, h));
    CHECK(!sz::parseRatio("-4:3", w, h));
    CHECK(!sz::parseRatio("1000:1", w, h));

    sz::Settings settings = sz::defaults();
    settings.titlePercent = 80;
    settings.actionSafe = false;
    settings.thirds = true;
    settings.follow = 2;
    settings.frames[2].on = true;
    settings.frames[2].shade = true;
    settings.frames[2].anchor = sz::Anchor::Top;
    settings.frames.push_back(sz::frameOf(3, 2));
    settings.frames.back().on = true;
    CHECK(sz::fromText(sz::toText(settings)) == settings);

    // Unreadable text falls back to the defaults, and out-of-range values
    // are kept in range.
    CHECK(sz::fromText("") == sz::defaults());
    CHECK(sz::fromText("nonsense;;=;frames=") .frames.empty());
    CHECK(sz::fromText("title=1,500").titlePercent == 100.0);
    CHECK(sz::fromText("follow=99").follow == -1);
}

TEST(safe_zones_live_in_one_document_attribute_only_when_used)
{
    fla::Document document(nullptr);
    document.extraAttributes.emplace_back("phoenixOther", "1");
    CHECK(sz::of(document) == sz::defaults());

    // The defaults leave the document as it was.
    CHECK(sz::withSettings(document, sz::defaults()) == document.extraAttributes);

    sz::Settings settings = sz::defaults();
    settings.frames[0].on = true;
    document.extraAttributes = sz::withSettings(document, settings);
    CHECK(document.extraAttributes.size() == 2);
    CHECK(sz::of(document) == settings);

    // Back to the defaults removes the attribute again.
    document.extraAttributes = sz::withSettings(document, sz::defaults());
    CHECK(document.extraAttributes.size() == 1 && document.extraAttributes[0].first == "phoenixOther");

    // Every preset names itself and reads back.
    for (const auto& [name, preset] : sz::presets())
    {
        CHECK(!name.empty());
        CHECK(sz::fromText(sz::toText(preset)) == preset);
    }
}
