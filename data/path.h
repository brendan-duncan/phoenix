#pragma once

#include "dom_element.h"
#include "point.h"
#include <string>
#include <vector>

struct PathSection
{
    enum class Command
    {
        MoveTo,
        LineTo,
        QuadTo,
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

class Path : DOMElement
{
public:
    int fillStyle[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int strokeStyle = -1;

    std::vector<PathSegment> segments;

    Path();
};
