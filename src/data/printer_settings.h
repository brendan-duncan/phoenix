#pragma once

#include "dom_element.h"

namespace fla {

class PrinterSettings : public DOMElement
{
public:
    PrinterSettings(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "PrinterSettings"; }

    DOMType domType() const override { return DOMType::PrinterSettings; }

    static DOMType staticDomType() { return DOMType::PrinterSettings; }
};

} // namespace fla
