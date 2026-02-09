#pragma once

#include "dom_element.h"
#include "point.h"
#include <string>
#include <vector>

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

struct PathSegment
{
    int styleIndex = -1;
    int lineStyleIndex = -1;
    int fillStyleIndex = -1;
    std::vector<PathSection> sections;
};

class Edge : DOMElement
{
public:
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;

    std::vector<PathSegment> segments;

    Edge();
};
