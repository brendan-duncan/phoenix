#pragma once

#include "../data/point.h"

#include <string>
#include <vector>

namespace fla {

/// One pin of an Asset Warp: where it was placed on the art and where it has
/// been dragged to, in the puppet symbol's space, in pixels.
struct WarpPin
{
    int id = 0;
    Point from;
    Point to;
    /// How far the pin was turned, in radians, clockwise on the stage (the
    /// file's rotationAngle, for a pin in turning mode): the art right
    /// round it turns with it.
    double rotation = 0.0;
    /// A frozen pin (pointFreeze): the art right round it keeps its own
    /// angle, whatever the pins about it do.
    bool frozen = false;
    /// The pins joined to this one by bones (the file's rig tree): the art
    /// along each bone follows the line between the two.
    std::vector<int> children;
};

namespace AssetWarp {

/// The pins of a puppet instance, read from its puppetHandles attribute (a
/// JSON document the FLA stores): each puppetPoints entry's orgLocation, in
/// pixels, and newLocation, in twips, its turn and whether it is frozen, and
/// the bones of the rigTree. Empty when there are none.
std::vector<WarpPin> pins(const std::string& puppetHandles);

/// Pins part way between two keyframes' (a classic tween of a puppet):
/// matched by ID, each dragged position between the two. Pins only in \a a
/// stay as they are.
std::vector<WarpPin> blend(const std::vector<WarpPin>& a, const std::vector<WarpPin>& b, double t);

/// Where the warp takes \a point: a rigid moving-least-squares deformation
/// (Schaefer, McPhail and Warren 2006) with the pins as handles, so the art
/// near a pin goes with it, turning but not stretching. One pin moves
/// everything with it; none leaves the art alone. The warp the format was made for builds
/// a triangle mesh and is not documented; this is a close stand-in.
Point warp(const std::vector<WarpPin>& pins, const Point& point);

/// \a puppetHandles with its pins made \a pins: a pin kept keeps the rest
/// of what the file stored for it and takes its new place; a new one is
/// written as FLA files store a pin just placed. The rig tree lists the
/// pins left, and the rest of the document is kept as it was (or, for an
/// empty one, as FLA files start one).
std::string withPins(const std::string& puppetHandles, const std::vector<WarpPin>& pins);

} // namespace AssetWarp

} // namespace fla
