#include "edge_bend.h"

#include "phoenix_view.h"

#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"

#include <algorithm>
#include <cmath>

namespace {

fla::Point toPoint(const QPointF& point)
{
    return fla::Point(point.x(), point.y());
}

/// Anchors meant to be the same point agree to well inside half a twip.
constexpr double kHalfTwip = 1.0 / 40.0;

/// A drag shorter than this, in screen pixels, is a click.
constexpr double kDragThresholdPixels = 3.0;

/// How far from a segment's ends a grab is pushed, so the lever 3t(1-t) never
/// gets small enough to fling the controls away.
constexpr double kMinimumT = 0.05;

} // namespace

EdgeBender::Probe EdgeBender::probe(PhoenixView& view, const HitResult& hit, const QPointF& documentPos)
{
    Probe result;

    if (!hit || !hit.element || hit.element->elementType() != fla::Element::Type::Shape)
        return result;

    fla::Shape* shape = static_cast<fla::Shape*>(hit.element);

    bool invertible = false;
    const QTransform toShape = hit.elementToDocument.inverted(&invertible);
    if (!invertible)
        return result;

    const QPointF localPos = toShape.map(documentPos);
    const double tolerance = PhoenixView::toleranceInLocalSpace(
        hit.elementToDocument, view.pickTolerance());

    Probe corner;
    Probe curve;
    double cornerDistance = tolerance;
    double curveDistance = tolerance;

    for (fla::Edge* edge : shape->edges)
    {
        if (!edge || !edge->visible)
            continue;

        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            const fla::Path* path = edge->paths[pathIndex];
            if (!path || !path->visible)
                continue;

            const fla::EditablePath editable = fla::EditablePath::fromPath(*path);

            for (const fla::Anchor& anchor : editable.anchors)
            {
                const double distance = std::hypot(anchor.position.x - localPos.x(),
                    anchor.position.y - localPos.y());
                if (distance <= cornerDistance)
                {
                    cornerDistance = distance;
                    corner.grab = Grab::Corner;
                    corner.edge = edge;
                    corner.pathIndex = pathIndex;
                    corner.position = anchor.position;
                }
            }

            const fla::EditablePath::PathPoint found = editable.closestPoint(toPoint(localPos));
            if (found.valid && found.distance <= curveDistance)
            {
                curveDistance = found.distance;
                curve.grab = Grab::Curve;
                curve.edge = edge;
                curve.pathIndex = pathIndex;
                curve.segment = found.segment;
                curve.t = found.t;
                curve.position = found.position;
            }
        }
    }

    // A segment runs through its own ends, so a corner within reach is what
    // the user is after.
    result = corner.grab != Grab::None ? corner : curve;
    if (result.grab == Grab::None)
        return result;

    result.shape = shape;
    result.shapeToDocument = hit.elementToDocument;
    return result;
}

void EdgeBender::grabCorner(fla::Shape& shape, const fla::Point& position)
{
    for (fla::Edge* edge : shape.edges)
    {
        if (!edge)
            continue;

        for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
        {
            const fla::Path* path = edge->paths[pathIndex];
            if (!path)
                continue;

            const fla::EditablePath editable = fla::EditablePath::fromPath(*path);
            for (size_t i = 0; i < editable.anchors.size(); ++i)
            {
                const fla::Point& at = editable.anchors[i].position;
                if (std::fabs(at.x - position.x) > kHalfTwip || std::fabs(at.y - position.y) > kHalfTwip)
                    continue;

                _edited.push_back({edge, pathIndex, editable, editable, i});
            }
        }
    }
}

void EdgeBender::begin(PhoenixView& view, const Probe& probe, const QPointF& documentPos, bool insertCorner)
{
    cancel(view);

    if (probe.grab == Grab::None || !probe.shape || !probe.edge)
        return;

    bool invertible = false;
    const QTransform toShape = probe.shapeToDocument.inverted(&invertible);
    if (!invertible)
        return;

    _shape = probe.shape;
    _shapeToDocument = probe.shapeToDocument;
    _startLocal = toShape.map(documentPos);
    _moved = false;
    _edited.clear();

    if (probe.grab == Grab::Corner)
    {
        _grab = Grab::Corner;
        grabCorner(*probe.shape, probe.position);
    }
    else if (insertCorner)
    {
        // Ctrl-drag: a new corner where the segment was grabbed, which then
        // moves like any other corner.
        if (probe.pathIndex >= probe.edge->paths.size() || !probe.edge->paths[probe.pathIndex])
            return;

        const fla::EditablePath before = fla::EditablePath::fromPath(*probe.edge->paths[probe.pathIndex]);
        fla::EditablePath current = before;
        const int inserted = current.splitSegment(probe.segment, probe.t);
        if (inserted < 0)
            return;

        _grab = Grab::Corner;
        _edited.push_back({probe.edge, probe.pathIndex, before, current, static_cast<size_t>(inserted)});
        _edited.back().start = current;
        _edited.back().started = true;
        // The drag is measured from the new corner, which sits where the
        // cursor pressed to within the pick tolerance.
        _startLocal = QPointF(probe.position.x, probe.position.y);
    }
    else
    {
        if (probe.pathIndex >= probe.edge->paths.size() || !probe.edge->paths[probe.pathIndex])
            return;

        const fla::EditablePath before = fla::EditablePath::fromPath(*probe.edge->paths[probe.pathIndex]);
        _grab = Grab::Curve;
        _segment = probe.segment;
        _t = std::clamp(probe.t, kMinimumT, 1.0 - kMinimumT);
        _edited.push_back({probe.edge, probe.pathIndex, before, before, 0});
    }

    if (_edited.empty())
        _grab = Grab::None;
}

