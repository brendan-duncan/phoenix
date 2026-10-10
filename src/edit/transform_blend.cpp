#include "transform_blend.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Parts
{
    double scaleX = 1.0;
    double scaleY = 1.0;
    double skewX = 0.0; // the angle of the y axis, as Animate's Skew X
    double skewY = 0.0; // the angle of the x axis, as Animate's Skew Y
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

Transform blendTransforms(const Transform& from, const Transform& to, double t,
    TweenRotation rotation, int extraTurns)
{
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

} // namespace fla
