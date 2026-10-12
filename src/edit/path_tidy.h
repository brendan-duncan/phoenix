#pragma once

#include "editable_path.h"

#include "../data/point.h"

#include <functional>
#include <vector>

namespace fla {

/// Animate's Modify > Shape commands on one path, free of Qt.

/// Smooth: every anchor's handles are pulled towards the Catmull-Rom
/// tangent through its neighbors (a third of the way to each neighbor,
/// along the line from the previous to the next), by \a amount from 0 (no
/// change) to 1 (all the way), and the anchor is made smooth. The ends of an
/// open path keep their place but gain a handle along the chord. With
/// \a which, only the anchors whose corner it accepts are smoothed: the
/// angle between the lines to the neighbors, in degrees, 180 for a
/// straight run (Advanced Smooth's angle limits).
void smoothPath(EditablePath& path, double amount, const std::function<bool(double)>& which = {});

/// Straighten: the path is sampled and reduced to straight segments by
/// Ramer-Douglas-Peucker within \a tolerance, so gentle curves become a few
/// lines and corners stay where they are.
void straightenPath(EditablePath& path, double tolerance);

/// Optimize: the path is sampled and fitted again with the pencil's fitter
/// within \a tolerance, which drops anchors a curve does not need.
void optimizePath(EditablePath& path, double tolerance);

/// Points along the path about \a spacing apart, the ends included; a
/// closed path's samples come back round to the start.
std::vector<Point> samplePath(const EditablePath& path, double spacing);

} // namespace fla
