#pragma once

#include "transform.h"
#include <string>

namespace fla {

class Point
{
public:
    double x;
    double y;
    std::string xs;
    std::string ys;

    Point(double x = 0.0, double y = 0.0, const std::string& xs = "", const std::string& ys = "")
        : x(x)
        , y(y)
        , xs(xs)
        , ys(ys)
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
