// Shape geometry: a shape's drawing as vertices, edges, half-edges and
// contours, read off the planar map merge drawing keeps it in, with the
// fills and strokes as plain objects. Edits (beginEdit, deleteEdge,
// Vertex.setLocation, Edge.splitEdge, Edge.setControl, endEdit) rebuild the
// shape through the same map, as one step of the script's undo.

#include "extensions.h"

#include "../data/bitmap_fill.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../geom/curve.h"
#include "../geom/planar_map.h"
#include "../geom/shape_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

namespace jsfl {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// A shape's drawing taken apart: every piece of curve between two meeting
/// points is an edge, each traveled both ways by a pair of half-edges (2k
/// and 2k + 1 for edge k, the first in the edge's own direction), and every
/// loop of half-edges round a region is a contour. A snapshot: it does not
/// change when the shape does; the next read takes a new one.
struct Geometry
{
    struct EdgeData
    {
        fla::Curve curve;
        /// The middle control point of the quadratic the edge is, or the
        /// nearest quadratic to a cubic.
        fla::Point control;
        /// The curve as drawn (its cubic segment), and the styles: the fill
        /// on each side of the edge's direction and its stroke.
        int source = -1;
        int leftFill = -1;
        int rightFill = -1;
        int stroke = -1;
    };
    struct HalfData
    {
        int vertex = -1;
        int next = -1;
        int prev = -1;
        int contour = -1;
    };
    struct VertexData
    {
        fla::Point position;
        int halfEdge = -1;
    };
    struct ContourData
    {
        int halfEdge = -1;
        int fill = -1;
        bool interior = false;
        int orientation = 0;
    };

    std::vector<fla::ShapeCurve> sources;
    std::vector<EdgeData> edges;
    std::vector<HalfData> halves;
    std::vector<VertexData> vertices;
    std::vector<ContourData> contours;
    /// What the drawing was made from, to tell whether it still holds.
    uint64_t fingerprint = 0;

    /// Edits made between beginEdit and endEdit, applied together.
    bool editing = false;
    std::vector<bool> deleted;
    std::vector<fla::Point> moved;
    std::vector<std::vector<double>> splits;
    std::map<std::pair<int, int>, fla::Point> controls;
    bool changed = false;

