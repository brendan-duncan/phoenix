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

    // It owns what its pointers point at, so a copy would delete them twice.
    // Copies are made with the clone functions instead.
    Group(const Group&) = delete;
    Group& operator=(const Group&) = delete;

    Element::Type elementType() const override { return Element::Type::Group; }

    std::string domTypeName() const override { return "Group"; }

    DOMType domType() const override { return DOMType::Group; }

    static DOMType staticDomType() { return DOMType::Group; }
};

} // namespace fla
