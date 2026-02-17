#pragma once

#include "dom_element.h"

namespace fla {

class Folder : public DOMElement
{
public:
    std::string name;
    std::string itemId;
    bool isExpanded = false;

    Folder() = default;

    std::string domTypeName() const override { return "Folder"; }
};

} // namespace fla
