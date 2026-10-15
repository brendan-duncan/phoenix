#pragma once

#include <QPainterPath>

#include <map>

namespace fla {
class Shape;
}

/// Modify > Shape > Expand Fill and Soften Fill Edges.
///
/// Both work from the shape's filled regions, one painter path per fill
/// style index as the stage stitches them (`PhoenixView::fillRegions`), so
/// the geometry they offset is exactly what is drawn. The shape's strokes
/// stay where they are; its fills are replaced.
namespace fla::filloffset {

/// Grows (or with \a inset shrinks) every fill by \a distance pixels. The
/// offset is the region unioned with (or less) a ring stroked round its
/// outline at twice the distance, which rounds corners outward. False when the shape has no fill to offset.
bool expandFill(fla::Shape& shape, const std::map<int, QPainterPath>& regions,
    double distance, bool inset);

/// Fades every solid fill's edge over \a distance pixels in \a steps bands,
/// each band a little more transparent than the last; outward from the
/// fill, or with \a inset inward, where the innermost core keeps the full
/// color. Fills that are not solid are offset whole without a fade. False
/// when the shape has no fill.
bool softenFillEdges(fla::Shape& shape, const std::map<int, QPainterPath>& regions,
    double distance, int steps, bool inset);

} // namespace fla::filloffset
