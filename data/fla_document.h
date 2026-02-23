#pragma once

#include "document.h"
#include "dom_element.h"

namespace fla {

class FLADocument : public DOMElement
{
public:
    std::string filepath;
    Document* document = nullptr;

    FLADocument(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~FLADocument();

    std::string domTypeName() const override { return "FLADocument"; }

    DOMType domType() const override { return DOMType::FLADocument; }
};

} // namespace fla
