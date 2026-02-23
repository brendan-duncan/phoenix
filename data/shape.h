#pragma once

#include "edge.h"
#include "element.h"
#include "fill_style.h"
#include "stroke_style.h"
#include "transform.h"
#include <vector>
#include <map>

namespace fla {

class Shape : public Element
{
public:
    std::vector<FillStyle *> fills;
    std::vector<StrokeStyle *> strokes;
    std::vector<Edge *> edges;

    std::map<int, StrokeStyle*> strokesMap;
    std::map<int, FillStyle*> fillsMap;

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
