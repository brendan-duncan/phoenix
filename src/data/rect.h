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

    /// Transforms the rect in place, becoming the axis-aligned bounding box of
    /// the transformed corners. All four corners are mapped, so this stays
    /// correct when the transform rotates or skews.
    void transform(const Transform& t)
    {
        const Point corners[4] = {
            topLeft,
            Point(bottomRight.x, topLeft.y),
            bottomRight,
            Point(topLeft.x, bottomRight.y)
        };

        reset();
        for (const Point& corner : corners)
        {
            const Point p = corner.transformed(t);
            topLeft.x = std::min(topLeft.x, p.x);
            topLeft.y = std::min(topLeft.y, p.y);
            bottomRight.x = std::max(bottomRight.x, p.x);
            bottomRight.y = std::max(bottomRight.y, p.y);
        }
    }

    void expandToInclude(const Rect& other);
};

} // namespace fla
