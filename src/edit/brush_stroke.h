#pragma once

#include "../data/point.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fla {

/// One reading of the pen (or mouse) during a brush stroke, in document
/// units, with what a tablet adds: pressure 0 to 1, tilt in degrees along
/// each axis (-60 to 60, 0 upright) and the time in milliseconds.
struct BrushSample
{
    double x = 0.0;
    double y = 0.0;
    double pressure = 1.0;
    double tiltX = 0.0;
    double tiltY = 0.0;
    double time = 0.0;
    /// The barrel's turn in degrees, from pens that read it (an art pen).
    double rotation = 0.0;
};

/// The settings of one brush, as its Properties panel sets them. The three
/// brushes share them and differ in which they show and their defaults:
///
/// - The **Brush** varies the width with
///   pressure and speed, tapers the ends, smooths the line and has an
///   elliptical tip with a roundness and an angle.
/// - The **Nib Brush** has a shaped
///   nib, with Use Pressure for the width and Use Tilt turning the nib with
///   the pen.
/// - Phoenix's **Ink Brush** adds what the others lack: a stabilizer that
///   trails the pen on a string for steady lines, a pressure response curve,
///   and tilt that broadens the line as a tilted pencil does.
struct BrushSettings
{
    enum class Tip
    {
        /// An ellipse, round at 100% roundness.
        Ellipse,
        /// A rectangle, square at 100% roundness.
        Rectangle,
        /// The tip library's other shapes, each squeezed by the roundness.
        Triangle,
        Diamond,
        Hexagon,
        Star,
        /// An outline of the user's own (customTip), traced from an image
        /// or kept from a preset.
        Custom
    };

    /// What turns the tip as the stroke goes.
    enum class AngleFunction
    {
        /// It keeps its angle.
        Constant,
        /// It follows the stroke's direction, as a calligraphy pen held to
        /// the line.
        Heading,
        /// It follows the way the pen leans.
        Azimuth,
        /// It follows the turn of the pen's barrel (an art pen).
        Twist
    };

    /// What the start and end tapers are measured in.
    enum class TaperUnits
    {
        Percent,
        Pixels
    };

    /// How the speed of the stroke changes its width (Brush).
    enum class Velocity
    {
        None,
        /// Faster is thicker.
        ThinToThick,
        /// Faster is thinner.
        ThickToThin
    };

    /// What the pen's tilt does, when it is used.
    enum class TiltEffect
    {
        /// Turns the tip to the direction the pen leans (Nib Brush).
        Angle,
        /// Broadens the line the further the pen leans (Ink Brush).
        Width
    };

    /// The width of the tip at full pressure, in pixels.
    double size = 12.0;
    Tip tip = Tip::Ellipse;
    /// The tip's height as a percentage of its width.
    double roundness = 100.0;
    /// The tip's turn, in degrees, clockwise on the stage.
    double angle = 0.0;

    bool usePressure = true;
    /// The width at the lightest touch, as a percentage of the size.
    double minSize = 20.0;
    /// The pressure response: below 1 a light touch gives more width (soft),
    /// above 1 it takes a firmer press to get width (firm).
    double pressureCurve = 1.0;

    bool useTilt = false;
    TiltEffect tiltEffect = TiltEffect::Angle;

    Velocity velocity = Velocity::None;

    /// How much of the stroke the start and end taper over, as percentages
    /// of its length.
    double taperStart = 0.0;
    double taperEnd = 0.0;

    /// The smoothing, 0 to 100: how much the wobble of the hand is
    /// taken out of the line. It works in screen pixels, so it feels the
    /// same at any zoom.
    int smoothing = 50;

    /// How closely the finished outline follows the painted one, 0 to 100.
    /// Lower leaves fewer, smoother curves.
    int precision = 50;

    /// The stabilizer's string, in screen pixels: the line follows the pen
    /// only once the pen is this far ahead, which takes out shake entirely.
    /// 0 is off.
    double stabilizer = 0.0;

