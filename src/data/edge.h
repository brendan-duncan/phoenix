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

    static DOMType staticDomType() { return DOMType::PathSegment; }
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

    // It owns what its pointers point at, so a copy would delete them twice.
    // Copies are made with the clone functions instead.
    Path(const Path&) = delete;
    Path& operator=(const Path&) = delete;

    std::string domTypeName() const override { return "Path"; }

    DOMType domType() const override { return DOMType::Path; }

    static DOMType staticDomType() { return DOMType::Path; }
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

    // It owns what its pointers point at, so a copy would delete them twice.
    // Copies are made with the clone functions instead.
    Edge(const Edge&) = delete;
    Edge& operator=(const Edge&) = delete;

    std::string domTypeName() const override { return "Edge"; }

    DOMType domType() const override { return DOMType::Edge; }

    static DOMType staticDomType() { return DOMType::Edge; }
};

} // namespace fla