void EdgeBender::update(PhoenixView& view, const QPointF& documentPos)
{
    if (_grab == Grab::None)
        return;

    bool invertible = false;
    const QTransform toShape = _shapeToDocument.inverted(&invertible);
    if (!invertible)
        return;

    const QPointF localPos = toShape.map(documentPos);
    const QPointF delta = localPos - _startLocal;

    if (!_moved)
    {
        const double threshold = PhoenixView::toleranceInLocalSpace(_shapeToDocument,
            kDragThresholdPixels / std::max(view.zoom(), 1.0e-6));
        if (std::hypot(delta.x(), delta.y()) <= threshold)
            return;
        _moved = true;
    }

    for (Edited& edited : _edited)
    {
        // Rebuilt from the starting geometry every time, so a long drag does
        // not accumulate rounding error.
        edited.current = edited.started ? edited.start : edited.before;

        if (_grab == Grab::Corner)
        {
            if (edited.anchorIndex < edited.current.anchors.size())
                edited.current.anchors[edited.anchorIndex].translate(delta.x(), delta.y());
            continue;
        }

        size_t from = 0;
        size_t to = 0;
        if (!edited.current.segmentAnchors(_segment, from, to))
            continue;

        fla::Anchor& a = edited.current.anchors[from];
        fla::Anchor& b = edited.current.anchors[to];

        // A straight segment becomes a curve with its controls on the chord,
        // which draws the same line and gives the bend something to move.
        if (!a.hasOutCurve() && !b.hasInCurve())
        {
            a.outHandle = fla::Point(a.position.x + (b.position.x - a.position.x) / 3.0,
                                     a.position.y + (b.position.y - a.position.y) / 3.0);
            b.inHandle = fla::Point(b.position.x - (b.position.x - a.position.x) / 3.0,
                                    b.position.y - (b.position.y - a.position.y) / 3.0);
        }

        // Moving both inner controls by d moves the point at t by 3t(1-t)d, so
        // dividing the drag by that lever keeps the grabbed point under the
        // cursor.
        const double lever = 3.0 * _t * (1.0 - _t);
        const double dx = delta.x() / lever;
        const double dy = delta.y() / lever;
        a.outHandle = fla::Point(a.outHandle.x + dx, a.outHandle.y + dy);
        b.inHandle = fla::Point(b.inHandle.x + dx, b.inHandle.y + dy);

        // The bend is this segment's alone; the neighbors' handles stay put.
        a.smooth = false;
        b.smooth = false;
    }

    applyCurrent(view);
}

void EdgeBender::applyCurrent(PhoenixView& view)
{
    for (const Edited& edited : _edited)
    {
        if (edited.edge)
            edited.current.applyTo(*edited.edge, edited.pathIndex);
    }

    view.clearCaches();
    view.update();
}

void EdgeBender::commit(PhoenixView& view, fla::CommandStack& commandStack)
{
    if (_grab == Grab::None)
        return;

    if (!_moved)
    {
        cancel(view);
        return;
    }

    const char* gesture = _grab == Grab::Corner ? "Move Corner" : "Bend Edge";

    const bool needsMacro = _edited.size() > 1;
    if (needsMacro)
        commandStack.beginMacro(gesture);

    for (const Edited& edited : _edited)
    {
        if (!edited.edge)
            continue;

        // The geometry is already in place; pushing re-applies it.
        commandStack.push(fla::CommandPtr(new fla::SetEdgeGeometryCommand(
            edited.edge, edited.before, edited.current, gesture, edited.pathIndex)));
    }

    if (needsMacro)
        commandStack.endMacro();
    commandStack.breakMergeChain();

    // A reshaped shape belongs to the artwork it now covers.
    if (_shape)
        view.markShapeEdited(_shape);

    _grab = Grab::None;
    _shape = nullptr;
    _edited.clear();
    _moved = false;
    view.clearCaches();
    view.update();
}

void EdgeBender::cancel(PhoenixView& view)
{
    if (_grab == Grab::None)
        return;

    for (const Edited& edited : _edited)
    {
        if (edited.edge)
            edited.before.applyTo(*edited.edge, edited.pathIndex);
    }

    _grab = Grab::None;
    _shape = nullptr;
    _edited.clear();
    _moved = false;
    view.clearCaches();
    view.update();
}
