#pragma once

#include "dom_element.h"

class PrinterSettings : public DOMElement
{
public:
    PrinterSettings() = default;

    std::string domTypeName() const override { return "PrinterSettings"; }
};
