#pragma once

#include "behaviors.h"

#include "../data/point.h"

#include <string>
#include <vector>

namespace fla {

class Shape;

/// Shape operators: a drawn shape's lines and fills changed each frame,
/// as behaviors change an instance. Trim Paths draws only part of the
/// strokes (a line that draws itself on), Wiggle Path shakes the outlines,
/// Zig-zag puts teeth along them. Each has settings that can be keyed, kept
/// with the shape in its phoenixShapeOperators attribute, which other apps
/// that open the FLA ignore, showing the shape as drawn. Built in, so the
/// stage draws them at speed. Free of Qt.
namespace shapeops {

/// The attribute a shape keeps its operators in.
extern const char* const kAttribute;

/// The operators there are, with their settings, for the Behaviors panel.
const std::vector<behaviors::Definition>& definitions();
const behaviors::Definition* find(const std::string& id);
/// An operator of \a id with its settings at their defaults.
behaviors::Applied defaults(const std::string& id, int seed);

/// The operators on \a shape, and whether any is on.
std::vector<behaviors::Applied> of(const Shape& shape);
bool any(const Shape& shape);

/// A line through points, open or closed: one subpath of a stroke or of a
/// fill's outline.
struct Line
{
    std::vector<Point> points;
    bool closed = false;
};

/// \a lines as \a list leaves them on \a frame (of the shape's timeline, at
/// \a frameRate). \a stroke says whether they are a stroke's (Trim Paths
/// trims strokes only) or a fill's outlines.
std::vector<Line> apply(const std::vector<Line>& lines, bool stroke, const std::vector<behaviors::Applied>& list,
    int frame, double frameRate);

} // namespace shapeops

} // namespace fla
