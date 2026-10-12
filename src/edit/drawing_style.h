#pragma once

#include "brush_stroke.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class DOMElement;
class Element;
class FillStyle;
class Shape;
class StrokeStyle;

/// The fill and stroke a new object is drawn with, plus the settings the shape
/// tools need.
///
/// One instance lives for the session and is what the properties panel edits, so
/// the next rectangle picks up whatever the last one was given.
///
/// Free of Qt: it holds document colors, not GUI ones.
class DrawingStyle
{
public:
    bool hasFill = true;
    uint8_t fillColor[4] = {0, 153, 255, 255};

    /// What new art is filled with, as the Color panel sets it: the fill
    /// color, a gradient or a library bitmap.
    enum class FillKind
    {
        Solid,
        Linear,
        Radial,
        Bitmap
    };
    FillKind fillKind = FillKind::Solid;

    /// A gradient stop: where along the gradient, 0 to 1, and its color.
    struct Stop
    {
        double ratio = 0.0;
        uint8_t color[4] = {0, 0, 0, 255};
    };
    /// The gradient's stops, in order; Animate starts one at white to black.
    std::vector<Stop> gradient = defaultGradient();
    /// How the colors go on past the ends: "" (extend), "reflect" or
    /// "repeat", and whether they blend in linear RGB.
    std::string gradientSpread;
    bool gradientLinearRGB = false;
    /// The library bitmap a bitmap fill tiles.
    std::string fillBitmap;

    static std::vector<Stop> defaultGradient();

    bool hasStroke = true;
    uint8_t strokeColor[4] = {0, 0, 0, 255};
    double strokeWeight = 1.0;

    /// Whether a new shape stays a separate object instead of merging into what
    /// is already there.
    ///
    /// Animate calls this Object Drawing Mode and leaves it off, so drawing
    /// merges by default: outlines cut each other and the newer fill replaces
    /// the older. Turning it on keeps each drawing its own object.
    bool objectDrawing = false;

    /// How the pencil tidies a freehand stroke.
    enum class PencilMode
    {
        /// Fit smooth curves through the stroke.
        Smooth,
        /// Fit straight runs, keeping the corners.
        Straighten,
        /// Keep the points as drawn.
        Ink
    };

    PencilMode pencilMode = PencilMode::Smooth;

    /// Animate's smoothing amount, 0 to 100. It is applied in screen pixels,
    /// so a stroke drawn zoomed in is not smoothed any harder than one drawn
    /// zoomed out.
    int pencilSmoothing = 50;

    /// The eraser's width, in pixels.
    double eraserSize = 12.0;

    /// Each brush's own settings: Animate's Fluid Brush and Classic Brush,
    /// and Phoenix's Ink Brush. Pressure is on in all three; a mouse always
    /// paints at full width.
    BrushSettings fluidBrush = BrushSettings::fluidDefaults();
    BrushSettings classicBrush = BrushSettings::classicDefaults();
    BrushSettings inkBrush = BrushSettings::inkDefaults();

    /// The pencil's Straighten mode settings: join the ends of a stroke that
    /// nearly meet, and replace a stroke that is clearly a rectangle, oval or
    /// triangle with the ideal one.
    bool connectLines = true;
    bool recognizeShapes = true;

    /// Animate's brush modes: where a stroke of any of the brushes lands.
    enum class BrushMode
    {
        /// Over everything, burying lines and fills it covers.
        Normal,
        /// Over fills and empty stage, under the lines.
        Fills,
        /// Only on empty stage, behind everything already there.
        Behind,
        /// Only within the selected fills.
        Selection,
        /// Only within the fill the stroke began in.
        Inside
    };
    BrushMode brushMode = BrushMode::Normal;

    /// Animate's eraser modes: what a pass of the eraser takes away.
    enum class EraserMode
    {
        /// Lines and fills alike.
        Normal,
        /// Fills only, leaving the lines.
        Fills,
        /// Lines only, leaving the fills.
        Lines,
        /// Only the selected fills.
        SelectedFills,
        /// Only the fill the pass began in.
        Inside
    };
    EraserMode eraserMode = EraserMode::Normal;

    /// The Magic Wand's settings: how far a color may be from the clicked
    /// pixel's, 0 to 200 on each channel, and how its area's outline is
    /// tidied (0 Pixels, 1 Rough, 2 Normal, 3 Smooth), as Animate's are.
    int wandThreshold = 10;
    int wandSmoothing = 2;

    /// The eraser's faucet: a click removes the fill or the stroke it lands
    /// on, whole, instead of a drag taking a band out of it.
    bool faucet = false;

    /// Which of the free transform tool's modes is on: plain, Distort (a
    /// corner drags on its own) or Envelope (the box's sides bend).
    enum class TransformMode
    {
        Free,
        Distort,
        Envelope
    };
    TransformMode transformMode = TransformMode::Free;

    /// How far apart the ends of an outline may be for the paint bucket to
    /// treat it as closed: Animate's gap size setting.
    enum class GapSize
    {
        None,
        Small,
        Medium,
        Large
    };
    GapSize gapSize = GapSize::Small;

    /// Corner radius for the rectangle tools, in pixels. The Up and Down
    /// arrows change it while a rectangle is being dragged out, as in Animate.
    double cornerRadius = 0.0;

    /// Oval primitive settings: the arc from the start angle clockwise to
    /// the end angle (equal angles mean the whole oval), in degrees, and the
    /// inner radius as a percentage of the outer.
    double ovalStartAngle = 0.0;
    double ovalEndAngle = 0.0;
    double ovalInnerRadius = 0.0;

    /// PolyStar settings. Sides is the point count in star mode.
    int sides = 5;
    bool star = false;

    /// How far in the inner points of a star sit, as a fraction of the radius.
    double starInnerRatio = 0.5;

    /// The face and size new text is set in. The face is written the way
    /// Animate names fonts, the family run together without spaces; the size
    /// is in pixels. Text takes the fill color.
    std::string textFace = "ArialMT";
    double textSize = 12.0;

    /// The kind of text field new text makes: 0 static, 1 dynamic, 2 input,
    /// in the order of StaticText::TextType.
    int textType = 0;

    /// Builds the fill for a new object. Returns null when fills are switched
    /// off. The caller owns the result. A gradient or a bitmap fill comes
    /// unplaced; fitFills puts it over the art once that has its shape.
    FillStyle* createFill(DOMElement* parent) const;

    /// Places a gradient or bitmap fill over the box \a left, \a top,
    /// \a right, \a bottom as Animate places a new one: a linear gradient
    /// across the width, a radial one centered and reaching the farther
    /// side, a bitmap at the top-left corner at its own size.
    static void fitFill(FillStyle& fill, double left, double top, double right, double bottom);

    /// Fits every unplaced gradient and bitmap fill of \a element (a shape,
    /// or a rectangle or oval primitive) to the art it fills.
    static void fitFills(Element& element);

    /// Builds the stroke for a new object, or null when strokes are switched
    /// off. The caller owns the result.
    StrokeStyle* createStroke(DOMElement* parent) const;
};

} // namespace fla
