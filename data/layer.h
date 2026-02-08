#pragma once

#include "dom_element.h"
#include "frame.h"
#include <cstdint>
#include <string>
#include <vector>

class Layer : public DOMElement
{
public:
    // Attributes from Layer element
    std::string name;
    uint8_t color[4];
    std::string current;
    bool autoNamed;
    bool isSelected;

    std::vector<Frame*> frames;

    Layer();

    ~Layer() override; 
};
