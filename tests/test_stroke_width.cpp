#include "test_util.h"

#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/drawing_style.h"

#include <memory>

TEST(width_profiles_travel_with_the_stroke_look)
{
    fla::SolidStroke stroke(nullptr);
    stroke.weight = 4.0;
    stroke.fill = new fla::SolidColor(&stroke);
    CHECK(fla::StrokeLook::of(stroke).widthProfile.empty());

    // A preset applied to a stroke, and read back as the same look.
    const std::vector<fla::WidthPreset>& presets = fla::widthPresets();
    CHECK(presets.size() >= 4 && presets[0].markers.empty());
    fla::StrokeLook look = fla::StrokeLook::of(stroke);
    look.widthProfile = presets[1].markers;
    std::unique_ptr<fla::StrokeStyle> tapered(look.applyTo(stroke, nullptr));
    CHECK(tapered->widthProfile == presets[1].markers && tapered->weight == 4.0);
    CHECK(fla::StrokeLook::of(*tapered) == look && fla::StrokeLook::of(*tapered) != fla::StrokeLook::of(stroke));

    // New strokes take the drawing style's profile.
    fla::DrawingStyle style;
    style.strokeLook.widthProfile = presets[2].markers;
    std::unique_ptr<fla::StrokeStyle> drawn(style.createStroke(nullptr));
    CHECK(drawn && drawn->widthProfile == presets[2].markers);
    CHECK(style.strokeMatches(*drawn));
}
