#pragma once

#include "../data/point.h"

#include <vector>

namespace fla {

/// Where a classic tween has got to, as a fraction of the way from one
/// keyframe to the next, after its easing.
///
/// \a acceleration is the keyframe's Ease value, -100 to 100: negative eases
/// in (a slow start), positive eases out (a slow finish), by blending the
/// straight progress with a quadratic. A custom ease, when there is one, is
/// a chain of cubic beziers from (0,0) to (1,1) as the Custom Ease dialog
/// draws it (four points, then three more per segment), read at the
/// progress along x.
double easeProgress(double progress, int acceleration, const std::vector<Point>& customEase);

} // namespace fla
