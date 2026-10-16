#include "geom.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

Rect Rect::united(const Rect& other) const
{
    if (other.isEmpty())
        return *this;
    if (isEmpty())
        return other;
    const double l = std::min(left(), other.left());
    const double t = std::min(top(), other.top());
    const double r = std::max(right(), other.right());
    const double b = std::max(bottom(), other.bottom());
    return Rect(l, t, r - l, b - t);
}

Rect Rect::intersected(const Rect& other) const
{
    const double l = std::max(left(), other.left());
    const double t = std::max(top(), other.top());
    const double r = std::min(right(), other.right());
    const double b = std::min(bottom(), other.bottom());
    if (!(r > l) || !(b > t))
        return Rect();
    return Rect(l, t, r - l, b - t);
}

Rect Matrix::mapRect(const Rect& rect) const
{
    const Point p[4] = {map(rect.left(), rect.top()), map(rect.right(), rect.top()), map(rect.left(), rect.bottom()),
        map(rect.right(), rect.bottom())};
    double l = p[0].x, r = p[0].x, t = p[0].y, b = p[0].y;
    for (const Point& q : p)
    {
        l = std::min(l, q.x);
        r = std::max(r, q.x);
        t = std::min(t, q.y);
        b = std::max(b, q.y);
    }
    return Rect(l, t, r - l, b - t);
}

Matrix Matrix::then(const Matrix& m) const
{
    return Matrix(a * m.a + b * m.c, a * m.b + b * m.d, c * m.a + d * m.c, c * m.b + d * m.d, tx * m.a + ty * m.c + m.tx,
        tx * m.b + ty * m.d + m.ty);
}

Matrix Matrix::inverted(bool* invertible) const
{
    const double det = determinant();
    if (det == 0.0 || !std::isfinite(det))
    {
        if (invertible)
            *invertible = false;
        return Matrix();
    }
    if (invertible)
        *invertible = true;
    const double ia = d / det;
    const double ib = -b / det;
    const double ic = -c / det;
    const double id = a / det;
    return Matrix(ia, ib, ic, id, -(ia * tx + ic * ty), -(ib * tx + id * ty));
}

void Matrix::rotate(double radians)
{
    const double cs = std::cos(radians);
    const double sn = std::sin(radians);
    *this = then(Matrix(cs, sn, -sn, cs, 0.0, 0.0));
}

void Matrix::scale(double sx, double sy)
{
    *this = then(Matrix(sx, 0.0, 0.0, sy, 0.0, 0.0));
}

void Matrix::translate(double dx, double dy)
{
    tx += dx;
    ty += dy;
}

void Matrix::createBox(double scaleX, double scaleY, double rotation, double x, double y)
{
    // As Flash Player builds it: turned, then scaled along the stage's
    // axes (so b goes with the y scale and c with the x).
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);
    a = scaleX * cs;
    b = scaleY * sn;
    c = -scaleX * sn;
    d = scaleY * cs;
    tx = x;
    ty = y;
}

void Matrix::createGradientBox(double width, double height, double rotation, double x, double y)
{
    createBox(width / 1638.4, height / 1638.4, rotation, x + width / 2.0, y + height / 2.0);
}

uint32_t ColorTransform::color() const
{
    const auto clamp = [](double v) { return static_cast<uint32_t>(std::max(0.0, std::min(255.0, v))); };
    return (clamp(redOffset) << 16) | (clamp(greenOffset) << 8) | clamp(blueOffset);
}

void ColorTransform::setColor(uint32_t rgb)
{
    redMultiplier = greenMultiplier = blueMultiplier = 0.0;
    redOffset = (rgb >> 16) & 0xff;
    greenOffset = (rgb >> 8) & 0xff;
    blueOffset = rgb & 0xff;
}

void ColorTransform::concat(const ColorTransform& second)
{
    redOffset += redMultiplier * second.redOffset;
    greenOffset += greenMultiplier * second.greenOffset;
    blueOffset += blueMultiplier * second.blueOffset;
    alphaOffset += alphaMultiplier * second.alphaOffset;
    redMultiplier *= second.redMultiplier;
    greenMultiplier *= second.greenMultiplier;
    blueMultiplier *= second.blueMultiplier;
    alphaMultiplier *= second.alphaMultiplier;
}

uint32_t ColorTransform::apply(uint32_t argb) const
{
    const auto channel = [](uint32_t value, double multiplier, double offset) {
        const double v = std::floor(value * multiplier + offset);
        return static_cast<uint32_t>(std::max(0.0, std::min(255.0, v)));
    };
    const uint32_t a = channel(argb >> 24, alphaMultiplier, alphaOffset);
    const uint32_t r = channel((argb >> 16) & 0xff, redMultiplier, redOffset);
    const uint32_t g = channel((argb >> 8) & 0xff, greenMultiplier, greenOffset);
    const uint32_t b = channel(argb & 0xff, blueMultiplier, blueOffset);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace display
} // namespace fla
