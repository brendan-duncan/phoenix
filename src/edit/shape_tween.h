#pragma once

#include "../data/transform.h"

#include <cstdint>

namespace fla {

class DOMElement;
class FillStyle;
class Frame;
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

/// The morph a new shape tween stores, worked out from its keyframes' shapes
/// as Animate does when a tween is created or a keyframe edited.
///
/// Each fill's outlines are chained from the edges with the fill on their
/// left, and each stroke's runs from the edges carrying it. Outlines pair up
/// fill by fill, outer outlines with outer and holes with holes, largest
/// first; strokes pair the same way by length. One with no partner grows
/// from, or shrinks to, a point where it is. A pair is cut so both have
/// their pieces at the same fractions of their length, a closed pair turned
/// so its start points match best, and each piece becomes quadratics, one
/// per pair of straight pieces and two otherwise. The caller owns the result.
MorphShape* buildMorph(const Shape& from, const Shape& to, DOMElement* parent);

/// A fingerprint of everything buildMorph reads from \a from and \a to;
/// never zero.
uint64_t morphSignature(const Shape& from, const Shape& to);

/// The shapes a shape tween runs between: the first shape on each keyframe.
/// False when either has none.
bool tweenShapes(const Frame& start, const Frame& end, const Shape*& from, const Shape*& to);

/// Makes \a start's morph match its shapes and \a end's: one the file stored
/// is taken as it is the first time, and from then on any change to either
/// shape builds it afresh. Returns whether it built one.
bool ensureMorph(Frame& start, const Frame& end);

} // namespace fla
