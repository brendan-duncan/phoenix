#pragma once

#include <functional>

namespace fla {

class Point;
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

/// Moves every point of a shape's geometry through \a map, which works in the
/// shape's own space. The free transform tool's Distort mode is built on
/// this: it is not an affine change, so it has to rewrite the points rather
/// than the transform. The bounds follow the points and the edge text the
/// writer prefers is dropped, since it no longer describes the geometry.
void warpShape(Shape& shape, const std::function<Point(const Point&)>& map);

/// Readies a shape's geometry for a warp that is not straight: a straight
/// segment only has its two ends to move, so it would stay straight however
/// the space between bends. Every segment is cut into pieces no longer than
/// \a pieceLength and written as a cubic, whose control points the warp can
/// then carry round the bend.
void prepareForWarp(Shape& shape, double pieceLength);

} // namespace fla
