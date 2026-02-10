#pragma once

#include "dom_element.h"

namespace fla {

class Scripts : public DOMElement
{
public:
    Scripts() = default;

    std::string domTypeName() const override { return "Scripts"; }
};

} // namespace fla
