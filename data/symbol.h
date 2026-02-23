#pragma once

#include "dom_element.h"
#include "timeline.h"
#include <vector>
#include <string>

namespace fla {

class Symbol : public DOMElement
{
public:
    std::string name;
    std::string itemId;
    std::string lastModified;

    std::vector<Timeline*> timelines;

    Symbol(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Symbol() override;

    std::string domTypeName() const override { return "Symbol"; }

    DOMType domType() const override { return DOMType::Symbol; }
};

} // namespace fla
