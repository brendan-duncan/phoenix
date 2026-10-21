#include "drawing_aids_view.h"

#include "phoenix_view.h"
#include "tool.h"

#include "../data/document.h"
#include "../data/fla_document.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace drawaids {

namespace {

namespace aids = fla::aids;

constexpr double kPi = 3.14159265358979323846;
const char* const kSelectedProperty = "phoenixSelectedDrawingGuide";
/// How far from the center the symmetry axis's turn handle sits, in screen
/// pixels.
constexpr double kTurnReach = 60.0;

fla::Document* documentOf(const PhoenixView& view)
{
    const fla::FLADocument* fla = view.flaDocument();
    return fla ? fla->document : nullptr;
}

QTransform stageToScope(const PhoenixView& view)
{
    bool invertible = false;
    const QTransform inverse = view.scopeToStage().inverted(&invertible);
    return invertible ? inverse : QTransform();
}

QPointF qt(const fla::Point& point)
{
    return QPointF(point.x, point.y);
}

bool drawingGuidesToolActive(const PhoenixView& view)
{
    return view.activeTool() && view.activeTool()->name() == "Drawing Guides";
}

/// What shows the aids over the stage, painted with the view's own frame
/// (PhoenixView::setAidsPainter): a widget of its own over the view would
/// sit under the canvas a browser's GPU stage lays over it. It takes no
/// input: the Drawing Guides tool does that through the view.
class Overlay
{
public:
    explicit Overlay(PhoenixView& view)
        : _view(&view)
    {
    }

    void paint(QPainter& painter)
    {
        if (!documentOf(*_view))
            return;
        const aids::Symmetry symmetry = drawaids::symmetry(*_view);
        const aids::DrawingGuides guides = drawaids::guides(*_view);
        if (!symmetry.on && (guides.hidden || guides.guides.empty()))
            return;
        painter.setRenderHint(QPainter::Antialiasing);
        const QTransform toWidget = _view->scopeToStage().inverted() * _view->documentToWidget();
        const bool editing = drawingGuidesToolActive(*_view);
        if (!guides.hidden)
            paintGuides(painter, toWidget, guides, editing);
        if (symmetry.on)
            paintSymmetry(painter, toWidget, symmetry, editing);
    }

private:
    int width() const { return _view->width(); }
    int height() const { return _view->height(); }

    /// A stage length that reaches across the whole view.
    double across(const QTransform& toWidget) const
    {
        const double scale = std::sqrt(std::fabs(toWidget.determinant()));
        return std::hypot(width(), height()) * 2.0 / std::max(scale, 1.0e-9);
    }

    void handle(QPainter& painter, const QPointF& at, bool square, const QColor& color) const
    {
        painter.setPen(QPen(Qt::white, 1.0));
        painter.setBrush(color);
        const QRectF box(at.x() - 4.0, at.y() - 4.0, 8.0, 8.0);
        if (square)
            painter.drawRect(box);
        else
            painter.drawEllipse(box);
    }

