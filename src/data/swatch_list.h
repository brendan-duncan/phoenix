#pragma once

#include "dom_element.h"
#include "swatch.h"
#include <string>
#include <vector>

namespace fla {

class SwatchList : public DOMElement
{
public:
    std::string name;
    std::vector<Swatch*> swatches;

    SwatchList(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~SwatchList() override;

    std::string domTypeName() const override { return "SwatchList"; }

    DOMType domType() const override { return DOMType::SwatchList; }

    static DOMType staticDomType() { return DOMType::SwatchList; }
};

} // namespace fla
