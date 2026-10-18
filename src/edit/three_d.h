#pragma once

#include "../data/point.h"
#include "../data/transform.h"

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
/// \a depth pushes the clip that far back along its own facing before it
/// is turned: where its extruded back is.
std::array<double, 9> projection(const Pose& pose, const Point& center, const Point& vanishing, double focal,
    double depth = 0.0);

Point project(const std::array<double, 9>& map, const Point& point);

/// The focal length of the document's own perspective, as FLA files keep
/// it: the view angle (viewAngle3D) across the stage's width, 55 degrees
/// when it has none.
double documentFocalLength(const Document& document);

/// The document's vanishing point (vanishingPoint3DX/Y), the stage's middle
/// when it has none.
Point vanishingPoint(const Document& document);

/// How an object motion tween in 3D draws its clip, as the FLA's 3D
/// motion has it: the clip placed by \a placed (its 2D matrix, from its own
/// space to its parent's), turned about its transformation point \a pivot
/// (in its own space; \a pivotZ pixels behind it) by \a rotationX, then
/// \a rotationY (degrees), then its 2D turn, the rest of its 2D matrix
/// (scale and skew) applied first; pushed \a z into the stage, and seen
/// from \a focal in front of \a vanishing. A 3x3 matrix, row-major, taking
/// the clip's own (x, y, 1) to (X, Y, W) for the point (X / W, Y / W).
std::array<double, 9> motionProjection(const Transform& placed, const Point& pivot, double pivotZ, double rotationX,
    double rotationY, double z, const Point& vanishing, double focal);

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

/// Several lights: phoenixLight holds each, one after another, separated by
/// ";" (the first as lightOf reads it). Those that are on, in order; none
/// when the document has no light.
std::vector<Light> lightsOf(const Document& document);
/// The document's attributes with \a lights in them (those that are on).
std::vector<std::pair<std::string, std::string>> withLights(const Document& document, const std::vector<Light>& lights);
/// What \a lights make of a clip turned by \a pose, a factor for each of
/// red, green and blue (0 to 1) in \a rgb: each light's shading in its
/// color, added up. With one light, its shading times its color. With
/// \a sides, for the clip's extruded edges, which face across the card and
/// so catch the light the face does not.
void shade(const Pose& pose, const std::vector<Light>& lights, double rgb[3], bool sides = false);

/// Shadows, Phoenix's own (phoenixShadows): when on, with the lights on,
/// a movie clip in front of a turned one (nearer the viewer, by Z) casts a
/// soft shadow on it, away from each light. \a strength is how dark (0 to
/// 1), \a softness the blur, in pixels for each hundred pixels between the
/// two.
struct Shadows
{
    bool on = false;
    double strength = 0.5;
    double softness = 6.0;
};
Shadows shadowsOf(const Document& document);
std::vector<std::pair<std::string, std::string>> withShadows(const Document& document, const Shadows& shadows);
/// Where \a light casts the shadow of a clip \a depth pixels in front of
/// what it falls on, from where the clip is, in the stage's directions;
/// false when the light is behind (it lights only the backs).
bool shadowOffset(const Light& light, double depth, Point& offset);
/// The blur of a shadow cast across \a depth pixels.
double shadowBlur(const Shadows& shadows, double depth);

/// Extrusion, Phoenix's own (phoenixExtrude, on a movie clip instance):
/// how deep the clip is, in pixels, drawn as its edges carried back that
/// far behind its face, shaded by the lights. 0 for a flat card.
double extrusionOf(const SymbolInstance& instance);
std::vector<std::pair<std::string, std::string>> withExtrusion(const SymbolInstance& instance, double depth);

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