    fla::Point vertexAt(int vertex) const { return moved.empty() ? vertices[static_cast<size_t>(vertex)].position : moved[static_cast<size_t>(vertex)]; }
};

using GeometryPtr = std::shared_ptr<Geometry>;

// Fingerprints, so a drawing read twice is taken apart once.

void mix(uint64_t& hash, const void* data, size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

void mix(uint64_t& hash, double value)
{
    mix(hash, &value, sizeof(value));
}

void mix(uint64_t& hash, int value)
{
    mix(hash, &value, sizeof(value));
}

uint64_t fingerprintOf(const std::vector<fla::ShapeCurve>& curves)
{
    uint64_t hash = 1469598103934665603ull;
    for (const fla::ShapeCurve& piece : curves)
    {
        for (int i = 0; i < 4; ++i)
        {
            mix(hash, piece.curve.controlPoint(i).x);
            mix(hash, piece.curve.controlPoint(i).y);
        }
        mix(hash, piece.curve.isLine() ? 1 : 0);
        mix(hash, piece.fillStyle0);
        mix(hash, piece.fillStyle1);
        mix(hash, piece.strokeStyle);
    }
    return hash;
}

/// The curves of a rectangle or oval object, filled with style 1 inside
/// and stroked with style 1, as breaking it apart would draw them.
std::vector<fla::ShapeCurve> primitiveCurves(const fla::Element& element)
{
    std::vector<fla::ShapeCurve> curves;
    const auto add = [&curves](const fla::Curve& curve, bool filled, bool stroked) {
        fla::ShapeCurve piece;
        piece.curve = curve;
        // Clockwise on the stage, which puts the inside on the left of the
        // direction as the planar map counts it.
        piece.fillStyle1 = filled ? 1 : -1;
        piece.strokeStyle = stroked ? 1 : -1;
        curves.push_back(piece);
    };
    if (const auto* rectangle = dynamic_cast<const fla::RectanglePrimitive*>(&element))
    {
        const fla::Rect& r = rectangle->rect;
        const bool filled = rectangle->fillStyle != nullptr;
        const bool stroked = rectangle->strokeStyle != nullptr;
        const double w = r.bottomRight.x - r.topLeft.x;
        const double h = r.bottomRight.y - r.topLeft.y;
        const double limit = std::min(std::fabs(w), std::fabs(h)) / 2.0;
        const auto radius = [limit](double value) { return std::clamp(std::fabs(value), 0.0, limit); };
        const double tl = radius(rectangle->topLeftRadius);
        const double tr = radius(rectangle->topRightRadius);
        const double br = radius(rectangle->bottomRightRadius);
        const double bl = radius(rectangle->bottomLeftRadius);
        const auto line = [&](double x0, double y0, double x1, double y1) {
            if (std::hypot(x1 - x0, y1 - y0) > 1e-9)
                add(fla::Curve::line(fla::Point(x0, y0), fla::Point(x1, y1)), filled, stroked);
        };
        const auto corner = [&](double x0, double y0, double cx, double cy, double x1, double y1, double rounded) {
            if (rounded > 1e-9)
                add(fla::Curve::quadratic(fla::Point(x0, y0), fla::Point(cx, cy), fla::Point(x1, y1)), filled, stroked);
        };
        line(r.topLeft.x + tl, r.topLeft.y, r.bottomRight.x - tr, r.topLeft.y);
        corner(r.bottomRight.x - tr, r.topLeft.y, r.bottomRight.x, r.topLeft.y, r.bottomRight.x, r.topLeft.y + tr, tr);
        line(r.bottomRight.x, r.topLeft.y + tr, r.bottomRight.x, r.bottomRight.y - br);
        corner(r.bottomRight.x, r.bottomRight.y - br, r.bottomRight.x, r.bottomRight.y, r.bottomRight.x - br, r.bottomRight.y, br);
        line(r.bottomRight.x - br, r.bottomRight.y, r.topLeft.x + bl, r.bottomRight.y);
        corner(r.topLeft.x + bl, r.bottomRight.y, r.topLeft.x, r.bottomRight.y, r.topLeft.x, r.bottomRight.y - bl, bl);
        line(r.topLeft.x, r.bottomRight.y - bl, r.topLeft.x, r.topLeft.y + tl);
        corner(r.topLeft.x, r.topLeft.y + tl, r.topLeft.x, r.topLeft.y, r.topLeft.x + tl, r.topLeft.y, tl);
    }
    else if (const auto* oval = dynamic_cast<const fla::OvalPrimitive*>(&element))
    {
        // Eight quadratics round the whole ellipse.
        const fla::Rect& r = oval->rect;
        const double cx = (r.topLeft.x + r.bottomRight.x) / 2.0;
        const double cy = (r.topLeft.y + r.bottomRight.y) / 2.0;
        const double rx = (r.bottomRight.x - r.topLeft.x) / 2.0;
        const double ry = (r.bottomRight.y - r.topLeft.y) / 2.0;
        const double reach = 1.0 / std::cos(kPi / 8.0);
        for (int k = 0; k < 8; ++k)
        {
            const double a0 = k * kPi / 4.0;
            const double a1 = (k + 1) * kPi / 4.0;
            const double am = (a0 + a1) / 2.0;
            add(fla::Curve::quadratic(fla::Point(cx + rx * std::cos(a0), cy + ry * std::sin(a0)),
                    fla::Point(cx + rx * reach * std::cos(am), cy + ry * reach * std::sin(am)),
                    fla::Point(cx + rx * std::cos(a1), cy + ry * std::sin(a1))),
                oval->fillStyle != nullptr, oval->strokeStyle != nullptr);
        }
    }
    return curves;
}

std::vector<fla::ShapeCurve> curvesOf(const fla::Element& element)
{
    if (const auto* shape = dynamic_cast<const fla::Shape*>(&element))
        return fla::shapeCurves(*shape);
    return primitiveCurves(element);
}

/// The quadratic a cubic was raised from, or the nearest one.
fla::Point quadraticControl(const fla::Curve& curve)
{
    const fla::Point& p0 = curve.controlPoint(0);
    const fla::Point& c1 = curve.controlPoint(1);
    const fla::Point& c2 = curve.controlPoint(2);
    const fla::Point& p3 = curve.controlPoint(3);
    if (curve.isLine())
        return fla::Point((p0.x + p3.x) / 2.0, (p0.y + p3.y) / 2.0);
    const fla::Point q1((3.0 * c1.x - p0.x) / 2.0, (3.0 * c1.y - p0.y) / 2.0);
    const fla::Point q2((3.0 * c2.x - p3.x) / 2.0, (3.0 * c2.y - p3.y) / 2.0);
    if (std::hypot(q1.x - q2.x, q1.y - q2.y) < 1e-6)
        return q1;
    return fla::Point((q1.x + q2.x) / 2.0, (q1.y + q2.y) / 2.0);
}

/// Twice the area a loop of curves encloses, positive when it runs
/// clockwise on the stage (y down).
double loopArea(const std::vector<fla::Curve>& loop)
{
    double total = 0.0;
    for (const fla::Curve& curve : loop)
    {
        fla::Point previous = curve.start();
        for (int i = 1; i <= 16; ++i)
        {
            const fla::Point p = curve.pointAt(i / 16.0);
            total += previous.x * p.y - p.x * previous.y;
            previous = p;
        }
    }
    return total;
}

GeometryPtr build(std::vector<fla::ShapeCurve> sources, uint64_t fingerprint)
{
    auto geometry = std::make_shared<Geometry>();
    geometry->fingerprint = fingerprint;
    fla::PlanarMap map;
    for (size_t i = 0; i < sources.size(); ++i)
        map.addCurve(sources[i].curve, static_cast<int>(i));
    map.build();

    const std::vector<fla::HalfEdge>& halves = map.halfEdges();
    const size_t pairs = halves.size() / 2;
    geometry->edges.resize(pairs);
    geometry->halves.resize(pairs * 2);
    // The map's vertices that edges meet at, renumbered from 0.
    std::vector<int> vertexIndex(map.vertices().size(), -1);
    for (size_t k = 0; k < pairs; ++k)
    {
        const fla::HalfEdge& forward = halves[2 * k];
        Geometry::EdgeData& edge = geometry->edges[k];
        edge.curve = forward.curve;
        edge.control = quadraticControl(edge.curve);
        edge.source = forward.source;
        if (forward.source >= 0 && forward.source < static_cast<int>(sources.size()))
        {
            const fla::ShapeCurve& source = sources[static_cast<size_t>(forward.source)];
            edge.leftFill = forward.forward ? source.fillStyle1 : source.fillStyle0;
            edge.rightFill = forward.forward ? source.fillStyle0 : source.fillStyle1;
            edge.stroke = source.strokeStyle;
        }
        for (size_t side = 0; side < 2; ++side)
        {
            const fla::HalfEdge& half = halves[2 * k + side];
            int& vertex = vertexIndex[static_cast<size_t>(half.from)];
            if (vertex < 0)
            {
                vertex = static_cast<int>(geometry->vertices.size());
                Geometry::VertexData data;
                data.position = map.vertices()[static_cast<size_t>(half.from)].position;
                data.halfEdge = static_cast<int>(2 * k + side);
                geometry->vertices.push_back(data);
            }
            Geometry::HalfData& out = geometry->halves[2 * k + side];
            out.vertex = vertex;
            out.next = half.next;
        }
    }
    for (size_t h = 0; h < geometry->halves.size(); ++h)
    {
        const int next = geometry->halves[h].next;
        if (next >= 0 && next < static_cast<int>(geometry->halves.size()))
            geometry->halves[static_cast<size_t>(next)].prev = static_cast<int>(h);
    }

    // Each loop of half-edges is a contour: the outside of the drawing and
    // the edges of holes first, then the regions, each by the fill on its
    // left.
    std::vector<Geometry::ContourData> outer;
    std::vector<Geometry::ContourData> inner;
    std::vector<std::vector<int>> outerMembers;
    std::vector<std::vector<int>> innerMembers;
    std::vector<bool> seen(geometry->halves.size(), false);
    for (size_t start = 0; start < geometry->halves.size(); ++start)
    {
        if (seen[start])
            continue;
        std::vector<int> members;
        std::vector<fla::Curve> loop;
        int h = static_cast<int>(start);
        while (h >= 0 && !seen[static_cast<size_t>(h)])
        {
            seen[static_cast<size_t>(h)] = true;
            members.push_back(h);
            loop.push_back(halves[static_cast<size_t>(h)].curve);
            h = geometry->halves[static_cast<size_t>(h)].next;
        }
        Geometry::ContourData contour;
        contour.halfEdge = static_cast<int>(start);
        const Geometry::EdgeData& edge = geometry->edges[start / 2];
        contour.fill = start % 2 == 0 ? edge.leftFill : edge.rightFill;
        const int face = halves[start].face;
        if (face >= 0 && face < static_cast<int>(map.faces().size()))
        {
            const fla::MapFace& region = map.faces()[static_cast<size_t>(face)];
            contour.interior = !region.unbounded && std::find(members.begin(), members.end(), region.halfEdge) != members.end();
        }
        const double area = loopArea(loop);
        contour.orientation = std::fabs(area) < 1e-9 ? 0 : area > 0.0 ? 1 : -1;
        if (contour.interior)
        {
            inner.push_back(contour);
            innerMembers.push_back(std::move(members));
        }
        else
        {
            outer.push_back(contour);
            outerMembers.push_back(std::move(members));
        }
    }
    for (size_t i = 0; i < outer.size(); ++i)
    {
        for (int h : outerMembers[i])
            geometry->halves[static_cast<size_t>(h)].contour = static_cast<int>(geometry->contours.size());
        geometry->contours.push_back(outer[i]);
    }
    for (size_t i = 0; i < inner.size(); ++i)
    {
        for (int h : innerMembers[i])
            geometry->halves[static_cast<size_t>(h)].contour = static_cast<int>(geometry->contours.size());
        geometry->contours.push_back(inner[i]);
    }
    geometry->sources = std::move(sources);
    return geometry;
}

/// The drawings read lately, and those being edited, by element.
struct Cache
{
    std::map<const fla::Element*, GeometryPtr> recent;
    std::map<const fla::Element*, GeometryPtr> editing;
};

/// Each session's, kept until it finishes or settles: after that its
/// elements may be freed, and another made where one was.
std::map<const Session*, Cache>& caches()
{
    static std::map<const Session*, Cache> all;
    return all;
}

Cache& cache(const Element& e)
{
    return caches()[&e.session];
}

/// The element's drawing now: the one being edited, the one read before
/// when nothing has changed since, or a new one.
GeometryPtr geometryOf(Element& e)
{
    fla::Element& element = e.get();
    Cache& c = cache(e);
    const auto editing = c.editing.find(&element);
    if (editing != c.editing.end())
        return editing->second;
    std::vector<fla::ShapeCurve> curves = curvesOf(element);
    const uint64_t fingerprint = fingerprintOf(curves);
    const auto found = c.recent.find(&element);
    if (found != c.recent.end() && found->second->fingerprint == fingerprint)
        return found->second;
    GeometryPtr geometry = build(std::move(curves), fingerprint);
    if (c.recent.size() > 64)
        c.recent.clear();
    c.recent[&element] = geometry;
    return geometry;
}

// Fills and strokes, as the plain objects scripts read.

Value matrixValue(const fla::Transform& m)
{
    Value value = Value::map();
    value.set("a", m.m11);
    value.set("b", m.m12);
    value.set("c", m.m21);
    value.set("d", m.m22);
    value.set("tx", m.tx);
    value.set("ty", m.ty);
    return value;
}

template <class Entries>
void gradientValue(Value& value, const fla::FillStyle& fill, const Entries& entries)
{
    Value colors = Value::array();
    Value positions = Value::array();
    for (const auto& entry : entries)
    {
        colors.items.emplace_back(colorText(entry.color));
        positions.items.emplace_back(std::round(entry.ratio * 255.0));
    }
    value.set("colorArray", colors);
    value.set("posArray", positions);
    value.set("matrix", matrixValue(fill.transform));
    const std::string& spread = fill.spreadMethod;
    value.set("overflow", spread == "reflect" ? "Reflect" : spread == "repeat" ? "Repeat" : "Extend");
    value.set("linearRGB", fill.interpolationMethod == "linearRGB");
}

Value fillValue(const fla::FillStyle* fill)
{
    Value value = Value::map();
    if (!fill)
    {
        value.set("style", "noFill");
        return value;
    }
    switch (fill->type())
    {
    case fla::FillStyle::Type::SolidColor:
        value.set("style", "solid");
        value.set("color", colorText(static_cast<const fla::SolidColor*>(fill)->color));
        break;
    case fla::FillStyle::Type::LinearGradient:
        value.set("style", "linearGradient");
        gradientValue(value, *fill, static_cast<const fla::LinearGradient*>(fill)->entries);
        break;
    case fla::FillStyle::Type::RadialGradient:
    {
        const auto* radial = static_cast<const fla::RadialGradient*>(fill);
        value.set("style", "radialGradient");
        gradientValue(value, *fill, radial->entries);
        value.set("focalPoint", std::round(radial->focalPointRatio * 255.0));
        break;
    }
    case fla::FillStyle::Type::BitmapFill:
    {
        const auto* bitmap = static_cast<const fla::BitmapFill*>(fill);
        value.set("style", "bitmap");
        value.set("bitmapPath", bitmap->bitmapPath);
        value.set("bitmapIsClipped", bitmap->bitmapIsClipped);
        value.set("matrix", matrixValue(fill->transform));
        break;
    }
    }
    return value;
}

const char* strokeStyleName(fla::StrokeStyle::Style style)
{
    switch (style)
    {
    case fla::StrokeStyle::Style::Dashed: return "dashed";
    case fla::StrokeStyle::Style::Ragged: return "ragged";
    case fla::StrokeStyle::Style::Stipple: return "stipple";
    case fla::StrokeStyle::Style::Dotted: return "dotted";
    case fla::StrokeStyle::Style::Hatched: return "hatched";
    default: return "solid";
    }
}

Value strokeValue(const fla::StrokeStyle* stroke)
{
    Value value = Value::map();
    if (!stroke)
    {
        value.set("style", "noStroke");
        return value;
    }
    value.set("style", strokeStyleName(stroke->style()));
    value.set("thickness", stroke->isHairline() ? 0.1 : stroke->weight);
    const fla::FillStyle* fill = stroke->fill;
    if (fill && fill->type() == fla::FillStyle::Type::SolidColor)
        value.set("color", colorText(static_cast<const fla::SolidColor*>(fill)->color));
    else
        value.set("color", "#000000");
    value.set("shapeFill", fillValue(fill));
    value.set("scaleType", stroke->scaleMode.empty() ? std::string("normal") : stroke->scaleMode);
    value.set("capType", stroke->caps.empty() ? std::string("round") : stroke->caps);
    value.set("joinType", stroke->joints.empty() ? std::string("round") : stroke->joints);
    value.set("miterLimit", stroke->miterLimit);
    value.set("strokeHinting", stroke->pixelHinting);
    value.set("breakAtCorners", false);
    if (stroke->style() == fla::StrokeStyle::Style::Dashed)
    {
        value.set("dash1", stroke->dash1);
        value.set("dash2", stroke->dash2);
    }
    for (const auto& attribute : stroke->styleAttributes)
        value.set(attribute.first, attribute.second);
    return value;
}

const fla::FillStyle* fillStyleOf(const fla::Element& element, int index)
{
    if (index < 0)
        return nullptr;
    if (const auto* shape = dynamic_cast<const fla::Shape*>(&element))
        return shape->getFillStyleByIndex(index);
    if (const auto* rectangle = dynamic_cast<const fla::RectanglePrimitive*>(&element))
        return rectangle->fillStyle;
    if (const auto* oval = dynamic_cast<const fla::OvalPrimitive*>(&element))
        return oval->fillStyle;
    return nullptr;
}

const fla::StrokeStyle* strokeStyleOf(const fla::Element& element, int index)
{
    if (index < 0)
        return nullptr;
    if (const auto* shape = dynamic_cast<const fla::Shape*>(&element))
        return shape->getStrokeStyleByIndex(index);
    if (const auto* rectangle = dynamic_cast<const fla::RectanglePrimitive*>(&element))
        return rectangle->strokeStyle;
    if (const auto* oval = dynamic_cast<const fla::OvalPrimitive*>(&element))
        return oval->strokeStyle;
    return nullptr;
}

Value pointValue(const fla::Point& point)
{
    Value value = Value::map();
    value.set("x", point.x);
    value.set("y", point.y);
    return value;
}

// Edits.

/// The shape the edits apply to; rectangle and oval objects are drawn by
/// their settings, not their edges.
fla::Shape& editableShape(Element& e)
{
    auto* shape = dynamic_cast<fla::Shape*>(&e.get());
    if (!shape)
        throw Error("Only shapes can have their edges edited; break a rectangle or oval object apart first.");
    return *shape;
}

/// Starts edits on the drawing as it is, kept until endEdit.
GeometryPtr beginEdits(Element& e)
{
    editableShape(e);
    GeometryPtr geometry = geometryOf(e);
    if (!geometry->editing)
    {
        geometry->editing = true;
        geometry->deleted.assign(geometry->edges.size(), false);
        geometry->moved.clear();
        for (const Geometry::VertexData& vertex : geometry->vertices)
            geometry->moved.push_back(vertex.position);
        geometry->splits.assign(geometry->edges.size(), {});
        geometry->controls.clear();
        geometry->changed = false;
        cache(e).editing[e.element] = geometry;
    }
    return geometry;
}

/// The edited drawing written into the shape: the edges left, moved and
/// split, arranged again so regions an edge no longer parts join up.
void applyEdits(Element& e, Geometry& g)
{
    fla::Shape& shape = editableShape(e);
    std::vector<fla::ShapeCurve> curves;
    for (size_t k = 0; k < g.edges.size(); ++k)
    {
        if (g.deleted[k])
            continue;
        const Geometry::EdgeData& edge = g.edges[k];
        fla::Point p[4];
        for (int i = 0; i < 4; ++i)
            p[i] = edge.curve.controlPoint(i);
        // An end moved takes its handle with it.
        const fla::Point from = g.vertexAt(g.halves[2 * k].vertex);
        const fla::Point to = g.vertexAt(g.halves[2 * k + 1].vertex);
        const fla::Point d0(from.x - p[0].x, from.y - p[0].y);
        const fla::Point d3(to.x - p[3].x, to.y - p[3].y);
        p[0] = from;
        p[3] = to;
        fla::Curve curve;
        const auto moved = g.controls.find({static_cast<int>(k), 1});
        if (moved != g.controls.end())
            curve = fla::Curve::quadratic(p[0], moved->second, p[3]);
        else if (edge.curve.isLine())
            curve = fla::Curve::line(p[0], p[3]);
        else
            curve = fla::Curve::cubic(p[0], fla::Point(p[1].x + d0.x, p[1].y + d0.y), fla::Point(p[2].x + d3.x, p[2].y + d3.y), p[3]);
        std::vector<double> cuts = g.splits[k];
        std::sort(cuts.begin(), cuts.end());
        double at = 0.0;
        cuts.push_back(1.0);
        for (double cut : cuts)
        {
            if (cut <= at + 1e-9)
                continue;
            fla::ShapeCurve piece;
            piece.curve = at == 0.0 && cut == 1.0 ? curve : curve.subcurve(at, cut);
            piece.fillStyle1 = edge.leftFill;
            piece.fillStyle0 = edge.rightFill;
            piece.strokeStyle = edge.stroke;
            curves.push_back(piece);
            at = cut;
        }
    }
    fla::PlanarMap map;
    for (size_t i = 0; i < curves.size(); ++i)
        map.addCurve(curves[i].curve, static_cast<int>(i));
    map.build();
    fla::attributeFillsFromSource(map, curves);
    std::unique_ptr<fla::Element> copy(fla::cloneElement(shape, shape.parent));
    auto* after = dynamic_cast<fla::Shape*>(copy.get());
    if (!after)
        throw Error("The shape could not be edited.");
    // An edge per piece of the arrangement, as rebuildShapeEdges writes
    // them but without joining pieces that run on into one another: a
    // vertex the script made by splitting an edge stays.
    for (fla::Edge* old : after->edges)
        delete old;
    after->edges.clear();
    const std::vector<fla::HalfEdge>& halves = map.halfEdges();
    for (size_t h = 0; h + 1 < halves.size(); h += 2)
    {
        const fla::HalfEdge& half = halves[h];
        const int left = half.leftFill;
        const int right = halves[h + 1].leftFill;
        const int stroke = half.source >= 0 && half.source < static_cast<int>(curves.size())
            ? curves[static_cast<size_t>(half.source)].strokeStyle
            : -1;
        if (left == right && stroke == -1)
            continue;
        auto* edge = new fla::Edge(after);
        edge->fillStyle1 = left;
        edge->fillStyle0 = right;
        edge->strokeStyle = stroke;
        auto* path = new fla::Path(edge);
        const fla::Curve& curve = half.curve;
        path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move, {curve.start()}, path));
        const fla::Point q = quadraticControl(curve);
        const fla::Point& c1 = curve.controlPoint(1);
        const bool quadratic = std::hypot((2.0 * q.x + curve.start().x) / 3.0 - c1.x, (2.0 * q.y + curve.start().y) / 3.0 - c1.y) < 1e-6;
        if (curve.isLine())
            path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Line, {curve.end()}, path));
        else if (quadratic)
            path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Quad, {q, curve.end()}, path));
        else
            path->segments.push_back(
                new fla::PathSegment(fla::PathSegment::Command::Cubic, {c1, curve.controlPoint(2), curve.end()}, path));
        edge->paths.push_back(path);
        after->edges.push_back(edge);
    }
    after->cubicEdges.clear();
    e.push(fla::CommandPtr(new fla::SetShapeContentsCommand(&shape, *after, "Edit Shape")));
}

