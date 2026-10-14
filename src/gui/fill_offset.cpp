#include "fill_offset.h"

#include "shape_outline.h"

#include "../data/edge.h"
#include "../data/fill_style.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../edit/shape_merge.h"

#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <cstdint>

namespace fla::filloffset {

namespace {

/// The loops of a region in a consistent orientation: outer boundaries one
/// way round, holes the other, as Qt's simplification leaves them.
QList<QPolygonF> loopsOf(const QPainterPath& region)
{
    QPainterPath area = region;
    area.setFillRule(Qt::WindingFill);
    return area.simplified().toSubpathPolygons();
}

QPolygonF reversed(const QPolygonF& polygon)
{
    QPolygonF result;
    result.reserve(polygon.size());
    for (int i = polygon.size() - 1; i >= 0; --i)
        result.push_back(polygon[i]);
    return result;
}

/// The region's boundary moved by \a distance: outward when positive,
/// inward when negative. The outline is stroked at twice the distance and
/// the resulting ring's loops sorted by which side of the region they lie
/// on. Qt's area subtraction is not trusted here: it hands back the wrong
/// operand for these inputs, so no boolean operation is used.
///
/// Outward loops keep their orientation. Inward loops are the ring's holes,
/// so they come reversed to bound the shrunken region the right way round.
QList<QPolygonF> boundaryAt(const QPainterPath& region, double distance)
{
    if (std::fabs(distance) < 1.0e-6)
        return loopsOf(region);

    QPainterPath area = region;
    area.setFillRule(Qt::WindingFill);

    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * std::fabs(distance));
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath ring = stroker.createStroke(area);
    ring.setFillRule(Qt::WindingFill);
    ring = ring.simplified();

    QList<QPolygonF> chosen;
    for (const QPolygonF& loop : ring.toSubpathPolygons())
    {
        if (loop.size() < 3)
            continue;
        // Every point of a ring loop is a full distance from the outline,
        // so any one of them says which side the loop is on.
        const bool inside = area.contains(loop[loop.size() / 2]);
        if (distance > 0.0 && !inside)
            chosen.push_back(loop);
        else if (distance < 0.0 && inside)
            chosen.push_back(reversed(loop));
    }
    return chosen;
}

/// Takes every fill off the shape, keeping its strokes: the edges that only
/// carried a fill go, the rest lose their fill sides.
void stripFills(fla::Shape& shape)
{
    std::vector<fla::Edge*> kept;
    for (fla::Edge* edge : shape.edges)
    {
        if (!edge)
            continue;
        bool stroked = edge->strokeStyle != -1;
        for (fla::Path* path : edge->paths)
        {
            if (path && path->lineStyleIndex != -1)
                stroked = true;
        }
        if (!stroked)
        {
            delete edge;
            continue;
        }
        edge->fillStyle0 = -1;
        edge->fillStyle1 = -1;
        for (fla::Path* path : edge->paths)
        {
            if (path)
                path->fillStyleIndex = -1;
        }
        kept.push_back(edge);
    }
    shape.edges = kept;

    for (fla::FillStyle* fill : shape.fills)
        delete fill;
    shape.fills.clear();
    shape.fillsMap.clear();
}

/// A copy of \a fill with its alpha scaled, for a solid color; any other
/// fill is copied as it is.
fla::FillStyle* fadedCopy(const fla::FillStyle* fill, double alphaScale)
{
    fla::FillStyle* copy = fla::cloneFill(fill, nullptr);
    if (copy && copy->type() == fla::FillStyle::Type::SolidColor)
    {
        uint8_t* color = static_cast<fla::SolidColor*>(copy)->color;
        color[3] = static_cast<uint8_t>(std::lround(std::clamp(color[3] * alphaScale, 0.0, 255.0)));
    }
    return copy;
}

/// Adds a shape bounded by \a loops, filled with a copy of \a fill, to the
/// pieces; nothing when the loops bound nothing.
void addPiece(std::vector<std::unique_ptr<fla::Shape>>& pieces, const QList<QPolygonF>& loops,
    const fla::FillStyle* fill, double alphaScale)
{
    if (loops.isEmpty())
        return;
    fla::FillStyle* copy = fadedCopy(fill, alphaScale);
    if (!copy)
        return;
    if (fla::Shape* piece = fla::outline::shapeFromLoops(loops, copy))
        pieces.emplace_back(piece);
    else
        delete copy;
}

/// Puts the new fills on the shape in place of its old ones, under its
/// strokes as paint goes.
bool replaceFills(fla::Shape& shape, std::vector<std::unique_ptr<fla::Shape>>& pieces)
{
    if (pieces.empty())
        return false;
    stripFills(shape);
    fla::MergeOptions options;
    options.keepStrokes = true;
    for (const std::unique_ptr<fla::Shape>& piece : pieces)
        fla::ShapeMerger::merge(shape, *piece, options);
    return true;
}

} // namespace

bool expandFill(fla::Shape& shape, const std::map<int, QPainterPath>& regions,
    double distance, bool inset)
{
    std::vector<std::unique_ptr<fla::Shape>> pieces;
    for (const auto& entry : regions)
    {
        const fla::FillStyle* fill = shape.getFillStyleByIndex(entry.first);
        if (!fill)
            continue;
        addPiece(pieces, boundaryAt(entry.second, inset ? -distance : distance), fill, 1.0);
    }
    return replaceFills(shape, pieces);
}

bool softenFillEdges(fla::Shape& shape, const std::map<int, QPainterPath>& regions,
    double distance, int steps, bool inset)
{
    steps = std::max(steps, 1);
    std::vector<std::unique_ptr<fla::Shape>> pieces;
    for (const auto& entry : regions)
    {
        const fla::FillStyle* fill = shape.getFillStyleByIndex(entry.first);
        if (!fill)
            continue;

        // The core keeps the full color: the fill itself, or the fill shrunk
        // by the whole distance. Each band beyond it is bounded by the next
        // offset outward and, reversed, the previous one, and is fainter
        // than the band inside it.
        const double start = inset ? -distance : 0.0;
        QList<QPolygonF> previous = boundaryAt(entry.second, start);
        addPiece(pieces, previous, fill, 1.0);

        if (fill->type() != fla::FillStyle::Type::SolidColor)
            continue;
        for (int k = 1; k <= steps; ++k)
        {
            const QList<QPolygonF> next = boundaryAt(entry.second, start + distance * k / steps);
            QList<QPolygonF> band = next;
            for (const QPolygonF& loop : previous)
                band.push_back(reversed(loop));
            addPiece(pieces, band, fill, 1.0 - static_cast<double>(k) / (steps + 1));
            previous = next;
        }
    }
    return replaceFills(shape, pieces);
}

} // namespace fla::filloffset
