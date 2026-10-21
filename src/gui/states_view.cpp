#include "states_view.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr double kWidth = 150.0;
constexpr double kHeight = 54.0;
/// Where the states sit below the Any State box.
constexpr double kLeft = 12.0;
constexpr double kTop = 50.0;
constexpr double kHandle = 6.0;

/// Where the line from \a center toward \a toward leaves \a rect.
QPointF edgePoint(const QRectF& rect, const QPointF& center, const QPointF& toward)
{
    const double dx = toward.x() - center.x();
    const double dy = toward.y() - center.y();
    if (std::fabs(dx) < 1e-9 && std::fabs(dy) < 1e-9)
        return center;
    const double tx = std::fabs(dx) > 1e-9 ? (rect.width() / 2.0) / std::fabs(dx) : 1e9;
    const double ty = std::fabs(dy) > 1e-9 ? (rect.height() / 2.0) / std::fabs(dy) : 1e9;
    const double t = std::min(tx, ty);
    return QPointF(center.x() + dx * t, center.y() + dy * t);
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double length = ab.x() * ab.x() + ab.y() * ab.y();
    double t = length > 0.0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}

} // namespace

StatesView::StatesView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(240, 200);
}

void StatesView::setGraph(const fla::states::Graph& graph, const std::vector<QString>& ranges)
{
    _graph = graph;
    _ranges = ranges;
    // Big enough for every box, so a scroll area around it scrolls to them.
    double right = 240.0, bottom = 200.0;
    for (size_t i = 0; i < _graph.states.size(); ++i)
    {
        if (!shown(static_cast<int>(i)))
            continue;
        const QRectF rect = box(static_cast<int>(i));
        right = std::max(right, rect.right() + 40.0);
        bottom = std::max(bottom, rect.bottom() + 40.0);
    }
    setMinimumSize(static_cast<int>(right), static_cast<int>(bottom));
    update();
}

void StatesView::setLayer(int layer)
{
    _layer = layer;
    update();
}

bool StatesView::shown(int state) const
{
    if (state < 0)
        return true;
    if (state >= static_cast<int>(_graph.states.size()))
        return false;
    const int layer = _graph.states[static_cast<size_t>(state)].layer;
    const int count = static_cast<int>(_graph.layers.size());
    return (layer > 0 && layer <= count ? layer : 0) == _layer;
}

void StatesView::setPicked(Pick kind, int index)
{
    _kind = index < 0 ? Pick::None : kind;
    _index = index;
    update();
}

QRectF StatesView::box(int state) const
{
    const fla::states::State& each = _graph.states[static_cast<size_t>(state)];
    return QRectF(kLeft + each.x, kTop + each.y, kWidth, kHeight);
}

QRectF StatesView::anyBox() const
{
    return QRectF(kLeft, 10.0, 96.0, 26.0);
}

QPointF StatesView::handle(int state) const
{
    const QRectF rect = state < 0 ? anyBox() : box(state);
    return QPointF(rect.right(), rect.center().y());
}

int StatesView::stateAt(const QPointF& at) const
{
    for (int i = static_cast<int>(_graph.states.size()) - 1; i >= 0; --i)
        if (shown(i) && box(i).contains(at))
            return i;
    return -1;
}

bool StatesView::arrow(int index, QPointF& from, QPointF& to, QPointF& middle) const
{
    const fla::states::Transition& transition = _graph.transitions[static_cast<size_t>(index)];
    const int count = static_cast<int>(_graph.states.size());
    if (transition.to < 0 || transition.to >= count || transition.from >= count || !shown(transition.to) ||
        !shown(transition.from))
        return false;
    const QRectF start = transition.from < 0 ? anyBox() : box(transition.from);
    const QRectF end = box(transition.to);
    // Arrows between the same two boxes, either way, side by side.
    const int low = std::min(transition.from, transition.to);
    const int high = std::max(transition.from, transition.to);
    int nth = 0, all = 0;
    for (int i = 0; i < static_cast<int>(_graph.transitions.size()); ++i)
    {
        const fla::states::Transition& other = _graph.transitions[static_cast<size_t>(i)];
        if (std::min(other.from, other.to) != low || std::max(other.from, other.to) != high)
            continue;
        if (i == index)
            nth = all;
        ++all;
    }
    const QRectF lowBox = low < 0 ? anyBox() : box(low);
    const QRectF highBox = box(high);
    QPointF along = highBox.center() - lowBox.center();
    const double length = std::hypot(along.x(), along.y());
    if (length < 1.0)
    {
        // Onto itself: a loop over the top of the box.
        from = QPointF(end.center().x() - 20.0, end.top());
        to = QPointF(end.center().x() + 20.0, end.top());
        middle = QPointF(end.center().x(), end.top() - 22.0 - 16.0 * nth);
        return true;
    }
    const QPointF normal(-along.y() / length, along.x() / length);
    const double offset = (nth - (all - 1) / 2.0) * 22.0;
    const QPointF shift = normal * offset;
    from = edgePoint(start, start.center() + shift, end.center() + shift);
    to = edgePoint(end, end.center() + shift, start.center() + shift);
    // Side by side, their words along them in turn, so they do not cover each other.
    double fraction = 0.5 + (nth - (all - 1) / 2.0) * 0.24;
    if (transition.from != low)
        fraction = 1.0 - fraction;
    middle = from + (to - from) * fraction;
    return true;
}