void endEdits(Element& e)
{
    Cache& c = cache(e);
    const auto found = c.editing.find(e.element);
    if (found == c.editing.end())
        return;
    GeometryPtr geometry = found->second;
    c.editing.erase(found);
    geometry->editing = false;
    if (geometry->changed)
        applyEdits(e, *geometry);
    c.recent.erase(e.element);
}

/// Runs \a change inside beginEdit and endEdit, or on its own when the
/// script did not call beginEdit. \a seen is the drawing the part changed
/// was read from, which must still be the shape's.
void edit(Element& e, const GeometryPtr& seen, const std::function<void(Geometry&)>& change)
{
    Cache& c = cache(e);
    const auto found = c.editing.find(e.element);
    const bool open = found != c.editing.end();
    const GeometryPtr current = open ? found->second : geometryOf(e);
    if (current != seen)
        throw Error("The shape changed since that part of it was read; read its edges again.");
    if (!open)
        beginEdits(e);
    change(*current);
    current->changed = true;
    if (!open)
        endEdits(e);
}

// The parts, each holding the drawing it came from and the element.

class ShapePart : public Object
{
public:
    ShapePart(std::shared_ptr<Element> owner, GeometryPtr geometry, int index)
        : owner(std::move(owner)), geometry(std::move(geometry)), index(index)
    {}
    const void* scope() const override { return owner->document; }
    std::shared_ptr<Element> owner;
    GeometryPtr geometry;
    int index;
};

