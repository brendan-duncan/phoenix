#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/data/timeline.h"
#include "../src/data/transform.h"
#include "../src/edit/color_art.h"
#include "../src/edit/region_split.h"
#include "../src/geom/curve.h"
#include "../src/parser/path_parser.h"

#include <memory>

namespace {

/// A filled square with a stroke round it: 0..100 pixels each way (the
/// edge string is in twips).
fla::Shape* strokedSquare(fla::DOMElement* parent)
{
    fla::Shape* shape = new fla::Shape(parent);
    fla::SolidColor* fill = new fla::SolidColor(shape);
    fill->color[0] = 200;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    fla::SolidStroke* stroke = new fla::SolidStroke(shape);
    stroke->weight = 1.0;
    stroke->fill = new fla::SolidColor(stroke);
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|2000 0|2000 2000|0 2000|0 0", shape);
    edge->fillStyle1 = 1;
    edge->strokeStyle = 1;
    shape->edges.push_back(edge);
    return shape;
}

} // namespace

TEST(line_and_color_art_separate_and_the_lines_close_the_paint)
{
    fla::Timeline scene(nullptr);
    fla::Layer* line = new fla::Layer(&scene);
    line->name = "Ink";
    fla::Frame* frame = new fla::Frame(line);
    frame->duration = 12;
    frame->elements.push_back(strokedSquare(frame));
    line->frames.push_back(frame);
    scene.layers.push_back(line);

    const fla::Shape& square = static_cast<const fla::Shape&>(*frame->elements.front());
    std::unique_ptr<fla::Shape> strokes(fla::colorart::strokesOnly(square));
    std::unique_ptr<fla::Shape> fills(fla::colorart::fillsOnly(square));
    CHECK(strokes->edges.size() == 1 && strokes->edges[0]->strokeStyle == 1 && strokes->edges[0]->fillStyle1 < 1);
    CHECK(fills->edges.size() == 1 && fills->edges[0]->strokeStyle < 1 && fills->edges[0]->fillStyle1 == 1);

    std::unique_ptr<fla::Layer> color(fla::colorart::newColorLayer(*line));
    CHECK(color->name == "Ink Color" && fla::colorart::isColorArt(*color));
    CHECK(color->frames.size() == 1 && color->frames[0]->duration == 12 && color->frames[0]->elements.size() == 1);
    scene.layers.push_back(color.get());
    CHECK(fla::colorart::lineLayerOf(scene, *color) == line);
    scene.layers.pop_back();

    // The strokes, as cutters, close a region an empty shape can be
    // painted in: the inside of the square.
    const std::vector<fla::Curve> cutters = fla::colorart::lineCutters(*line, 5, fla::Transform());
    CHECK(!cutters.empty());
    fla::Shape empty(nullptr);
    fla::RegionSplitter regions(empty, cutters);
    CHECK(regions.faceAt(fla::Point(50, 50)) >= 0);
    CHECK(regions.faceAt(fla::Point(150, 50)) < 0);
    // Paint already there, its edges lying on the lines: the face is found.
    fla::RegionSplitter repaint(*fills, cutters);
    const int face = repaint.faceAt(fla::Point(50, 50));
    CHECK(face >= 0);
    // The lines the paint already lies along are not laid over it again.
    CHECK(fla::colorart::withoutShared(cutters, *fills).empty());
    CHECK(fla::colorart::withoutShared(cutters, empty).size() == cutters.size());

    // Past the layer's frames there is nothing to paint within.
    CHECK(fla::colorart::lineCutters(*line, 20, fla::Transform()).empty());
}
