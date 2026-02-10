#pragma once

#include "dom_element.h"

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

    virtual Type type() const = 0;
};

} // namespace fla
