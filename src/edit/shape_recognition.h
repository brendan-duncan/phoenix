#pragma once

#include "editable_path.h"

#include "../data/point.h"

#include <vector>

namespace fla {

/// What the pencil's Straighten mode does to a stroke after fitting it, the
/// way the Connect Lines and Recognize Shapes settings ask.
struct StrokeTidyOptions
{
    /// Close a stroke whose ends come within this distance of each other.
    bool connectLines = true;
    double closeTolerance = 10.0;

    /// Replace a closed stroke that reads as a rectangle or an oval with the
    /// ideal one. A triangle is already what its three straight sides make.
    bool recognizeShapes = true;
};

/// Tidies a straightened stroke in place. \a raw is the stroke as drawn,
/// which is what tells a roundish stroke from a many-sided one.
void tidyStraightenedStroke(EditablePath& path, const std::vector<Point>& raw,
    const StrokeTidyOptions& options);

} // namespace fla
