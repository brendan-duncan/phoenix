#pragma once

#include "geom.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace fla {
namespace display {

class BitmapData;
class DisplayObject;

/// A gradient as beginGradientFill and lineGradientStyle take it.
struct Gradient
{
    enum class Spread
    {
        Pad,
        Reflect,
        Repeat
    };
    struct Stop
    {
        /// RGB, and alpha from 0 to 1.
        uint32_t color = 0;
        double alpha = 1.0;
        /// Where along the gradient, 0 to 255.
        int ratio = 0;
    };

    bool radial = false;
    std::vector<Stop> stops;
    /// From the gradient square (-819.2 to 819.2 on each axis) to the
    /// shape's space (Matrix.createGradientBox makes one).
    Matrix matrix;
    Spread spread = Spread::Pad;
    /// InterpolationMethod.LINEAR_RGB rather than RGB.
    bool linearRGB = false;
    /// A radial gradient's focal point along its axis, -1 to 1.
    double focalPointRatio = 0.0;

    /// The stops from the three arrays ActionScript passes, as Flash Player
    /// takes them: as many as the shortest has, alpha clamped to 0 to 1,
    /// ratios to 0 to 255, at most 15.
    static std::vector<Stop> stopsOf(const std::vector<uint32_t>& colors, const std::vector<double>& alphas,
        const std::vector<double>& ratios);
};

/// How a fill (or a gradient or bitmap stroke) paints.
struct FillStyle
{
    enum class Type
    {
        None,
        Solid,
        Gradient,
        Bitmap
    };
    Type type = Type::None;
    uint32_t color = 0;
    double alpha = 1.0;
    Gradient gradient;
    std::shared_ptr<BitmapData> bitmap;
    /// From the bitmap's pixels to the shape's space.
    Matrix bitmapMatrix;
    bool repeat = true;
    bool smooth = false;
};

/// lineStyle: a stroke's width and how it is drawn. A NaN thickness is no
/// stroke; 0 is a hairline, one device pixel wide whatever the scale.
struct LineStyle
{
    enum class ScaleMode
    {
        Normal,
        None,
        Vertical,
        Horizontal
    };
    enum class Caps
    {
        Round,
        None,
        Square
    };
    enum class Joints
    {
        Round,
        Bevel,
        Miter
    };

    double thickness = std::numeric_limits<double>::quiet_NaN();
    bool pixelHinting = false;
    ScaleMode scaleMode = ScaleMode::Normal;
    Caps caps = Caps::Round;
    Joints joints = Joints::Round;
    double miterLimit = 3.0;
    /// Solid with color and alpha, or the gradient or bitmap
    /// lineGradientStyle and lineBitmapStyle give it.
    FillStyle fill;

    bool on() const { return !std::isnan(thickness); }
    static ScaleMode scaleModeNamed(const std::string& name);
    static Caps capsNamed(const std::string& name);
    static Joints jointsNamed(const std::string& name);
};

/// One step of a path: move, line, quadratic curve (one control point) or
/// cubic curve (two), each to its last point.
struct PathStep
{
    enum class Op
    {
        Move,
        Line,
        Quad,
        Cubic
    };
    Op op = Op::Move;
    Point points[3];
};

/// A filled area or a run of strokes, drawn in turn: the order Graphics
/// keeps them in is the order they draw.
struct GraphicsPath
{
    enum class Kind
    {
        Fill,
        Line
    };
    Kind kind = Kind::Fill;
    FillStyle fill;
    LineStyle line;
    std::vector<PathStep> steps;
    /// drawPath's winding: even-odd unless GraphicsPathWinding.NON_ZERO.
    bool nonZero = false;
};

/// flash.display.Graphics: what drawing calls have made of a Shape's or a
/// Sprite's own art, as a list of fills and strokes. Lines drawn while a
/// fill is open draw over it once it closes, as Flash Player draws them.
class Graphics
{
public:
    explicit Graphics(DisplayObject* owner = nullptr)
        : _owner(owner)
    {}
    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;

