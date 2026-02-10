#pragma once

#include "dom_element.h"

class PublishHistory : public DOMElement
{
public:
    PublishHistory() = default;

    std::string domTypeName() const override { return "PublishHistory"; }
};
