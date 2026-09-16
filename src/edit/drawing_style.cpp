#include "drawing_style.h"

#include "../data/solid_color.h"
#include "../data/stroke_style.h"

namespace fla {

namespace {

void copyColor(const uint8_t source[4], uint8_t target[4])
{
    for (int i = 0; i < 4; ++i)
        target[i] = source[i];
}

} // namespace

FillStyle* DrawingStyle::createFill(DOMElement* parent) const
{
    if (!hasFill)
        return nullptr;

    SolidColor* fill = new SolidColor(parent);
    copyColor(fillColor, fill->color);
    return fill;
}

StrokeStyle* DrawingStyle::createStroke(DOMElement* parent) const
{
    if (!hasStroke)
        return nullptr;

    SolidStroke* stroke = new SolidStroke(parent);
    stroke->weight = strokeWeight;

    // A stroke is coloured by a fill of its own, the same way the format stores
    // it, which is what lets a stroke carry a gradient later on.
    SolidColor* color = new SolidColor(stroke);
    copyColor(strokeColor, color->color);
    stroke->fill = color;

    return stroke;
}

} // namespace fla
