#pragma once

#include "brush_stroke.h"

#include "../data/stroke_style.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fla {

class DOMElement;
class Element;
class FillStyle;
class Shape;

/// How a stroke looks apart from its color and weight: its kind (solid,
/// dashed, dotted, ragged, stipple, hatched), whether it is a hairline, its
/// caps and joins and how it scales, as Animate's stroke controls set them.
/// The empty strings are Animate's defaults (round caps and joins, normal
/// scaling).
struct StrokeLook
{
    StrokeStyle::Style kind = StrokeStyle::Style::Solid;
    bool hairline = false;
    std::string caps;
    std::string joints;
    std::string scaleMode;
    double miterLimit = 3.0;
    /// Pixel hinting, kept with the stroke for players that snap it.
    bool hinting = false;
    /// How the width varies along the stroke; empty for even.
    std::vector<StrokeStyle::WidthMarker> widthProfile;
    /// A graphic symbol drawn along the stroke instead of a line: stretched
    /// from end to end (an art brush), or repeated along it (a pattern
    /// brush). Empty for a plain stroke. Kept on the stroke for Phoenix
    /// (phoenixBrush, phoenixBrushPattern).
    std::string brush;
    bool brushPattern = false;
    /// The kind's own settings (Custom Stroke Style): a dashed stroke's
    /// dash and gap, a dotted one's spacing, and a ragged, stipple or
    /// hatched one's choices by attribute name, as the file spells them.
    double dash1 = 6.0;
    double dash2 = 6.0;
    double dotSpace = 3.0;
    std::map<std::string, std::string> styleAttributes;

    static StrokeLook of(const StrokeStyle& stroke);
    bool operator==(const StrokeLook& other) const;
    bool operator!=(const StrokeLook& other) const { return !(*this == other); }

    /// A copy of \a stroke with this look: a new object when the kind
    /// changes, keeping its color, weight and anything else it has. The
    /// caller owns it.
    StrokeStyle* applyTo(const StrokeStyle& stroke, DOMElement* parent) const;
};

/// A named width profile for the stroke settings' Width menu.
struct WidthPreset
{
    const char* name;
    std::vector<StrokeStyle::WidthMarker> markers;
};

/// Even first, then the tapers and swells; the Width tool makes others.
const std::vector<WidthPreset>& widthPresets();

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

    /// Lock Fill: a gradient or bitmap the bucket or a brush puts down is
    /// placed over the whole stage rather than fitted to each piece, so
    /// separate areas read as one sweep of color.
    bool lockFill = false;
    /// The bucket's Fill All Regions: a click paints every enclosed region
    /// of the shape that has the clicked region's fill.
    bool fillAllRegions = false;

    static std::vector<Stop> defaultGradient();

    bool hasStroke = true;
    uint8_t strokeColor[4] = {0, 0, 0, 255};
    double strokeWeight = 1.0;
    StrokeLook strokeLook;

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
    /// The eraser's nib, pressure and tilt, as a brush's (its size is
    /// eraserSize); with eraserSyncWithBrush it takes the Nib Brush's
    /// size and nib instead.
    BrushSettings eraserBrush = []() {
        BrushSettings eraser = BrushSettings::classicDefaults();
        eraser.usePressure = false;
        return eraser;
    }();
    bool eraserSyncWithBrush = false;

    /// Each brush's own settings: the Brush (fluidBrush), the Nib Brush
    /// (classicBrush) and the Ink Brush. Pressure is on in all three; a mouse always
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
        Inside,
        /// Only over fills already there, recoloring them; nothing lands on
        /// empty stage (Harmony's Repaint Brush).
        Repaint
    };
    BrushMode brushMode = BrushMode::Normal;

    /// Textured brushes: the grain a brush stroke takes on (empty for none,
    /// a built-in texture's name, or "bitmap:" and a library bitmap's), its
    /// scale and its strength, in percent.
    std::string brushTexture;
    double brushTextureScale = 100.0;
    double brushTextureStrength = 100.0;
    /// Where the grain starts and its turn, so strokes need not share one
    /// grain; the strength following the pen's pressure (a light touch
    /// shows more grain, as chalk does); and the grain fixed to the frame
    /// rather than scaling with the art under a camera.
    double brushTextureOffsetX = 0.0;
    double brushTextureOffsetY = 0.0;
    double brushTextureRotation = 0.0;
    bool brushTexturePressure = false;
    bool brushTextureFixed = false;

    /// Harmony's painting options: a stroke that closes on itself is
    /// filled in (Auto Fill), and a stroke begun on a ruler guide keeps to
    /// it.
    bool brushAutoFill = false;
    bool brushSnapGuides = false;

    /// The bitmap brush: strokes paint pixels into the frame's paint canvas,
    /// a bitmap in the library, rather than vector shapes. Softness feathers
    /// the tip's edge, opacity is each stroke's strength (both in percent),
    /// and Erase takes paint away instead.
    bool bitmapBrush = false;
    double bitmapSoftness = 50.0;
    double bitmapOpacity = 100.0;
    bool bitmapErase = false;
    /// How much paint each dab lays down, so overlapping dabs build up (100
    /// lays it all and nothing builds); the dabs' spacing as a percentage
    /// of the tip; and how firmly the soft edge holds before it falls away.
    double bitmapFlow = 100.0;
    double bitmapSpacing = 10.0;
    double bitmapHardness = 50.0;
    /// The pen's pressure taking the opacity, and the flow, down with it.
    bool bitmapOpacityPressure = false;
    bool bitmapFlowPressure = false;
    /// How a stroke's paint mixes with the paint already on the canvas.
    enum class BitmapBlend
    {
        Normal,
        Multiply,
        Screen,
        Overlay,
        Darken,
        Lighten,
        Add
    };
    BitmapBlend bitmapBlend = BitmapBlend::Normal;
    /// A library bitmap stamped as the tip, its brightness and alpha the
    /// paint's coverage; empty for the round soft tip. And a texture on the
    /// tip itself, turning with it, rather than on the paper.
    std::string bitmapTip;
    std::string bitmapTipTexture;

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
    /// Places every unplaced gradient or bitmap fill of a shape over the
    /// rectangle given (in the shape's space), as Lock Fill places them
    /// over the stage.
    static void fitFillsTo(Element& element, double left, double top, double right, double bottom);

    /// Builds the stroke for a new object, or null when strokes are switched
    /// off. The caller owns the result.
    StrokeStyle* createStroke(DOMElement* parent) const;

    /// Whether \a stroke is what createStroke would make: the ink bottle and
    /// the drawing tools reuse a shape's matching stroke rather than adding
    /// another.
    bool strokeMatches(const StrokeStyle& stroke) const;
};

} // namespace fla
