#include "element_clone.h"

#include "../data/bitmap_instance.h"
#include "../data/color_transform.h"
#include "../data/linear_gradient.h"
#include "../data/radial_gradient.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../data/element.h"
#include "../data/group.h"
#include "../data/oval_primitive.h"
#include "../data/raw_element.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/static_text.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "shape_merge.h"
#include <cstdint>

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
    to.keepMatrix = from.keepMatrix;
    to.keepTransformationPoint = from.keepTransformationPoint;
    to.extraAttributes = from.extraAttributes;
    to.presentAttributes = from.presentAttributes;
    to.extraChildren = from.extraChildren;
}

} // namespace

Element* cloneElement(const Element& element, DOMElement* parent)
{
    switch (element.elementType())
    {
    case Element::Type::Shape:
    {
        const Shape& shape = static_cast<const Shape&>(element);
        Shape* copy = cloneShape(shape, parent);
        copyElementFields(shape, *copy);
        copy->cubicEdges = shape.cubicEdges;
        copy->readEdgeCount = shape.readEdgeCount;
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
        copy->name = instance.name;
        copy->firstFrame = instance.firstFrame;
        copy->symbolType = instance.symbolType;
        copy->loopType = instance.loopType;
        copy->colorTransform = instance.colorTransform;
        copy->blendMode = instance.blendMode;
        copy->otherSymbolType = instance.otherSymbolType;
        copy->keepColor = instance.keepColor;
        copy->extraFilters = instance.extraFilters;
        copy->filters = instance.filters;
        copy->symbol = instance.symbol;
        copy->hasReadMatrix = instance.hasReadMatrix;
        copy->readMatrix = instance.readMatrix;
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
        copy->textType = text.textType;
        copy->instanceName = text.instanceName;
        copy->lineType = text.lineType;
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
    case Element::Type::Raw:
    {
        const RawElement& raw = static_cast<const RawElement&>(element);
        RawElement* copy = new RawElement(parent);
        copyElementFields(raw, *copy);
        copy->xml = raw.xml;
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

namespace {

uint8_t clampByte(double value)
{
    return static_cast<uint8_t>(value < 0.0 ? 0.0 : (value > 255.0 ? 255.0 : value));
}

/// One color through the effect, the way the stage draws it: tint, then
/// brightness towards white or black, then the advanced multipliers and
/// offsets.
void recolor(uint8_t color[4], const ColorTransform& t)
{
    double c[4] = {static_cast<double>(color[0]), static_cast<double>(color[1]),
        static_cast<double>(color[2]), static_cast<double>(color[3])};

    if (t.tintMultiplier != 0.0)
    {
        for (int i = 0; i < 3; ++i)
            c[i] = (1.0 - t.tintMultiplier) * c[i] + t.tintMultiplier * t.tintColor[i];
    }

    if (t.brightness > 0.0)
    {
        for (int i = 0; i < 3; ++i)
            c[i] = c[i] + (255.0 - c[i]) * t.brightness;
    }
    else if (t.brightness < 0.0)
    {
        for (int i = 0; i < 3; ++i)
            c[i] = c[i] * (1.0 + t.brightness);
    }

    c[0] = c[0] * t.redMultiplier + t.redOffset;
    c[1] = c[1] * t.greenMultiplier + t.greenOffset;
    c[2] = c[2] * t.blueMultiplier + t.blueOffset;
    c[3] = c[3] * t.alphaMultiplier + t.alphaOffset;

    for (int i = 0; i < 4; ++i)
        color[i] = clampByte(c[i]);
}

void recolorFill(FillStyle* fill, const ColorTransform& t)
{
    if (!fill)
        return;
    switch (fill->type())
    {
    case FillStyle::Type::SolidColor:
        recolor(static_cast<SolidColor*>(fill)->color, t);
        break;
    case FillStyle::Type::LinearGradient:
        for (GradientEntry& entry : static_cast<LinearGradient*>(fill)->entries)
            recolor(entry.color, t);
        break;
    case FillStyle::Type::RadialGradient:
        for (RadialEntry& entry : static_cast<RadialGradient*>(fill)->entries)
            recolor(entry.color, t);
        break;
    case FillStyle::Type::BitmapFill:
        break;
    }
}

} // namespace

void applyColorTransform(Element& element, const ColorTransform& transform)
{
    if (transform.isIdentity())
        return;

    switch (element.elementType())
    {
    case Element::Type::Shape:
    {
        Shape& shape = static_cast<Shape&>(element);
        for (FillStyle* fill : shape.fills)
            recolorFill(fill, transform);
        for (StrokeStyle* stroke : shape.strokes)
        {
            if (stroke)
                recolorFill(stroke->fill, transform);
        }
        break;
    }
    case Element::Type::Group:
        for (Element* member : static_cast<Group&>(element).members)
        {
            if (member)
                applyColorTransform(*member, transform);
        }
        break;
    case Element::Type::Rectangle:
    {
        RectanglePrimitive& rectangle = static_cast<RectanglePrimitive&>(element);
        recolorFill(rectangle.fillStyle, transform);
        if (rectangle.strokeStyle)
            recolorFill(rectangle.strokeStyle->fill, transform);
        break;
    }
    case Element::Type::Oval:
    {
        OvalPrimitive& oval = static_cast<OvalPrimitive&>(element);
        recolorFill(oval.fillStyle, transform);
        if (oval.strokeStyle)
            recolorFill(oval.strokeStyle->fill, transform);
        break;
    }
    case Element::Type::StaticText:
        for (TextRun& run : static_cast<StaticText&>(element).runs)
            recolor(run.fillColor, transform);
        break;
    case Element::Type::SymbolInstance:
    case Element::Type::BitmapInstance:
    case Element::Type::Raw:
        break;
    }
}

void setTextContents(StaticText& target, const StaticText& source)
{
    target.left = source.left;
    target.top = source.top;
    target.width = source.width;
    target.height = source.height;
    target.autoExpand = source.autoExpand;
    target.textType = source.textType;
    target.instanceName = source.instanceName;
    target.lineType = source.lineType;
    target.runs = source.runs;
    target.localBounds = source.localBounds;
    target.bounds = source.bounds;
}

void relinkSymbols(Element& element, const SymbolList* library)
{
    if (element.elementType() == Element::Type::SymbolInstance)
    {
        SymbolInstance& instance = static_cast<SymbolInstance&>(element);
        instance.symbol = nullptr;
        if (library)
        {
            const auto found = library->symbolMap.find(instance.libraryItemName);
            if (found != library->symbolMap.end())
                instance.symbol = found->second;
        }
    }
    else if (element.elementType() == Element::Type::Group)
    {
        for (Element* member : static_cast<Group&>(element).members)
        {
            if (member)
                relinkSymbols(*member, library);
        }
    }
}

} // namespace fla
