#pragma once

#include "dom_element.h"
#include "point.h"

namespace fla {

class MorphCurves : public DOMElement
{
public:
    Point controlPointA;
    Point anchorPointA;
    Point controlPointB;
    Point anchorPointB;

    MorphCurves(fla::DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "MorphCurves"; }

    DOMType domType() const override { return DOMType::MorphCurves; }

    static DOMType staticDomType() { return DOMType::MorphCurves; }
};

} // namespace fla