class EdgePart final : public ShapePart
{
public:
    using ShapePart::ShapePart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return &geometry->edges[static_cast<size_t>(index)]; }
    const Geometry::EdgeData& data() const { return geometry->edges[static_cast<size_t>(index)]; }
};

class HalfEdgePart final : public ShapePart
{
public:
    using ShapePart::ShapePart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return &geometry->halves[static_cast<size_t>(index)]; }
    const Geometry::HalfData& data() const { return geometry->halves[static_cast<size_t>(index)]; }
};

class VertexPart final : public ShapePart
{
public:
    using ShapePart::ShapePart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return &geometry->vertices[static_cast<size_t>(index)]; }
};

class ContourPart final : public ShapePart
{
public:
    using ShapePart::ShapePart;
    const ClassInfo& classInfo() const override;
    const void* identity() const override { return &geometry->contours[static_cast<size_t>(index)]; }
    const Geometry::ContourData& data() const { return geometry->contours[static_cast<size_t>(index)]; }
};

std::shared_ptr<Element> ownerOf(Element& e)
{
    return std::make_shared<Element>(e.session, e.document, e.element, e.classInfo());
}

Value edgeValue(const std::shared_ptr<Element>& owner, const GeometryPtr& geometry, int index)
{
    if (index < 0 || index >= static_cast<int>(geometry->edges.size()))
        return Value::null();
    return Value(std::make_shared<EdgePart>(owner, geometry, index));
}