    void paintSymmetry(QPainter& painter, const QTransform& toWidget, const aids::Symmetry& symmetry, bool editing)
    {
        const QColor color(230, 0, 160, 200);
        QPen pen(color, 1.0, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPointF center(symmetry.x, symmetry.y);
        const double reach = across(toWidget);
        const double turn = symmetry.angle * kPi / 180.0;
        const auto ray = [&](double angle, bool both) {
            const QPointF direction(std::cos(angle), std::sin(angle));
            const QPointF from = both ? center - direction * reach : center;
            painter.drawLine(toWidget.map(from), toWidget.map(center + direction * reach));
        };
        switch (symmetry.mode)
        {
        case aids::SymmetryMode::Vertical: ray(turn + kPi / 2.0, true); break;
        case aids::SymmetryMode::Horizontal: ray(turn, true); break;
        case aids::SymmetryMode::Both:
            ray(turn + kPi / 2.0, true);
            ray(turn, true);
            break;
        case aids::SymmetryMode::Radial:
        {
            // A spoke for each copy, the first straight up from the center.
            const int n = std::clamp(symmetry.copies, 2, 64);
            for (int k = 0; k < n; ++k)
                ray(turn - kPi / 2.0 + 2.0 * kPi * k / n, false);
            break;
        }
        }
        const QPointF middle = toWidget.map(center);
        if (editing)
        {
            painter.setPen(QPen(color, 1.0));
            const QPointF turnAt = toWidget.map(qt(symmetryTurnHandle(*_view, symmetry)));
            painter.drawLine(middle, turnAt);
            handle(painter, turnAt, true, color);
            handle(painter, middle, false, color);
        }
        else
        {
            painter.setPen(QPen(color, 1.5));
            painter.drawEllipse(middle, 4.0, 4.0);
        }
    }

    void paintEllipse(QPainter& painter, const QTransform& toWidget, const aids::DrawingGuide& guide, double scale)
    {
        const double rx = std::hypot(guide.b.x - guide.a.x, guide.b.y - guide.a.y) * scale;
        const double ry = std::fabs(guide.minor) * scale;
        QTransform place;
        place.translate(guide.a.x, guide.a.y);
        place.rotateRadians(std::atan2(guide.b.y - guide.a.y, guide.b.x - guide.a.x));
        QPainterPath path;
        path.addEllipse(QPointF(0.0, 0.0), rx, ry);
        painter.drawPath((place * toWidget).map(path));
    }

    void paintGuides(QPainter& painter, const QTransform& toWidget, const aids::DrawingGuides& guides, bool editing)
    {
        const int selected = selectedGuide(*_view);
        const double reach = across(toWidget);
        const QColor color(0, 150, 255, guides.snap ? 220 : 120);
        const QColor faint(0, 150, 255, 45);
        const QColor picked(255, 140, 0);
        for (size_t i = 0; i < guides.guides.size(); ++i)
        {
            const aids::DrawingGuide& guide = guides.guides[i];
            const bool isSelected = editing && static_cast<int>(i) == selected;
            const QPen solid(isSelected ? picked : color, isSelected ? 2.0 : 1.5);
            const QPen thin(faint, 1.0);
            painter.setBrush(Qt::NoBrush);
            const QPointF a = qt(guide.a);
            const QPointF b = qt(guide.b);
            const QPointF c = qt(guide.c);
            std::vector<QPointF> handles;
            switch (guide.kind)
            {
            case aids::GuideKind::Ruler:
            case aids::GuideKind::ParallelRuler:
            {
                QPointF direction = b - a;
                const double length = std::hypot(direction.x(), direction.y());
                if (length > 1.0e-9)
                    direction /= length;
                painter.setPen(thin);
                if (guide.kind == aids::GuideKind::Ruler)
                    painter.drawLine(toWidget.map(a - direction * reach), toWidget.map(b + direction * reach));
                painter.setPen(solid);
                painter.drawLine(toWidget.map(a), toWidget.map(b));
                if (guide.kind == aids::GuideKind::ParallelRuler)
                {
                    // Arrows at both ends: strokes run this way anywhere.
                    const QPointF wa = toWidget.map(a);
                    const QPointF wb = toWidget.map(b);
                    QPointF along = wb - wa;
                    const double l = std::hypot(along.x(), along.y());
                    if (l > 1.0e-9)
                    {
                        along /= l;
                        const QPointF side(-along.y(), along.x());
                        for (const auto& [tip, back] : {std::pair(wb, -along), std::pair(wa, along)})
                        {
                            painter.drawLine(tip, tip + back * 10.0 + side * 5.0);
                            painter.drawLine(tip, tip + back * 10.0 - side * 5.0);
                        }
                    }
                }
                handles = {a, b};
                break;
            }
            case aids::GuideKind::Ellipse:
            case aids::GuideKind::ConcentricEllipse:
                if (guide.kind == aids::GuideKind::ConcentricEllipse)
                {
                    painter.setPen(thin);
                    for (double scale : {0.5, 1.5, 2.0, 3.0})
                        paintEllipse(painter, toWidget, guide, scale);
                }
                painter.setPen(solid);
                paintEllipse(painter, toWidget, guide, 1.0);
                handles = {a, b, qt(minorHandle(guide))};
                break;
            case aids::GuideKind::Perspective1:
            case aids::GuideKind::Perspective2:
            case aids::GuideKind::Perspective3:
            {
                std::vector<QPointF> points = {a};
                if (guide.kind != aids::GuideKind::Perspective1)
                    points.push_back(b);
                if (guide.kind == aids::GuideKind::Perspective3)
                    points.push_back(c);
                if (guide.grid)
                {
                    painter.setPen(thin);
                    for (const QPointF& point : points)
                    {
                        for (const auto& [from, to] : aids::fanLines(fla::Point(point.x(), point.y()), 16, reach))
                            painter.drawLine(toWidget.map(qt(from)), toWidget.map(qt(to)));
                    }
                }
                // The horizon: level through the one point, or through the
                // first two.
                painter.setPen(solid);
                QPointF horizon = guide.kind == aids::GuideKind::Perspective1 ? QPointF(1.0, 0.0) : b - a;
                const double length = std::hypot(horizon.x(), horizon.y());
                if (length > 1.0e-9)
                {
                    horizon /= length;
                    painter.drawLine(toWidget.map(a - horizon * reach), toWidget.map(a + horizon * reach));
                }
                for (const QPointF& point : points)
                {
                    const QPointF at = toWidget.map(point);
                    painter.drawLine(at + QPointF(-7, 0), at + QPointF(7, 0));
                    painter.drawLine(at + QPointF(0, -7), at + QPointF(0, 7));
                    painter.drawEllipse(at, 4.0, 4.0);
                }
                handles = points;
                break;
            }
            }
            if (editing)
            {
                for (const QPointF& point : handles)
                    handle(painter, toWidget.map(point), true, isSelected ? picked : color);
            }
        }
    }

    PhoenixView* _view;
};

} // namespace

aids::Symmetry symmetry(const PhoenixView& view)
{
    const fla::Document* document = documentOf(view);
    if (!document)
        return aids::Symmetry();
    if (document->symmetry.empty())
    {
        aids::Symmetry centered;
        centered.x = document->width / 2.0;
        centered.y = document->height / 2.0;
        return centered;
    }
    return aids::symmetryFromText(document->symmetry);
}

void setSymmetry(PhoenixView& view, const aids::Symmetry& symmetry)
{
    if (fla::Document* document = documentOf(view))
        document->symmetry = aids::toText(symmetry);
    refresh(view);
}

aids::DrawingGuides guides(const PhoenixView& view)
{
    const fla::Document* document = documentOf(view);
    return document ? aids::guidesFromText(document->drawingGuides) : aids::DrawingGuides();
}

void setGuides(PhoenixView& view, const aids::DrawingGuides& guides)
{
    if (fla::Document* document = documentOf(view))
        document->drawingGuides = guides == aids::DrawingGuides() ? std::string() : aids::toText(guides);
    refresh(view);
}

int selectedGuide(const PhoenixView& view)
{
    const QVariant value = view.property(kSelectedProperty);
    return value.isValid() ? value.toInt() : -1;
}

void setSelectedGuide(PhoenixView& view, int index)
{
    view.setProperty(kSelectedProperty, index);
    refresh(view);
}

QTransform toQt(const fla::Transform& transform)
{
    return QTransform(transform.m11, transform.m12, transform.m21, transform.m22, transform.tx, transform.ty);
}

std::vector<QTransform> copiesInScope(const PhoenixView& view)
{
    std::vector<QTransform> copies;
    const QTransform toStage = view.scopeToStage();
    const QTransform back = stageToScope(view);
    for (const fla::Transform& copy : aids::symmetryCopies(symmetry(view)))
        copies.push_back(toStage * toQt(copy) * back);
    return copies;
}

QPainterPath withCopies(const QPainterPath& path, const std::vector<QTransform>& copies)
{
    // Joined as areas rather than added as outlines: a mirrored copy winds
    // the other way, and added it would cut holes where it overlaps.
    QPainterPath all = path.simplified();
    for (const QTransform& copy : copies)
        all = all.united(copy.map(path).simplified());
    return all;
}

void drawWithCopies(QPainter& painter, const QPainterPath& path, const std::vector<QTransform>& copies)
{
    painter.drawPath(path);
    for (const QTransform& copy : copies)
        painter.drawPath(copy.map(path));
}

double stagePixel(const PhoenixView& view)
{
    const QTransform toWidget = stageToScope(view) * view.documentToWidget();
    const double scale = std::sqrt(std::fabs(toWidget.determinant()));
    return 1.0 / std::max(scale, 1.0e-9);
}

std::vector<fla::Point> snapped(const PhoenixView& view, const std::vector<fla::Point>& points)
{
    const aids::DrawingGuides all = guides(view);
    if (all.hidden || !all.snap || all.guides.empty() || points.empty())
        return points;
    const QTransform toStage = view.scopeToStage();
    const QTransform back = stageToScope(view);
    std::vector<fla::Point> onStage;
    onStage.reserve(points.size());
    for (const fla::Point& point : points)
    {
        const QPointF p = toStage.map(qt(point));
        onStage.emplace_back(p.x(), p.y());
    }
    std::vector<fla::Point> out;
    out.reserve(points.size());
    for (const fla::Point& point : aids::snapStroke(all, onStage, stagePixel(view)))
    {
        const QPointF p = back.map(qt(point));
        out.emplace_back(p.x(), p.y());
    }
    return out;
}

std::vector<fla::BrushSample> snapped(const PhoenixView& view, const std::vector<fla::BrushSample>& samples)
{
    std::vector<fla::Point> points;
    points.reserve(samples.size());
    for (const fla::BrushSample& sample : samples)
        points.emplace_back(sample.x, sample.y);
    const std::vector<fla::Point> moved = snapped(view, points);
    std::vector<fla::BrushSample> out = samples;
    for (size_t i = 0; i < out.size() && i < moved.size(); ++i)
    {
        out[i].x = moved[i].x;
        out[i].y = moved[i].y;
    }
    return out;
}

void paintStabilizer(QPainter& painter, const QPointF& pen, const QPointF& line, double radius, double scale)
{
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 120, 255, 150), 1.0 * scale, Qt::DashLine));
    painter.drawEllipse(line, radius, radius);
    painter.setPen(QPen(QColor(0, 120, 255, 200), 1.0 * scale));
    painter.drawLine(line, pen);
    painter.restore();
}

