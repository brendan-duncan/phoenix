#pragma once

#include "dom_element.h"
#include "layer.h"
#include <string>
#include <vector>

namespace fla {

class Timeline : public DOMElement
{
public:
    std::string name;
    bool layerDepthEnabled = false;

    std::vector<Layer*> layers;

    Timeline(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Timeline() override;

    std::string domTypeName() const override { return "Timeline"; }

    DOMType domType() const override { return DOMType::Timeline; }

    static DOMType staticDomType() { return DOMType::Timeline; }
};

} // namespace fla
