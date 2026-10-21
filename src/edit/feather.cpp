#include "feather.h"

#include "../data/fill_style.h"
#include "../data/group.h"
#include "../data/oval_primitive.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/stroke_style.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fla {

namespace feather {

const char* const kAttribute = "phoenixFeather";

double of(const DOMElement& style)
{
    for (const auto& [key, value] : style.extraAttributes)
        if (key == kAttribute)
            return std::max(0.0, std::atof(value.c_str()));
    return 0.0;
}

std::vector<std::pair<std::string, std::string>> with(const DOMElement& style, double amount)
{
    std::vector<std::pair<std::string, std::string>> attributes = style.extraAttributes;
    attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                         [](const std::pair<std::string, std::string>& pair) { return pair.first == kAttribute; }),
        attributes.end());
    if (amount > 0.0)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.4g", amount);
        attributes.emplace_back(kAttribute, text);
    }
    return attributes;
}

void set(DOMElement& style, double amount)
{
    style.extraAttributes = with(style, amount);
}

bool any(const Shape& shape)
{
    for (const FillStyle* fill : shape.fills)
        if (fill && of(*fill) > 0.0)
            return true;
    for (const StrokeStyle* stroke : shape.strokes)
        if (stroke && of(*stroke) > 0.0)
            return true;
    return false;
}

bool any(const Element& element)
{
    switch (element.elementType())
    {
    case Element::Type::Shape:
        return any(static_cast<const Shape&>(element));
    case Element::Type::Rectangle:
    {
        const auto& rectangle = static_cast<const RectanglePrimitive&>(element);
        return (rectangle.fillStyle && of(*rectangle.fillStyle) > 0.0) ||
            (rectangle.strokeStyle && of(*rectangle.strokeStyle) > 0.0);
    }
    case Element::Type::Oval:
    {
        const auto& oval = static_cast<const OvalPrimitive&>(element);
        return (oval.fillStyle && of(*oval.fillStyle) > 0.0) || (oval.strokeStyle && of(*oval.strokeStyle) > 0.0);
    }
    case Element::Type::Group:
        for (const Element* member : static_cast<const Group&>(element).members)
            if (member && any(*member))
                return true;
        return false;
    default:
        return false;
    }
}

namespace {

template <typename Style>
double first(const std::vector<Style*>& styles, bool* mixed)
{
    bool found = false;
    double amount = 0.0;
    if (mixed)
        *mixed = false;
    for (const Style* style : styles)
    {
        if (!style)
            continue;
        const double each = of(*style);
        if (!found)
        {
            amount = each;
            found = true;
        }
        else if (each != amount && mixed)
        {
            *mixed = true;
        }
    }
    return amount;
}

} // namespace

double ofFills(const Shape& shape, bool* mixed)
{
    return first(shape.fills, mixed);
}

double ofStrokes(const Shape& shape, bool* mixed)
{
    return first(shape.strokes, mixed);
}

void setFills(Shape& shape, double amount)
{
    for (FillStyle* fill : shape.fills)
        if (fill)
            set(*fill, amount);
}

void setStrokes(Shape& shape, double amount)
{
    for (StrokeStyle* stroke : shape.strokes)
        if (stroke)
            set(*stroke, amount);
}

} // namespace feather

} // namespace fla
