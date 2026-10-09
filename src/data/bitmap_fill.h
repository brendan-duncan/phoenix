#pragma once

#include "fill_style.h"

#include <string>

namespace fla {

/// A fill that tiles a library bitmap through its matrix. The matrix maps
/// the bitmap's pixels into the shape's space in twips, so an unscaled
/// bitmap carries a scale of 20.
class BitmapFill : public FillStyle
{
public:
    /// The library item's name.
    std::string bitmapPath;

    /// Whether the bitmap is shown once rather than tiled.
    bool bitmapIsClipped = false;

    BitmapFill(DOMElement* parent)
        : FillStyle(parent)
    {}

    Type type() const override { return Type::BitmapFill; }

    std::string domTypeName() const override { return "BitmapFill"; }

    DOMType domType() const override { return DOMType::BitmapFill; }

    static DOMType staticDomType() { return DOMType::BitmapFill; }
};

} // namespace fla