Value halfValue(const std::shared_ptr<Element>& owner, const GeometryPtr& geometry, int index)
{
    if (index < 0 || index >= static_cast<int>(geometry->halves.size()))
        return Value::null();
    return Value(std::make_shared<HalfEdgePart>(owner, geometry, index));
}

Value vertexValue(const std::shared_ptr<Element>& owner, const GeometryPtr& geometry, int index)
{
    if (index < 0 || index >= static_cast<int>(geometry->vertices.size()))
        return Value::null();
    return Value(std::make_shared<VertexPart>(owner, geometry, index));
}

const ClassInfo& EdgePart::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<EdgePart> def(info, "Edge");
        def.get("id", [](EdgePart& p) { return Value(p.index); });
        def.get("isLine", [](EdgePart& p) { return Value(p.data().curve.isLine()); });
        def.get("cubicSegmentIndex", [](EdgePart& p) { return Value(p.data().source); });
        def.get("stroke", [](EdgePart& p) { return strokeValue(strokeStyleOf(p.owner->get(), p.data().stroke)); });
        def.method("getControl", 1, [](EdgePart& p, const Args& args) {
            const int i = arg(args, 0).toInt();
            const Geometry& g = *p.geometry;
            if (i == 0)
                return pointValue(g.vertexAt(g.halves[static_cast<size_t>(2 * p.index)].vertex));
            if (i == 2)
                return pointValue(g.vertexAt(g.halves[static_cast<size_t>(2 * p.index + 1)].vertex));
            if (i == 1)
            {
                const auto moved = g.controls.find({p.index, 1});
                return pointValue(moved != g.controls.end() ? moved->second : p.data().control);
            }
            throw Error("An edge's control points are 0, 1 and 2.");
        });
        def.method("setControl", 3, [](EdgePart& p, const Args& args) {
            const int i = arg(args, 0).toInt();
            const fla::Point point(arg(args, 1).toNumber(), arg(args, 2).toNumber());
            if (i < 0 || i > 2)
                throw Error("An edge's control points are 0, 1 and 2.");
            edit(*p.owner, p.geometry, [&p, i, point](Geometry& g) {
                if (i == 1)
                    g.controls[{p.index, 1}] = point;
                else
                    g.moved[static_cast<size_t>(g.halves[static_cast<size_t>(2 * p.index + (i == 2 ? 1 : 0))].vertex)] = point;
            });
            return Value();
        });
        def.method("getHalfEdge", 1, [](EdgePart& p, const Args& args) {
            return halfValue(p.owner, p.geometry, 2 * p.index + (arg(args, 0).toInt() == 1 ? 1 : 0));
        });
        def.method("splitEdge", 1, [](EdgePart& p, const Args& args) {
            const double t = arg(args, 0).toNumber();
            if (!(t > 0.0 && t < 1.0))
                throw Error("An edge is split between its ends: at more than 0 and less than 1.");
            edit(*p.owner, p.geometry, [&p, t](Geometry& g) { g.splits[static_cast<size_t>(p.index)].push_back(t); });
            return Value();
        });
        return info;
    }();
    return info;
}

