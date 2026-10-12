#include "test_util.h"

#include "../src/edit/swatches.h"

using fla::swatches::Color;

TEST(swatches_replace_sort_and_color_tables)
{
    fla::swatches::State state;
    state = fla::swatches::withAdded(state, {255, 0, 0, 255});
    // A gradient swatch the list keeps whatever happens to the solids.
    const size_t close = state.xml.find("</swatches>");
    state.xml.insert(close, "<LinearGradientSwatchItem/>");
    const fla::swatches::State replaced = fla::swatches::withColors(state, {{0, 0, 255, 255}, {0, 255, 0, 128}});
    CHECK(replaced.colors.size() == 2 && replaced.colors[1][3] == 128);
    CHECK(replaced.xml.find("<LinearGradientSwatchItem/>") != std::string::npos);
    CHECK(fla::swatches::withColors(state, {}).colors.empty() && fla::swatches::withColors(state, {}).exists);

    // Grays first, then by hue: red, green, blue.
    const std::vector<Color> sorted = fla::swatches::sortedByColor(
        {{0, 0, 255, 255}, {128, 128, 128, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}});
    CHECK(sorted[0][0] == 128 && sorted[1][0] == 255 && sorted[2][1] == 255 && sorted[3][2] == 255);

    // Color tables, both ways.
    const std::vector<Color> colors = {{10, 20, 30, 255}, {200, 100, 50, 255}};
    const std::vector<uint8_t> act = fla::swatches::writeAct(colors);
    CHECK(act.size() == 772 && act[769] == 2);
    const std::vector<Color> fromAct = fla::swatches::readAct(act);
    CHECK(fromAct.size() == 2 && fromAct[1][0] == 200 && fromAct[1][2] == 50);
    CHECK(fla::swatches::readAct(std::vector<uint8_t>(10, 0)).empty());

    const std::string gpl = fla::swatches::writeGpl(colors, "Test");
    CHECK(gpl.rfind("GIMP Palette", 0) == 0);
    const std::vector<Color> fromGpl = fla::swatches::readGpl(gpl);
    CHECK(fromGpl.size() == 2 && fromGpl[0][1] == 20 && fromGpl[1][1] == 100);
}
