#pragma once

#include "element.h"
#include <string>

namespace fla {

/// An element of a kind Phoenix does not model (TLF text, a component, a
/// video instance...), kept as the XML it was read as so a save writes it
/// back in its place among the frame's elements. It is not drawn.
class RawElement : public Element
{
public:
    std::string xml;

    RawElement(DOMElement* parent)
        : Element(parent)
    {}

    Type elementType() const override { return Type::Raw; }

    std::string domTypeName() const override { return "RawElement"; }

    DOMType domType() const override { return DOMType::RawElement; }

    static DOMType staticDomType() { return DOMType::RawElement; }
};

} // namespace fla
