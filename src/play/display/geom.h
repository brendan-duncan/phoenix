#pragma once

#include <cstdint>

namespace fla {

/// The display engine a movie's ActionScript 3 works with, as Test Movie
/// plays it: the display list (flash.display), its events (flash.events),
/// drawing (Graphics), pixels (BitmapData), filters and text fields. Free of
/// Qt and of the script VM: the movie runtime and the script bindings drive
/// it, and whatever draws the movie renders it.
namespace display {

/// flash.geom.Point.
struct Point
{
    double x = 0.0;
    double y = 0.0;
};

/// flash.geom.Rectangle: its top-left corner and size. Empty when either
/// side is not positive.
struct Rect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    Rect() = default;
    Rect(double x, double y, double width, double height)
        : x(x), y(y), width(width), height(height)
    {}

    double left() const { return x; }
    double top() const { return y; }
    double right() const { return x + width; }
    double bottom() const { return y + height; }
    bool isEmpty() const { return !(width > 0.0) || !(height > 0.0); }
    bool contains(double px, double py) const { return px >= x && px < x + width && py >= y && py < y + height; }
    /// The smallest box holding both; an empty one counts for nothing.
    Rect united(const Rect& other) const;
    /// Where the two overlap; empty (all zero) when they do not.
    Rect intersected(const Rect& other) const;
    bool intersects(const Rect& other) const { return !intersected(other).isEmpty(); }
};

/// flash.geom.Matrix: maps (x, y) to (a x + c y + tx, b x + d y + ty).
struct Matrix
{
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double tx = 0.0;
    double ty = 0.0;

    Matrix() = default;
    Matrix(double a, double b, double c, double d, double tx, double ty)
        : a(a), b(b), c(c), d(d), tx(tx), ty(ty)
    {}

    bool isIdentity() const { return a == 1.0 && b == 0.0 && c == 0.0 && d == 1.0 && tx == 0.0 && ty == 0.0; }
    bool operator==(const Matrix& o) const
    {
        return a == o.a && b == o.b && c == o.c && d == o.d && tx == o.tx && ty == o.ty;
    }
    bool operator!=(const Matrix& o) const { return !(*this == o); }

    Point map(double x, double y) const { return {a * x + c * y + tx, b * x + d * y + ty}; }
    Point map(const Point& p) const { return map(p.x, p.y); }
    /// Without the translation (Matrix.deltaTransformPoint).
    Point mapDelta(double x, double y) const { return {a * x + c * y, b * x + d * y}; }
    /// The box around \a rect's four corners mapped.
    Rect mapRect(const Rect& rect) const;

    /// This, then \a then: Matrix.concat (this = this * then, the
    /// ActionScript order, where this one applies first).
    Matrix then(const Matrix& then) const;
    /// The inverse; the identity when there is none (as Matrix.invert
    /// leaves a singular matrix).
    Matrix inverted(bool* invertible = nullptr) const;
    double determinant() const { return a * d - b * c; }

    void identity() { *this = Matrix(); }
    void invert() { *this = inverted(); }
    /// Matrix.rotate, scale and translate: applied after what it does now.
    void rotate(double radians);
    void scale(double sx, double sy);
    void translate(double dx, double dy);
    /// Matrix.createBox: rotate, then scale, then move.
    void createBox(double scaleX, double scaleY, double rotation = 0.0, double tx = 0.0, double ty = 0.0);
    /// Matrix.createGradientBox: the matrix that stretches the gradient
    /// square (-819.2 to 819.2 twips, so 1638.4 units across) over a box
    /// \a width by \a height at (\a tx, \a ty), turned by \a rotation.
    void createGradientBox(double width, double height, double rotation = 0.0, double tx = 0.0, double ty = 0.0);
};

/// flash.geom.ColorTransform: each channel times its multiplier plus its
/// offset (-255 to 255), on unpremultiplied color.
struct ColorTransform
{
    double redMultiplier = 1.0;
    double greenMultiplier = 1.0;
    double blueMultiplier = 1.0;
    double alphaMultiplier = 1.0;
    double redOffset = 0.0;
    double greenOffset = 0.0;
    double blueOffset = 0.0;
    double alphaOffset = 0.0;

    bool isIdentity() const
    {
        return redMultiplier == 1.0 && greenMultiplier == 1.0 && blueMultiplier == 1.0 && alphaMultiplier == 1.0 &&
            redOffset == 0.0 && greenOffset == 0.0 && blueOffset == 0.0 && alphaOffset == 0.0;
    }
    /// Only the alpha multiplier differs from the identity.
    bool alphaOnly() const
    {
        return redMultiplier == 1.0 && greenMultiplier == 1.0 && blueMultiplier == 1.0 && redOffset == 0.0 &&
            greenOffset == 0.0 && blueOffset == 0.0 && alphaOffset == 0.0;
    }
    /// ColorTransform.color: the offsets as an RGB color.
    uint32_t color() const;
    /// Setting it makes the multipliers 0 and the offsets the color's.
    void setColor(uint32_t rgb);
    /// ColorTransform.concat, as the player does it: \a second applies
    /// first, then this one (a parent's over its child's).
    void concat(const ColorTransform& second);
    /// \a argb (unpremultiplied, alpha in the top byte) transformed.
    uint32_t apply(uint32_t argb) const;
};

constexpr double kPi = 3.14159265358979323846;

} // namespace display

} // namespace fla
