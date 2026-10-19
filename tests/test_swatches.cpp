#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/edge.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/oval_primitive.h"
#include "../src/data/radial_gradient.h"
#include "../src/data/rectangle_primitive.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/element_clone.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/swatches.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/path_parser.h"
#include "../src/writer/xfl_writer.h"

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

namespace {

std::unique_ptr<fla::Document> parsed(const std::string& xml)
{
    DocumentParser parser(nullptr);
    return std::unique_ptr<fla::Document>(parser.parse(xml, nullptr));
}

} // namespace

TEST(swatches_gradient_swatches_add_delete_undo_and_round_trip)
{
    // A document with only solid swatches shows the default gradients.
    const std::string source =
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"550\" height=\"400\"><timelines/>"
        "<swatchLists><swatchList><swatchListInfo name=\"Default Swatches\"/><swatches>"
        "<SolidSwatchItem color=\"#FF0000\"/></swatches></swatchList></swatchLists></DOMDocument>";
    std::unique_ptr<fla::Document> document = parsed(source);
    CHECK(document != nullptr);
    if (!document)
        return;
    CHECK(fla::swatches::gradientsOf(*document).empty());
    CHECK(fla::swatches::shownGradients(document.get()).size() == 8);
    fla::XFLWriter writer;
    const std::string unchanged = writer.writeDocument(*document);

    // A see-through, reflected radial gradient added: the defaults become
    // the document's own ahead of it, in its extended swatch lists.
    fla::swatches::Gradient added;
    added.radial = true;
    added.spread = "reflect";
    added.linearRGB = true;
    added.stops.resize(3);
    added.stops[0].color[0] = 255;
    added.stops[0].color[3] = 128;
    added.stops[1].ratio = 0.25;
    added.stops[1].color[1] = 255;
    added.stops[2].ratio = 1.0;
    fla::CommandStack stack;
    const fla::swatches::State before = fla::swatches::stateOf(*document);
    stack.push(fla::CommandPtr(new fla::SetSwatchesCommand(document.get(), before,
        fla::swatches::withGradientAdded(before, added), "Add Swatch")));
    std::vector<fla::swatches::Gradient> own = fla::swatches::gradientsOf(*document);
    CHECK(own.size() == 9 && fla::swatches::of(*document).size() == 1);
    CHECK(document->extraSections.size() == 1);

    // Written and read back as it was added.
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("<extendedSwatchLists>") != std::string::npos);
    std::unique_ptr<fla::Document> reread = parsed(written);
    CHECK(reread != nullptr);
    if (reread)
    {
        const std::vector<fla::swatches::Gradient> back = fla::swatches::gradientsOf(*reread);
        CHECK(back.size() == 9);
        if (back.size() == 9)
        {
            const fla::swatches::Gradient& last = back.back();
            CHECK(last.radial && last.spread == "reflect" && last.linearRGB && last.stops.size() == 3);
            CHECK(last.stops[0].color[0] == 255 && last.stops[0].color[3] == 128 && last.stops[1].ratio == 0.25);
            CHECK(last.stops[1].color[1] == 255 && last.stops[2].color[0] == 0 && last.stops[2].color[3] == 255);
            CHECK(!back[0].radial && back[0].stops[0].color[0] == 255);
        }
        CHECK(fla::swatches::of(*reread).size() == 1);
    }

    // The first deleted: the radial white to black leads.
    const fla::swatches::State withNine = fla::swatches::stateOf(*document);
    stack.push(fla::CommandPtr(new fla::SetSwatchesCommand(document.get(), withNine,
        fla::swatches::withGradientRemoved(withNine, 0), "Delete Swatch")));
    own = fla::swatches::gradientsOf(*document);
    CHECK(own.size() == 8 && own[0].radial);
    // A solid swatch edit leaves the gradients be.
    const fla::swatches::State withEight = fla::swatches::stateOf(*document);
    stack.push(fla::CommandPtr(new fla::SetSwatchesCommand(document.get(), withEight,
        fla::swatches::withAdded(withEight, {0, 0, 255, 255}), "Add Swatch")));
    CHECK(fla::swatches::gradientsOf(*document).size() == 8 && fla::swatches::of(*document).size() == 2);

    // Each undone in turn, back to the file as read, written the same.
    stack.undo();
    CHECK(fla::swatches::gradientsOf(*document).size() == 8);
    stack.undo();
    CHECK(fla::swatches::gradientsOf(*document).size() == 9);
    stack.undo();
    CHECK(fla::swatches::gradientsOf(*document).empty() && document->extraSections.empty());
    CHECK(writer.writeDocument(*document) == unchanged);

    // Gradients in the swatch list itself come first when deleting.
    fla::swatches::State mixed;
    mixed.exists = true;
    mixed.xml = "<swatchList><swatches>" + fla::swatches::itemOf(added) + "</swatches></swatchList>";
    mixed = fla::swatches::withGradientAdded(mixed, fla::swatches::defaultGradients()[0]);
    CHECK(fla::swatches::gradientsOf(mixed).size() == 2);
    const fla::swatches::State gone = fla::swatches::withGradientRemoved(mixed, 0);
    CHECK(fla::swatches::gradientsOf(gone).size() == 1 && !fla::swatches::gradientsOf(gone)[0].radial);
    CHECK(fla::swatches::gradientsOf(fla::swatches::withGradientRemoved(mixed, 5)).size() == 2);

    // A duplicate goes just after its original, in either list.
    const fla::swatches::State twice = fla::swatches::withGradientDuplicated(mixed, 0);
    const std::vector<fla::swatches::Gradient> doubled = fla::swatches::gradientsOf(twice);
    CHECK(doubled.size() == 3 && doubled[0].radial && doubled[1].radial && !doubled[2].radial);
    CHECK(twice.extendedXml == mixed.extendedXml);
    const std::vector<fla::swatches::Gradient> after = fla::swatches::gradientsOf(fla::swatches::withGradientDuplicated(mixed, 1));
    CHECK(after.size() == 3 && !after[1].radial && !after[2].radial);
    CHECK(fla::swatches::gradientsOf(fla::swatches::withGradientDuplicated(mixed, 5)).size() == 2);
    // The defaults become the document's own, the copy among them.
    const std::vector<fla::swatches::Gradient> defaults = fla::swatches::defaultGradients();
    const std::vector<fla::swatches::Gradient> ownCopy =
        fla::swatches::gradientsOf(fla::swatches::withGradientDuplicated(fla::swatches::State(), 2));
    CHECK(ownCopy.size() == defaults.size() + 1);
    if (ownCopy.size() == defaults.size() + 1)
        CHECK(fla::swatches::itemOf(ownCopy[3]) == fla::swatches::itemOf(defaults[2]) &&
            fla::swatches::itemOf(ownCopy[4]) == fla::swatches::itemOf(defaults[3]));
}

