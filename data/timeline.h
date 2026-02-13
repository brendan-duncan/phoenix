#pragma once

#include "dom_element.h"
#include "layer.h"
#include <string>
#include <vector>

namespace fla {

class Timeline : public DOMElement
{
public:
    std::string name;
    bool layerDepthEnabled = false;

    std::vector<Layer*> layers;

    Timeline() = default;

    ~Timeline() override;

    std::string domTypeName() const override { return "Timeline"; }
};

} // namespace fla
