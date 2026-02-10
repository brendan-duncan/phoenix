#pragma once

#include "dom_element.h"
#include "point.h"
#include <string>
#include <vector>

namespace fla {

struct PathSection
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

    PathSection(Command cmd, const std::vector<Point>& pts)
        : command(cmd), points(pts)
    {}
};

struct PathSegment : public DOMElement
{
    int styleIndex = -1;
    int lineStyleIndex = -1;
    int fillStyleIndex = -1;
    std::vector<PathSection> sections;

    PathSegment() = default;

    std::string domTypeName() const override { return "PathSegment"; }
};

class Edge : public DOMElement
{
public:
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;

    std::vector<PathSegment> segments;

    Edge();

    std::string domTypeName() const override { return "Edge"; }
};

} // namespace fla
