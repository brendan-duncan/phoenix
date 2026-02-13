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

    Symbol() = default;

    ~Symbol() override;

    std::string domTypeName() const override { return "Symbol"; }
};

} // namespace fla
