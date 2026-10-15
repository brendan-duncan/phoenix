#pragma once

#include <QPainterPath>

namespace fla {
class StrokeStyle;
}

/// The procedural stroke styles drawn from a stroke's centerline:
/// ragged (a wavering line), stipple (dots scattered across the stroke's
/// width) and hatched (short marks across the line). The result is an area
/// to fill with the stroke's color, in the same units as \a centerline.
/// The randomness is seeded from the geometry, so a stroke looks the same
/// every time it is drawn. Empty for any other style.
namespace StrokeEffects {

bool isProcedural(const fla::StrokeStyle& style);

QPainterPath outline(const QPainterPath& centerline, const fla::StrokeStyle& style);

/// How far a variable-width stroke reaches to its left and right, in
/// pixels, a fraction t of the way along it.
void widthAt(const fla::StrokeStyle& style, double t, double& left, double& right);

} // namespace StrokeEffects
