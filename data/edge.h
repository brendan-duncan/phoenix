#pragma once

#include "dom_element.h"
#include "point.h"
#include <string>
#include <vector>

namespace fla {

struct PathSegment : public DOMElement
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

    PathSegment(Command cmd, const std::vector<Point>& pts, DOMElement* parent)
        : DOMElement(parent)
        , command(cmd)
        , points(pts)
    {}

    std::string domTypeName() const override { return "PathSegment"; }

    DOMType domType() const override { return DOMType::PathSegment; }
};

struct Path : public DOMElement
{
    int styleIndex = -1;
    int lineStyleIndex = -1;
    int fillStyleIndex = -1;
    std::vector<PathSegment*> segments;

    Path(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Path() override;

    std::string domTypeName() const override { return "Path"; }
    DOMType domType() const override { return DOMType::Path; }
};

class Edge : public DOMElement
{
public:
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;
    std::string data; // Original path data string from XML
    std::vector<Path*> paths;

    Edge(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Edge() override;

    std::string domTypeName() const override { return "Edge"; }
    DOMType domType() const override { return DOMType::Edge; }
};

} // namespace fla
