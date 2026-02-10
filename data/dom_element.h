#pragma once

#include <string>

namespace fla {

class DOMElement
{
public:
    bool visible = true;

    DOMElement();

    virtual ~DOMElement() = default;

    virtual std::string domTypeName() const = 0;
};

} // namespace fla
