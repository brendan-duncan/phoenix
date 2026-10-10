#pragma once

#include "dom_element.h"

#include <string>

namespace fla {

class PrinterSettings : public DOMElement
{
public:
    /// The element as read, written back as it was: nothing edits it yet.
    std::string xml;

    PrinterSettings(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "PrinterSettings"; }

    DOMType domType() const override { return DOMType::PrinterSettings; }

    static DOMType staticDomType() { return DOMType::PrinterSettings; }
};

} // namespace fla
