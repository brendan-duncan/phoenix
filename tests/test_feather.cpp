#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/rectangle_primitive.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/data/timeline.h"
#include "../src/edit/element_clone.h"
#include "../src/edit/feather.h"
#include "../src/edit/phoenix_features.h"
#include "../src/edit/shape_merge.h"
#include "../src/edit/shape_tween.h"
#include "../src/edit/swatches.h"

#include <cmath>
#include <memory>

TEST(a_feather_is_kept_on_each_fill_and_stroke_and_goes_with_copies)
{
    fla::Shape shape(nullptr);
    auto* fill = new fla::SolidColor(&shape);
    auto* gradient = new fla::LinearGradient(&shape);
    auto* stroke = new fla::SolidStroke(&shape);
    stroke->fill = new fla::SolidColor(stroke);
    shape.fills = {fill, gradient};
    shape.fillsMap = {{1, fill}, {2, gradient}};
    shape.strokes = {stroke};
    shape.strokesMap = {{1, stroke}};

    CHECK(!fla::feather::any(shape));
    CHECK_NEAR(fla::feather::of(*fill), 0.0);

    fla::feather::setFills(shape, 6.5);
    CHECK_NEAR(fla::feather::of(*fill), 6.5);
    CHECK_NEAR(fla::feather::of(*gradient), 6.5);
    CHECK_NEAR(fla::feather::of(*stroke), 0.0);
    bool mixed = true;
    CHECK_NEAR(fla::feather::ofFills(shape, &mixed), 6.5);
    CHECK(!mixed);
    CHECK(fla::feather::any(shape));

    fla::feather::set(*gradient, 2.0);
    fla::feather::ofFills(shape, &mixed);
    CHECK(mixed);

    // A stroke's own, and taken out again at 0.
    fla::feather::setStrokes(shape, 3.0);
    CHECK_NEAR(fla::feather::ofStrokes(shape), 3.0);
    fla::feather::set(*stroke, 0.0);
    CHECK(stroke->extraAttributes.empty());
    fla::feather::set(*stroke, 3.0);

    // Copies keep it.
    std::unique_ptr<fla::FillStyle> fillCopy(fla::cloneFill(gradient, nullptr));
    CHECK_NEAR(fla::feather::of(*fillCopy), 2.0);
    std::unique_ptr<fla::StrokeStyle> strokeCopy(fla::cloneStroke(stroke, nullptr));
    CHECK_NEAR(fla::feather::of(*strokeCopy), 3.0);
    std::unique_ptr<fla::Element> shapeCopy(fla::cloneElement(shape, nullptr));
    CHECK(fla::feather::any(*shapeCopy));
}

// A shape tween softens or sharpens the edge between its keyframes, and
// recoloring keeps it.
TEST(a_feather_blends_through_a_tween_and_survives_recoloring)
{
    fla::SolidColor sharp(nullptr);
    fla::SolidColor soft(nullptr);
    fla::feather::set(soft, 10.0);
    std::unique_ptr<fla::FillStyle> between(fla::blendFills(&sharp, &soft, 0.25, nullptr));
    CHECK(between && std::abs(fla::feather::of(*between) - 2.5) < 1e-6);

    fla::SolidStroke from(nullptr);
    from.fill = new fla::SolidColor(&from);
    fla::SolidStroke to(nullptr);
    to.fill = new fla::SolidColor(&to);
    fla::feather::set(to, 4.0);
    std::unique_ptr<fla::StrokeStyle> line(fla::blendStrokes(&from, &to, 0.5, nullptr));
    CHECK(line && std::abs(fla::feather::of(*line) - 2.0) < 1e-6);

    fla::Shape shape(nullptr);
    auto* gradient = new fla::LinearGradient(&shape);
    fla::feather::set(*gradient, 7.0);
    shape.fills = {gradient};
    shape.fillsMap = {{1, gradient}};
    fla::swatches::paintFills(shape, fla::swatches::Color{255, 0, 0, 255});
    CHECK(shape.fills.size() == 1 && shape.fills[0]->type() == fla::FillStyle::Type::SolidColor);
    CHECK_NEAR(fla::feather::of(*shape.fills[0]), 7.0);
}

TEST(a_feathered_primitive_counts_as_a_phoenix_feature)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    fla::Timeline* scene = new fla::Timeline(document.get());
    scene->name = "Scene 1";
    document->timelines.push_back(scene);
    fla::Layer* layer = new fla::Layer(scene);
    layer->name = "Glow";
    scene->layers.push_back(layer);
    fla::Frame* frame = new fla::Frame(layer);
    layer->frames.push_back(frame);
    auto* rectangle = new fla::RectanglePrimitive(frame);
    rectangle->fillStyle = new fla::SolidColor(rectangle);
    frame->elements.push_back(rectangle);

    CHECK(!fla::feather::any(*rectangle));
    CHECK(fla::features::used(*document).empty());
    fla::feather::set(*rectangle->fillStyle, 12.0);
    CHECK(fla::feather::any(*rectangle));
    const std::vector<fla::features::Use> uses = fla::features::used(*document);
    CHECK(uses.size() == 1 && std::string(uses[0].feature->id) == "vector-feather");
}
