#pragma once

#include "command.h"

#include "../data/point.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fla {

class SymbolInstance;

/// Deformers: a symbol instance's art bent by handles
/// rather than redrawn. A curve deformer bends it along a spine (an arm at
/// the elbow, a tail); an envelope deformer pulls it by a four by four grid
/// of handles around it (a squash, a lean); a corner pin puts its four
/// corners anywhere, in true perspective (a sign on a wall, a picture on a
/// screen, a floor going back). Each keyframe keeps its own pose
/// and a classic tween goes between them. Kept on the instance where other
/// apps that open the FLA ignore it, so they show the art as drawn. Free of
/// Qt.
namespace deform {

enum class Kind
{
    Curve,
    Envelope,
    CornerPin
};

struct Deformer
{
    Kind kind = Kind::Curve;
    /// The handles where they were put on the art, in the symbol's own
    /// space: a curve's chain of cubic Bezier points (3n + 1 of them), or an
    /// envelope's 16, row by row from the top left; a corner pin's 4, top
    /// left, top right, bottom right, bottom left.
    std::vector<Point> rest;
    /// And where they are now; the same count.
    std::vector<Point> posed;

    bool valid() const;
};

/// A curve along the long middle of \a left, \a top, \a right, \a bottom,
/// or an envelope around it, both at rest.
Deformer curveAround(double left, double top, double right, double bottom);
Deformer envelopeAround(double left, double top, double right, double bottom);
Deformer cornerPinAround(double left, double top, double right, double bottom);

/// The perspective map (a homography) that takes the four corners \a from
/// to the four corners \a to, in the same order: a 3x3 matrix, row-major,
/// taking (x, y, 1) to (X, Y, W) for the point (X / W, Y / W). Straight lines
/// stay straight. False when \a from has no area.
bool homography(const Point from[4], const Point to[4], std::array<double, 9>& map);

/// A corner pin's map from its rest corners to its posed ones; false for
/// any other deformer, or corners with no area.
bool cornerPinMap(const Deformer& deformer, std::array<double, 9>& map);

/// The instance's deformer, if it has one.
std::optional<Deformer> of(const SymbolInstance& instance);
std::string text(const Deformer& deformer);
std::optional<Deformer> parse(const std::string& text);

/// The instance's attributes with \a deformer on it, or none.
std::vector<std::pair<std::string, std::string>> withDeformer(const SymbolInstance& instance,
    const std::optional<Deformer>& deformer);

/// Sets or clears the instance's deformer, undoably.
CommandPtr setDeformer(SymbolInstance& instance, const std::optional<Deformer>& deformer, const std::string& name);

/// Part way between two poses of the same deformer (a classic tween); \a a
/// when they do not match.
Deformer blend(const Deformer& a, const Deformer& b, double t);

/// Whether every handle is where it was put, so nothing moves.
bool atRest(const Deformer& deformer);

/// Where the deformer takes \a point.
Point apply(const Deformer& deformer, const Point& point);

/// The point the deformer takes to \a shown, near it: where to look in the
/// art as drawn for what shows bent at \a shown (picking it on the stage).
Point unapply(const Deformer& deformer, const Point& shown);

} // namespace deform

} // namespace fla
