#pragma once

#include "dom_element.h"

namespace fla {

class PublishHistory : public DOMElement
{
public:
    PublishHistory(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "PublishHistory"; }

    DOMType domType() const override { return DOMType::PublishHistory; }
};

} // namespace fla
