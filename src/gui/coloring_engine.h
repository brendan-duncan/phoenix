#pragma once

#include "../data/color_transform.h"

/// A paint engine that colors what is drawn through it by a symbol's color
/// effect itself, one shape at a time, as Flash colors a symbol's art: the
/// GPU stage's. A symbol with only a color effect is then drawn straight
/// through it rather than as a picture colored pixel by pixel. Effects
/// pushed inside others apply first.
class ColoringEngine
{
public:
    virtual ~ColoringEngine() = default;
    virtual void pushColor(const fla::ColorTransform& color) = 0;
    virtual void popColor() = 0;
    /// Softens the edges of what is drawn next by \a feather, in the
    /// coordinates of the painter's transform (0 for sharp edges again);
    /// false when the engine cannot, and the caller softens them itself.
    virtual bool setFeather(double feather)
    {
        (void)feather;
        return false;
    }
};
