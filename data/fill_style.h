#pragma once

#include "dom_element.h"
#include "transform.h"

namespace fla {

class FillStyle : public DOMElement
{
public:
    enum class Type
    {
        SolidColor,
        LinearGradient,
        RadialGradient,
        BitmapFill
    };

    Transform transform;

    FillStyle(DOMElement* parent)
        : DOMElement(parent)
    {}

    virtual Type type() const = 0;
};

} // namespace fla
