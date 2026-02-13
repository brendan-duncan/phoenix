#pragma once

#include "document.h"
#include "dom_element.h"

namespace fla {

class FLADocument : public DOMElement
{
public:
    std::string filepath;
    Document* document = nullptr;

    FLADocument() = default;

    ~FLADocument();

    std::string domTypeName() const override { return "FLADocument"; }
};

} // namespace fla
