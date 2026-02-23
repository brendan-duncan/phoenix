#pragma once

#include "dom_element.h"
#include "resource.h"
#include "timeline.h"
#include <vector>
#include <string>

namespace fla {

class Symbol : public Resource
{
public:
    std::string name;
    std::string itemId;
    std::string lastModified;

    std::vector<Timeline*> timelines;

    Symbol(DOMElement* parent)
        : Resource(parent)
    {}

    ~Symbol() override;

    Type resourceType() const override { return Type::Symbol; }

    std::string domTypeName() const override { return "Symbol"; }

    DOMType domType() const override { return DOMType::Symbol; }

    static DOMType staticDomType() { return DOMType::Symbol; }
};

} // namespace fla
