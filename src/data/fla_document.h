#pragma once

#include "document.h"
#include "dom_element.h"

namespace fla {

class FLADocument : public DOMElement
{
public:
    std::string filepath;
    Document* document = nullptr;
    /// Read from a Flash CS4 (or earlier) binary .fla, which saving would
    /// replace with the current format.
    bool fromBinary = false;

    FLADocument(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~FLADocument();

    std::string domTypeName() const override { return "FLADocument"; }

    DOMType domType() const override { return DOMType::FLADocument; }

    static DOMType staticDomType() { return DOMType::FLADocument; }
};

} // namespace fla
