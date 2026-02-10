#pragma once

#include "dom_element.h"

namespace fla {

class PrinterSettings : public DOMElement
{
public:
    PrinterSettings() = default;

    std::string domTypeName() const override { return "PrinterSettings"; }
};

} // namespace fla
