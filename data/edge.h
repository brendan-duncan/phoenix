#pragma once

#include "dom_element.h"
#include "point.h"
#include <string>
#include <vector>

namespace fla {

struct PathSegment
{
    enum class Command
    {
        Move,
        Line,
        Quad,
        Cubic,
        Close
    };

    Command command;
    std::vector<Point> points;

    PathSegment(Command cmd, const std::vector<Point>& pts)
        : command(cmd), points(pts)
    {}
};

struct Path : public DOMElement
{
    int styleIndex = -1;
    int lineStyleIndex = -1;
    int fillStyleIndex = -1;
    std::vector<PathSegment> segments;

    Path() = default;

    std::string domTypeName() const override { return "Path"; }
};

class Edge : public DOMElement
{
public:
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;

    std::vector<Path> paths;

    Edge() = default;

    std::string domTypeName() const override { return "Edge"; }
};

} // namespace fla
