#pragma once

#include <vector>

namespace fla {

/// What a coordinate snapped to, so a view can show the right feedback and a
/// caller can tell "landed on something" from "left alone".
enum class SnapKind
{
    None,
    Grid,
    Object
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

/// Works out how far to nudge a drag so it lands on the grid or lines up with
/// another object.
///
/// A gesture hands over the coordinates that are moving -- for a box, its left,
/// centre and right on the x axis -- and gets back one adjustment for the whole
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

    /// Whether anything at all would snap. Lets a caller skip gathering
    /// candidates when both kinds are switched off.
    bool isEnabled() const { return _gridEnabled || _objectSnapEnabled; }

    void clearCandidates();

    /// Coordinates of things already on the stage that a drag can line up with.
    void addCandidateX(double x);

    void addCandidateY(double y);

    /// Finds the smallest adjustment that brings one of \a values onto a target.
    SnapResult snapX(const std::vector<double>& values) const;

    SnapResult snapY(const std::vector<double>& values) const;

private:
    SnapResult snap(const std::vector<double>& values, double gridSpacing,
        const std::vector<double>& candidates) const;

    double _tolerance = 4.0;

    bool _gridEnabled = false;
    double _gridSpacingX = 0.0;
    double _gridSpacingY = 0.0;

    bool _objectSnapEnabled = false;
    std::vector<double> _candidatesX;
    std::vector<double> _candidatesY;
};

} // namespace fla
