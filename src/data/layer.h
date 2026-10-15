#pragma once

#include "dom_element.h"
#include "frame.h"
#include <cstdint>
#include <string>
#include <utility>
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
    /// Outline view: the layer draws as thin lines in its color, no fills.
    bool outline = false;

    /// A layer type Phoenix does not model, kept as spelled; layerType is
    /// Normal for it.
    std::string otherLayerType;

    /// Child elements other than the frames, as XML text, written back
    /// after them.
    std::vector<std::string> extraChildren;

    std::vector<Frame*> frames;

    int firstFrame = 0;
    int lastFrame = 0;

    Layer* parentLayer = nullptr; // Pointer to parent layer (if any)

    Layer(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Layer() override;

    // It owns what its pointers point at, so a copy would delete them twice.
    // Copies are made with the clone functions instead.
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    std::string domTypeName() const override { return "Layer"; }

    DOMType domType() const override { return DOMType::Layer; }

    static DOMType staticDomType() { return DOMType::Layer; }

    /// Whether the layer shows. A folder's eye is not inherited: hiding a
    /// folder hides each layer in it (see the layers panel), and a file
    /// whose folder is hidden over layers that are not shows them.
    bool isVisible() const
    {
        return visible;
    }

    /// Whether the layer is locked, by itself or by a folder it sits in:
    /// locking a folder locks what is in it.
    bool isLocked() const
    {
        if (locked)
            return true;
        if (parentLayer && parentLayer->layerType == Type::Folder)
            return parentLayer->isLocked();
        return false;
    }
};

} // namespace fla
