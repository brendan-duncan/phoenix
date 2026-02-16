#pragma once

#include "transform.h"

namespace fla {

class Point
{
public:
    double x;
    double y;

    Point(double x = 0.0, double y = 0.0)
        : x(x)
        , y(y)
    {}

    void reset()
    {
        x = 0.0;
        y = 0.0;
    }

    Point& translate(double tx, double ty)
    {
        x += tx;
        y += ty;
        return *this;
    }

    Point& transform(const Transform& t)
    {
        double newX = t.m11 * x + t.m12 * y + t.tx;
        double newY = t.m12 * x + t.m22 * y + t.ty;
        x = newX;
        y = newY;
        return *this;
    }

    Point transformed(const Transform& t) const
    {
        double newX = t.m11 * x + t.m21 * y + t.tx;
        double newY = t.m12 * x + t.m22 * y + t.ty;
        return Point(newX, newY);
    }
};

} // namespace fla
