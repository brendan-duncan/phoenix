#pragma once

#include "curve.h"
#include "planar_map.h"

#include <functional>
#include <map>
#include <vector>

namespace fla {

class Shape;

/// One curve taken from a shape's edges, with the styles the file recorded
/// alongside it.
///
/// The format stores a fill for each side of an edge and a stroke for the edge
/// itself. Which side is which depends on the direction the edge was written in,
/// so the curve and its styles have to travel together.
struct ShapeCurve
{
    Curve curve;

    /// Index of the Edge this came from.
    int edgeIndex = -1;

    /// The two fills the file records, one per side, and the stroke.
    int fillStyle0 = -1;
    int fillStyle1 = -1;
    int strokeStyle = -1;
};

/// Flattens a shape's edges into curves, keeping each one's styles.
///
/// Drawing commands become curves: a line or a cubic between two points, with
/// quadratics raised to cubics. A close command returns to where its subpath
/// began, so a closed outline comes back closed.
///
/// Free of Qt.
std::vector<ShapeCurve> shapeCurves(const Shape& shape);

/// Reads the fills the file already recorded into the arrangement's faces.
///
/// Each half-edge's source says what lies on each of its sides, and the
/// convention is that `fillStyle1` is the fill on the left of the direction the
/// edge was written in. That is enough to say what every face is painted with,
/// without re-deciding it from paint order.
///
/// Useful for taking an existing shape apart and putting it back together, and
/// for checking that an arrangement agrees with the file it came from.
void attributeFillsFromSource(PlanarMap& map, const std::vector<ShapeCurve>& sources);

/// What one shape paints on a face of an arrangement that holds several
/// shapes' curves, read from that shape's own edges round the face: a
/// half-edge with the face on its left carries the fill its source recorded on
/// that side. Only sources in [\a first, \a last) count as the shape's.
/// Returns false when none of the shape's edges border the face, in which
/// case the caller has to ask geometrically.
bool faceFillFromSources(const PlanarMap& map, int face, const std::vector<ShapeCurve>& sources,
    size_t first, size_t last, int& fill);

/// Joins curves running end to start into one cubic, when one cubic can
/// follow them all to within \a tolerance.
///
/// The joined curve keeps the run's ends and the directions it leaves and
/// arrives in; only how far the handles reach is fitted, by least squares
/// against points along the pieces. This is how a curve an earlier merge cut
/// into separate edges is put back together once what cut it has gone, which
/// the pieces' own parameters cannot do, since they no longer share a source.
/// Returns false, leaving \a joined alone, when the fit strays further than
/// \a tolerance from any of the pieces.
bool joinCurves(const std::vector<Curve>& pieces, double tolerance, Curve& joined);

/// The outlines of every fill of a shape, read off its arrangement: for each
/// fill, each face it paints as its outer boundary followed by its holes, a
/// loop of curves each, walked with the face on the left. Faces too thin to
/// see are left out.
///
/// The edges' own fills have to describe the arrangement exactly: every
/// half-edge must find on its left the fill its source records on that side.
/// Where they do not -- outlines that do not quite close, a fill recorded on
/// one edge of a face and not another -- this returns false and the caller
/// falls back on tracing the fills from the edges as stored, which joins up
/// such leftovers the way they have always been drawn. Shapes with hidden
/// edges, or more curves than \a maximumCurves, are also left to the
/// tracing.
bool fillOutlinesFromMap(const Shape& shape,
    std::map<int, std::vector<std::vector<Curve>>>& outlines,
    size_t maximumCurves = 20000);

/// Decides whether a rebuilt piece keeps the stroke its source carried.
///
/// A merge uses this to drop the older artwork's strokes where the newer
/// drawing's fill has buried them: drawing a filled shape over something covers
/// what was there, outlines included.
using StrokeFilter = std::function<bool(const HalfEdge& half)>;

/// Chooses a stroke for a rebuilt piece in place of the one its source
/// carried. Returning kKeepStroke leaves the source's stroke alone. The ink
/// bottle uses this to stroke the outline of one fill without touching the
/// rest of the curve that outline is part of.
using StrokeOverride = std::function<int(const HalfEdge& half)>;

constexpr int kKeepStroke = -2;

/// Replaces a shape's edges with the arrangement's, keeping its fill and stroke
/// styles.
///
/// Each pair of opposite half-edges becomes one edge, carrying the fill from
/// each of its sides. A piece with no fill on either side and no stroke draws
/// nothing and is left out, which is how the seams inside a merged shape
/// disappear.
/// \a keepStroke, when given, is asked about each piece; a piece it rejects is
/// rebuilt without its stroke. A piece left with nothing on either side and no
/// stroke then drops out entirely, as any other such piece does.
void rebuildShapeEdges(Shape& shape, const PlanarMap& map,
    const std::vector<ShapeCurve>& sources,
    const StrokeFilter& keepStroke = nullptr,
    const StrokeOverride& strokeFor = nullptr);

} // namespace fla
