#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/edge.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/radial_gradient.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_clone.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/swatches.h"
#include "../src/parser/path_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

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

namespace {

/// A two by four square filled with solid red and stroked in black.
std::unique_ptr<fla::Shape> redSquare()
{
    std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|2000 0|2000 4000|0 4000|0 0", shape.get());
    edge->fillStyle0 = -1;
    edge->fillStyle1 = 1;
    edge->strokeStyle = 1;
    shape->edges.push_back(edge);
    auto* fill = new fla::SolidColor(shape.get());
    fill->color[0] = 255;
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    auto* stroke = new fla::SolidStroke(shape.get());
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;
    return shape;
}

} // namespace

TEST(swatches_gradients_from_the_file_and_the_defaults)
{
    // The reference app's default row: white to black linear first, then
    // radial, with two color ramps among them.
    const std::vector<fla::swatches::Gradient> defaults = fla::swatches::defaultGradients();
    CHECK(defaults.size() == 8);
    CHECK(!defaults[0].radial && defaults[1].radial && defaults[0].stops.size() == 2);
    CHECK(defaults[0].stops[0].color[0] == 255 && defaults[0].stops[1].color[0] == 0);
    CHECK(defaults[6].stops.size() == 7);

    // As the file keeps them: a black stop needs no color, alpha is 0 to 1,
    // and an item with no stops is passed over.
    const std::string xml =
        "<extendedSwatchLists><swatchList><swatches>"
        "<LinearGradientSwatchItem><GradientEntry color=\"#FFFFFF\" ratio=\"0\"/><GradientEntry ratio=\"1\"/>"
        "</LinearGradientSwatchItem>"
        "<RadialGradientSwatchItem spreadMethod=\"reflect\" interpolationMethod=\"linearRGB\">"
        "<GradientEntry color=\"#FF0000\" alpha=\"0.5\" ratio=\"0\"/><GradientEntry color=\"#00FF00\" ratio=\"0.25\"/>"
        "<GradientEntry color=\"#0000FF\" ratio=\"1\"/></RadialGradientSwatchItem>"
        "<LinearGradientSwatchItem/>"
        "</swatches></swatchList></extendedSwatchLists>";
    const std::vector<fla::swatches::Gradient> read = fla::swatches::gradientsIn(xml);
    CHECK(read.size() == 2);
    if (read.size() != 2)
        return;
    CHECK(!read[0].radial && read[0].stops.size() == 2 && read[0].stops[1].color[0] == 0 && read[0].stops[1].ratio == 1.0);
    CHECK(read[1].radial && read[1].stops.size() == 3 && read[1].spread == "reflect" && read[1].linearRGB);
    CHECK(read[1].stops[0].color[3] == 128 && read[1].stops[1].ratio == 0.25 && read[1].stops[2].color[2] == 255);

    // A document's own, kept as read in its extended swatch lists.
    fla::Document document(nullptr);
    CHECK(fla::swatches::gradientsOf(document).empty());
    document.extraSections.push_back({xml, false});
    CHECK(fla::swatches::gradientsOf(document).size() == 2);
    document.extraSections.push_back({"<brushDefinitions/>", false});
    CHECK(fla::swatches::gradientsOf(document).size() == 2);
}

TEST(swatches_gradient_swatch_fills_a_shape_fitted_and_undoes_in_one_step)
{
    std::unique_ptr<fla::Shape> shape = redSquare();
    double left = 1.0e9, top = 1.0e9, right = -1.0e9, bottom = -1.0e9;
    for (const fla::Path* path : shape->edges.front()->paths)
        for (const fla::PathSegment* segment : path->segments)
            for (const fla::Point& point : segment->points)
            {
                left = std::min(left, point.x);
                top = std::min(top, point.y);
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
    CHECK(right > left && bottom - top > right - left);

    // The radial white to black: the fill becomes it, centered on the
    // square and reaching across its longer side, as one undo step.
    const fla::swatches::Gradient radial = fla::swatches::defaultGradients()[1];
    {
        std::unique_ptr<fla::Element> copy(fla::cloneElement(*shape, nullptr));
        fla::swatches::paintFills(static_cast<fla::Shape&>(*copy), radial);
        fla::CommandStack stack;
        stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape.get(), static_cast<fla::Shape&>(*copy), "Fill Color")));
        CHECK(stack.count() == 1 && stack.topCommand() && stack.topCommand()->name() == "Fill Color");
        CHECK(shape->fills.size() == 1 && shape->fillsMap[1] == shape->fills[0]);
        const auto* gradient = dynamic_cast<const fla::RadialGradient*>(shape->fills[0]);
        CHECK(gradient && gradient->entries.size() == 2);
        if (gradient)
        {
            CHECK(gradient->entries[0].color[0] == 255 && gradient->entries[1].color[0] == 0);
            const fla::Transform& t = gradient->transform;
            CHECK(std::abs(t.tx - (left + right) / 2.0) < 1e-6 && std::abs(t.ty - (top + bottom) / 2.0) < 1e-6);
            CHECK(std::abs(t.m11 - (bottom - top) / 1638.4) < 1e-9 && t.m11 == t.m22);
        }
        // The stroke is left alone.
        CHECK(dynamic_cast<const fla::SolidColor*>(shape->strokes[0]->fill) != nullptr);

        // One undo brings the solid red back.
        stack.undo();
        const auto* solid = dynamic_cast<const fla::SolidColor*>(shape->fills[0]);
        CHECK(solid && solid->color[0] == 255 && solid->color[3] == 255);
    }

    // A linear one along the stroke, fitted to its lines; a solid swatch
    // over a gradient makes it solid again.
    const fla::swatches::Gradient linear = fla::swatches::defaultGradients()[0];
    fla::swatches::paintStrokes(*shape, linear);
    const auto* line = dynamic_cast<const fla::LinearGradient*>(shape->strokes[0]->fill);
    CHECK(line && line->entries.size() == 2 && std::abs(line->transform.m22 - (bottom - top) / 1638.4) < 1e-9);
    fla::swatches::paintStrokes(*shape, Color{0, 0, 255, 255});
    const auto* blue = dynamic_cast<const fla::SolidColor*>(shape->strokes[0]->fill);
    CHECK(blue && blue->color[2] == 255);
    fla::swatches::paintFills(*shape, linear);
    CHECK(dynamic_cast<const fla::LinearGradient*>(shape->fills[0]) != nullptr);
    fla::swatches::paintFills(*shape, Color{0, 255, 0, 128});
    const auto* green = dynamic_cast<const fla::SolidColor*>(shape->fills[0]);
    CHECK(green && green->color[1] == 255 && green->color[3] == 128 && shape->fillsMap[1] == shape->fills[0]);

    // For new art: the style's fill becomes the gradient.
    fla::DrawingStyle style;
    style.hasFill = false;
    fla::swatches::useGradient(style, radial);
    CHECK(style.hasFill && style.fillKind == fla::DrawingStyle::FillKind::Radial && style.gradient.size() == 2);
}
