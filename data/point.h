#pragma once

namespace fla {

class Point
{
public:
    Point(double x = 0.0, double y = 0.0)
        : x(x)
        , y(y)
    {}

    double x;
    double y;
};

} // namespace fla
