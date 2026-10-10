#pragma once

#include "dom_element.h"

#include <string>

namespace fla {

class ScriptList : public DOMElement
{
public:
    /// The element as read, written back as it was: nothing edits it yet.
    std::string xml;

    ScriptList(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "ScriptList"; }

    DOMType domType() const override { return DOMType::ScriptList; }

    static DOMType staticDomType() { return DOMType::ScriptList; }
};

} // namespace fla
