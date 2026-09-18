#pragma once

namespace fla {

class DOMElement;
class Shape;

/// Deep-copies a shape's geometry and styles. The copy owns everything it holds.
///
/// Undo needs a snapshot of a shape before an edit rewrites it, and a shape owns
/// its edges and styles outright, so the snapshot has to be a real copy rather
/// than a second pointer to the same things.
Shape* cloneShape(const Shape& shape, DOMElement* parent);

/// Replaces everything in a target shape with a copy of another.
void setShapeContents(Shape& target, const Shape& source);

/// Merges one shape into another, the way Animate's default drawing mode does.
///
/// Drawing over an existing shape does not stack a second object on top of it.
/// The two become one: outlines cut each other where they cross, the newer fill
/// replaces the older wherever they overlap, and any seam left between two
/// regions that ended up the same colour disappears.
///
/// This is what the planar map was built for. The two shapes are taken apart
/// into an arrangement, every region is asked what paints it, and the result is
/// written back out as edges.
class ShapeMerger
{
public:
    /// Merges \a addition into \a target, rewriting the target in place.
    ///
    /// The addition's fills and strokes are copied into the target and
    /// renumbered, since the two shapes number their styles independently and
    /// the same index means different things in each.
    ///
    /// The addition is left untouched; the caller still owns it.
    ///
    /// Returns false when there is nothing to merge, leaving the target alone.
    static bool merge(Shape& target, const Shape& addition);

    /// Takes  cutter's filled area out of  target, leaving a hole.
    ///
    /// The inverse of merging, and what lifting a drawing back out of the
    /// artwork leaves behind. Drawing over something destroys what was under it,
    /// so moving the drawing away afterwards reveals the hole rather than
    /// putting anything back.
    ///
    /// The cutter contributes nothing of its own: no fill, and no outline around
    /// the hole. Only where it is filled does anything happen, so a stroke-only
    /// cutter takes nothing away.
    ///
    /// Returns false when there is nothing to cut, leaving the target alone.
    static bool subtract(Shape& target, const Shape& cutter);

private:
    enum class Operation
    {
        /// The addition paints its area, replacing what was there.
        Paint,
        /// The addition takes its area away.
        Erase
    };

    static bool combine(Shape& target, const Shape& addition, Operation operation);
};

} // namespace fla
