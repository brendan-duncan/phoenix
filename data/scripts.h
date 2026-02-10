#pragma once

#include "dom_element.h"

class Scripts : public DOMElement
{
public:
    Scripts() = default;

    std::string domTypeName() const override { return "Scripts"; }
};
