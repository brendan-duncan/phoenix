#include "test_util.h"

#include "../src/edit/filter_presets.h"

TEST(filter_presets_keep_every_setting)
{
    fla::Filter shadow;
    shadow.type = fla::Filter::Type::DropShadow;
    shadow.blurX = 7.5;
    shadow.angle = 120.0;
    shadow.color[0] = 200;
    shadow.color[3] = 128;
    shadow.inner = true;
    shadow.hideObject = true;
    fla::Filter glow;
    glow.type = fla::Filter::Type::GradientGlow;
    glow.enabled = false;
    glow.bevelType = "outer";
    glow.entries = {{0.0, {255, 0, 0, 255}}, {1.0, {0, 0, 255, 0}}};
    fla::Filter adjust;
    adjust.type = fla::Filter::Type::AdjustColor;
    adjust.hue = -45.0;
    adjust.saturation = 30.0;

    const std::vector<fla::Filter> back = fla::filterpresets::fromText(fla::filterpresets::toText({shadow, glow, adjust}));
    CHECK(back.size() == 3);
    if (back.size() != 3)
        return;
    CHECK(back[0].type == fla::Filter::Type::DropShadow && back[0].blurX == 7.5 && back[0].angle == 120.0);
    CHECK(back[0].color[0] == 200 && back[0].color[3] == 128 && back[0].inner && back[0].hideObject);
    CHECK(!back[1].enabled && back[1].bevelType == "outer" && back[1].entries.size() == 2);
    CHECK(back[1].entries[1].ratio == 1.0 && back[1].entries[1].color[2] == 255 && back[1].entries[1].color[3] == 0);
    CHECK(back[2].hue == -45.0 && back[2].saturation == 30.0);
    CHECK(fla::filterpresets::fromText("type=nonsense\ngarbage").empty());
}
