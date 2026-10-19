#include "test_util.h"

#include "../src/data/edge.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/data/symbol_instance.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/inbetween.h"
#include "../src/edit/shape_tween.h"
#include "../src/edit/timeline_edit.h"

#include <cmath>
#include <memory>

using fla::Point;

namespace {

bool near(double a, double b, double tolerance = 1.0e-3)
{
    return std::fabs(a - b) < tolerance;
}

/// A filled square of side \a size at (\a x, \a y), as a drawing.
fla::Shape* square(fla::DOMElement* parent, double x, double y, double size)
{
    fla::Shape* shape = new fla::Shape(parent);
    fla::SolidColor* fill = new fla::SolidColor(shape);
    fill->color[0] = 255;
    fill->color[3] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    fla::Edge* edge = new fla::Edge(shape);
    edge->fillStyle1 = 1;
    fla::Path* path = new fla::Path(edge);
    const Point corners[5] = {Point(x, y), Point(x + size, y), Point(x + size, y + size), Point(x, y + size), Point(x, y)};
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move, {corners[0]}, path));
    for (int i = 1; i < 5; ++i)
        path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Line, {corners[i]}, path));
    edge->paths.push_back(path);
    shape->edges.push_back(edge);
    return shape;
}

fla::Frame* keyframe(fla::Layer& layer, int index, int duration)
{
    fla::Frame* frame = new fla::Frame(&layer);
    frame->index = index;
    frame->duration = duration;
    frame->keyMode = "9728";
    layer.frames.push_back(frame);
    layer.lastFrame = index + duration;
    return frame;
}

/// Two drawings \a gap frames apart: a square of 100 at the origin, and
/// one of 200 at (300, 50).
std::unique_ptr<fla::Layer> twoDrawings(int gap)
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = keyframe(*layer, 0, gap);
    a->elements.push_back(square(a, 0.0, 0.0, 100.0));
    fla::Frame* b = keyframe(*layer, gap, 1);
    b->elements.push_back(square(b, 300.0, 50.0, 200.0));
    return layer;
}

const fla::Shape* shapeOn(const fla::Layer& layer, int index)
{
    for (const fla::Frame* frame : layer.frames)
        if (frame->index == index && !frame->elements.empty() &&
            frame->elements.front()->elementType() == fla::Element::Type::Shape)
            return static_cast<const fla::Shape*>(frame->elements.front());
    return nullptr;
}

} // namespace

TEST(inbetween_halfway_lies_half_way_in_place_and_size)
{
    std::unique_ptr<fla::Layer> layer = twoDrawings(4);
    fla::inbetween::Report report;
    const int at = fla::inbetween::insertInbetween(*layer, 0, 0, 0.5, report);
    CHECK(at == 2);
    CHECK(layer->frames.size() == 3);
    CHECK(report.morphed == 1 && report.held == 0 && report.keyframes == 1);
    const fla::Shape* mid = shapeOn(*layer, 2);
    CHECK(mid != nullptr);
    if (!mid)
        return;
    CHECK(near(mid->localBounds.topLeft.x, 150.0) && near(mid->localBounds.topLeft.y, 25.0));
    CHECK(near(mid->localBounds.bottomRight.x, 300.0) && near(mid->localBounds.bottomRight.y, 175.0));
    // An ordinary keyframe: the first key's span ends where it starts.
    CHECK(layer->frames[0]->duration == 2 && layer->frames[1]->duration == 2);
    CHECK(layer->frames[1]->tweenType == fla::TweenType::None);
}

TEST(inbetween_a_third_lies_a_third_of_the_way)
{
    std::unique_ptr<fla::Layer> layer = twoDrawings(6);
    fla::inbetween::Report report;
    const int at = fla::inbetween::insertInbetween(*layer, 3, 3, 1.0 / 3.0, report);
    CHECK(at == 2);
    const fla::Shape* third = shapeOn(*layer, 2);
    CHECK(third != nullptr);
    if (!third)
        return;
    CHECK(near(third->localBounds.topLeft.x, 100.0) && near(third->localBounds.topLeft.y, 50.0 / 3.0));
    CHECK(near(third->localBounds.width(), 100.0 + 100.0 / 3.0));
}

TEST(inbetween_follows_the_shape_hints_on_the_first_key)
{
    // Two like squares; a hint takes the top left corner to the bottom
    // right, so half way the square has turned onto its middle.
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = keyframe(*layer, 0, 2);
    a->elements.push_back(square(a, 0.0, 0.0, 100.0));
    fla::Frame* b = keyframe(*layer, 2, 1);
    b->elements.push_back(square(b, 0.0, 0.0, 100.0));
    fla::inbetween::Report report;
    std::unique_ptr<fla::Frame> plain(new fla::Frame(nullptr));
    for (fla::Element* element : fla::inbetween::drawingBetween(*a, *b, 0.5, plain.get(), report))
        plain->elements.push_back(element);
    CHECK(plain->elements.size() == 1);

    fla::ShapeHint hint;
    hint.start = Point(0.0, 0.0);
    hint.end = Point(100.0, 100.0);
    fla::setHints(a->extraAttributes, {hint});
    CHECK(fla::inbetween::insertInbetween(*layer, 0, 2, 0.5, report) == 1);
    const fla::Shape* hinted = shapeOn(*layer, 1);
    CHECK(hinted != nullptr);
    if (!hinted || plain->elements.empty())
        return;
    const auto* unhinted = static_cast<const fla::Shape*>(plain->elements.front());
    CHECK(near(unhinted->localBounds.width(), 100.0));
    CHECK(hinted->localBounds.width() < 1.0);
}

