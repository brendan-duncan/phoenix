#pragma once

#include "../data/point.h"
#include "../data/transform.h"

#include <string>
#include <vector>

namespace fla {

/// Drawing aids: the symmetry axis that copies freehand strokes across it,
/// and the drawing guides (rulers, ellipses and perspective) that freehand
/// strokes snap to. Both are Phoenix's own, kept with the document as text
/// (Document::symmetry and Document::drawingGuides) and never drawn into the
/// art or exported. Everything is in the stage's coordinates. Free of Qt.
namespace aids {

/// How the symmetry axis copies a stroke.
enum class SymmetryMode
{
    /// Mirrored left to right across an upright axis.
    Vertical,
    /// Mirrored top to bottom across a level axis.
    Horizontal,
    /// Both: four copies in all.
    Both,
    /// Turned about the center, so the stroke is drawn copies times in all.
    Radial
};

struct Symmetry
{
    bool on = false;
    SymmetryMode mode = SymmetryMode::Vertical;
    /// The center the axes cross at.
    double x = 0.0;
    double y = 0.0;
    /// How far the axes are turned, in degrees, clockwise on the stage.
    double angle = 0.0;
    /// Radial: how many times the stroke is drawn in all, 2 to 64.
    int copies = 6;
    /// Radial: each copy mirrored too, as a kaleidoscope does, which draws
    /// the stroke twice as many times.
    bool mirror = false;

    bool operator==(const Symmetry& other) const;
    bool operator!=(const Symmetry& other) const { return !(*this == other); }
};

/// The maps that make the copies of a stroke, the stroke itself (the
/// identity) left out; none while symmetry is off.
std::vector<Transform> symmetryCopies(const Symmetry& symmetry);

/// Whether \a transform mirrors, which turns a shape's winding round.
bool mirrors(const Transform& transform);

/// \a point mapped by \a transform.
Point apply(const Transform& transform, const Point& point);

std::string toText(const Symmetry& symmetry);
/// Reads toText()'s form; what it does not say keeps its default, and
/// empty text is symmetry off.
Symmetry symmetryFromText(const std::string& text);

/// The kinds of drawing guide.
enum class GuideKind
{
    /// A straight edge through a and b: a stroke begun near it runs along it.
    Ruler,
    /// Strokes run parallel to the line from a to b, wherever they begin.
    ParallelRuler,
    /// An ellipse centered on a, its first radius out to b and its second
    /// minor long: a stroke begun near it runs round it.
    Ellipse,
    /// Ellipses of the same shape and center, through wherever a stroke
    /// begins.
    ConcentricEllipse,
    /// Perspective with one vanishing point (a): strokes run toward it, or
    /// level, or upright.
    Perspective1,
    /// Two vanishing points on the horizon (a, b): strokes run toward
    /// either, or upright to the horizon.
    Perspective2,
    /// Three vanishing points (a, b, c): strokes run toward one of them.
    Perspective3
};

struct DrawingGuide
{
    GuideKind kind = GuideKind::Ruler;
    Point a;
    Point b;
    Point c;
    /// The ellipses' second radius.
    double minor = 50.0;
    /// Perspective: lines fanning out from the vanishing points are shown.
    bool grid = true;

    bool operator==(const DrawingGuide& other) const;
    bool operator!=(const DrawingGuide& other) const { return !(*this == other); }
};

struct DrawingGuides
{
    std::vector<DrawingGuide> guides;
    /// Strokes snap to the guides.
    bool snap = true;
    /// The guides are hidden, and strokes do not snap to them.
    bool hidden = false;
    /// How firmly a stroke keeps to its guide, 0 (not at all) to 100
    /// (exactly on it).
    int magnetism = 100;

    bool operator==(const DrawingGuides& other) const;
    bool operator!=(const DrawingGuides& other) const { return !(*this == other); }
};

/// The name of a kind, as the menus show it: "Ruler", "2-Point Perspective".
const char* kindName(GuideKind kind);

/// A guide of \a kind laid out around \a center, \a size across: what
/// adding one to the stage starts with.
DrawingGuide makeGuide(GuideKind kind, const Point& center, double size);

/// The guide moved by \a dx, \a dy: all its points.
DrawingGuide moved(const DrawingGuide& guide, double dx, double dy);

std::string toText(const DrawingGuides& guides);
DrawingGuides guidesFromText(const std::string& text);

/// What a stroke keeps to: a line through origin along direction, or an
/// ellipse.
struct Constraint
{
    enum class Kind
    {
        None,
        Line,
        Ellipse
    };
    Kind kind = Kind::None;
    /// The line: a point on it, and its direction, one unit long.
    Point origin;
    Point direction;
    /// The ellipse: its center, radii and turn in radians.
    Point center;
    double rx = 0.0;
    double ry = 0.0;
    double angle = 0.0;

    /// The nearest point on the line, or the point of the ellipse in the
    /// direction of \a point from its center; \a point itself for None.
    Point project(const Point& point) const;
};

/// What a stroke along \a stroke keeps to under \a guides: a ruler or an
/// ellipse it begins within reach of (the nearest), or else the direction
/// among the others that its first movement comes closest to. \a pixel is
/// the size of a screen pixel in stage units, for the reach and for how far
/// the stroke has to move to have a direction. None when nothing applies
/// yet.
Constraint constraintFor(const DrawingGuides& guides, const std::vector<Point>& stroke, double pixel);

/// \a stroke pulled onto its guide by the magnetism; as it is when the
/// guides are hidden, off or none apply.
std::vector<Point> snapStroke(const DrawingGuides& guides, const std::vector<Point>& stroke, double pixel);

/// For showing a perspective guide's grid: the lines through \a vanishing
/// point, \a count of them spread evenly round it, each \a length long.
std::vector<std::pair<Point, Point>> fanLines(const Point& vanishing, int count, double length);

} // namespace aids
} // namespace fla
