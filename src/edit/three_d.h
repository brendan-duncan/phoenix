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

/// The document's light, Phoenix's own (phoenixLight): when on, each clip
/// turned in 3D is shaded by how squarely it faces the light, so a card
/// turning away darkens. The light comes from \a azimuth (degrees round
/// from above: 90 from the right, -90 from the left) and \a elevation
/// (degrees out toward the viewer: 90 straight from the front, 0 grazing).
/// \a ambient is the light everywhere (0 to 1), \a intensity the light's
/// own strength, and \a color its color.
struct Light
{
    bool on = false;
    double azimuth = -45.0;
    double elevation = 45.0;
    double ambient = 0.3;
    double intensity = 1.0;
    unsigned char color[3] = {255, 255, 255};
};
Light lightOf(const Document& document);
std::vector<std::pair<std::string, std::string>> withLight(const Document& document, const Light& light);
/// How brightly \a light lights a clip turned by \a pose: 0 (dark) to 1.
/// Either face of a card catches the light.
double shading(const Pose& pose, const Light& light);

/// Depth of field, Phoenix's own (phoenixDepthOfField): when on, clips
/// moved in 3D blur the further they are from the \a focus depth (their
/// Z), by \a aperture pixels of blur for each hundred pixels away.
struct DepthOfField
{
    bool on = false;
    double focus = 0.0;
    double aperture = 4.0;
};
DepthOfField depthOfFieldOf(const Document& document);
std::vector<std::pair<std::string, std::string>> withDepthOfField(const Document& document, const DepthOfField& depth);
/// The blur, in pixels, for a clip at depth \a z.
double blurAt(const DepthOfField& depth, double z);

} // namespace threed

} // namespace fla
