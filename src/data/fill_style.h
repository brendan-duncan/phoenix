#pragma once

#include "dom_element.h"
#include "transform.h"

#include <string>

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

    /// For gradients: how the colours continue past the ends (pad, reflect,
    /// repeat) and how they are blended, carried through as the file names
    /// them. Empty means the default, pad and RGB.
    std::string spreadMethod;
    std::string interpolationMethod;

    FillStyle(DOMElement* parent)
        : DOMElement(parent)
    {}

    virtual Type type() const = 0;
};

} // namespace fla
