#pragma once

#include "behaviors.h"

#include "../data/point.h"

#include <string>
#include <vector>

namespace fla {

class FillStyle;
class Shape;
class StrokeStyle;

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

/// A stroke's width along its lines, as the last Taper operator on the
/// list sets it on a frame: from Start to End of the stroke's weight (in
/// percent) along each line, and a wave travelling down it. Off when no
/// Taper is on.
struct Taper
{
    bool on = false;
    double start = 100.0;
    double end = 100.0;
    /// The wave: how far it swells and thins (percent of the weight), how
    /// long one wave is (pixels), and how many go by a second.
    double wave = 0.0;
    double wavelength = 40.0;
    double speed = 0.0;
};
Taper taperOf(const std::vector<behaviors::Applied>& list, int frame);

/// How the last Merge Paths operator on the list combines a shape's
/// outlines on a frame: "Add", "Subtract", "Intersect" or "Exclude"; empty
/// when there is none. The combining is the drawing's to do.
std::string mergeOf(const std::vector<behaviors::Applied>& list, int frame);

/// The outline of \a line stroked \a weight wide as \a taper says, at
/// \a seconds into the movie: a closed polygon to fill.
std::vector<Point> taperedOutline(const Line& line, double weight, const Taper& taper, double seconds);

/// Baking, for apps without shape operators: \a outlines, closed rings
/// that make one area by nesting (a hole inside an outline), as a drawing
/// object filled with a copy of \a paint, a gradient kept as it is placed.
/// A tapered stroke bakes this way, its outline filled with the stroke's
/// own fill. Null when nothing is left to draw.
Shape* bakedFill(const std::vector<Line>& outlines, const FillStyle& paint);
/// \a lines (each dash its own) as a drawing object stroked with a copy of
/// \a stroke: its width, caps, joins and fill, a gradient along the stroke
/// kept as the stroke's own. Null when nothing is left to draw.
Shape* bakedStroke(const std::vector<Line>& lines, const StrokeStyle& stroke);

/// \a lines as \a list leaves them on \a frame (of the shape's timeline, at
/// \a frameRate). \a stroke says whether they are a stroke's (Trim Paths
/// trims strokes only) or a fill's outlines.
std::vector<Line> apply(const std::vector<Line>& lines, bool stroke, const std::vector<behaviors::Applied>& list,
    int frame, double frameRate);

} // namespace shapeops

} // namespace fla
