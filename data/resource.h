#pragma once

#include "dom_element.h"

class Resource : public DOMElement
{
public:
    enum class Type
    {
        Bitmap,
        Font
    };

    Resource() = default;

    virtual Type resourceType() const = 0;
};