TEST(swatches_paint_primitives)
{
    // A rectangle primitive with a red fill and a black stroke.
    fla::RectanglePrimitive rectangle(nullptr);
    rectangle.rect = fla::Rect(fla::Point(10, 20), fla::Point(110, 70));
    auto* fill = new fla::SolidColor(&rectangle);
    fill->color[0] = 255;
    rectangle.fillStyle = fill;
    rectangle.strokeStyle = new fla::SolidStroke(&rectangle);
    auto* line = new fla::SolidColor(rectangle.strokeStyle);
    rectangle.strokeStyle->fill = line;

    // A color keeps the fill and changes it; undone, it is as it was.
    std::unique_ptr<fla::Command> blue(fla::swatches::paintPrimitive(rectangle, false, Color{0, 0, 255, 128}, "Fill Color"));
    CHECK(blue != nullptr);
    if (!blue)
        return;
    blue->redo();
    CHECK(rectangle.fillStyle == fill && fill->color[0] == 0 && fill->color[2] == 255 && fill->color[3] == 128);
    blue->undo();
    CHECK(fill->color[0] == 255 && fill->color[2] == 0 && fill->color[3] == 255);

    // A gradient replaces the fill, fitted to the box; undone, the solid
    // fill is back.
    const fla::swatches::Gradient gradient = fla::swatches::defaultGradients()[0];
    std::unique_ptr<fla::Command> shaded(fla::swatches::paintPrimitive(rectangle, false, gradient, "Fill Color"));
    CHECK(shaded != nullptr);
    if (!shaded)
        return;
    shaded->redo();
    CHECK(rectangle.fillStyle != fill && rectangle.fillStyle->type() == fla::FillStyle::Type::LinearGradient);
    CHECK(rectangle.fillStyle->parent == &rectangle);
    shaded->undo();
    CHECK(rectangle.fillStyle == fill);

    // The stroke takes a color but not a gradient.
    std::unique_ptr<fla::Command> green(fla::swatches::paintPrimitive(rectangle, true, Color{0, 255, 0, 255}, "Stroke Color"));
    CHECK(green != nullptr);
    if (green)
    {
        green->redo();
        CHECK(line->color[1] == 255 && fill->color[0] == 255);
        green->undo();
        CHECK(line->color[1] == 0);
    }
    CHECK(fla::swatches::paintPrimitive(rectangle, true, gradient, "Stroke Color") == nullptr);

    // An oval with no fill has none to paint; a shape is not a primitive.
    fla::OvalPrimitive oval(nullptr);
    CHECK(fla::swatches::isPrimitive(oval) && fla::swatches::isPrimitive(rectangle));
    CHECK(fla::swatches::paintPrimitive(oval, false, Color{0, 0, 0, 255}, "Fill Color") == nullptr);
    std::unique_ptr<fla::Shape> shape = redSquare();
    CHECK(!fla::swatches::isPrimitive(*shape));
    CHECK(fla::swatches::paintPrimitive(*shape, false, Color{0, 0, 0, 255}, "Fill Color") == nullptr);
}

TEST(swatches_no_color_keeps_a_fill_or_a_stroke)
{
    // Fill and stroke: either can go, the other stays.
    std::unique_ptr<fla::Shape> shape = redSquare();
    std::unique_ptr<fla::Shape> strokeOnly(fla::swatches::withoutPart(*shape, false));
    std::unique_ptr<fla::Shape> fillOnly(fla::swatches::withoutPart(*shape, true));
    CHECK(strokeOnly && fillOnly);
    if (!strokeOnly || !fillOnly)
        return;
    CHECK(!strokeOnly->edges.empty() && !fillOnly->edges.empty());
    for (const fla::Edge* edge : strokeOnly->edges)
        CHECK(edge->fillStyle0 <= 0 && edge->fillStyle1 <= 0 && edge->strokeStyle > 0);
    for (const fla::Edge* edge : fillOnly->edges)
        CHECK(edge->strokeStyle <= 0 && edge->fillStyle1 > 0);

    // The last part is never taken: nothing comes back.
    CHECK(fla::swatches::withoutPart(*strokeOnly, true) == nullptr);
    CHECK(fla::swatches::withoutPart(*fillOnly, false) == nullptr);
}
