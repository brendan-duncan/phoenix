#pragma once

#include <string>

class DOMElement
{
public:
    bool visible = true;

    DOMElement();

    virtual ~DOMElement() = default;

    virtual std::string domTypeName() const = 0;
};
