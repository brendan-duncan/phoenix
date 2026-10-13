#pragma once

#include <vector>

namespace fla {

/// What a coordinate snapped to, so a view can show the right feedback and a
/// caller can tell "landed on something" from "left alone".
enum class SnapKind
{
    None,
    Grid,
    Object,
    Guide,
    Pixel
};

/// The adjustment to apply along one axis.
struct SnapResult
{
    /// Add this to every moving coordinate on the axis.
    double adjustment = 0.0;

    SnapKind kind = SnapKind::None;

    /// The coordinate that was snapped to, for drawing a guide line.
    double target = 0.0;

    bool snapped() const { return kind != SnapKind::None; }
};

/// A scale handle being dragged. Wherever the drag takes it, the handle sits
/// at anchor + scaleX * axisX + scaleY * axisY: the anchor is the point held
/// still (the opposite handle, or the center), and the axes are the handle's
/// offset from it along each side of the box, at the size the drag began.
struct HandleDrag
{
    double anchorX = 0.0;
    double anchorY = 0.0;
    double axisXx = 0.0;
    double axisXy = 0.0;
    double axisYx = 0.0;
    double axisYy = 0.0;

    /// The scales the pointer asks for.
    double scaleX = 1.0;
    double scaleY = 1.0;

    /// Which scales follow the pointer. An edge handle moves only one.
    bool scalesX = true;
    bool scalesY = true;

    /// Both axes take the same scale, as Shift keeps the proportions.
    bool uniform = false;
};

/// Where a handle drag should land instead.
struct HandleSnapResult
{
    double scaleX = 1.0;
    double scaleY = 1.0;

    SnapKind kind = SnapKind::None;

    /// Whether it landed on a point (a vertex, a grid crossing, or two lines
    /// at once) rather than a single line.
    bool onPoint = false;

    /// Where the handle lands.
    double x = 0.0;
    double y = 0.0;

    bool snapped() const { return kind != SnapKind::None; }
};

/// Works out how far to nudge a drag so it lands on the grid or lines up with
/// another object.
///
/// A gesture hands over the coordinates that are moving -- for a box, its left,
/// center and right on the x axis -- and gets back one adjustment for the whole
/// axis, so the shape does not distort just because one edge snapped.
///
/// Free of Qt, so it can be tested on its own.
class Snapper
{
public:
    Snapper() = default;

    /// How close, in document units, a coordinate has to be before it snaps.
    void setTolerance(double tolerance) { _tolerance = tolerance; }

    double tolerance() const { return _tolerance; }

    void setGridEnabled(bool enabled) { _gridEnabled = enabled; }

    bool gridEnabled() const { return _gridEnabled; }

    /// Grid spacing in document units. A non-positive spacing disables that axis.
    void setGridSpacing(double x, double y)
    {
        _gridSpacingX = x;
        _gridSpacingY = y;
    }

    void setObjectSnapEnabled(bool enabled) { _objectSnapEnabled = enabled; }

    bool objectSnapEnabled() const { return _objectSnapEnabled; }

    /// Ruler guides: vertical guides at \a xs, horizontal ones at \a ys.
    /// They stay until replaced; gathering candidates does not clear them.
    void setGuides(const std::vector<double>& xs, const std::vector<double>& ys)
    {
        _guidesX = xs;
        _guidesY = ys;
    }
    void setGuideSnapEnabled(bool enabled) { _guideSnapEnabled = enabled; }
    bool guideSnapEnabled() const { return _guideSnapEnabled; }

    /// Snap to Pixels: what snaps to nothing else lands on a whole pixel.
    void setPixelSnapEnabled(bool enabled) { _pixelSnapEnabled = enabled; }
    bool pixelSnapEnabled() const { return _pixelSnapEnabled; }

    /// Whether anything at all would snap. Lets a caller skip gathering
    /// candidates when every kind is switched off.
    bool isEnabled() const
    {
        return _gridEnabled || _objectSnapEnabled || _pixelSnapEnabled ||
            (_guideSnapEnabled && (!_guidesX.empty() || !_guidesY.empty()));
    }

    void clearCandidates();

    /// Coordinates of things already on the stage that a drag can line up with.
    void addCandidateX(double x);

    void addCandidateY(double y);

    /// A vertex of other artwork. Handle drags land on these in preference to
    /// lines.
    void addCandidatePoint(double x, double y);

    /// A straight piece of other artwork's outline, for handle drags.
    void addCandidateSegment(double x1, double y1, double x2, double y2);

    /// Finds the smallest adjustment that brings one of \a values onto a target.
    SnapResult snapX(const std::vector<double>& values) const;

    SnapResult snapY(const std::vector<double>& values) const;

    /// Snaps a scale handle. For each target the scale is solved directly so
    /// the handle lands exactly on it (as near as it can, for a point off the
    /// handle's path). A snap that would flip or collapse the box, or move the
    /// handle more than twice the tolerance, is refused, and a point beats a
    /// line. Returns the drag's own scales when nothing is in reach.
    HandleSnapResult snapHandle(const HandleDrag& drag) const;

private:
    struct CandidatePoint
    {
        double x;
        double y;
    };

    struct CandidateSegment
    {
        double x1;
        double y1;
        double x2;
        double y2;
    };

    SnapResult snap(const std::vector<double>& values, double gridSpacing,
        const std::vector<double>& candidates, const std::vector<double>& guides) const;

    double _tolerance = 4.0;

    bool _gridEnabled = false;
    double _gridSpacingX = 0.0;
    double _gridSpacingY = 0.0;

    bool _objectSnapEnabled = false;
    std::vector<double> _candidatesX;
    std::vector<double> _candidatesY;
    std::vector<CandidatePoint> _candidatePoints;
    std::vector<CandidateSegment> _candidateSegments;
    bool _guideSnapEnabled = true;
    bool _pixelSnapEnabled = false;
    std::vector<double> _guidesX;
    std::vector<double> _guidesY;
};

} // namespace fla
