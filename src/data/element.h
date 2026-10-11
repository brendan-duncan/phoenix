#pragma once

#include "dom_element.h"
#include "point.h"
#include "rect.h"
#include "transform.h"
#include <string>
#include <vector>

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
        Oval,
        Raw
    };

    bool isLocked = false;
    bool isSelected = false;
    bool isFloating = false;
    Transform transform;
    /// transformationPoint is the pivot for applying new transformstions. It is
    /// not used in the rendering process.
    Point transformationPoint;
    fla::Rect localBounds;
    fla::Rect bounds;

    /// Whether the file wrote a matrix, or a transformation point, that
    /// Phoenix would leave out as the identity or the origin: they are
    /// written back as they came.
    bool keepMatrix = false;
    bool keepTransformationPoint = false;

    /// Child elements read but not modeled, as XML text, written back after
    /// the rest: a button's or a movie clip's Actionscript, a text field's
    /// filters...
    std::vector<std::string> extraChildren;

    Element(DOMElement* parent)
        : DOMElement(parent)
    {}

    virtual Type elementType() const = 0;

    static DOMType staticDomType() { return DOMType::Element; }

    Point transformPoint(const Point& p) const
    {
        Point transformed = p;
        transformed.transform(transform);
        return transformed;
    }
};

} // namespace fla
