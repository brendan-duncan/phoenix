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

    void translate(double tx, double ty)
    {
        x += tx;
        y += ty;
    }

    void transform(const Transform& t)
    {
        x = t.m11 * x + t.m21 * y + t.tx;
        y = t.m12 * x + t.m22 * y + t.ty;
    }
};

} // namespace fla
