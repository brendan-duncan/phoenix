#include "lines_to_fills.h"

#include "shape_outline.h"

#include "../data/edge.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/shape_merge.h"

#include <QPainterPath>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

using fla::outline::painterPathOf;
using fla::outline::shapeFromOutline;

bool convertLinesToFills(fla::Shape& shape)
{
    // Every stroked piece, gathered by stroke style, as one painter path
    // each: one stroker pass per style gives the joins and caps Animate
    // would draw, and the union of the outlines under non-zero is the area.
    std::map<int, QPainterPath> byStroke;
    for (const fla::Edge* edge : shape.edges)
    {
        if (!edge)
            continue;
        for (const fla::Path* path : edge->paths)
        {
            if (!path)
                continue;
            const int stroke = path->lineStyleIndex != -1 ? path->lineStyleIndex : edge->strokeStyle;
            if (stroke == -1)
                continue;
            byStroke[stroke].addPath(painterPathOf(*path));
        }
    }
    if (byStroke.empty())
        return false;

    std::vector<std::unique_ptr<fla::Shape>> outlines;
    for (const auto& entry : byStroke)
    {
        const fla::StrokeStyle* style = shape.getStrokeStyleByIndex(entry.first);
        if (!style || !style->fill)
            continue;

        QPainterPathStroker stroker;
        stroker.setWidth(style->isHairline() ? 1.0 : std::max(style->weight, 0.05));
        stroker.setCapStyle(style->caps == "none" ? Qt::FlatCap
            : style->caps == "square" ? Qt::SquareCap : Qt::RoundCap);
        stroker.setJoinStyle(style->joints == "miter" ? Qt::MiterJoin
            : style->joints == "bevel" ? Qt::BevelJoin : Qt::RoundJoin);
        stroker.setMiterLimit(style->miterLimit);

        QPainterPath outline = stroker.createStroke(entry.second);
        outline.setFillRule(Qt::WindingFill);
        outline = outline.simplified();
        if (outline.isEmpty())
            continue;

        fla::FillStyle* fill = fla::cloneFill(style->fill, nullptr);
        if (!fill)
            continue;
        if (fla::Shape* made = shapeFromOutline(outline, fill))
            outlines.emplace_back(made);
        else
            delete fill;
    }

    // The strokes go, the edges that only carried a stroke with them.
    for (fla::Edge* edge : shape.edges)
    {
        if (!edge)
            continue;
        edge->strokeStyle = -1;
        for (fla::Path* path : edge->paths)
        {
            if (path)
                path->lineStyleIndex = -1;
        }
    }
    for (fla::StrokeStyle* stroke : shape.strokes)
        delete stroke;
    shape.strokes.clear();
    shape.strokesMap.clear();

    // Then the new fills land on the shape as a drawn fill would.
    for (const std::unique_ptr<fla::Shape>& outline : outlines)
        fla::ShapeMerger::merge(shape, *outline);
    return true;
}
