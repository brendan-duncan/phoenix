#pragma once

#include "../data/point.h"

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
};

/// The settings of one brush, as its Properties panel sets them. The three
/// brushes share them and differ in which they show and their defaults:
///
/// - The **Flow Brush** (Animate's Fluid Brush) varies the width with
///   pressure and speed, tapers the ends, smooths the line and has an
///   elliptical tip with a roundness and an angle.
/// - The **Nib Brush** (Animate's Classic Brush, Flash's brush) has a shaped
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
        Rectangle
    };

    /// How the speed of the stroke changes its width (Flow Brush).
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

    /// Animate's smoothing, 0 to 100: how much the wobble of the hand is
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
};

namespace BrushStroke {

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

/// The polygons whose union is the painted stroke: the hull of each pair of
/// neighboring dabs, so the tip is swept between them, or the one dab of a
/// tap. All wind the same way.
std::vector<std::vector<Point>> sweep(const std::vector<BrushDab>& dabs, double zoom);

} // namespace BrushStroke

} // namespace fla
