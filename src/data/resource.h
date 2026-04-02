#pragma once

#include "dom_element.h"

namespace fla {

class Resource : public DOMElement
{
public:
    enum class Type
    {
        Bitmap,
        Font,
        Symbol,
    };

    Resource(DOMElement* parent)
        : DOMElement(parent)
    {}

    static DOMType staticDomType() { return DOMType::Resource; }

    virtual Type resourceType() const = 0;
};

} // namespace fla
