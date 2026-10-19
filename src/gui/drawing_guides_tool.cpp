#include "drawing_guides_tool.h"

#include "phoenix_view.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../edit/drawing_style.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

namespace aids = fla::aids;

constexpr double kPi = 3.14159265358979323846;

/// \a to turned about \a from to the nearest 15 degrees.
QPointF inSteps(const QPointF& from, const QPointF& to)
{
    const QPointF d = to - from;
    const double length = std::hypot(d.x(), d.y());
    const double step = kPi / 12.0;
    const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return from + QPointF(std::cos(angle), std::sin(angle)) * length;
}

QPointF qt(const fla::Point& point)
{
    return QPointF(point.x, point.y);
}

fla::Point point(const QPointF& at)
{
    return fla::Point(at.x(), at.y());
}

bool isEllipse(aids::GuideKind kind)
{
    return kind == aids::GuideKind::Ellipse || kind == aids::GuideKind::ConcentricEllipse;
}

bool isPerspective(aids::GuideKind kind)
{
    return kind == aids::GuideKind::Perspective1 || kind == aids::GuideKind::Perspective2 ||
           kind == aids::GuideKind::Perspective3;
}

} // namespace

bool DrawingGuidesTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !view.flaDocument())
        return false;
    const QPointF stage = view.scopeToStage().map(documentPos);
    _pressed = stage;
    _held = drawaids::handleAt(view, stage);
    _adding = false;
    aids::DrawingGuides guides = drawaids::guides(view);
    if (_held.part != drawaids::Handle::Part::None)
    {
        if (_held.guide >= 0)
            _before = guides.guides[static_cast<size_t>(_held.guide)];
        drawaids::setSelectedGuide(view, _held.guide);
        _dragging = true;
        return true;
    }

    // Empty stage: a new guide of the kind chosen, laid out by the drag.
    const auto kind = static_cast<aids::GuideKind>(std::clamp(_style.drawingGuideKind, 0, 6));
    aids::DrawingGuide guide;
    guide.kind = kind;
    if (isPerspective(kind))
    {
        // The vanishing points spread out across the stage about the press;
        // dragging moves them all.
        const fla::Document* document = view.flaDocument()->document;
        const double size = document ? document->width / 2.0 : 275.0;
        guide = aids::makeGuide(kind, point(stage), size);
        _held.part = drawaids::Handle::Part::Body;
    }
    else
    {
        guide.a = point(stage);
        guide.b = point(stage);
        guide.minor = 0.0;
        _held.part = drawaids::Handle::Part::B;
    }
    guides.guides.push_back(guide);
    guides.hidden = false;
    _held.guide = static_cast<int>(guides.guides.size()) - 1;
    _before = guide;
    _adding = true;
    _dragging = true;
    drawaids::setGuides(view, guides);
    drawaids::setSelectedGuide(view, _held.guide);
    return true;
}

