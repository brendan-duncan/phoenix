#pragma once

#include "dom_element.h"
#include "layer.h"
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Timeline : public DOMElement
{
public:
    std::string name;
    bool layerDepthEnabled = false;

    /// Ruler guides, in the timeline's own coordinates: the vertical ones by
    /// their x, the horizontal ones by their y. Animate keeps them in the
    /// DOMTimeline's guides attribute.
    std::vector<double> guidesX;
    std::vector<double> guidesY;

    /// Child elements other than the layers, as XML text, written back
    /// after them.
    std::vector<std::string> extraChildren;

    std::vector<Layer*> layers;

    Timeline(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Timeline() override;

    std::string domTypeName() const override { return "Timeline"; }

    DOMType domType() const override { return DOMType::Timeline; }

    static DOMType staticDomType() { return DOMType::Timeline; }
};

} // namespace fla
