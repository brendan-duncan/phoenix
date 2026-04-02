#pragma once

#include "dom_element.h"

namespace fla {

class ActionScript : public DOMElement
{
public:
    std::string code;

    ActionScript(fla::DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "ActionScript"; }

    DOMType domType() const override { return DOMType::ActionScript; }

    static DOMType staticDomType() { return DOMType::ActionScript; }
};

} // namespace fla
