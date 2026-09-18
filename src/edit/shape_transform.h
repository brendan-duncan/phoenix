#pragma once

namespace fla {

class Shape;

/// Folds a shape's transform into its geometry, leaving the transform identity.
///
/// Merging works on raw edge coordinates: it takes two shapes apart into a
/// single arrangement, which only means anything if both are already in the
/// same space. A shape that has been dragged carries the move in its transform
/// rather than in its points, so it has to be brought down into its geometry
/// before it can merge with anything.
///
/// The shape draws identically afterwards -- the same points, reached a
/// different way.
///
/// Does nothing when the transform is already the identity.
void bakeTransform(Shape& shape);

} // namespace fla
