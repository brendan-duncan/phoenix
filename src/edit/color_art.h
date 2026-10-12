#pragma once

#include <vector>

namespace fla {

class Curve;
class Layer;
class Shape;
class Timeline;
class Transform;

/// Line and color art, as in Harmony: a drawing's clean lines on one layer
/// and its paint on the layer under it, so painting never disturbs a line
/// and lines can be redrawn without losing the paint. The Paint Bucket on
/// a color art layer fills the regions the line layer's strokes close,
/// leaving the lines where they are. Both are ordinary layers to Animate.
/// Free of Qt.
namespace colorart {

/// Whether \a layer is a color art layer, and the line layer above it
/// whose strokes it paints within (null when that layer is gone).
bool isColorArt(const Layer& layer);
Layer* lineLayerOf(const Timeline& timeline, const Layer& colorLayer);

/// \a shape with only its strokes, and with only its fills. The caller
/// owns them.
Shape* strokesOnly(const Shape& shape);
Shape* fillsOnly(const Shape& shape);

/// A color art layer for \a line, named "<name> Color": a keyframe where
/// \a line has one, holding the fills of its shapes. The caller owns it
/// until it is added under \a line.
Layer* newColorLayer(const Layer& line);

/// The strokes of what \a line shows on frame \a frame, as curves in the
/// space whose transform from the stage is \a stageToTarget, for the
/// bucket to paint within.
std::vector<Curve> lineCutters(const Layer& line, int frame, const Transform& stageToTarget);

/// \a cutters without those \a shape already has an edge along: the paint
/// on a color layer was cut by the same lines, and a curve laid twice
/// over itself would leave slivers between the copies.
std::vector<Curve> withoutShared(const std::vector<Curve>& cutters, const Shape& shape);

} // namespace colorart

} // namespace fla
