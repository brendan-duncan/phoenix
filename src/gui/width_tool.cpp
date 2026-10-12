#include "width_tool.h"

#include "phoenix_view.h"
#include "stroke_effects.h"

#include "../data/shape.h"
#include "../edit/command_stack.h"
#include "../edit/property_command.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>


WidthTool::WidthTool(fla::CommandStack& commands)
    : _commands(commands)
{
}

double WidthTool::nearestAlong(const QPainterPath& path, const QPointF& point, double& distance)
{
    // Coarse samples by length, then a finer look round the best.
    distance = 1.0e300;
    double best = 0.0;
    const int samples = 200;
    for (int i = 0; i <= samples; ++i)
    {
        const double t = static_cast<double>(i) / samples;
        const QPointF at = path.pointAtPercent(t);
        const double d = std::hypot(at.x() - point.x(), at.y() - point.y());
        if (d < distance)
        {
            distance = d;
            best = t;
        }
    }
    double step = 1.0 / samples;
    for (int round = 0; round < 12; ++round)
    {
        step /= 2.0;
        for (const double t : {best - step, best + step})
        {
            if (t < 0.0 || t > 1.0)
                continue;
            const QPointF at = path.pointAtPercent(t);
            const double d = std::hypot(at.x() - point.x(), at.y() - point.y());
            if (d < distance)
            {
                distance = d;
                best = t;
            }
        }
    }
    return best;
}

void WidthTool::refresh(PhoenixView& view)
{
    if (_shape)
        view.forgetShape(_shape);
    view.invalidateBounds();
    view.update();
}

bool WidthTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    fla::Shape* shape = hit && hit.element->elementType() == fla::Element::Type::Shape
        ? static_cast<fla::Shape*>(hit.element) : nullptr;
    _selected = -1;
    if (!shape)
    {
        _shape = nullptr;
        _style = nullptr;
        view.update();
        return false;
    }
    bool invertible = false;
    const QPointF local = hit.elementToDocument.inverted(&invertible).map(documentPos);
    if (!invertible)
        return false;

    // The stroke run under the cursor, within its own width.
    const double tolerance = PhoenixView::toleranceInLocalSpace(hit.elementToDocument, view.pickTolerance());
    double bestDistance = 1.0e300;
    double along = 0.0;
    const fla::StrokeStyle* found = nullptr;
    QPainterPath run;
    for (const auto& [path, style] : view.strokeRuns(shape))
    {
        if (path.isEmpty() || path.length() <= 0.0)
            continue;
        double distance = 0.0;
        const double t = nearestAlong(path, local, distance);
        double left = 0.5 * style->weight, right = left;
        if (!style->widthProfile.empty())
            StrokeEffects::widthAt(*style, t, left, right);
        const double reach = std::max(left, right) + tolerance * 2.0;
        if (distance <= reach && distance < bestDistance)
        {
            bestDistance = distance;
            along = t;
            found = style;
            run = path;
        }
    }
    if (!found)
    {
        _shape = nullptr;
        _style = nullptr;
        view.update();
        return false;
    }
    _shape = shape;
    _style = const_cast<fla::StrokeStyle*>(found);
    _run = run;
    _toDocument = hit.elementToDocument;
    _before = _style->widthProfile;

    // A width point near the press, or a new one there.
    std::vector<fla::StrokeStyle::WidthMarker>& profile = _style->widthProfile;
    const double length = run.length();
    for (size_t i = 0; i < profile.size(); ++i)
    {
        if (std::fabs(profile[i].position - along) * length <= tolerance * 2.0)
            _selected = static_cast<int>(i);
    }
    if (_selected < 0)
    {
        if (profile.empty())
        {
            fla::StrokeStyle::WidthMarker start;
            start.position = 0.0;
            fla::StrokeStyle::WidthMarker end;
            end.position = 1.0;
            profile = {start, end};
        }
        fla::StrokeStyle::WidthMarker marker;
        marker.position = along;
        double left = 0.0, right = 0.0;
        StrokeEffects::widthAt(*_style, along, left, right);
        const double weight = std::max(_style->weight, 0.1);
        marker.left = left / weight;
        marker.right = right / weight;
        const auto at = std::upper_bound(profile.begin(), profile.end(), along,
            [](double position, const fla::StrokeStyle::WidthMarker& m) { return position < m.position; });
        _selected = static_cast<int>(at - profile.begin());
        profile.insert(at, marker);
    }
    _dragging = true;
    refresh(view);
    return true;
}