const ClassInfo& HalfEdgePart::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<HalfEdgePart> def(info, "HalfEdge");
        def.get("id", [](HalfEdgePart& p) { return Value(p.index); });
        def.method("getVertex", 0, [](HalfEdgePart& p, const Args&) { return vertexValue(p.owner, p.geometry, p.data().vertex); });
        def.method("getNext", 0, [](HalfEdgePart& p, const Args&) { return halfValue(p.owner, p.geometry, p.data().next); });
        def.method("getPrev", 0, [](HalfEdgePart& p, const Args&) { return halfValue(p.owner, p.geometry, p.data().prev); });
        def.method("getOppositeHalfEdge", 0, [](HalfEdgePart& p, const Args&) { return halfValue(p.owner, p.geometry, p.index ^ 1); });
        def.method("getEdge", 0, [](HalfEdgePart& p, const Args&) { return edgeValue(p.owner, p.geometry, p.index / 2); });
        return info;
    }();
    return info;
}

const ClassInfo& VertexPart::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<VertexPart> def(info, "Vertex");
        def.get("x", [](VertexPart& p) { return Value(p.geometry->vertexAt(p.index).x); });
        def.get("y", [](VertexPart& p) { return Value(p.geometry->vertexAt(p.index).y); });
        def.method("getHalfEdge", 0, [](VertexPart& p, const Args&) {
            return halfValue(p.owner, p.geometry, p.geometry->vertices[static_cast<size_t>(p.index)].halfEdge);
        });
        def.method("setLocation", 2, [](VertexPart& p, const Args& args) {
            const fla::Point point(arg(args, 0).toNumber(), arg(args, 1).toNumber());
            edit(*p.owner, p.geometry, [&p, point](Geometry& g) { g.moved[static_cast<size_t>(p.index)] = point; });
            return Value();
        });
        return info;
    }();
    return info;
}

