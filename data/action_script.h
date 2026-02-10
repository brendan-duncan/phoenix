#pragma once

#include "dom_element.h"

namespace fla {

class ActionScript : public DOMElement
{
public:
    std::string code;

    ActionScript() = default;

    std::string domTypeName() const override { return "ActionScript"; }
};

} // namespace fla
