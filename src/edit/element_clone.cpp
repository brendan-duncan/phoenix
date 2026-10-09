#include "element_clone.h"

#include "../data/bitmap_instance.h"
#include "../data/element.h"
#include "../data/group.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/symbol_instance.h"
#include "shape_merge.h"

namespace fla {

namespace {

/// What every element carries regardless of kind.
void copyElementFields(const Element& from, Element& to)
{
    to.visible = from.visible;
    to.isLocked = from.isLocked;
    to.transform = from.transform;
    to.transformationPoint = from.transformationPoint;
    to.localBounds = from.localBounds;
    to.bounds = from.bounds;
}

} // namespace

Element* cloneElement(const Element& element, DOMElement* parent)
{
    switch (element.elementType())
    {
    case Element::Type::Shape:
    {
        Shape* copy = cloneShape(static_cast<const Shape&>(element), parent);
        copy->visible = element.visible;
        copy->isLocked = element.isLocked;
        return copy;
    }
    case Element::Type::Group:
    {
        const Group& group = static_cast<const Group&>(element);
        Group* copy = new Group(parent);
        copyElementFields(group, *copy);
        for (const Element* member : group.members)
        {
            if (!member)
                continue;
            if (Element* memberCopy = cloneElement(*member, copy))
                copy->members.push_back(memberCopy);
        }
        return copy;
    }
    case Element::Type::SymbolInstance:
    {
        const SymbolInstance& instance = static_cast<const SymbolInstance&>(element);
        SymbolInstance* copy = new SymbolInstance(parent);
        copyElementFields(instance, *copy);
        copy->libraryItemName = instance.libraryItemName;
        copy->firstFrame = instance.firstFrame;
        copy->symbolType = instance.symbolType;
        copy->loopType = instance.loopType;
        copy->colorTransform = instance.colorTransform;
        copy->symbol = instance.symbol;
        return copy;
    }
    case Element::Type::BitmapInstance:
    {
        const BitmapInstance& instance = static_cast<const BitmapInstance&>(element);
        BitmapInstance* copy = new BitmapInstance(parent);
        copyElementFields(instance, *copy);
        copy->libraryItemName = instance.libraryItemName;
        return copy;
    }
    case Element::Type::StaticText:
    {
        const StaticText& text = static_cast<const StaticText&>(element);
        StaticText* copy = new StaticText(parent);
        copyElementFields(text, *copy);
        copy->left = text.left;
        copy->top = text.top;
        copy->width = text.width;
        copy->height = text.height;
        copy->autoExpand = text.autoExpand;
        copy->runs = text.runs;
        return copy;
    }
    case Element::Type::Rectangle:
    {
        const RectanglePrimitive& rectangle = static_cast<const RectanglePrimitive&>(element);
        RectanglePrimitive* copy = new RectanglePrimitive(parent);
        copyElementFields(rectangle, *copy);
        copy->rect = rectangle.rect;
        copy->fillStyle = cloneFill(rectangle.fillStyle, copy);
        copy->strokeStyle = cloneStroke(rectangle.strokeStyle, copy);
        copy->topLeftRadius = rectangle.topLeftRadius;
        copy->topRightRadius = rectangle.topRightRadius;
        copy->bottomLeftRadius = rectangle.bottomLeftRadius;
        copy->bottomRightRadius = rectangle.bottomRightRadius;
        return copy;
    }
    case Element::Type::Oval:
    {
        const OvalPrimitive& oval = static_cast<const OvalPrimitive&>(element);
        OvalPrimitive* copy = new OvalPrimitive(parent);
        copyElementFields(oval, *copy);
        copy->rect = oval.rect;
        copy->startAngle = oval.startAngle;
        copy->endAngle = oval.endAngle;
        copy->innerRadius = oval.innerRadius;
        copy->fillStyle = cloneFill(oval.fillStyle, copy);
        copy->strokeStyle = cloneStroke(oval.strokeStyle, copy);
        return copy;
    }
    }
    return nullptr;
}

void setTextContents(StaticText& target, const StaticText& source)
{
    target.left = source.left;
    target.top = source.top;
    target.width = source.width;
    target.height = source.height;
    target.autoExpand = source.autoExpand;
    target.runs = source.runs;
    target.localBounds = source.localBounds;
    target.bounds = source.bounds;
}

} // namespace fla
