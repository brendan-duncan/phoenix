#include "transform_blend.h"
#include "../data/element.h"
#include "../data/bitmap_instance.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Parts
{
    double scaleX = 1.0;
    double scaleY = 1.0;
    double skewX = 0.0; // the angle of the y axis, the Transform panel's Skew X
    double skewY = 0.0; // the angle of the x axis, the Transform panel's Skew Y
    double tx = 0.0;
    double ty = 0.0;
};

/// a = sx cos(skewY), b = sx sin(skewY), c = -sy sin(skewX), d = sy cos(skewX)
Parts takeApart(const Transform& m)
{
    Parts parts;
    parts.scaleX = std::hypot(m.m11, m.m12);
    parts.scaleY = std::hypot(m.m21, m.m22);
    parts.skewY = std::atan2(m.m12, m.m11);
    parts.skewX = std::atan2(-m.m21, m.m22);
    parts.tx = m.tx;
    parts.ty = m.ty;

    // A flip is a negative scale, not two skews of half a turn: when the
    // matrix is mirrored, fold the sign into the y scale so the angles stay
    // close to each other and blend as a rotation.
    const double det = m.m11 * m.m22 - m.m12 * m.m21;
    if (det < 0.0)
    {
        parts.scaleY = -parts.scaleY;
        parts.skewX = std::atan2(m.m21, -m.m22);
    }
    return parts;
}

Transform putTogether(const Parts& parts)
{
    Transform m;
    m.m11 = parts.scaleX * std::cos(parts.skewY);
    m.m12 = parts.scaleX * std::sin(parts.skewY);
    m.m21 = -parts.scaleY * std::sin(parts.skewX);
    m.m22 = parts.scaleY * std::cos(parts.skewX);
    m.tx = parts.tx;
    m.ty = parts.ty;
    return m;
}

double blendAngle(double from, double to, double t, TweenRotation rotation, int extraTurns)
{
    double delta = std::fmod(to - from, 2.0 * kPi);
    switch (rotation)
    {
    case TweenRotation::Auto:
        if (delta > kPi)
            delta -= 2.0 * kPi;
        else if (delta < -kPi)
            delta += 2.0 * kPi;
        break;
    case TweenRotation::Clockwise:
        // On a stage with y down, clockwise is the increasing angle.
        if (delta < 0.0)
            delta += 2.0 * kPi;
        delta += extraTurns * 2.0 * kPi;
        break;
    case TweenRotation::CounterClockwise:
        if (delta > 0.0)
            delta -= 2.0 * kPi;
        delta -= extraTurns * 2.0 * kPi;
        break;
    case TweenRotation::None:
        break;
    }
    return from + delta * t;
}

double blend(double from, double to, double t)
{
    return from + (to - from) * t;
}

} // namespace

ColorTransform blendColorTransforms(const ColorTransform& from, const ColorTransform& to, double t)
{
    ColorTransform mixed;
    mixed.tintMultiplier = blend(from.tintMultiplier, to.tintMultiplier, t);
    for (int c = 0; c < 4; ++c)
    {
        const double value = blend(from.tintColor[c], to.tintColor[c], t);
        mixed.tintColor[c] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(value))));
    }
    mixed.brightness = blend(from.brightness, to.brightness, t);
    mixed.redMultiplier = blend(from.redMultiplier, to.redMultiplier, t);
    mixed.greenMultiplier = blend(from.greenMultiplier, to.greenMultiplier, t);
    mixed.blueMultiplier = blend(from.blueMultiplier, to.blueMultiplier, t);
    mixed.alphaMultiplier = blend(from.alphaMultiplier, to.alphaMultiplier, t);
    mixed.redOffset = static_cast<int>(std::lround(blend(from.redOffset, to.redOffset, t)));
    mixed.greenOffset = static_cast<int>(std::lround(blend(from.greenOffset, to.greenOffset, t)));
    mixed.blueOffset = static_cast<int>(std::lround(blend(from.blueOffset, to.blueOffset, t)));
    mixed.alphaOffset = static_cast<int>(std::lround(blend(from.alphaOffset, to.alphaOffset, t)));
    return mixed;
}

namespace {

/// A filter of the same kind as \a filter with no effect, what a keyframe
/// without it is taken to have.
Filter withoutEffect(const Filter& filter)
{
    Filter none = filter;
    none.blurX = 0.0;
    none.blurY = 0.0;
    none.distance = 0.0;
    none.strength = 0.0;
    none.brightness = 0.0;
    none.contrast = 0.0;
    none.saturation = 0.0;
    none.hue = 0.0;
    // A matrix that leaves the color as it is; a kernel of the pixel alone.
    if (filter.type == Filter::Type::ColorMatrix)
        none.matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    else if (filter.type == Filter::Type::Convolution)
    {
        std::fill(none.matrix.begin(), none.matrix.end(), 0.0);
        if (!none.matrix.empty())
            none.matrix[static_cast<size_t>((filter.matrixY / 2) * filter.matrixX + filter.matrixX / 2)] = 1.0;
        none.divisor = 1.0;
        none.bias = 0.0;
    }
    return none;
}

void blendColor(const uint8_t from[4], const uint8_t to[4], double t, uint8_t out[4])
{
    for (int c = 0; c < 4; ++c)
        out[c] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(blend(from[c], to[c], t)))));
}

} // namespace

