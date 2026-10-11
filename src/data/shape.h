#pragma once

#include "edge.h"
#include "element.h"
#include "fill_style.h"
#include "stroke_style.h"
#include "transform.h"
#include <vector>
#include <map>
#include <string>
#include <utility>

namespace fla {

class Shape : public Element
{
public:
    std::vector<FillStyle *> fills;
    std::vector<StrokeStyle *> strokes;
    std::vector<Edge *> edges;

    std::map<int, StrokeStyle*> strokesMap;
    std::map<int, FillStyle*> fillsMap;

    /// A drawing object: a shape drawn with Object Drawing mode on. It keeps
    /// to itself -- it never merges with the artwork under it and is selected
    /// as a whole with a bounding box, like a group -- while a plain shape is
    /// merge artwork that other shapes cut into. Animate writes it as
    /// `isDrawingObject="true"` on the DOMShape.
    bool isDrawingObject = false;

    /// The Edge elements carrying only cubics, the curves a pen or brush
    /// drew as Animate's editor keeps them beside the quadratic edges it
    /// draws, as read: each with the number of edges before it. They stand
    /// for the edges as read, so they are written back only while those are
    /// unedited (see cubicsStillMatch).
    std::vector<std::pair<size_t, std::string>> cubicEdges;
    size_t readEdgeCount = 0;

    /// Whether the edges are still the ones read, so the cubics kept beside
    /// them still describe them.
    bool cubicsStillMatch() const
    {
        if (cubicEdges.empty() || edges.size() != readEdgeCount)
            return false;
        for (const Edge* edge : edges)
        {
            if (!edge || edge->data.empty())
                return false;
        }
        return true;
    }

    Shape(DOMElement* parent)
        : Element(parent)
    {}

    ~Shape() override;

    Element::Type elementType() const override { return Element::Type::Shape; }

    const FillStyle* getFillStyleByIndex(int index) const
    {
        const auto it = fillsMap.find(index);
        if (it != fillsMap.end())
            return it->second;
        return nullptr;
    }

    const StrokeStyle* getStrokeStyleByIndex(int index) const
    {
        const auto it = strokesMap.find(index);
        if (it != strokesMap.end())
            return it->second;
        return nullptr;
    }

    std::string domTypeName() const override { return "Shape"; }

    DOMType domType() const override { return DOMType::Shape; }

    static DOMType staticDomType() { return DOMType::Shape; }
};

} // namespace fla
