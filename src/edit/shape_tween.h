#pragma once

#include "../data/transform.h"

namespace fla {

class DOMElement;
class FillStyle;
class MorphShape;
class Shape;
class StrokeStyle;

/// A shape tween part way between its keyframes, free of Qt.
///
/// The file stores the tween as a MorphShape on the first keyframe: a list
/// of segments, each a run of quadratic curves with a start and an end
/// position for every point (the A and B points, in the first and the last
/// keyframe's own space), and the fill on either side and the stroke by
/// index into the keyframes' style tables. The shape shown at progress t
/// has every point part way from A to B, and every style part way between
/// the first keyframe's and the last keyframe's style of the same index.

/// The fill between \a a and \a b at \a t: colors blend, gradients blend
/// stop by stop and matrix and all when they are alike, anything else is a
/// copy of \a a. The result is owned by the caller.
FillStyle* blendFills(const FillStyle* a, const FillStyle* b, double t, DOMElement* parent);

/// The stroke between \a a and \a b: the weight and the fill blend, the
/// rest is \a a's. The result is owned by the caller.
StrokeStyle* blendStrokes(const StrokeStyle* a, const StrokeStyle* b, double t, DOMElement* parent);

/// The shape the tween shows at \a t, built from the morph: \a from and \a to
/// are the keyframes' shapes, \a fromToDocument and \a toToDocument take each
/// one's points to the stage, and \a documentToLocal brings the result into
/// the space it is to be drawn in. A segment without any style index (as
/// some files write them) takes the first keyframe's first fill and stroke.
/// The caller owns the result.
Shape* morphedShape(const MorphShape& morph, const Shape& from, const Shape& to, double t,
    const Transform& fromToDocument, const Transform& toToDocument,
    const Transform& documentToLocal);

} // namespace fla