std::vector<Filter> blendFilters(const std::vector<Filter>& from, const std::vector<Filter>& to, double t)
{
    std::vector<Filter> mixed;
    const size_t count = std::max(from.size(), to.size());
    for (size_t i = 0; i < count; ++i)
    {
        const Filter a = i < from.size() ? from[i] : withoutEffect(to[i]);
        const Filter b = i < to.size() ? to[i] : withoutEffect(from[i]);
        Filter filter = a;
        if (a.type == b.type)
        {
            filter.blurX = blend(a.blurX, b.blurX, t);
            filter.blurY = blend(a.blurY, b.blurY, t);
            filter.quality = static_cast<int>(std::lround(blend(a.quality, b.quality, t)));
            filter.angle = blend(a.angle, b.angle, t);
            filter.distance = blend(a.distance, b.distance, t);
            filter.strength = blend(a.strength, b.strength, t);
            blendColor(a.color, b.color, t, filter.color);
            blendColor(a.highlightColor, b.highlightColor, t, filter.highlightColor);
            blendColor(a.shadowColor, b.shadowColor, t, filter.shadowColor);
            filter.brightness = blend(a.brightness, b.brightness, t);
            filter.contrast = blend(a.contrast, b.contrast, t);
            filter.saturation = blend(a.saturation, b.saturation, t);
            filter.hue = blend(a.hue, b.hue, t);
            if (a.matrix.size() == b.matrix.size() && a.matrixX == b.matrixX)
            {
                for (size_t k = 0; k < a.matrix.size(); ++k)
                    filter.matrix[k] = blend(a.matrix[k], b.matrix[k], t);
                filter.divisor = blend(a.divisor, b.divisor, t);
                filter.bias = blend(a.bias, b.bias, t);
            }
            if (a.entries.size() == b.entries.size())
            {
                for (size_t e = 0; e < a.entries.size(); ++e)
                {
                    filter.entries[e].ratio = blend(a.entries[e].ratio, b.entries[e].ratio, t);
                    blendColor(a.entries[e].color, b.entries[e].color, t, filter.entries[e].color);
                }
            }
        }
        mixed.push_back(filter);
    }
    return mixed;
}

Transform blendTransforms(const Transform& from, const Transform& to, double t,
    TweenRotation rotation, int extraTurns)
{
    if (rotation == TweenRotation::None)
    {
        Transform mixed;
        mixed.m11 = blend(from.m11, to.m11, t);
        mixed.m12 = blend(from.m12, to.m12, t);
        mixed.m21 = blend(from.m21, to.m21, t);
        mixed.m22 = blend(from.m22, to.m22, t);
        mixed.tx = blend(from.tx, to.tx, t);
        mixed.ty = blend(from.ty, to.ty, t);
        return mixed;
    }

    const Parts a = takeApart(from);
    const Parts b = takeApart(to);

    Parts mixed;
    mixed.scaleX = blend(a.scaleX, b.scaleX, t);
    mixed.scaleY = blend(a.scaleY, b.scaleY, t);
    mixed.skewX = blendAngle(a.skewX, b.skewX, t, rotation, extraTurns);
    mixed.skewY = blendAngle(a.skewY, b.skewY, t, rotation, extraTurns);
    mixed.tx = blend(a.tx, b.tx, t);
    mixed.ty = blend(a.ty, b.ty, t);
    return putTogether(mixed);
}

namespace {

/// What a classic tween can move, and what makes two of them the same.
bool tweenable(const Element* element)
{
    if (!element)
        return false;
    const Element::Type type = element->elementType();
    return type == Element::Type::SymbolInstance || type == Element::Type::Group ||
           type == Element::Type::StaticText || type == Element::Type::BitmapInstance;
}

std::string identity(const Element* element)
{
    if (const auto* instance = dynamic_cast<const SymbolInstance*>(element))
        return "symbol:" + instance->libraryItemName;
    if (const auto* bitmap = dynamic_cast<const BitmapInstance*>(element))
        return "bitmap:" + bitmap->libraryItemName;
    return std::to_string(static_cast<int>(element->elementType()));
}

} // namespace

std::vector<int> pairTweenElements(const std::vector<Element*>& from, const std::vector<Element*>& to)
{
    std::vector<int> pairs(from.size(), -1);
    std::vector<bool> used(to.size(), false);
    // The same symbol at the same place.
    for (size_t i = 0; i < from.size() && i < to.size(); ++i)
    {
        if (tweenable(from[i]) && tweenable(to[i]) && identity(from[i]) == identity(to[i]))
        {
            pairs[i] = static_cast<int>(i);
            used[i] = true;
        }
    }
    // The same symbol elsewhere in the stack.
    for (size_t i = 0; i < from.size(); ++i)
    {
        if (pairs[i] >= 0 || !tweenable(from[i]))
            continue;
        for (size_t k = 0; k < to.size(); ++k)
        {
            if (!used[k] && tweenable(to[k]) && identity(from[i]) == identity(to[k]))
            {
                pairs[i] = static_cast<int>(k);
                used[k] = true;
                break;
            }
        }
    }
    // A swapped symbol, alone on both keyframes.
    if (from.size() == 1 && to.size() == 1 && pairs[0] < 0 && tweenable(from[0]) && tweenable(to[0]) &&
        from[0]->elementType() == to[0]->elementType())
        pairs[0] = 0;
    return pairs;
}

} // namespace fla
