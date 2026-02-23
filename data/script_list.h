#pragma once

#include "dom_element.h"

namespace fla {

class ScriptList : public DOMElement
{
public:
    ScriptList(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "ScriptList"; }

    DOMType domType() const override { return DOMType::ScriptList; }
};

} // namespace fla