const ClassInfo& ContourPart::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<ContourPart> def(info, "Contour");
        def.get("interior", [](ContourPart& p) { return Value(p.data().interior); });
        def.get("orientation", [](ContourPart& p) { return Value(p.data().orientation); });
        def.get("fill", [](ContourPart& p) { return fillValue(fillStyleOf(p.owner->get(), p.data().fill)); });
        def.method("getHalfEdge", 0, [](ContourPart& p, const Args&) { return halfValue(p.owner, p.geometry, p.data().halfEdge); });
        return info;
    }();
    return info;
}

} // namespace

Value fillToValue(const fla::FillStyle* fill)
{
    return fillValue(fill);
}

Value strokeToValue(const fla::StrokeStyle* stroke)
{
    return strokeValue(stroke);
}

void forgetShapes(const Session& session)
{
    caches().erase(&session);
}

void describeShapeGeometry(ClassDef<Element>& def)
{
    def.get("vertices", [](Element& e) {
        GeometryPtr geometry = geometryOf(e);
        auto owner = ownerOf(e);
        Value list = Value::array();
        for (size_t i = 0; i < geometry->vertices.size(); ++i)
            list.items.push_back(vertexValue(owner, geometry, static_cast<int>(i)));
        return list;
    });
    def.get("edges", [](Element& e) {
        GeometryPtr geometry = geometryOf(e);
        auto owner = ownerOf(e);
        Value list = Value::array();
        for (size_t i = 0; i < geometry->edges.size(); ++i)
            list.items.push_back(edgeValue(owner, geometry, static_cast<int>(i)));
        return list;
    });
    def.get("contours", [](Element& e) {
        GeometryPtr geometry = geometryOf(e);
        auto owner = ownerOf(e);
        Value list = Value::array();
        for (size_t i = 0; i < geometry->contours.size(); ++i)
            list.items.emplace_back(std::make_shared<ContourPart>(owner, geometry, static_cast<int>(i)));
        return list;
    });
    def.get("numCubicSegments", [](Element& e) { return Value(static_cast<int>(geometryOf(e)->sources.size())); });
    def.method("getCubicSegmentPoints", 1, [](Element& e, const Args& args) {
        GeometryPtr geometry = geometryOf(e);
        const int index = arg(args, 0).toInt();
        if (index < 0 || index >= static_cast<int>(geometry->sources.size()))
            throw Error("The shape has no cubic segment " + std::to_string(index) + ".");
        const fla::Curve& curve = geometry->sources[static_cast<size_t>(index)].curve;
        Value list = Value::array();
        if (curve.isLine())
        {
            const fla::Point& a = curve.start();
            const fla::Point& b = curve.end();
            list.items.push_back(pointValue(a));
            list.items.push_back(pointValue(fla::Point(a.x + (b.x - a.x) / 3.0, a.y + (b.y - a.y) / 3.0)));
            list.items.push_back(pointValue(fla::Point(a.x + 2.0 * (b.x - a.x) / 3.0, a.y + 2.0 * (b.y - a.y) / 3.0)));
            list.items.push_back(pointValue(b));
        }
        else
        {
            for (int i = 0; i < 4; ++i)
                list.items.push_back(pointValue(curve.controlPoint(i)));
        }
        return list;
    });
    def.method("beginEdit", 0, [](Element& e, const Args&) {
        beginEdits(e);
        return Value();
    });
    def.method("endEdit", 0, [](Element& e, const Args&) {
        endEdits(e);
        return Value();
    });
    def.method("deleteEdge", 1, [](Element& e, const Args& args) {
        const auto open = cache(e).editing.find(e.element);
        const GeometryPtr geometry = open != cache(e).editing.end() ? open->second : geometryOf(e);
        const int index = arg(args, 0).toInt();
        if (index < 0 || index >= static_cast<int>(geometry->edges.size()))
            throw Error("The shape has no edge " + std::to_string(index) + ".");
        edit(e, geometry, [index](Geometry& g) { g.deleted[static_cast<size_t>(index)] = true; });
        return Value();
    });
}

} // namespace jsfl
