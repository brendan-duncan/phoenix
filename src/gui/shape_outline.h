#pragma once

#include <QList>
#include <QPainterPath>
#include <QPolygonF>

namespace fla {
class FillStyle;
class Path;
class Shape;
}

namespace fla::outline {

/// The path's geometry as Qt draws it.
QPainterPath painterPathOf(const fla::Path& path);

/// A merge shape filled with \a fill wherever \a outline covers, each loop
/// of the outline refitted into a few cubics as the brush's outline is.
/// Takes ownership of \a fill; null when the outline covers nothing, in
/// which case \a fill is still the caller's.
fla::Shape* shapeFromOutline(const QPainterPath& outline, fla::FillStyle* fill);

/// The same from loops already in hand. A loop's winding says which side
/// the fill is on: counterclockwise (positive area) loops are filled
/// inside, clockwise ones outside, so a hole is a reversed loop.
fla::Shape* shapeFromLoops(const QList<QPolygonF>& loops, fla::FillStyle* fill);

} // namespace fla::outline
