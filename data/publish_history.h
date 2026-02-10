#pragma once

#include "dom_element.h"

namespace fla {

class PublishHistory : public DOMElement
{
public:
    PublishHistory() = default;

    std::string domTypeName() const override { return "PublishHistory"; }
};

} // namespace fla