void DrawingGuidesTool::drag(PhoenixView& view, const QPointF& stagePos, bool steps)
{
    using Part = drawaids::Handle::Part;
    if (_held.part == Part::SymmetryCenter || _held.part == Part::SymmetryTurn)
    {
        aids::Symmetry symmetry = drawaids::symmetry(view);
        if (_held.part == Part::SymmetryCenter)
        {
            symmetry.x = stagePos.x();
            symmetry.y = stagePos.y();
        }
        else
        {
            const QPointF center(symmetry.x, symmetry.y);
            const QPointF to = steps ? inSteps(center, stagePos) : stagePos;
            // The handle sits straight up the axis when it is not turned.
            double angle = std::atan2(to.y() - center.y(), to.x() - center.x()) * 180.0 / kPi + 90.0;
            angle = std::remainder(angle, 360.0);
            symmetry.angle = std::fabs(angle) < 1.0e-9 ? 0.0 : angle;
        }
        drawaids::setSymmetry(view, symmetry);
        return;
    }

    aids::DrawingGuides guides = drawaids::guides(view);
    if (_held.guide < 0 || _held.guide >= static_cast<int>(guides.guides.size()))
        return;
    aids::DrawingGuide& guide = guides.guides[static_cast<size_t>(_held.guide)];
    const QPointF delta = stagePos - _pressed;
    switch (_held.part)
    {
    case Part::A:
        if (isEllipse(guide.kind))
            guide = aids::moved(_before, delta.x(), delta.y());
        else
            guide.a = point(steps && !isPerspective(guide.kind) ? inSteps(qt(guide.b), stagePos) : stagePos);
        break;
    case Part::B:
        guide.b = point(steps && !isPerspective(guide.kind) ? inSteps(qt(guide.a), stagePos) : stagePos);
        // Laying out a new ellipse: half as tall as it is wide.
        if (_adding && isEllipse(guide.kind))
            guide.minor = std::hypot(guide.b.x - guide.a.x, guide.b.y - guide.a.y) / 2.0;
        break;
    case Part::C:
        guide.c = point(stagePos);
        break;
    case Part::Minor:
    {
        // How far the handle is from the first radius's line.
        double dx = guide.b.x - guide.a.x;
        double dy = guide.b.y - guide.a.y;
        const double length = std::hypot(dx, dy);
        if (length > 1.0e-9)
            guide.minor = std::fabs(((stagePos.x() - guide.a.x) * -dy + (stagePos.y() - guide.a.y) * dx) / length);
        break;
    }
    case Part::Body:
        guide = aids::moved(_before, delta.x(), delta.y());
        break;
    default:
        break;
    }
    drawaids::setGuides(view, guides);
}

bool DrawingGuidesTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (!_dragging)
        return false;
    drag(view, view.scopeToStage().map(documentPos), (event->modifiers() & Qt::ShiftModifier) != 0);
    return true;
}

bool DrawingGuidesTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton || !_dragging)
        return false;
    const QPointF stage = view.scopeToStage().map(documentPos);
    drag(view, stage, (event->modifiers() & Qt::ShiftModifier) != 0);
    _dragging = false;
    if (_adding)
    {
        // A click rather than a drag: a guide of a size to work with,
        // 200 screen pixels across.
        aids::DrawingGuides guides = drawaids::guides(view);
        if (_held.guide >= 0 && _held.guide < static_cast<int>(guides.guides.size()))
        {
            aids::DrawingGuide& guide = guides.guides[static_cast<size_t>(_held.guide)];
            const double pixel = drawaids::stagePixel(view);
            if (!isPerspective(guide.kind) && std::hypot(guide.b.x - guide.a.x, guide.b.y - guide.a.y) < 4.0 * pixel)
            {
                guide = aids::makeGuide(guide.kind, point(_pressed), 200.0 * pixel);
                drawaids::setGuides(view, guides);
            }
        }
        _adding = false;
    }
    return true;
}

bool DrawingGuidesTool::keyPress(PhoenixView& view, QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && _dragging)
    {
        // Back as it was; a guide being added goes.
        aids::DrawingGuides guides = drawaids::guides(view);
        if (_held.guide >= 0 && _held.guide < static_cast<int>(guides.guides.size()))
        {
            if (_adding)
                guides.guides.erase(guides.guides.begin() + _held.guide);
            else
                guides.guides[static_cast<size_t>(_held.guide)] = _before;
            drawaids::setGuides(view, guides);
        }
        _dragging = false;
        _adding = false;
        return true;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        const int selected = drawaids::selectedGuide(view);
        aids::DrawingGuides guides = drawaids::guides(view);
        if (selected < 0 || selected >= static_cast<int>(guides.guides.size()))
            return false;
        guides.guides.erase(guides.guides.begin() + selected);
        drawaids::setGuides(view, guides);
        drawaids::setSelectedGuide(view, -1);
        return true;
    }
    return false;
}

void DrawingGuidesTool::deactivate(PhoenixView& view)
{
    _dragging = false;
    _adding = false;
    drawaids::refresh(view);
}
