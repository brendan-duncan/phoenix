#pragma once

#include "dom_element.h"
#include "point.h"
#include "rect.h"
#include "transform.h"

namespace fla {

class Element : public DOMElement
{
public:
    enum class Type
    {
        Shape,
        SymbolInstance,
        Group,
        StaticText,
        BitmapInstance,
        Rectangle,
        Oval
    };

    bool isLocked = false;
    bool isSelected = false;
    bool isFloating = false;
    Transform transform;
    /// transformationPoint is the pivot for applying new transformstions. It is
    /// not used in the rendering process.
    Point transformationPoint;
    fla::Rect bounds;

    Element(DOMElement* parent)
        : DOMElement(parent)
    {}

    virtual Type elementType() const = 0;

    Point transformPoint(const Point& p) const
    {
        Point transformed = p;
        transformed.transform(transform);
        return transformed;
    }
};

} // namespace fla