TEST(inbetween_between_neighboring_keys_makes_room)
{
    std::unique_ptr<fla::Layer> layer = twoDrawings(1);
    fla::inbetween::Report report;
    // Standing on the last key: between it and the one before.
    CHECK(fla::inbetween::insertInbetween(*layer, 1, 1, 0.25, report) == 1);
    CHECK(layer->frames.size() == 3);
    CHECK(layer->frames[2]->index == 2);
    const fla::Shape* favor = shapeOn(*layer, 1);
    CHECK(favor && near(favor->localBounds.topLeft.x, 75.0));
}

TEST(inbetween_moves_symbol_instances_and_holds_the_unpaired)
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = keyframe(*layer, 0, 2);
    fla::Frame* b = keyframe(*layer, 2, 1);
    auto* ball = new fla::SymbolInstance(a);
    ball->libraryItemName = "Ball";
    ball->transform = fla::Transform(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    a->elements.push_back(ball);
    auto* ballLater = new fla::SymbolInstance(b);
    ballLater->libraryItemName = "Ball";
    ballLater->transform = fla::Transform(2.0, 0.0, 0.0, 2.0, 100.0, 40.0);
    b->elements.push_back(ballLater);
    auto* star = new fla::SymbolInstance(b);
    star->libraryItemName = "Star";
    b->elements.push_back(star);

    fla::inbetween::Report report;
    CHECK(fla::inbetween::insertInbetween(*layer, 0, 0, 0.5, report) == 1);
    CHECK(report.moved == 1 && report.held == 1);
    const fla::Frame* mid = layer->frames[1];
    // Nearer neither: held from the second key at half way.
    CHECK(mid->elements.size() == 2);
    if (mid->elements.empty())
        return;
    CHECK(near(mid->elements[0]->transform.tx, 50.0) && near(mid->elements[0]->transform.ty, 20.0));
    CHECK(near(mid->elements[0]->transform.m11, 1.5));
}

TEST(fill_inbetweens_fills_every_frame_with_the_spacing_asked)
{
    std::unique_ptr<fla::Layer> layer(new fla::Layer(nullptr));
    fla::Frame* a = keyframe(*layer, 0, 4);
    a->elements.push_back(square(a, 0.0, 0.0, 100.0));
    a->tweenType = fla::TweenType::Shape;
    fla::Frame* b = keyframe(*layer, 4, 1);
    b->elements.push_back(square(b, 400.0, 0.0, 100.0));

    fla::inbetween::Report report;
    CHECK(fla::inbetween::fillInbetweens(*layer, 0, 4, fla::inbetween::Spacing::Even, report) == 3);
    CHECK(layer->frames.size() == 5);
    CHECK(layer->frames[0]->tweenType == fla::TweenType::None);
    for (int i = 1; i <= 3; ++i)
    {
        const fla::Shape* shape = shapeOn(*layer, i);
        CHECK(shape && near(shape->localBounds.topLeft.x, 100.0 * i));
    }
    CHECK(fla::inbetween::spacingAt(fla::inbetween::Spacing::EaseIn, 1, 4) < 0.25);
    CHECK(fla::inbetween::spacingAt(fla::inbetween::Spacing::EaseOut, 1, 4) > 0.25);

    // Eased in: bunched toward the first key.
    std::unique_ptr<fla::Layer> eased(new fla::Layer(nullptr));
    fla::Frame* c = keyframe(*eased, 0, 4);
    c->elements.push_back(square(c, 0.0, 0.0, 100.0));
    fla::Frame* d = keyframe(*eased, 4, 1);
    d->elements.push_back(square(d, 400.0, 0.0, 100.0));
    CHECK(fla::inbetween::fillInbetweens(*eased, 2, 2, fla::inbetween::Spacing::EaseIn, report) == 3);
    const fla::Shape* first = shapeOn(*eased, 1);
    const fla::Shape* last = shapeOn(*eased, 3);
    CHECK(first && near(first->localBounds.topLeft.x, 25.0));
    CHECK(last && near(last->localBounds.topLeft.x, 225.0));

    // Nothing between neighbors to fill.
    std::unique_ptr<fla::Layer> tight = twoDrawings(1);
    CHECK(fla::inbetween::fillInbetweens(*tight, 0, 0, fla::inbetween::Spacing::Even, report) == 0);
}

TEST(inbetweens_undo_in_one_step)
{
    std::unique_ptr<fla::Layer> layer = twoDrawings(5);
    fla::Layer* edited = layer.get();
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::EditFramesCommand({edited}, "Fill Inbetweens",
        [edited](std::vector<fla::Frame*>&) {
            fla::inbetween::Report report;
            fla::inbetween::fillInbetweens(*edited, 0, 0, fla::inbetween::Spacing::EaseOut, report);
        })));
    CHECK(layer->frames.size() == 6);
    stack.undo();
    CHECK(layer->frames.size() == 2 && layer->frames[0]->duration == 5);
    stack.redo();
    CHECK(layer->frames.size() == 6);
}