int StatesView::transitionAt(const QPointF& at) const
{
    int best = -1;
    double nearest = 7.0;
    const QFontMetricsF metrics(font());
    for (int i = 0; i < static_cast<int>(_graph.transitions.size()); ++i)
    {
        QPointF from, to, middle;
        if (!arrow(i, from, to, middle))
            continue;
        const double distance = distanceToSegment(at, from, to);
        // Its words count as it too.
        const QString words = QString::fromStdString(
            fla::states::describe(_graph, _graph.transitions[static_cast<size_t>(i)]));
        QRectF label(QPointF(), QSizeF(std::min(metrics.horizontalAdvance(words), 180.0) + 8.0, metrics.height() + 2.0));
        label.moveCenter(middle);
        if (label.contains(at))
            return i;
        if (distance < nearest)
        {
            nearest = distance;
            best = i;
        }
    }
    return best;
}

void StatesView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& colors = palette();
    painter.fillRect(rect(), colors.base());
    const QColor line = colors.color(QPalette::Text);
    const QColor accent = colors.color(QPalette::Highlight);
    const QColor start(76, 175, 80);

    bool any = false;
    for (int i = 0; i < static_cast<int>(_graph.states.size()) && !any; ++i)
        any = shown(i);
    if (!any)
    {
        painter.setPen(colors.color(QPalette::PlaceholderText));
        painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
            _layer > 0 ? "No states in this layer yet.\n\nClick Add State to make one of its movie clip's frame "
                         "labels a state."
                       : "No states yet.\n\nLabel the frames of each pose or animation in the timeline, "
                         "then click Add State to make each label a state.");
        return;
    }

    // Any State, for the transitions that leave whatever state is playing.
    const QRectF anyState = anyBox();
    painter.setPen(QPen(colors.color(QPalette::Mid), 1.0, Qt::DashLine));
    painter.setBrush(colors.alternateBase());
    painter.drawRoundedRect(anyState, 6, 6);
    painter.setPen(line);
    painter.drawText(anyState, Qt::AlignCenter, "Any state");

    // The arrows under the boxes.
    const QFontMetricsF metrics(font());
    for (int i = 0; i < static_cast<int>(_graph.transitions.size()); ++i)
    {
        QPointF from, to, middle;
        if (!arrow(i, from, to, middle))
            continue;
        const bool picked = _kind == Pick::Transition && _index == i;
        const QColor color = picked ? accent : line;
        painter.setPen(QPen(color, picked ? 2.4 : 1.4));
        painter.setBrush(Qt::NoBrush);
        const fla::states::Transition& transition = _graph.transitions[static_cast<size_t>(i)];
        QPointF tip = to, tail = from;
        if (transition.from == transition.to)
        {
            QPainterPath loop(from);
            loop.cubicTo(from + QPointF(-10, -40), to + QPointF(10, -40), to);
            painter.drawPath(loop);
            tail = to + QPointF(10, -40);
        }
        else
            painter.drawLine(from, to);
        // Its head.
        const QPointF direction = tip - tail;
        const double length = std::hypot(direction.x(), direction.y());
        if (length > 0.0)
        {
            const QPointF unit = direction / length;
            const QPointF side(-unit.y(), unit.x());
            QPolygonF head;
            head << tip << tip - unit * 10.0 + side * 5.0 << tip - unit * 10.0 - side * 5.0;
            painter.setBrush(color);
            painter.drawPolygon(head);
        }
    }

    // The boxes.
    const int entryState = _graph.entryOf(_layer);
    for (int i = 0; i < static_cast<int>(_graph.states.size()); ++i)
    {
        if (!shown(i))
            continue;
        const fla::states::State& state = _graph.states[static_cast<size_t>(i)];
        const QRectF rect = box(i);
        const bool picked = _kind == Pick::State && _index == i;
        const bool entry = entryState == i;
        painter.setPen(QPen(picked ? accent : entry ? start : colors.color(QPalette::Mid), picked || entry ? 2.4 : 1.2));
        painter.setBrush(colors.button());
        painter.drawRoundedRect(rect, 8, 8);
        QFont bold = font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.setPen(colors.color(QPalette::ButtonText));
        const QRectF title = rect.adjusted(10, 6, -14, -rect.height() / 2.0);
        painter.drawText(title, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetricsF(bold).elidedText(QString::fromStdString(state.label), Qt::ElideRight, title.width()));
        painter.setFont(font());
        painter.setPen(colors.color(QPalette::PlaceholderText));
        QString detail = QString::fromStdString(fla::states::words(state.play));
        if (state.play == fla::states::Play::Blend && !state.blend.empty())
            detail = "follows " + QString::fromStdString(state.blend);
        if (static_cast<size_t>(i) < _ranges.size() && !_ranges[static_cast<size_t>(i)].isEmpty())
            detail += ", " + _ranges[static_cast<size_t>(i)];
        const QRectF below = rect.adjusted(10, rect.height() / 2.0, -14, -6);
        painter.drawText(below, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(detail, Qt::ElideRight, below.width()));
        if (entry)
        {
            // The starting state: an arrow in from the left.
            painter.setPen(QPen(start, 2.0));
            const QPointF at(rect.left(), rect.center().y());
            painter.drawLine(at - QPointF(14, 0), at);
            painter.setBrush(start);
            QPolygonF head;
            head << at << at + QPointF(-7, -4) << at + QPointF(-7, 4);
            painter.drawPolygon(head);
        }
        // The dot to drag a new transition from.
        painter.setPen(Qt::NoPen);
        painter.setBrush(picked ? accent : colors.color(QPalette::Mid));
        painter.drawEllipse(handle(i), kHandle - 1.5, kHandle - 1.5);
    }
    painter.setBrush(colors.color(QPalette::Mid));
    painter.drawEllipse(handle(-1), kHandle - 2.0, kHandle - 2.0);

    // What sets each arrow off, on it, over the boxes so it can be read.
    for (int i = 0; i < static_cast<int>(_graph.transitions.size()); ++i)
    {
        QPointF from, to, middle;
        if (!arrow(i, from, to, middle))
            continue;
        const bool picked = _kind == Pick::Transition && _index == i;
        const QString words = metrics.elidedText(
            QString::fromStdString(fla::states::describe(_graph, _graph.transitions[static_cast<size_t>(i)])),
            Qt::ElideRight, 180.0);
        QRectF label(QPointF(), QSizeF(metrics.horizontalAdvance(words) + 8.0, metrics.height() + 2.0));
        label.moveCenter(middle);
        painter.setPen(QPen(picked ? accent : colors.color(QPalette::Mid), 1.0));
        painter.setBrush(colors.base());
        painter.drawRoundedRect(label, 4, 4);
        painter.setPen(picked ? accent : line);
        painter.drawText(label, Qt::AlignCenter, words);
    }

    if (_connecting)
    {
        painter.setPen(QPen(accent, 1.6, Qt::DashLine));
        painter.drawLine(_grab, _pointer);
    }
}

