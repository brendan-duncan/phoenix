#include "lines_to_fills.h"

#include "../data/edge.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/curve_fit.h"
#include "../edit/editable_path.h"
#include "../edit/shape_merge.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace {

constexpr int kStyleIndex = 1;

/// The path's geometry as Qt draws it.
QPainterPath painterPathOf(const fla::Path& path)
{
    QPainterPath result;
    QPointF current;
    for (const fla::PathSegment* segment : path.segments)
    {
        if (!segment || segment->points.empty())
            continue;
        const std::vector<fla::Point>& pts = segment->points;
        switch (segment->command)
        {
        case fla::PathSegment::Command::Move:
            current = QPointF(pts[0].x, pts[0].y);
            result.moveTo(current);
            break;
        case fla::PathSegment::Command::Line:
            current = QPointF(pts[0].x, pts[0].y);
            result.lineTo(current);
            break;
        case fla::PathSegment::Command::Quad:
            if (pts.size() >= 2)
            {
                current = QPointF(pts[1].x, pts[1].y);
                result.quadTo(QPointF(pts[0].x, pts[0].y), current);
            }
            break;
        case fla::PathSegment::Command::Cubic:
            if (pts.size() >= 3)
            {
                current = QPointF(pts[2].x, pts[2].y);
                result.cubicTo(QPointF(pts[0].x, pts[0].y), QPointF(pts[1].x, pts[1].y), current);
            }
            break;
        case fla::PathSegment::Command::Close:
            result.closeSubpath();
            break;
        }
    }
    return result;
}

double signedArea(const QPolygonF& polygon)
{
    double area = 0.0;
    for (int i = 0; i < polygon.size(); ++i)
    {
        const QPointF& a = polygon[i];
        const QPointF& b = polygon[(i + 1) % polygon.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area;
}

/// A merge shape filled with \a fill wherever \a outline covers, each loop
/// of the outline refitted into a few cubics as the brush's outline is.
fla::Shape* shapeFromOutline(const QPainterPath& outline, fla::FillStyle* fill)
{
    fla::Shape* shape = new fla::Shape(nullptr);
    fill->parent = shape;
    shape->fills.push_back(fill);
    shape->fillsMap[kStyleIndex] = fill;

    const QList<QPolygonF> loops = outline.toSubpathPolygons();
    for (const QPolygonF& polygon : loops)
    {
        if (polygon.size() < 3)
            continue;

        fla::Edge* edge = new fla::Edge(shape);
        if (signedArea(polygon) >= 0.0)
            edge->fillStyle1 = kStyleIndex;
        else
            edge->fillStyle0 = kStyleIndex;

        std::vector<fla::Point> ring;
        for (const QPointF& point : polygon)
            ring.push_back(fla::Point(point.x(), point.y()));
        if (std::hypot(ring.back().x - ring.front().x, ring.back().y - ring.front().y) > 1.0e-6)
            ring.push_back(ring.front());

        fla::CurveFit::Options options;
        options.tolerance = 0.3;
        options.minimumSpacing = 0.2;
        fla::EditablePath fitted = fla::CurveFit::fit(ring, options);
        if (fitted.anchors.size() >= 2)
        {
            const fla::Point& first = fitted.anchors.front().position;
            const fla::Point& last = fitted.anchors.back().position;
            if (fitted.anchors.size() > 2 && std::hypot(last.x - first.x, last.y - first.y) < 1.0e-6)
                fitted.anchors.pop_back();
            fitted.closed = true;
            fitted.applyTo(*edge);
        }
        else
        {
            fla::Path* path = new fla::Path(edge);
            path->segments.push_back(new fla::PathSegment(
                fla::PathSegment::Command::Move, {ring.front()}, path));
            for (size_t i = 1; i < ring.size(); ++i)
            {
                path->segments.push_back(new fla::PathSegment(
                    fla::PathSegment::Command::Line, {ring[i]}, path));
            }
            edge->paths.push_back(path);
        }
        shape->edges.push_back(edge);
    }

    if (shape->edges.empty())
    {
        delete shape;
        return nullptr;
    }
    return shape;
}

} // namespace

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
