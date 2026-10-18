#pragma once

#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <utility>

namespace fla {

/// Shift and Trace: an onion skin ghost moved, turned and scaled on the
/// stage, to trace over it where the new drawing goes, while its frame
/// stays exactly as it is. The shifts belong to the view, never the
/// document: nothing here touches a document, and nothing is saved. Free of
/// Qt.
namespace onion {

struct Shift
{
    /// How far the ghost is moved, in the stage's pixels.
    double dx = 0.0;
    double dy = 0.0;
    /// How far it is turned, in degrees clockwise, and how big it is drawn,
    /// both about \a pivot.
    double rotation = 0.0;
    double scale = 1.0;
    /// The middle of the ghost as drawn, in the stage's space.
    Point pivot;

    /// Whether it is anywhere but where its frame has it.
    bool moved() const { return dx != 0.0 || dy != 0.0 || rotation != 0.0 || scale != 1.0; }
};

/// The map, in the stage's space, that draws a ghost shifted: turned and
/// scaled about its pivot, then moved.
Transform matrix(const Shift& shift);

/// The shifts of the ghosts of one view, by timeline (any key: the view's
/// own pointer to it) and frame.
class Shifts
{
public:
    /// The frame's shift, or null where it is not shifted.
    const Shift* find(const void* timeline, int frame) const;
    /// The frame's shift, made about \a pivot when it has none.
    Shift& at(const void* timeline, int frame, const Point& pivot);
    /// Every ghost back where its frame has it.
    void reset() { _shifts.clear(); }
    bool empty() const { return _shifts.empty(); }
    size_t size() const { return _shifts.size(); }

private:
    std::map<std::pair<const void*, int>, Shift> _shifts;
};

} // namespace onion

} // namespace fla
