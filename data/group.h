#pragma once

#include "element.h"
#include <vector>

class Group : public Element
{
public:
    std::vector<Element*> members;

    Group();

    ~Group() override;

    Element::Type elementType() const override { return Element::Type::Group; }

    std::string domTypeName() const override { return "Group"; }
};
