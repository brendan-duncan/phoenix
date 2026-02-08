#pragma once

#include "dom_element.h"
#include "element.h"
#include <string>
#include <vector>

class Frame : public DOMElement
{
public:
    int index;
    std::string keyMode;

    std::vector<Element*> elements;

    Frame();

    ~Frame() override;
};
