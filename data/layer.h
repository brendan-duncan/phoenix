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
    enum class Type
    {
        Normal,
        Mask,
        Masked,
        Folder,
        Guide
    };
    // Attributes from Layer element
    std::string name;
    uint8_t color[4] = {0, 0, 0, 255}; // Default to opaque black
    std::string current;
    Type layerType = Type::Normal;
    int parentLayerIndex = -1; // -1 indicates no parent
    bool autoNamed = false;
    bool selected = false;
    bool locked = false;

    std::vector<Frame*> frames;

    Layer* parentLayer = nullptr; // Pointer to parent layer (if any)

    Layer(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Layer() override;

    std::string domTypeName() const override { return "Layer"; }

    DOMType domType() const override { return DOMType::Layer; }

    static DOMType staticDomType() { return DOMType::Layer; }

    // Checks if the layer is visible, taking into account all ancestor layers.
    bool isVisible() const
    {
        if (!visible)
            return false;
        if (parentLayer && parentLayer->layerType == Type::Folder)
        {
            return parentLayer->isVisible();
        }
        return true;
    }
};

} // namespace fla
