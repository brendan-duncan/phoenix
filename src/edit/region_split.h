#pragma once

#include "../data/point.h"
#include "../geom/planar_map.h"
#include "../geom/shape_geometry.h"

#include <functional>
#include <vector>

namespace fla {

class Shape;

/// What a click on merge-drawn artwork picked: one fill region, or one run of
/// stroke, of the shape's arrangement.
struct RegionPick
{
    enum class Kind
    {
        None,
        Fill,
        Stroke
    };

    Kind kind = Kind::None;

    /// The face, for a fill.
    int face = -1;

    /// Forward half-edges whose strokes are picked: the one clicked for a
    /// stroke, or the fill's boundary strokes when a double-click took them
    /// with it. Empty for a plain fill.
    std::vector<int> strokeHalfEdges;

    bool isEmpty() const { return kind == Kind::None; }
};

/// Takes a merge-drawn shape apart the way Animate's arrow tool selects it:
/// by region rather than as a whole.
///
/// Animate selects the fill or the stroke segment you click, not the shape it
/// is part of. Phoenix keeps one shape per layer, so selecting a region means
/// lifting it out as a shape of its own -- the piece -- and leaving the rest
/// behind as the remainder. The piece floats, selected, exactly where it was,
/// so nothing looks different until it is moved or deleted; letting go of it
/// untouched puts it straight back.
///
/// Free of Qt.
class RegionSplitter
{
public:
    explicit RegionSplitter(const Shape& shape);

    /// Arranges the shape together with \a cutters, closed outlines that
    /// divide its regions without painting anything, so that the regions on
    /// either side of a marquee or a lasso can be told apart and lifted out.
    /// Each face keeps the fill the shape painted there.
    RegionSplitter(const Shape& shape, const std::vector<Curve>& cutters);

    /// Whether the shape had any geometry to arrange.
    bool valid() const { return !_curves.empty(); }

    /// What sits under \a localPos, in the shape's own space. A stroke within
    /// \a tolerance wins over the fill beneath it, since strokes draw on top.
    /// With \a extended, as for a double-click, a fill takes the strokes round
    /// it and a stroke takes every stroke connected to it through corners
    /// where nothing else meets.
    RegionPick pickAt(const Point& localPos, double tolerance, bool extended) const;

    /// Builds the lifted piece and what is left. Both are new shapes the caller
    /// owns; the remainder carries the source's styles and is meant to replace
    /// the source's contents. Returns false for an empty pick.
    bool split(const RegionPick& pick, Shape*& piece, Shape*& remainder) const;

    /// Every painted face whose interior satisfies \a inside. After the shape
    /// has been divided along an area's outline, this is the set of regions
    /// within that area.
    std::vector<int> facesWhere(const std::function<bool(const Point&)>& inside) const;

    /// Straight joins that close the gaps in an outline that does not quite
    /// meet: each loose stroke end is joined to the nearest other loose end
    /// within \a gap, or failing that to the nearest point on another piece
    /// within \a gap. Arranging the shape with these as cutters is how the
    /// paint bucket's gap size setting lets it fill such an outline; the
    /// joins become fill boundaries without a stroke.
    std::vector<Curve> gapJoins(double gap) const;

    /// Every stroked piece, as a forward half-edge, lying wholly where
    /// \a inside holds.
    std::vector<int> strokesWhere(const std::function<bool(const Point&)>& inside) const;

    /// Lifts several fills and strokes out together as one piece, leaving the
    /// rest, as split does for one. Both results are new shapes the caller
    /// owns. Returns false when nothing was given.
    bool splitSet(const std::vector<int>& faces, const std::vector<int>& strokes,
        Shape*& piece, Shape*& remainder) const;

    /// The bounded face under a point, or -1. Unlike pickAt this answers for an
    /// enclosed area holding no fill, which is what the paint bucket fills.
    int faceAt(const Point& localPos) const;

    /// What paints a face, or -1.
    int faceFill(int face) const;

    /// The stroke of a stroke pick, or -1.
    int strokeOf(const RegionPick& pick) const;

    /// The shape's contents with \a face painted \a fillIndex, as a new shape
    /// the caller owns. A seam between two faces of one colour disappears.
    Shape* withFaceFill(int face, int fillIndex) const;

    /// The shape's contents with \a strokeIndex along the pick: round a fill's
    /// outline, or along the picked strokes. -1 removes the stroke. A new
    /// shape the caller owns.
    Shape* withStroke(const RegionPick& pick, int strokeIndex) const;

private:
    int strokeOf(const HalfEdge& half) const;

    /// The forward half-edge of a pair.
    int forwardOf(int half) const;

    /// Every half-edge round a face: its outer boundary and each hole.
    std::vector<int> faceBoundary(int face) const;

    /// Strokes joined to \a half through vertices where exactly two pieces
    /// meet, which is what a double-click on a stroke selects.
    std::vector<int> connectedStrokes(int half) const;

    const Shape& _shape;
    std::vector<ShapeCurve> _curves;
    PlanarMap _map;
};

} // namespace fla
