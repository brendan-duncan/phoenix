#pragma once

#include "element.h"
#include <string>

class BitmapInstance : public Element
{
public:
    std::string libraryItemName;

    BitmapInstance() = default;

    Element::Type elementType() const override { return Element::Type::BitmapInstance; }
};
