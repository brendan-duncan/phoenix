#pragma once

#include "../data/transform.h"

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

} // namespace fla