fla::Point symmetryTurnHandle(const PhoenixView& view, const aids::Symmetry& symmetry)
{
    // Up the upright axis (a radial axis's first spoke), turned with it.
    const double angle = symmetry.angle * kPi / 180.0 - kPi / 2.0;
    const double reach = kTurnReach * stagePixel(view);
    return fla::Point(symmetry.x + std::cos(angle) * reach, symmetry.y + std::sin(angle) * reach);
}

fla::Point minorHandle(const aids::DrawingGuide& guide)
{
    double dx = guide.b.x - guide.a.x;
    double dy = guide.b.y - guide.a.y;
    const double length = std::hypot(dx, dy);
    if (length > 1.0e-9)
    {
        dx /= length;
        dy /= length;
    }
    else
    {
        dx = 1.0;
        dy = 0.0;
    }
    return fla::Point(guide.a.x - dy * guide.minor, guide.a.y + dx * guide.minor);
}

Handle handleAt(const PhoenixView& view, const QPointF& stagePos)
{
    Handle found;
    const double pixel = stagePixel(view);
    const double reach = 6.0 * pixel;
    const auto near = [&](const fla::Point& point) { return std::hypot(point.x - stagePos.x(), point.y - stagePos.y()) <= reach; };

    const aids::Symmetry axis = symmetry(view);
    if (axis.on)
    {
        if (near(fla::Point(axis.x, axis.y)))
        {
            found.part = Handle::Part::SymmetryCenter;
            return found;
        }
        if (near(symmetryTurnHandle(view, axis)))
        {
            found.part = Handle::Part::SymmetryTurn;
            return found;
        }
    }

    const aids::DrawingGuides all = guides(view);
    if (all.hidden)
        return found;
    // The topmost (last added) first.
    for (int i = static_cast<int>(all.guides.size()) - 1; i >= 0; --i)
    {
        const aids::DrawingGuide& guide = all.guides[static_cast<size_t>(i)];
        found.guide = i;
        if (near(guide.a))
        {
            found.part = Handle::Part::A;
            return found;
        }
        const bool hasB = guide.kind != aids::GuideKind::Perspective1;
        if (hasB && near(guide.b))
        {
            found.part = Handle::Part::B;
            return found;
        }
        if (guide.kind == aids::GuideKind::Perspective3 && near(guide.c))
        {
            found.part = Handle::Part::C;
            return found;
        }
        if ((guide.kind == aids::GuideKind::Ellipse || guide.kind == aids::GuideKind::ConcentricEllipse) &&
            near(minorHandle(guide)))
        {
            found.part = Handle::Part::Minor;
            return found;
        }
    }
    for (int i = static_cast<int>(all.guides.size()) - 1; i >= 0; --i)
    {
        const aids::DrawingGuide& guide = all.guides[static_cast<size_t>(i)];
        found.guide = i;
        const fla::Point at(stagePos.x(), stagePos.y());
        switch (guide.kind)
        {
        case aids::GuideKind::Ruler:
        case aids::GuideKind::ParallelRuler:
        {
            // Along the segment between its ends.
            const double dx = guide.b.x - guide.a.x;
            const double dy = guide.b.y - guide.a.y;
            const double length2 = dx * dx + dy * dy;
            if (length2 <= 1.0e-12)
                break;
            const double t = std::clamp(((at.x - guide.a.x) * dx + (at.y - guide.a.y) * dy) / length2, 0.0, 1.0);
            if (near(fla::Point(guide.a.x + dx * t, guide.a.y + dy * t)))
            {
                found.part = Handle::Part::Body;
                return found;
            }
            break;
        }
        case aids::GuideKind::Ellipse:
        case aids::GuideKind::ConcentricEllipse:
        {
            aids::DrawingGuides one;
            aids::DrawingGuide plain = guide;
            plain.kind = aids::GuideKind::Ellipse;
            one.guides.push_back(plain);
            const aids::Constraint ellipse = aids::constraintFor(one, {at}, pixel);
            if (ellipse.kind != aids::Constraint::Kind::None && near(ellipse.project(at)))
            {
                found.part = Handle::Part::Body;
                return found;
            }
            break;
        }
        case aids::GuideKind::Perspective1:
        case aids::GuideKind::Perspective2:
        case aids::GuideKind::Perspective3:
        {
            // Along the horizon: level through the one point, or through
            // the first two.
            double dx = guide.kind == aids::GuideKind::Perspective1 ? 1.0 : guide.b.x - guide.a.x;
            double dy = guide.kind == aids::GuideKind::Perspective1 ? 0.0 : guide.b.y - guide.a.y;
            const double length = std::hypot(dx, dy);
            if (length <= 1.0e-9)
                break;
            dx /= length;
            dy /= length;
            const double t = (at.x - guide.a.x) * dx + (at.y - guide.a.y) * dy;
            if (near(fla::Point(guide.a.x + dx * t, guide.a.y + dy * t)))
            {
                found.part = Handle::Part::Body;
                return found;
            }
            break;
        }
        }
    }
    found.guide = -1;
    return found;
}

void attach(PhoenixView* view)
{
    if (view)
        view->setAidsPainter([view](QPainter& painter) { Overlay(*view).paint(painter); });
}

void refresh(PhoenixView& view)
{
    view.update();
}

} // namespace drawaids
