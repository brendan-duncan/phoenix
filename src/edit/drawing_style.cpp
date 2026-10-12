#include "drawing_style.h"

#include "../data/bitmap_fill.h"
#include "../data/edge.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "shape_merge.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace fla {

namespace {

void copyColor(const uint8_t source[4], uint8_t target[4])
{
    for (int i = 0; i < 4; ++i)
        target[i] = source[i];
}

/// Flash's gradient square is 1638.4 pixels across before its matrix.
constexpr double kGradientSpan = 1638.4;

/// A new gradient or bitmap fill is made with the identity matrix, which no
/// placed one has, so that is how fitFills knows which to place.
bool unplaced(const FillStyle& fill)
{
    if (fill.type() == FillStyle::Type::SolidColor)
        return false;
    const Transform& t = fill.transform;
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 && t.m22 == 1.0 && t.tx == 0.0 && t.ty == 0.0;
}

} // namespace

std::vector<DrawingStyle::Stop> DrawingStyle::defaultGradient()
{
    Stop white;
    white.ratio = 0.0;
    white.color[0] = white.color[1] = white.color[2] = 255;
    Stop black;
    black.ratio = 1.0;
    return {white, black};
}

FillStyle* DrawingStyle::createFill(DOMElement* parent) const
{
    if (!hasFill)
        return nullptr;

    if ((fillKind == FillKind::Linear || fillKind == FillKind::Radial) && gradient.size() >= 2)
    {
        FillStyle* fill = nullptr;
        if (fillKind == FillKind::Linear)
        {
            LinearGradient* linear = new LinearGradient(parent);
            for (const Stop& stop : gradient)
            {
                GradientEntry entry{stop.ratio, {}};
                copyColor(stop.color, entry.color);
                linear->entries.push_back(entry);
            }
            fill = linear;
        }
        else
        {
            RadialGradient* radial = new RadialGradient(parent);
            for (const Stop& stop : gradient)
            {
                RadialEntry entry{stop.ratio, {}};
                copyColor(stop.color, entry.color);
                radial->entries.push_back(entry);
            }
            fill = radial;
        }
        fill->spreadMethod = gradientSpread;
        if (gradientLinearRGB)
            fill->interpolationMethod = "linearRGB";
        return fill;
    }
    if (fillKind == FillKind::Bitmap && !fillBitmap.empty())
    {
        BitmapFill* fill = new BitmapFill(parent);
        fill->bitmapPath = fillBitmap;
        return fill;
    }

    SolidColor* fill = new SolidColor(parent);
    copyColor(fillColor, fill->color);
    return fill;
}

void DrawingStyle::fitFill(FillStyle& fill, double left, double top, double right, double bottom)
{
    const double width = std::max(right - left, 1.0);
    const double height = std::max(bottom - top, 1.0);
    Transform& t = fill.transform;
    switch (fill.type())
    {
    case FillStyle::Type::LinearGradient:
        t = Transform(width / kGradientSpan, 0.0, 0.0, height / kGradientSpan, (left + right) / 2.0, (top + bottom) / 2.0);
        break;
    case FillStyle::Type::RadialGradient:
    {
        const double reach = std::max(width, height) / kGradientSpan;
        t = Transform(reach, 0.0, 0.0, reach, (left + right) / 2.0, (top + bottom) / 2.0);
        break;
    }
    case FillStyle::Type::BitmapFill:
        // The matrix takes the bitmap's pixels to twips.
        t = Transform(20.0, 0.0, 0.0, 20.0, left, top);
        break;
    case FillStyle::Type::SolidColor:
        break;
    }
}

void DrawingStyle::fitFillsTo(Element& element, double left, double top, double right, double bottom)
{
    if (element.elementType() != Element::Type::Shape)
        return;
    for (const auto& entry : static_cast<Shape&>(element).fillsMap)
        if (entry.second && unplaced(*entry.second))
            fitFill(*entry.second, left, top, right, bottom);
}

void DrawingStyle::fitFills(Element& element)
{
    if (element.elementType() == Element::Type::Rectangle)
    {
        RectanglePrimitive& rectangle = static_cast<RectanglePrimitive&>(element);
        if (rectangle.fillStyle && unplaced(*rectangle.fillStyle))
            fitFill(*rectangle.fillStyle, rectangle.rect.topLeft.x, rectangle.rect.topLeft.y,
                rectangle.rect.bottomRight.x, rectangle.rect.bottomRight.y);
        return;
    }
    if (element.elementType() == Element::Type::Oval)
    {
        OvalPrimitive& oval = static_cast<OvalPrimitive&>(element);
        if (oval.fillStyle && unplaced(*oval.fillStyle))
            fitFill(*oval.fillStyle, oval.rect.topLeft.x, oval.rect.topLeft.y,
                oval.rect.bottomRight.x, oval.rect.bottomRight.y);
        return;
    }
    if (element.elementType() != Element::Type::Shape)
        return;

    // Each unplaced fill goes over the edges that border it.
    Shape& shape = static_cast<Shape&>(element);
    for (const auto& entry : shape.fillsMap)
    {
        if (!entry.second || !unplaced(*entry.second))
            continue;
        double left = std::numeric_limits<double>::max(), top = left;
        double right = -left, bottom = -left;
        for (const Edge* edge : shape.edges)
        {
            if (!edge || (edge->fillStyle0 != entry.first && edge->fillStyle1 != entry.first))
                continue;
            for (const Path* path : edge->paths)
            {
                if (!path)
                    continue;
                for (const PathSegment* segment : path->segments)
                {
                    if (!segment)
                        continue;
                    for (const Point& point : segment->points)
                    {
                        left = std::min(left, point.x);
                        top = std::min(top, point.y);
                        right = std::max(right, point.x);
                        bottom = std::max(bottom, point.y);
                    }
                }
            }
        }
        if (left <= right && top <= bottom)
            fitFill(*entry.second, left, top, right, bottom);
    }
}

