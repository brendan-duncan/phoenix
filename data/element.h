#pragma once

#include "dom_element.h"
#include "point.h"
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
        BitmapInstance
    };

    bool isSelected = false;
    bool isFloating = false;
    Transform transform;
    Point transformationPoint;

    Element();

    virtual Type elementType() const = 0;
};

} // namespace fla
