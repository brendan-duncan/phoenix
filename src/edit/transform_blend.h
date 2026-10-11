#pragma once

#include "../data/transform.h"
#include "../data/color_transform.h"
#include "../data/filter.h"

#include <vector>

namespace fla {

/// The matrix part way between two keyframes' matrices, as a motion tween
/// shows it: each matrix is taken apart into translation, x and y scale and
/// the two skew angles Animate's Transform panel shows (a rotation is both
/// skews equal), those are blended, each angle the short way round, and the
/// result put back together. Blending the six entries directly shrinks a
/// turning object and skews it on the way.
/// Which way a classic tween turns: the short way round, or a given way
/// round with so many extra whole turns.
enum class TweenRotation
{
    Auto,
    Clockwise,
    CounterClockwise
};

Transform blendTransforms(const Transform& from, const Transform& to, double t,
    TweenRotation rotation = TweenRotation::Auto, int extraTurns = 0);

/// The color transform part way between two keyframes': every amount,
/// multiplier and offset blends straight, as does the tint color.
ColorTransform blendColorTransforms(const ColorTransform& from, const ColorTransform& to, double t);

/// The filters part way between two keyframes', as a classic tween shows
/// them. Filters pair up by position and kind; one that only one keyframe
/// has is met by the same kind at no effect (no blur, distance or
/// strength), as Animate puts in, so it fades in or out. Amounts and colors
/// blend; switches (inner, knockout, the bevel's placement) and a pair of
/// different kinds come from the first keyframe.
std::vector<Filter> blendFilters(const std::vector<Filter>& from, const std::vector<Filter>& to, double t);

} // namespace fla