    /// Harder pressure gives a thinner line, and a steeper lean a thinner
    /// one (for tilt that broadens).
    bool invertPressure = false;
    bool invertTilt = false;
    /// How quickly speed has its full effect on the width, 1 to 100: higher
    /// reaches it at a slower stroke.
    double velocitySensitivity = 50.0;
    TaperUnits taperUnits = TaperUnits::Percent;
    /// The bitmap brush's paint fades in over this many pixels from the
    /// start of a stroke, and out over the last ones.
    double fadeStart = 0.0;
    double fadeEnd = 0.0;
    /// The tip keeps its size on screen under a camera zoom: zoomed in, it
    /// paints a finer line on the art.
    bool sizeFollowsCamera = false;
    AngleFunction angleFunction = AngleFunction::Constant;
    /// How much a lean stretches the tip along its width, as a percentage
    /// at the steepest lean.
    double tiltStretch = 0.0;
    /// The Custom tip's outline, a polygon one unit wide centered on the
    /// origin; and where it came from (a library bitmap's name), for show.
    std::vector<Point> customTip;
    std::string tipSource;

    /// The angle function in effect: Use Tilt with the Angle effect is the
    /// Azimuth function, as the Nib Brush always had it.
    AngleFunction effectiveAngleFunction() const;

    static BrushSettings fluidDefaults();
    static BrushSettings classicDefaults();
    static BrushSettings inkDefaults();
};

/// One imprint of the tip along the stroke: its center, its full width and
/// height, and its turn in radians, clockwise on the stage.
struct BrushDab
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double angle = 0.0;
    bool rectangular = false;
    /// For the library's shapes and custom tips: the outline, one unit wide
    /// and centered, which the dab scales to its width and height; empty
    /// for an ellipse or a rectangle.
    std::shared_ptr<const std::vector<Point>> shape;
    /// The pen's pressure there, 0 to 1, and how much of the paint shows
    /// there for a fade, 0 to 1.
    double pressure = 1.0;
    double opacity = 1.0;
};

/// The outline of one of the tip library's shapes, one unit wide and
/// centered on the origin; empty for an ellipse or a rectangle, which are
/// drawn as such, and the settings' own for a custom tip.
std::vector<Point> tipPolygon(const BrushSettings& settings);

/// Whether a polygon is convex, which lets a stroke be swept from dab to
/// dab rather than stamped.
bool isConvex(const std::vector<Point>& polygon);

/// A tip's outline traced from an image's coverage (\a alpha, \a width by
/// \a height, 0 to 255 a pixel): the largest area covered, as a polygon
/// one unit wide centered on the origin, simplified. Empty when nothing is
/// covered.
std::vector<Point> traceTip(const std::vector<uint8_t>& alpha, int width, int height);

/// The settings as text, one "key=value" a line, and back. Keys not given
/// keep the value \a into has; ones not known are skipped, so presets from
/// later versions still load.
std::string toText(const BrushSettings& settings);
void fromText(const std::string& text, BrushSettings& into);

namespace BrushStroke {

/// The stabilizer on its own: the line pulled along behind the pen on a
/// string \a length long (in the samples' units), moving only once the pen
/// is further away than that. With \a finished the line runs on to where
/// the pen lifted. The tools without a brush's settings use it as it is.
std::vector<BrushSample> stabilized(const std::vector<BrushSample>& samples, double length, bool finished);

/// The dabs a stroke leaves: the samples stabilized and smoothed, their
/// pressure and tilt filtered, spaced out evenly along the line, each with
/// the tip's size and turn there. \a zoom is the view's, so the settings
/// that work in screen pixels do; \a finished says the pen has lifted,
/// which lets the stabilizer catch up to it.
std::vector<BrushDab> dabs(const std::vector<BrushSample>& samples, const BrushSettings& settings,
    double zoom, bool finished);

/// The outline of one dab, a convex polygon, with enough points to look
/// smooth at \a zoom.
std::vector<Point> dabOutline(const BrushDab& dab, double zoom);

/// The convex hull of \a points. Every hull winds the same way: with a
/// positive signed area in the stage's coordinates, which run y down.
std::vector<Point> convexHull(std::vector<Point> points);

/// \a polygon cut into triangles that cover it exactly, whichever way it
/// winds; a polygon that crosses itself is cut as far as it can be.
std::vector<std::vector<Point>> triangulate(const std::vector<Point>& polygon);

/// The polygons whose union is the painted stroke: the hull of each pair of
/// neighboring dabs, so the tip is swept between them, or the one dab of a
/// tap. All wind the same way.
std::vector<std::vector<Point>> sweep(const std::vector<BrushDab>& dabs, double zoom);

} // namespace BrushStroke

} // namespace fla
