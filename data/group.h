#pragma once

#include "element.h"
#include <vector>

namespace fla {

class Group : public Element
{
public:
    std::vector<Element*> members;

    Group() = default;

    ~Group() override;

    Element::Type elementType() const override { return Element::Type::Group; }

    std::string domTypeName() const override { return "Group"; }
};

} // namespace fla
