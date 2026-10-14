#pragma once

#include "dom_element.h"

#include <string>
#include <vector>

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

    /// Child elements of the library item read but not modeled, as XML
    /// text, written back after the rest: the persistentData scripts keep
    /// on an item.
    std::vector<std::string> extraChildren;

    Resource(DOMElement* parent)
        : DOMElement(parent)
    {}

    static DOMType staticDomType() { return DOMType::Resource; }

    virtual Type resourceType() const = 0;
};

} // namespace fla