    void clear();
    /// lineStyle(): no arguments (NaN thickness) for no stroke.
    void lineStyle(double thickness = std::numeric_limits<double>::quiet_NaN(), uint32_t color = 0, double alpha = 1.0,
        bool pixelHinting = false, LineStyle::ScaleMode scaleMode = LineStyle::ScaleMode::Normal,
        LineStyle::Caps caps = LineStyle::Caps::Round, LineStyle::Joints joints = LineStyle::Joints::Round,
        double miterLimit = 3.0);
    void lineGradientStyle(const Gradient& gradient);
    void lineBitmapStyle(std::shared_ptr<BitmapData> bitmap, const Matrix& matrix, bool repeat = true,
        bool smooth = false);
    void beginFill(uint32_t color, double alpha = 1.0);
    void beginGradientFill(const Gradient& gradient);
    void beginBitmapFill(std::shared_ptr<BitmapData> bitmap, const Matrix& matrix = Matrix(), bool repeat = true,
        bool smooth = false);
    void endFill();
    void moveTo(double x, double y);
    void lineTo(double x, double y);
    void curveTo(double controlX, double controlY, double anchorX, double anchorY);
    void cubicCurveTo(double c1x, double c1y, double c2x, double c2y, double anchorX, double anchorY);
    void drawRect(double x, double y, double width, double height);
    /// Corner ellipses \a ellipseWidth across and \a ellipseHeight high
    /// (NaN: the same as the width).
    void drawRoundRect(double x, double y, double width, double height, double ellipseWidth,
        double ellipseHeight = std::numeric_limits<double>::quiet_NaN());
    void drawCircle(double x, double y, double radius);
    void drawEllipse(double x, double y, double width, double height);
    /// drawPath: GraphicsPathCommand values (1 MOVE_TO, 2 LINE_TO, 3
    /// CURVE_TO, 4 WIDE_MOVE_TO, 5 WIDE_LINE_TO, 6 CUBIC_CURVE_TO) taking
    /// their coordinates from \a data in turn.
    void drawPath(const std::vector<int>& commands, const std::vector<double>& data, bool nonZero = false);
    /// drawTriangles: each three indices (or each three vertices, without
    /// them) a triangle in the current fill; \a culling 0 none, 1 positive
    /// (drops clockwise ones), 2 negative.
    void drawTriangles(const std::vector<double>& vertices, const std::vector<int>& indices = {}, int culling = 0);
    /// copyFrom: the same drawing as \a other.
    void copyFrom(const Graphics& other);

    /// What it draws, in order; the fill still open is included, closed.
    const std::vector<GraphicsPath>& paths() const;
    bool empty() const { return paths().empty(); }
    /// Bumped by every change, for whatever draws it to know its picture is
    /// old.
    uint64_t revision() const { return _revision; }

    /// The box the drawing covers in its own space: with half of each
    /// stroke's width around it, or the bare geometry (getRect).
    Rect bounds(bool withStrokes = true) const;
    /// Whether (\a x, \a y) in its own space is in a fill or on a stroke.
    bool hits(double x, double y) const;

    DisplayObject* owner() const { return _owner; }

private:
    void changed();
    /// Closes the fill being drawn, its lines after it.
    void flushFill();
    /// The step list the next segment goes into: the open fill's, the open
    /// stroke's, or both.
    void addStep(const PathStep& step);
    void startLine();

    DisplayObject* _owner = nullptr;
    std::vector<GraphicsPath> _paths;
    /// The fill being drawn, and the strokes drawn with it, which go after
    /// it once it closes.
    bool _filling = false;
    GraphicsPath _fill;
    Point _fillStart;
    std::vector<GraphicsPath> _pendingLines;
    /// The stroke being drawn.
    LineStyle _line;
    bool _lineOpen = false;
    GraphicsPath _stroke;
    Point _cursor;
    uint64_t _revision = 0;
    /// paths() with the open fill and stroke included, made when asked.
    mutable std::vector<GraphicsPath> _shown;
    mutable bool _shownValid = false;
};

/// Points along \a steps, the curves cut into short lines, each subpath
/// a polygon of its own (a fill's are closed).
std::vector<std::vector<Point>> flatten(const std::vector<PathStep>& steps, double tolerance = 0.25);

} // namespace display
} // namespace fla
