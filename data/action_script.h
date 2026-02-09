#pragma once

#include "dom_element.h"

class ActionScript : public DOMElement
{
public:
    std::string code;

    ActionScript() = default;

    std::string domTypeName() const override { return "ActionScript"; }
};
