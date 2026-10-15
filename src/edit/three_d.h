#pragma once

#include "../data/point.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;
class SymbolInstance;

/// A movie clip turned and moved in 3D, as the 3D Rotation and 3D
/// Translation tools leave it, and the perspective the stage sees it in.
/// Kept on the instance for Phoenix (phoenixRotationX, phoenixRotationY,
/// phoenixZ) beside its 2D matrix, which stays as other apps read it. Free of
/// Qt.
namespace threed {

struct Pose
{
    /// Turns about the clip's horizontal and vertical axes, in degrees.
    double rotationX = 0.0;
    double rotationY = 0.0;
    /// How far into the stage, in pixels: away from the viewer for positive.
    double z = 0.0;

    bool flat() const { return rotationX == 0.0 && rotationY == 0.0 && z == 0.0; }
};

Pose of(const SymbolInstance& instance);

/// The instance's attributes with \a pose in them (none for a flat one).
std::vector<std::pair<std::string, std::string>> withPose(const SymbolInstance& instance, const Pose& pose);

/// Part way from \a a to \a b.
Pose blend(const Pose& a, const Pose& b, double t);

/// The document's perspective angle, in degrees: how wide the view of the
/// stage is (55 unless set).
double perspectiveAngle(const Document& document);

/// The document's attributes with its perspective angle set to \a angle
/// (1 to 179 degrees); 55, the default, is written as no attribute.
std::vector<std::pair<std::string, std::string>> withPerspectiveAngle(const Document& document, double angle);

/// How far the viewer is from the stage for that angle across \a width.
double focalLength(double angle, double width);

/// The map, in the space the clip is placed in, that turns the clip about
/// \a center by \a pose and draws it in perspective as seen from straight
/// in front of \a vanishing at \a focal: a 3x3 matrix, row-major, taking
/// (x, y, 1) to (X, Y, W) for the point (X / W, Y / W).
std::array<double, 9> projection(const Pose& pose, const Point& center, const Point& vanishing, double focal);

Point project(const std::array<double, 9>& map, const Point& point);

} // namespace threed

} // namespace fla
