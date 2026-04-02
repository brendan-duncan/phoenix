#pragma once

#include "dom_element.h"
#include "morph_curves.h"
#include <vector>

namespace fla {

class MorphSegment : public DOMElement
{
public:
    Point startPointA;
    Point startPointB;
    int strokeIndex1 = 0;
    int strokeIndex2 = 0;
    int fillIndex1 = 0;
    int fillIndex2 = 0;
    std::vector<MorphCurves*> curves;

    MorphSegment(fla::DOMElement* parent)
        : DOMElement(parent)
    {}

    ~MorphSegment() override
    {
        for (auto* curve : curves)
        {
            delete curve;
        }
    }

    std::string domTypeName() const override { return "MorphSegment"; }

    DOMType domType() const override { return DOMType::MorphSegment; }

    static DOMType staticDomType() { return DOMType::MorphSegment; }
};

} // namespace fla
