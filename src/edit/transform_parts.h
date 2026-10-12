#pragma once

#include "../data/transform.h"

#include <cmath>

namespace fla {

/// A matrix as Animate's Transform and Properties panels show it: scale in
/// percent on each axis and skew in degrees on each, equal skews being a
/// rotation. A mirrored matrix shows as a negative vertical scale.
struct TransformParts
{
    double scaleX = 100.0;
    double scaleY = 100.0;
    double skewX = 0.0;
    double skewY = 0.0;

    static TransformParts of(const Transform& t)
    {
        constexpr double degrees = 180.0 / 3.14159265358979323846;
        TransformParts parts;
        parts.scaleX = std::sqrt(t.m11 * t.m11 + t.m12 * t.m12) * 100.0;
        parts.scaleY = std::sqrt(t.m21 * t.m21 + t.m22 * t.m22) * 100.0;
        parts.skewY = std::atan2(t.m12, t.m11) * degrees;
        parts.skewX = std::atan2(-t.m21, t.m22) * degrees;
        if (t.m11 * t.m22 - t.m12 * t.m21 < 0.0)
        {
            parts.scaleY = -parts.scaleY;
            parts.skewX = std::atan2(t.m21, -t.m22) * degrees;
        }
        return parts;
    }

    /// The matrix of these parts, placed at \a tx, \a ty.
    Transform matrix(double tx = 0.0, double ty = 0.0) const
    {
        constexpr double radians = 3.14159265358979323846 / 180.0;
        const double sx = scaleX / 100.0;
        const double sy = scaleY / 100.0;
        return Transform(sx * std::cos(skewY * radians), sx * std::sin(skewY * radians), -sy * std::sin(skewX * radians),
            sy * std::cos(skewX * radians), tx, ty);
    }
};

} // namespace fla
