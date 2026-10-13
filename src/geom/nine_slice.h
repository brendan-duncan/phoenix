#pragma once

#include "../data/rect.h"
#include "../data/transform.h"

#include <vector>

namespace fla {

/// 9-slice scaling: a movie clip with scale guides draws as nine cells, the
/// corners at their own size, the edges stretched one way and the middle both.
///
/// The instance's matrix is taken as a stretch along the symbol's own axes
/// followed by whatever turns or slants it: the stretch along x is the length
/// the matrix gives the symbol's x axis, and the same for y. The cells are cut
/// for that stretch, in the symbol's space, and the whole matrix then applied,
/// so a turned or skewed instance keeps its corners as they are in the symbol
/// along their own edges. Flash Player instead drops the guides once an
/// instance is turned and stretches it whole; that makes a panel's corners
/// jump in size the moment it turns by any amount, which a tween through a
/// small turn shows plainly, so the guides are kept here for every matrix.
namespace nineslice {

/// The stretch a matrix gives the symbol's x and y axes.
struct AxisScale
{
    double x = 1.0;
    double y = 1.0;
};

AxisScale axisScale(const Transform& matrix);

/// Whether an instance placed with this matrix draws sliced: it must stretch
/// the symbol at all and not collapse it.
bool stretches(const Transform& matrix);

/// One of the nine cells: the part of the symbol, \a source, and where it is
/// drawn, \a target, both in the symbol's space before the instance's matrix.
struct Cell
{
    Rect source;
    Rect target;
};

/// The cells of a symbol with art over \a bounds and guides at \a left,
/// \a top, \a right and \a bottom, placed with the stretch \a scale. A corner
/// \a w wide is cut to \a w / scale.x so the instance's matrix brings it back
/// to \a w. Cells with nothing in them, or with no room left for them, are
/// left out.
std::vector<Cell> cells(const Rect& bounds, double left, double top, double right, double bottom,
    const AxisScale& scale);

} // namespace nineslice

} // namespace fla
