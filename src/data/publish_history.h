#pragma once

#include "dom_element.h"

#include <string>

namespace fla {

class PublishHistory : public DOMElement
{
public:
    /// The element as read, written back as it was: nothing edits it yet.
    std::string xml;

    PublishHistory(DOMElement* parent)
        : DOMElement(parent)
    {}

    std::string domTypeName() const override { return "PublishHistory"; }

    DOMType domType() const override { return DOMType::PublishHistory; }

    static DOMType staticDomType() { return DOMType::PublishHistory; }
};

} // namespace fla