bool WidthTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_dragging || !_style || _selected < 0 || _selected >= static_cast<int>(_style->widthProfile.size()))
        return false;
    bool invertible = false;
    const QPointF local = _toDocument.inverted(&invertible).map(documentPos);
    if (!invertible)
        return true;
    fla::StrokeStyle::WidthMarker& marker = _style->widthProfile[static_cast<size_t>(_selected)];
    // How far out from the centerline, on the side the cursor is: that side
    // takes it, and the other with it unless Alt is held.
    const QPointF center = _run.pointAtPercent(marker.position);
    const double angle = _run.angleAtPercent(marker.position) * 3.14159265358979323846 / 180.0;
    // Qt measures angles counterclockwise with y up; on the page, y is down.
    const QPointF tangent(std::cos(angle), -std::sin(angle));
    const QPointF normal(tangent.y(), -tangent.x());
    const QPointF offset = local - center;
    const double out = offset.x() * normal.x() + offset.y() * normal.y();
    const double weight = std::max(_style->weight, 0.1);
    const double reach = std::min(std::fabs(out) / weight, 50.0);
    if (event->modifiers() & Qt::AltModifier)
    {
        if (out >= 0.0)
            marker.left = reach;
        else
            marker.right = reach;
    }
    else
    {
        marker.left = reach;
        marker.right = reach;
    }
    refresh(view);
    return true;
}

bool WidthTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    (void)documentPos;
    if (!_dragging)
        return false;
    _dragging = false;
    if (_style && _style->widthProfile != _before)
    {
        const std::vector<fla::StrokeStyle::WidthMarker> after = _style->widthProfile;
        _commands.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::StrokeStyle::WidthMarker>>(
            _style->widthProfile, _before, after, "Width")));
        _commands.breakMergeChain();
    }
    refresh(view);
    return true;
}

bool WidthTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (!_style || _selected < 0 || (event->key() != Qt::Key_Delete && event->key() != Qt::Key_Backspace))
        return false;
    std::vector<fla::StrokeStyle::WidthMarker> profile = _style->widthProfile;
    if (_selected >= static_cast<int>(profile.size()))
        return false;
    profile.erase(profile.begin() + _selected);
    // Only the two ends left, both plain: the stroke is even again.
    const bool even = std::all_of(profile.begin(), profile.end(), [](const fla::StrokeStyle::WidthMarker& m) {
        return m.left == 0.5 && m.right == 0.5;
    });
    if (even)
        profile.clear();
    _commands.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::StrokeStyle::WidthMarker>>(
        _style->widthProfile, _style->widthProfile, profile, "Remove Width Point")));
    _commands.breakMergeChain();
    _selected = -1;
    refresh(view);
    return true;
}

void WidthTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (!_style || _run.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(_toDocument, true);
    const double unit = scale / std::max(std::sqrt(std::fabs(_toDocument.determinant())), 1.0e-9);
    const double weight = std::max(_style->weight, 0.1);
    for (size_t i = 0; i < _style->widthProfile.size(); ++i)
    {
        const fla::StrokeStyle::WidthMarker& marker = _style->widthProfile[i];
        const QPointF center = _run.pointAtPercent(marker.position);
        const double angle = _run.angleAtPercent(marker.position) * 3.14159265358979323846 / 180.0;
        const QPointF tangent(std::cos(angle), -std::sin(angle));
        const QPointF normal(tangent.y(), -tangent.x());
        const QPointF left = center + normal * marker.left * weight;
        const QPointF right = center - normal * marker.right * weight;
        painter.setPen(QPen(QColor(40, 90, 220), 1.0 * unit));
        painter.drawLine(left, right);
        painter.setBrush(static_cast<int>(i) == _selected ? QColor(255, 200, 0) : QColor(255, 255, 255));
        for (const QPointF& end : {left, right})
            painter.drawEllipse(end, 3.0 * unit, 3.0 * unit);
        painter.setBrush(QColor(40, 90, 220));
        painter.drawEllipse(center, 2.0 * unit, 2.0 * unit);
    }
    painter.restore();
}

void WidthTool::deactivate(PhoenixView& view)
{
    _dragging = false;
    _shape = nullptr;
    _style = nullptr;
    _selected = -1;
    view.update();
}
