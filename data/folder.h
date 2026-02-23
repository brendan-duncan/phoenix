#pragma once

#include "dom_element.h"

namespace fla {

class Folder : public DOMElement
{
public:
    std::string name;
    std::string itemId;
    bool isExpanded = false;

    Folder(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "Folder"; }

    DOMType domType() const override { return DOMType::Folder; }
};

} // namespace fla
