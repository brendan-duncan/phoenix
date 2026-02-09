#pragma once

#include "dom_element.h"
#include "action_script.h"
#include "element.h"
#include <string>
#include <vector>

class Frame : public DOMElement
{
public:
    int index;
    std::string keyMode;

    ActionScript* actionScript = nullptr;
    std::vector<Element*> elements;

    Frame();

    ~Frame() override;

    std::string domTypeName() const override { return "Frame"; }
};