void StatesView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF at = event->position();
    _dragMoved = false;
    // A dot: a new transition from there.
    for (int i = -1; i < static_cast<int>(_graph.states.size()); ++i)
    {
        if (!shown(i))
            continue;
        const QPointF dot = handle(i);
        if (std::hypot(at.x() - dot.x(), at.y() - dot.y()) <= kHandle + 2.0)
        {
            _connecting = true;
            _dragging = i;
            _grab = dot;
            _pointer = at;
            update();
            return;
        }
    }
    const int state = stateAt(at);
    if (state >= 0)
    {
        _dragging = state;
        _origin = QPointF(_graph.states[static_cast<size_t>(state)].x, _graph.states[static_cast<size_t>(state)].y);
        _dragOffset = at - box(state).topLeft();
        setPicked(Pick::State, state);
        emit picked(Pick::State, state);
        return;
    }
    const int transition = transitionAt(at);
    setPicked(transition >= 0 ? Pick::Transition : Pick::None, transition);
    emit picked(_kind, _index);
}

void StatesView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    if (_connecting)
    {
        _pointer = at;
        update();
        return;
    }
    if (_dragging < 0 || !(event->buttons() & Qt::LeftButton))
    {
        bool over = false;
        for (int i = -1; i < static_cast<int>(_graph.states.size()) && !over; ++i)
            over = shown(i) && std::hypot(at.x() - handle(i).x(), at.y() - handle(i).y()) <= kHandle + 2.0;
        setCursor(over ? Qt::CrossCursor : Qt::ArrowCursor);
        setToolTip(over ? "Drag to another state to add a transition" : QString());
        return;
    }
    fla::states::State& state = _graph.states[static_cast<size_t>(_dragging)];
    const QPointF topLeft = at - _dragOffset;
    state.x = std::max(0.0, std::round(topLeft.x() - kLeft));
    state.y = std::max(0.0, std::round(topLeft.y() - kTop));
    _dragMoved = _dragMoved || state.x != _origin.x() || state.y != _origin.y();
    update();
}

void StatesView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int dragged = _dragging;
    _dragging = -1;
    if (_connecting)
    {
        _connecting = false;
        update();
        const int target = stateAt(event->position());
        if (target >= 0 && target != dragged)
            emit connectRequested(dragged, target);
        return;
    }
    if (dragged >= 0 && _dragMoved)
    {
        const fla::states::State& state = _graph.states[static_cast<size_t>(dragged)];
        emit moved(dragged, state.x, state.y);
    }
}
