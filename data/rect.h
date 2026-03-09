#pragma once

#include "point.h"
#include "transform.h"
#include <algorithm>

namespace fla {

class Rect
{
public:
    Point topLeft;
    Point bottomRight;

    Rect() = default;

    Rect(const Point& topLeft, const Point& bottomRight)
        : topLeft(topLeft)
        , bottomRight(bottomRight)
    {}

    Rect(const Rect& other)
        : topLeft(other.topLeft)
        , bottomRight(other.bottomRight)
    {}

    double width() const { return bottomRight.x - topLeft.x; }

    double height() const { return bottomRight.y - topLeft.y; }

    Point center() const { return Point((topLeft.x + bottomRight.x) / 2.0, (topLeft.y + bottomRight.y) / 2.0); }

    void reset()
    {
        topLeft = Point(1.0e30, 1.0e30);
        bottomRight = Point(-1.0e30, -1.0e30);
    }

    void translate(double tx, double ty)
    {
        topLeft.x += tx;
        topLeft.y += ty;
        bottomRight.x += tx;
        bottomRight.y += ty;
    }

    void transform(const Transform& t)
    {
        topLeft.x = t.m11 * topLeft.x + t.m21 * topLeft.y + t.tx;
        topLeft.y = t.m12 * topLeft.x + t.m22 * topLeft.y + t.ty;
        bottomRight.x = t.m11 * bottomRight.x + t.m21 * bottomRight.y + t.tx;
        bottomRight.y = t.m12 * bottomRight.x + t.m22 * bottomRight.y + t.ty;
    }

    void expandToInclude(const Rect& other);
};

} // namespace fla
