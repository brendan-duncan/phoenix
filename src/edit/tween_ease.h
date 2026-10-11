#pragma once

#include "../data/point.h"

#include <string>
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
/// progress along x.  method names one of Animate's ease presets
/// (quadIn, cubicInOut, bounceOut...), which takes the place of the Ease
/// value; an empty method, "none" or "classic" leaves it to the Ease value.
/// Back and elastic eases overshoot, so the result can leave 0..1.
double easeProgress(double progress, int acceleration, const std::vector<Point>& customEase,
    const std::string& method = std::string());

/// The ease preset  method at  t, or -1 when Phoenix does not know it.
double presetEase(const std::string& method, double t);

} // namespace fla
