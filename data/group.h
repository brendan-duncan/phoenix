#pragma once

#include "element.h"
#include <vector>

namespace fla {

class Group : public Element
{
public:
    std::vector<Element*> members;

    Group(DOMElement* parent)
        : Element(parent)
    {}

    ~Group() override;

    Element::Type elementType() const override { return Element::Type::Group; }

    std::string domTypeName() const override { return "Group"; }

    DOMType domType() const override { return DOMType::Group; }
};

} // namespace fla
