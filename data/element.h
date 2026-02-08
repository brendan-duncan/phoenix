#pragma once

#include "dom_element.h"
#include "point.h"
#include "transform.h"

class Element : public DOMElement
{
public:
    enum class Type
    {
        Shape,
        SymbolInstance,
        Group
    };

    bool isSelected = false;
    bool isFloating = false;
    Transform transform;
    Point transformationPoint;

    Element();

    virtual Type elementType() const = 0;
};
