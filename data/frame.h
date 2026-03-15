#pragma once

#include "dom_element.h"
#include "action_script.h"
#include "element.h"
#include <string>
#include <vector>

namespace fla {

class Frame : public DOMElement
{
public:
    int index = 0;
    int duration = 1;
    std::string keyMode;
    TweenType tweenType = TweenType::None;
    bool motionTWeenSnap = false;

    ActionScript* actionScript = nullptr;
    std::vector<Element*> elements;

    Frame(DOMElement* parent)
        : DOMElement(parent)
    {}

    ~Frame() override;

    std::string domTypeName() const override { return "Frame"; }

    DOMType domType() const override { return DOMType::Frame; }

    static DOMType staticDomType() { return DOMType::Frame; }
};

} // namespace fla
