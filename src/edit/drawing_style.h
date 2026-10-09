#pragma once

#include <cstdint>
#include <string>

namespace fla {

class DOMElement;
class FillStyle;
class StrokeStyle;

/// The fill and stroke a new object is drawn with, plus the settings the shape
/// tools need.
///
/// One instance lives for the session and is what the properties panel edits, so
/// the next rectangle picks up whatever the last one was given.
///
/// Free of Qt: it holds document colours, not GUI ones.
class DrawingStyle
{
public:
    bool hasFill = true;
    uint8_t fillColor[4] = {0, 153, 255, 255};

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

    /// Brush and eraser widths, in pixels.
    double brushSize = 12.0;
    double eraserSize = 12.0;

    /// Animate's brush modes: where a stroke of the brush lands.
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

    /// The eraser's faucet: a click removes the fill or the stroke it lands
    /// on, whole, instead of a drag taking a band out of it.
    bool faucet = false;

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

    /// PolyStar settings. Sides is the point count in star mode.
    int sides = 5;
    bool star = false;

    /// How far in the inner points of a star sit, as a fraction of the radius.
    double starInnerRatio = 0.5;

    /// The face and size new text is set in. The face is written the way
    /// Animate names fonts, the family run together without spaces; the size
    /// is in pixels. Text takes the fill colour.
    std::string textFace = "ArialMT";
    double textSize = 12.0;

    /// Builds the fill for a new object. Returns null when fills are switched
    /// off. The caller owns the result.
    FillStyle* createFill(DOMElement* parent) const;

    /// Builds the stroke for a new object, or null when strokes are switched
    /// off. The caller owns the result.
    StrokeStyle* createStroke(DOMElement* parent) const;
};

} // namespace fla
