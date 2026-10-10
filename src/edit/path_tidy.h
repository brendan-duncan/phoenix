#pragma once

#include "editable_path.h"

#include "../data/point.h"

#include <vector>

namespace fla {

/// Animate's Modify > Shape commands on one path, free of Qt.

/// Smooth: every anchor's handles are pulled towards the Catmull-Rom
/// tangent through its neighbours (a third of the way to each neighbour,
/// along the line from the previous to the next), by \a amount from 0 (no
/// change) to 1 (all the way), and the anchor is made smooth. The ends of an
/// open path keep their place but gain a handle along the chord.
void smoothPath(EditablePath& path, double amount);

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
