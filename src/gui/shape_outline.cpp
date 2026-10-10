#include "shape_outline.h"

#include "../data/edge.h"
#include "../data/fill_style.h"
#include "../data/shape.h"
#include "../edit/curve_fit.h"
#include "../edit/editable_path.h"

#include <QPolygonF>

#include <cmath>
#include <vector>

namespace fla::outline {

namespace {

constexpr int kStyleIndex = 1;

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

} // namespace

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

fla::Shape* shapeFromOutline(const QPainterPath& outline, fla::FillStyle* fill)
{
    return shapeFromLoops(outline.toSubpathPolygons(), fill);
}

fla::Shape* shapeFromLoops(const QList<QPolygonF>& loops, fla::FillStyle* fill)
{
    fla::Shape* shape = new fla::Shape(nullptr);

    // Outer boundaries all run one way round and holes the other, so the
    // biggest loop says which way is "outside in": a loop running with it
    // is filled on its inside, one running against it on its outside.
    double dominant = 0.0;
    for (const QPolygonF& polygon : loops)
    {
        const double area = signedArea(polygon);
        if (std::fabs(area) > std::fabs(dominant))
            dominant = area;
    }

    for (const QPolygonF& polygon : loops)
    {
        if (polygon.size() < 3)
            continue;

        fla::Edge* edge = new fla::Edge(shape);
        const double area = signedArea(polygon);
        const bool filledInside = (area >= 0.0) == (dominant >= 0.0);
        // A positive loop has its inside on its left, so a loop filled
        // inside takes the fill on the left when positive and on the right
        // when negative; a hole takes the other side.
        if ((area >= 0.0) == filledInside)
            edge->fillStyle1 = kStyleIndex;
        else
            edge->fillStyle0 = kStyleIndex;

        // The fitter finds corners by how far the run turns over a short
        // stretch either side of a point, so a loop given as bare vertices
        // (a plain rectangle, say) is first filled in with points every
        // couple of units along each side; the straight runs then fit as
        // lines and the corners stay sharp.
        std::vector<fla::Point> ring;
        const double spacing = 2.0;
        const int count = polygon.size();
        for (int i = 0; i < count; ++i)
        {
            const QPointF& from = polygon[i];
            const QPointF& to = polygon[(i + 1) % count];
            ring.push_back(fla::Point(from.x(), from.y()));
            const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
            const int pieces = static_cast<int>(length / spacing);
            for (int k = 1; k < pieces; ++k)
            {
                const double t = static_cast<double>(k) / pieces;
                ring.push_back(fla::Point(from.x() + (to.x() - from.x()) * t, from.y() + (to.y() - from.y()) * t));
            }
        }
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

    fill->parent = shape;
    shape->fills.push_back(fill);
    shape->fillsMap[kStyleIndex] = fill;
    return shape;
}

} // namespace fla::outline
