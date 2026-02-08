#pragma once

#include "element.h"
#include <vector>

class Group : public Element
{
public:
    Group();

    ~Group() override;

    Element::Type elementType() const override { return Element::Type::Group; }

    std::vector<Element*> members;
};
