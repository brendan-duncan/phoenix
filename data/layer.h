#pragma once

#include "dom_element.h"
#include "frame.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Layer : public DOMElement
{
public:
    // Attributes from Layer element
    std::string name;
    uint8_t color[4] = {0, 0, 0, 255}; // Default to opaque black
    std::string current;
    bool autoNamed = false;
    bool selected = false;
    bool locked = false;

    std::vector<Frame*> frames;

    Layer() = default;

    ~Layer() override;

    std::string domTypeName() const override { return "Layer"; }
};

} // namespace fla
