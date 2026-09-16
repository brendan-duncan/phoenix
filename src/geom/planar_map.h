#pragma once

#include "curve.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace fla {

/// A point where pieces of curve meet.
struct MapVertex
{
    Point position;

    /// Half-edges leaving this vertex, sorted by the angle they leave at.
    /// Walking faces depends on that order.
    std::vector<int> outgoing;
};

/// A piece of curve, travelled in one direction.
///
/// Every piece appears twice, once each way. Which side a fill is on is a
/// property of the direction, which is exactly what `fillStyle0` and
/// `fillStyle1` record in the file format.
struct HalfEdge
{
    int from = -1;
    int to = -1;

    /// The same piece travelled the other way.
    int twin = -1;

    /// The next half-edge around the same face.
    int next = -1;

    int face = -1;

    /// Geometry, oriented from `from` to `to`.
    Curve curve;

    /// Which input curve this piece came from, so styles can be carried across.
    int source = -1;

    /// The fill on the left of this direction, which is the fill of the face
    /// this half-edge belongs to. The opposite direction carries the fill on the
    /// other side, and the pair of them is what the file format stores as
    /// `fillStyle0` and `fillStyle1`.
    int leftFill = -1;
};

/// A region enclosed by half-edges.
struct MapFace
{
    /// One half-edge on the outer boundary; the rest follow through `next`.
    int halfEdge = -1;

    /// One half-edge from each boundary enclosed by this face. A shape drawn
    /// inside another leaves a hole rather than a separate region, and the fill
    /// has to know about it.
    std::vector<int> holes;

    /// The single unbounded face, which is everything outside the drawing.
    bool unbounded = false;

    /// Signed area of the boundary. Bounded faces come out positive.
    double area = 0.0;

    /// What paints this region, or -1 for nothing.
    int fillStyle = -1;
};

/// The arrangement of a set of curves: every crossing becomes a vertex, every
/// piece between crossings becomes a pair of opposite half-edges, and every
/// enclosed region becomes a face.
///
/// This is what merge drawing needs and what a boolean kernel does not give: the
/// faces are the regions a fill can occupy, and the half-edges are where a left
/// and right fill get recorded.
///
/// Coordinates are snapped to the twip grid, which is the resolution the file
/// format stores anyway. That turns "are these two points the same" from a
/// tolerance question into an exact one, which is what keeps the topology
/// consistent.
///
/// Free of Qt.
class PlanarMap
{
public:
    /// One twentieth of a pixel: the grid Flash stores coordinates on.
    static constexpr double kTwip = 1.0 / 20.0;

    /// Adds a curve to be arranged. Nothing is computed until build().
    void addCurve(const Curve& curve, int source = -1);

    /// Splits every curve at its crossings, welds coincident points, and builds
    /// the half-edges and faces.
    void build(double tolerance = kTwip / 4.0);

    void clear();

    const std::vector<MapVertex>& vertices() const { return _vertices; }

    const std::vector<HalfEdge>& halfEdges() const { return _halfEdges; }

    const std::vector<MapFace>& faces() const { return _faces; }

    /// The unbounded face's index, or -1 before build().
    int unboundedFace() const { return _unboundedFace; }

    /// Rounds a coordinate onto the twip grid.
    static Point snapToTwips(const Point& point);

    /// The half-edges forming a boundary, starting from one of them.
    std::vector<int> cycleFrom(int halfEdge) const;

    /// Whether a point is inside a face: within its outer boundary and outside
    /// every hole.
    bool faceContains(int face, const Point& point) const;

    /// A point strictly inside a face, for asking what covers it. Returns false
    /// for the unbounded face, which has no inside.
    bool interiorPoint(int face, Point& result) const;

    /// The fill each face ended up with, assigned by attributeFills().
    int faceFill(int face) const;

    void setFaceFill(int face, int fillStyle);

private:
    struct Input
    {
        Curve curve;
        int source = -1;
    };

    /// Finds or creates the vertex at a snapped position.
    int vertexAt(const Point& snapped);

    /// Cuts every input curve at its crossings with the others and with itself.
    std::vector<Input> splitAtCrossings(double tolerance) const;

    void buildHalfEdges(const std::vector<Input>& pieces);

    void sortOutgoingEdges();

    void linkFaces();

    /// A closed run of half-edges, found by following `next`.
    struct Cycle
    {
        std::vector<int> edges;
        /// Positive for a region, negative for a boundary seen from outside.
        double area = 0.0;
    };

    std::vector<Cycle> collectCycles() const;

    /// Whether a point falls inside the region a cycle encloses.
    bool cycleContains(const Cycle& cycle, const Point& point) const;

    /// A point just outside the region a cycle encloses, for asking which face
    /// the cycle sits in. A vertex of the cycle will not do: it lies on the
    /// boundary, where a ray cast can answer either way.
    Point sampleOutside(const Cycle& cycle) const;

    std::vector<Input> _inputs;
    std::vector<MapVertex> _vertices;
    std::vector<HalfEdge> _halfEdges;
    std::vector<MapFace> _faces;

    /// Vertex lookup by exact twip coordinates, which is what snapping buys:
    /// welding becomes a dictionary hit rather than a distance search.
    std::map<std::pair<int64_t, int64_t>, int> _vertexLookup;

    int _unboundedFace = -1;
};

} // namespace fla