StrokeLook StrokeLook::of(const StrokeStyle& stroke)
{
    StrokeLook look;
    look.kind = stroke.style();
    look.hairline = stroke.isHairline();
    look.caps = stroke.caps;
    look.joints = stroke.joints;
    look.scaleMode = stroke.scaleMode;
    look.miterLimit = stroke.miterLimit;
    look.widthProfile = stroke.widthProfile;
    return look;
}

const std::vector<WidthPreset>& widthPresets()
{
    const auto marker = [](double position, double reach) {
        StrokeStyle::WidthMarker m;
        m.position = position;
        m.left = reach;
        m.right = reach;
        return m;
    };
    static const std::vector<WidthPreset> presets = {
        {"Even", {}},
        {"Taper Both Ends", {marker(0.0, 0.0), marker(0.5, 0.5), marker(1.0, 0.0)}},
        {"Taper Start", {marker(0.0, 0.0), marker(1.0, 0.5)}},
        {"Taper End", {marker(0.0, 0.5), marker(1.0, 0.0)}},
        {"Swell", {marker(0.0, 0.2), marker(0.5, 1.0), marker(1.0, 0.2)}},
        {"Pinch", {marker(0.0, 0.75), marker(0.5, 0.15), marker(1.0, 0.75)}},
    };
    return presets;
}

bool StrokeLook::operator==(const StrokeLook& other) const
{
    return kind == other.kind && hairline == other.hairline && caps == other.caps && joints == other.joints &&
        scaleMode == other.scaleMode && miterLimit == other.miterLimit && widthProfile == other.widthProfile;
}

StrokeStyle* StrokeLook::applyTo(const StrokeStyle& stroke, DOMElement* parent) const
{
    StrokeStyle* copy = nullptr;
    switch (kind)
    {
    case StrokeStyle::Style::Dashed: copy = new DashedStroke(parent); break;
    case StrokeStyle::Style::Ragged: copy = new RaggedStroke(parent); break;
    case StrokeStyle::Style::Stipple: copy = new StippleStroke(parent); break;
    case StrokeStyle::Style::Dotted: copy = new DottedStroke(parent); break;
    case StrokeStyle::Style::Hatched: copy = new HatchedStroke(parent); break;
    case StrokeStyle::Style::Solid: copy = new SolidStroke(parent); break;
    }
    copy->weight = stroke.weight;
    copy->pixelHinting = stroke.pixelHinting;
    copy->dash1 = stroke.dash1;
    copy->dash2 = stroke.dash2;
    copy->dotSpace = stroke.dotSpace;
    copy->extraChildren = stroke.extraChildren;
    copy->widthProfile = widthProfile;
    copy->extraAttributes = stroke.extraAttributes;
    // A kind's own settings only mean something to that kind.
    if (kind == stroke.style())
        copy->styleAttributes = stroke.styleAttributes;
    copy->fill = cloneFill(stroke.fill, copy);
    copy->caps = caps;
    copy->joints = joints;
    copy->scaleMode = scaleMode;
    copy->miterLimit = miterLimit;
    copy->solidStyle = hairline && kind == StrokeStyle::Style::Solid ? "hairline" : std::string();
    return copy;
}

StrokeStyle* DrawingStyle::createStroke(DOMElement* parent) const
{
    if (!hasStroke)
        return nullptr;

    SolidStroke plain(nullptr);
    plain.weight = strokeWeight;
    // A stroke is colored by a fill of its own, the same way the format stores
    // it, which is what lets a stroke carry a gradient later on.
    SolidColor* color = new SolidColor(&plain);
    copyColor(strokeColor, color->color);
    plain.fill = color;

    return strokeLook.applyTo(plain, parent);
}

bool DrawingStyle::strokeMatches(const StrokeStyle& stroke) const
{
    if (StrokeLook::of(stroke) != strokeLook || stroke.weight != strokeWeight || !stroke.fill ||
        stroke.fill->type() != FillStyle::Type::SolidColor)
        return false;
    const uint8_t* color = static_cast<const SolidColor*>(stroke.fill)->color;
    return std::equal(color, color + 4, strokeColor);
}

} // namespace fla
