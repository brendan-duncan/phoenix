#pragma once

#include "document.h"
#include "dom_element.h"

class FLADocument : public DOMElement
{
public:
    std::string filepath;
    Document* document;

    FLADocument();

    ~FLADocument();

    std::string domTypeName() const override { return "FLADocument"; }
};
