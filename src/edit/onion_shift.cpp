#include "onion_shift.h"

#include <cmath>

namespace fla {

namespace onion {

Transform matrix(const Shift& shift)
{
    const double turn = shift.rotation * 3.14159265358979323846 / 180.0;
    const double c = std::cos(turn) * shift.scale;
    const double s = std::sin(turn) * shift.scale;
    // About the pivot, then moved: p' = R S (p - pivot) + pivot + d, with
    // the matrix's x' = a x + c y + tx and y' = b x + d y + ty.
    const double px = shift.pivot.x, py = shift.pivot.y;
    return Transform(c, s, -s, c, px - (c * px - s * py) + shift.dx, py - (s * px + c * py) + shift.dy);
}

const Shift* Shifts::find(const void* timeline, int frame) const
{
    const auto found = _shifts.find({timeline, frame});
    return found == _shifts.end() ? nullptr : &found->second;
}

Shift& Shifts::at(const void* timeline, int frame, const Point& pivot)
{
    const auto found = _shifts.find({timeline, frame});
    if (found != _shifts.end())
        return found->second;
    Shift& shift = _shifts[{timeline, frame}];
    shift.pivot = pivot;
    return shift;
}

} // namespace onion

} // namespace fla
