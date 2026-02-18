#pragma once

#include "dom_element.h"
#include "swatch.h"
#include <string>
#include <vector>

namespace fla {

class SwatchList : public DOMElement
{
public:
    std::string name;
    std::vector<Swatch*> swatches;

    SwatchList() = default;

    ~SwatchList() override;

    std::string domTypeName() const override { return "SwatchList"; }
};

} // namespace fla
