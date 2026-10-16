#include "node_graph_view.h"

#include "node_graph_style.h"
#include "node_search.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using fla::nodegraph::Graph;
using fla::nodegraph::Node;

// Sizes, in graph units.
constexpr double kWidth = 184.0;
constexpr double kHeader = 30.0;
constexpr double kRow = 22.0;
constexpr double kPad = 8.0;
constexpr double kPin = 5.0;
constexpr double kRadius = 8.0;
constexpr double kDot = 18.0;
constexpr double kMinZoom = 0.2;
constexpr double kMaxZoom = 2.5;
constexpr double kGrid = 20.0;
constexpr double kColumn = 70.0;

const char* const kClipboardMark = "phoenix-nodes";

bool isDot(const Node& node)
{
    return node.type() == "dot";
}

std::string categoryOf(const Node& node)
{
    const fla::nodegraph::Type* type = fla::nodegraph::type(node.type());
    return type ? type->category : std::string("Utility");
}

bool hasOutput(const Node& node)
{
    const fla::nodegraph::Type* type = fla::nodegraph::type(node.type());
    return !type || type->hasOutput;
}

/// Matte and map inputs are drawn apart from the picture ones.
bool secondaryInput(const std::string& name)
{
    return name == "Matte" || name == "Map";
}

QColor mixed(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
        a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

double distanceToPath(const QPainterPath& path, const QPointF& point)
{
    double best = 1e18;
    QPointF previous = path.pointAtPercent(0.0);
    for (int i = 1; i <= 40; ++i)
    {
        const QPointF next = path.pointAtPercent(i / 40.0);
        const QPointF d = next - previous;
        const double length2 = d.x() * d.x() + d.y() * d.y();
        double t = length2 > 0 ? ((point - previous).x() * d.x() + (point - previous).y() * d.y()) / length2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const QPointF nearest = previous + d * t;
        best = std::min(best, std::hypot(point.x() - nearest.x(), point.y() - nearest.y()));
        previous = next;
    }
    return best;
}

/// The checkerboard behind a picture's transparent parts.
void drawChecker(QPainter& painter, const QRectF& rect, double cell)
{
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    const bool dark = theme::isDark();
    const QColor a = dark ? QColor(46, 48, 54) : QColor(236, 237, 240);
    const QColor b = dark ? QColor(56, 58, 65) : QColor(250, 250, 252);
    painter.fillRect(rect, a);
    painter.setPen(Qt::NoPen);
    painter.setBrush(b);
    for (double y = rect.top(), row = 0; y < rect.bottom(); y += cell, ++row)
        for (double x = rect.left() + (static_cast<int>(row) % 2) * cell; x < rect.right(); x += 2 * cell)
            painter.drawRect(QRectF(x, y, cell, cell));
    painter.restore();
}

/// An eye, open, in \a box.
void drawEye(QPainter& painter, const QRectF& box, const QColor& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF c = box.center();
    const double w = box.width() * 0.42, h = box.height() * 0.26;
    QPainterPath lid;
    lid.moveTo(c.x() - w, c.y());
    lid.quadTo(c.x(), c.y() - 2.2 * h, c.x() + w, c.y());
    lid.quadTo(c.x(), c.y() + 2.2 * h, c.x() - w, c.y());
    painter.setPen(QPen(color, box.width() / 11.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(lid);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(c, h * 0.95, h * 0.95);
    painter.restore();
}

} // namespace

NodeGraphView::NodeGraphView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 160);

    auto* create = new QPushButton("Create Node Graph", this);
    create->setCursor(Qt::PointingHandCursor);
    create->setToolTip("Composite this timeline's layers through a graph of nodes");
    connect(create, &QPushButton::clicked, this, &NodeGraphView::createRequested);
    _createButton = create;

    _animation = new QVariantAnimation(this);
    _animation->setDuration(220);
    _animation->setEasingCurve(QEasingCurve::OutCubic);
}

NodeGraphView::~NodeGraphView() = default;

// ---------------------------------------------------------------------------
// What it shows

void NodeGraphView::set(const Graph* graph)
{
    const bool had = _hasGraph;
    _hasGraph = graph != nullptr;
    _graph = graph ? *graph : Graph();
    // The selection keeps what is still there.
    QSet<int> kept;
    for (int id : _selected)
        if (_graph.node(id) || _graph.backdrop(id))
            kept.insert(id);
    const bool selectionChanged = kept != _selected;
    _selected = kept;
    if (_selectedLink >= static_cast<int>(_graph.links.size()))
        _selectedLink = -1;
    _createButton->setVisible(!_hasGraph);
    resizeEvent(nullptr);
    if (_hasGraph && !had)
        frameAll(false);
    update();
    if (selectionChanged)
        emit this->selectionChanged();
}

void NodeGraphView::setPictures(const QMap<int, QImage>& pictures, const QMap<int, QString>& problems)
{
    _pictures = pictures;
    _problems = problems;
    update();
}

void NodeGraphView::setStageAspect(double aspect)
{
    if (aspect > 0.05 && aspect < 20.0 && std::fabs(aspect - _aspect) > 1e-6)
    {
        _aspect = aspect;
        update();
    }
}

void NodeGraphView::setViewed(int node)
{
    if (_viewed != node)
    {
        _viewed = node;
        update();
    }
}

QList<int> NodeGraphView::selectedNodes() const
{
    QList<int> ids;
    for (const Node& node : _graph.nodes)
        if (_selected.contains(node.id))
            ids << node.id;
    return ids;
}

int NodeGraphView::currentNode() const
{
    const QList<int> ids = selectedNodes();
    return ids.size() == 1 ? ids.front() : 0;
}

void NodeGraphView::selectNode(int id)
{
    _selected = {id};
    _selectedLink = -1;
    update();
    emit selectionChanged();
}

void NodeGraphView::setShowPictures(bool show)
{
    _showPictures = show;
    update();
}

void NodeGraphView::setShowMinimap(bool show)
{
    _showMinimap = show;
    update();
}

void NodeGraphView::setView(const View& view)
{
    _animation->stop();
    _pan = view.pan;
    _zoom = std::clamp(view.zoom, kMinZoom, kMaxZoom);
    update();
}

// ---------------------------------------------------------------------------
// Geometry

double NodeGraphView::pictureHeight() const
{
    return std::clamp((kWidth - 2 * kPad) / _aspect, 48.0, 168.0);
}

QRectF NodeGraphView::nodeRect(const Node& node) const
{
    if (isDot(node))
        return QRectF(node.x, node.y, kDot, kDot);
    const int rows = static_cast<int>(fla::nodegraph::inputNames(node).size());
    double height = kHeader;
    if (rows > 0)
        height += 4.0 + rows * kRow + 2.0;
    if (_showPictures && !node.collapsed)
        height += kPad + pictureHeight() + kPad;
    else
        height += rows > 0 ? 4.0 : 6.0;
    return QRectF(node.x, node.y, kWidth, height);
}

QRectF NodeGraphView::pictureRect(const Node& node) const
{
    const QRectF r = nodeRect(node);
    const double h = pictureHeight();
    return QRectF(r.left() + kPad, r.bottom() - kPad - h, r.width() - 2 * kPad, h);
}

QPointF NodeGraphView::outputPin(const Node& node) const
{
    const QRectF r = nodeRect(node);
    if (isDot(node))
        return QPointF(r.right(), r.center().y());
    return QPointF(r.right(), r.top() + kHeader / 2.0);
}

QPointF NodeGraphView::inputPin(const Node& node, int input) const
{
    const QRectF r = nodeRect(node);
    if (isDot(node))
        return QPointF(r.left(), r.center().y());
    return QPointF(r.left(), r.top() + kHeader + 4.0 + input * kRow + kRow / 2.0);
}

QRectF NodeGraphView::eyeRect(const Node& node) const
{
    const QRectF r = nodeRect(node);
    return QRectF(r.right() - 26.0, r.top() + 6.0, 18.0, 18.0);
}

QRectF NodeGraphView::graphBounds(bool selectedOnly) const
{
    QRectF bounds;
    for (const Node& node : _graph.nodes)
        if (!selectedOnly || _selected.contains(node.id))
            bounds = bounds.isNull() ? nodeRect(node) : bounds.united(nodeRect(node));
    for (const fla::nodegraph::Backdrop& backdrop : _graph.backdrops)
        if (!selectedOnly || _selected.contains(backdrop.id))
        {
            const QRectF r(backdrop.x, backdrop.y, backdrop.width, backdrop.height);
            bounds = bounds.isNull() ? r : bounds.united(r);
        }
    return bounds;
}

QPainterPath NodeGraphView::linkPath(const QPointF& from, const QPointF& to) const
{
    const double dx = to.x() - from.x();
    // Forward, the curve eases out and in; going back, it swings wider.
    const double reach = dx >= 0 ? std::max(40.0, dx * 0.5) : std::max(80.0, std::min(240.0, -dx * 0.6 + 40.0));
    QPainterPath path(from);
    path.cubicTo(from + QPointF(reach, 0), to - QPointF(reach, 0), to);
    return path;
}

QPointF NodeGraphView::toGraph(const QPointF& screen) const
{
    return (screen - _pan) / _zoom;
}

QPointF NodeGraphView::toScreen(const QPointF& graph) const
{
    return graph * _zoom + _pan;
}

QRectF NodeGraphView::minimapRect() const
{
    const QSizeF size(160, 104);
    return QRectF(width() - size.width() - 12, height() - size.height() - 12, size.width(), size.height());
}

QRectF NodeGraphView::minimapWorld() const
{
    QRectF world = graphBounds(false);
    const QRectF visible(toGraph(QPointF(0, 0)), toGraph(QPointF(width(), height())));
    world = world.isNull() ? visible : world.united(visible);
    return world.adjusted(-60, -60, 60, 60);
}

bool NodeGraphView::minimapShown() const
{
    // Only with room for it, and something out of view to find.
    if (!_showMinimap || _graph.nodes.empty() || width() < 360 || height() < 220)
        return false;
    const QRectF visible(toGraph(QPointF(0, 0)), toGraph(QPointF(width(), height())));
    return !visible.contains(graphBounds(false));
}

NodeGraphView::Hit NodeGraphView::hitAt(const QPointF& screen) const
{
    Hit hit;
    if (!_hasGraph)
        return hit;
    if (minimapShown() && minimapRect().contains(screen))
    {
        hit.kind = Hit::Minimap;
        return hit;
    }
    const QPointF p = toGraph(screen);
    const double pinReach = std::max(kPin + 3.0, 10.0 / _zoom);
    // Pins first, topmost node first.
    for (auto it = _graph.nodes.rbegin(); it != _graph.nodes.rend(); ++it)
    {
        const Node& node = *it;
        if (hasOutput(node))
        {
            const QPointF pin = outputPin(node);
            if (std::hypot(p.x() - pin.x(), p.y() - pin.y()) <= pinReach)
            {
                hit.kind = Hit::Output;
                hit.node = node.id;
                return hit;
            }
        }
        const int inputs = static_cast<int>(fla::nodegraph::inputNames(node).size());
        for (int i = 0; i < inputs; ++i)
        {
            const QPointF pin = inputPin(node, i);
            if (std::hypot(p.x() - pin.x(), p.y() - pin.y()) <= pinReach)
            {
                hit.kind = Hit::Input;
                hit.node = node.id;
                hit.input = i;
                return hit;
            }
        }
    }
    for (auto it = _graph.nodes.rbegin(); it != _graph.nodes.rend(); ++it)
    {
        const Node& node = *it;
        const QRectF r = nodeRect(node);
        if (!r.contains(p))
            continue;
        hit.node = node.id;
        if (!isDot(node) && hasOutput(node) && eyeRect(node).adjusted(-2, -2, 2, 2).contains(p) && _zoom >= 0.45)
            hit.kind = Hit::Eye;
        else if (isDot(node) || p.y() < r.top() + kHeader)
            hit.kind = Hit::Header;
        else
            hit.kind = Hit::Node;
        return hit;
    }
    // Links.
    double best = 7.0 / _zoom;
    for (int i = 0; i < static_cast<int>(_graph.links.size()); ++i)
    {
        const fla::nodegraph::Link& link = _graph.links[static_cast<size_t>(i)];
        const Node* from = _graph.node(link.from);
        const Node* to = _graph.node(link.to);
        if (!from || !to)
            continue;
        const QPainterPath path = linkPath(outputPin(*from), inputPin(*to, link.input));
        if (!path.boundingRect().adjusted(-best, -best, best, best).contains(p))
            continue;
        const double d = distanceToPath(path, p);
        if (d < best)
        {
            best = d;
            hit.kind = Hit::Link;
            hit.link = i;
        }
    }
    if (hit.kind == Hit::Link)
        return hit;
    for (auto it = _graph.backdrops.rbegin(); it != _graph.backdrops.rend(); ++it)
    {
        const QRectF r(it->x, it->y, it->width, it->height);
        if (!r.contains(p))
            continue;
        hit.node = it->id;
        if (QRectF(r.right() - 16, r.bottom() - 16, 16, 16).contains(p))
            hit.kind = Hit::BackdropCorner;
        else if (p.y() < r.top() + 28.0)
            hit.kind = Hit::BackdropTitle;
        else
            hit.kind = Hit::Backdrop;
        return hit;
    }
    return hit;
}

// ---------------------------------------------------------------------------
// Drawing

void NodeGraphView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(rect(), theme::color("nodes.background"));
    if (!_hasGraph)
    {
        drawEmpty(painter);
        return;
    }
    drawGrid(painter);

    painter.save();
    painter.translate(_pan);
    painter.scale(_zoom, _zoom);
    drawBackdrops(painter);
    drawLinks(painter);
    // Selected nodes over the rest.
    for (const Node& node : _graph.nodes)
        if (!_selected.contains(node.id))
            drawNode(painter, node);
    for (const Node& node : _graph.nodes)
        if (_selected.contains(node.id))
            drawNode(painter, node);

    // Lines a dragged node lines up with.
    if (_drag == Drag::Nodes && (_guideX || _guideY))
    {
        QPen guide(theme::color("accent"), 1.0, Qt::DashLine);
        guide.setCosmetic(true);
        painter.setPen(guide);
        const QRectF visible(toGraph(QPointF(0, 0)), toGraph(QPointF(width(), height())));
        if (_guideX)
            painter.drawLine(QPointF(*_guideX, visible.top()), QPointF(*_guideX, visible.bottom()));
        if (_guideY)
            painter.drawLine(QPointF(visible.left(), *_guideY), QPointF(visible.right(), *_guideY));
    }
    painter.restore();

    if (_drag == Drag::Box && !_box.isNull())
    {
        const QRectF box(toScreen(_box.topLeft()), toScreen(_box.bottomRight()));
        QColor fill = theme::color("accent");
        fill.setAlpha(28);
        QColor edge = theme::color("accent");
        edge.setAlpha(200);
        painter.setPen(QPen(edge, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(box.normalized(), 3, 3);
    }
    if (minimapShown())
        drawMinimap(painter);
}

void NodeGraphView::drawEmpty(QPainter& painter)
{
    const QPointF c(width() / 2.0, height() / 2.0 - 46);
    // Three nodes linked, faintly.
    QColor faint = theme::color("nodes.dimText");
    faint.setAlpha(110);
    painter.setPen(QPen(faint, 1.6));
    painter.setBrush(Qt::NoBrush);
    const QRectF a(c.x() - 74, c.y() - 30, 40, 26), b(c.x() - 74, c.y() + 6, 40, 26), out(c.x() + 30, c.y() - 12, 44, 26);
    for (const QRectF& r : {a, b, out})
        painter.drawRoundedRect(r, 6, 6);
    painter.drawPath(linkPath(QPointF(a.right(), a.center().y()), QPointF(out.left(), out.center().y() - 5)));
    painter.drawPath(linkPath(QPointF(b.right(), b.center().y()), QPointF(out.left(), out.center().y() + 5)));
    painter.setPen(theme::color("nodes.dimText"));
    QFont font = painter.font();
    painter.setFont(font);
    painter.drawText(QRectF(16, c.y() + 44, width() - 32, 40), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
        "This timeline's layers stack as usual. A node graph composites them through effects, merges and "
        "mattes instead.");
}

void NodeGraphView::drawGrid(QPainter& painter)
{
    const QRectF visible(toGraph(QPointF(0, 0)), toGraph(QPointF(width(), height())));
    const auto lines = [&](double step, const QColor& color) {
        if (step * _zoom < 7.0)
            return;
        painter.setPen(QPen(color, 1.0));
        for (double x = std::floor(visible.left() / step) * step; x <= visible.right(); x += step)
        {
            const double sx = std::round(x * _zoom + _pan.x()) + 0.5;
            painter.drawLine(QPointF(sx, 0), QPointF(sx, height()));
        }
        for (double y = std::floor(visible.top() / step) * step; y <= visible.bottom(); y += step)
        {
            const double sy = std::round(y * _zoom + _pan.y()) + 0.5;
            painter.drawLine(QPointF(0, sy), QPointF(width(), sy));
        }
    };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    lines(kGrid, theme::color("nodes.grid"));
    lines(kGrid * 5, theme::color("nodes.gridMajor"));
    painter.restore();
}

void NodeGraphView::drawBackdrops(QPainter& painter)
{
    for (const fla::nodegraph::Backdrop& backdrop : _graph.backdrops)
    {
        const QRectF r(backdrop.x, backdrop.y, backdrop.width, backdrop.height);
        const QColor color = QColor::fromRgb(backdrop.color);
        QColor fill = color;
        fill.setAlpha(theme::isDark() ? 26 : 38);
        QColor edge = color;
        edge.setAlpha(_selected.contains(backdrop.id) ? 230 : 110);
        QPen pen(edge, _selected.contains(backdrop.id) ? 2.0 : 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(fill);
        painter.drawRoundedRect(r, 10, 10);
        // Its title bar.
        QPainterPath title;
        title.addRoundedRect(r, 10, 10);
        QPainterPath bar;
        bar.addRect(QRectF(r.left(), r.top(), r.width(), 28));
        QColor barColor = color;
        barColor.setAlpha(theme::isDark() ? 70 : 90);
        painter.setPen(Qt::NoPen);
        painter.setBrush(barColor);
        painter.drawPath(title.intersected(bar));
        if (_zoom >= 0.35)
        {
            QFont font = painter.font();
            font.setPixelSize(13);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(theme::color("nodes.text"));
            painter.drawText(QRectF(r.left() + 12, r.top(), r.width() - 24, 28), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(font).elidedText(QString::fromStdString(backdrop.title), Qt::ElideRight, r.width() - 24));
        }
        // The corner to size it by.
        QColor grip = color;
        grip.setAlpha(150);
        painter.setPen(QPen(grip, 1.5));
        for (int i = 0; i < 3; ++i)
            painter.drawLine(QPointF(r.right() - 4 - i * 4, r.bottom() - 4), QPointF(r.right() - 4, r.bottom() - 4 - i * 4));
    }
}

void NodeGraphView::drawLinks(QPainter& painter)
{
    const QColor base = theme::color("nodes.link");
    const QColor accent = theme::color("accent");
    const QColor background = theme::color("nodes.background");
    const auto stroke = [&](const QPainterPath& path, const QColor& color, double width, bool dashed) {
        QPen under(background, width + 3.0, Qt::SolidLine, Qt::RoundCap);
        under.setCosmetic(true);
        painter.setPen(under);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        QPen pen(color, width, dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap);
        pen.setCosmetic(true);
        if (dashed)
            pen.setDashPattern({3.0, 2.5});
        painter.setPen(pen);
        painter.drawPath(path);
    };
    for (int i = 0; i < static_cast<int>(_graph.links.size()); ++i)
    {
        const fla::nodegraph::Link& link = _graph.links[static_cast<size_t>(i)];
        const Node* from = _graph.node(link.from);
        const Node* to = _graph.node(link.to);
        if (!from || !to)
            continue;
        const QPainterPath path = linkPath(outputPin(*from), inputPin(*to, link.input));
        const std::vector<std::string> names = fla::nodegraph::inputNames(*to);
        const bool secondary = link.input < static_cast<int>(names.size()) && secondaryInput(names[link.input]);
        const bool related = _selected.contains(link.from) || _selected.contains(link.to);
        const bool hovered = _hover.kind == Hit::Link && _hover.link == i;
        QColor color = secondary ? theme::color("nodes.matte") : base;
        double width = 2.0;
        if (i == _insertLink)
        {
            QPen glow(accent, 7.0, Qt::SolidLine, Qt::RoundCap);
            glow.setCosmetic(true);
            QColor soft = accent;
            soft.setAlpha(80);
            glow.setColor(soft);
            painter.setPen(glow);
            painter.drawPath(path);
            color = accent;
            width = 2.6;
        }
        else if (i == _selectedLink)
        {
            color = accent;
            width = 3.0;
        }
        else if (related)
        {
            color = mixed(color, accent, 0.75);
            width = 2.4;
        }
        else if (hovered)
        {
            color = mixed(color, theme::color("nodes.text"), 0.5);
            width = 2.6;
        }
        stroke(path, color, width, secondary);
    }
    // A link being drawn.
    if (_drag == Drag::Link)
    {
        QPointF a, b;
        if (const Node* from = _graph.node(_linkFrom))
        {
            a = outputPin(*from);
            b = _linkEnd;
            if (_hover.kind == Hit::Input)
                if (const Node* target = _graph.node(_hover.node))
                    b = inputPin(*target, _hover.input);
        }
        else if (const Node* to = _graph.node(_linkTo))
        {
            b = inputPin(*to, _linkInput);
            a = _linkEnd;
            if (_hover.kind == Hit::Output)
                if (const Node* source = _graph.node(_hover.node))
                    a = outputPin(*source);
        }
        stroke(linkPath(a, b), accent, 2.2, _hover.kind != Hit::Input && _hover.kind != Hit::Output);
    }
}

void NodeGraphView::drawNode(QPainter& painter, const Node& node)
{
    const QRectF r = nodeRect(node);
    const bool selected = _selected.contains(node.id);
    const bool hovered = _hover.node == node.id && _hover.kind != Hit::None && _hover.kind != Hit::Link;
    const std::string category = categoryOf(node);
    QColor header = nodestyle::categoryColor(category);
    const QColor accent = theme::color("accent");
    const QColor body = theme::color("nodes.node");
    const QColor border = theme::color("nodes.border");
    const QColor pictureColor = theme::color("nodes.picture");
    const bool detailed = _zoom >= 0.45;

    if (isDot(node))
    {
        // A reroute: a small round point.
        painter.setPen(QPen(selected ? accent : pictureColor, 2.0));
        painter.setBrush(body);
        painter.drawEllipse(r.center(), kDot / 2 - 2, kDot / 2 - 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(selected ? accent : pictureColor);
        painter.drawEllipse(r.center(), 3.0, 3.0);
        return;
    }

    painter.save();
    if (node.bypassed())
    {
        header = mixed(QColor(120, 122, 130), header, 0.25);
        painter.setOpacity(0.62);
    }
    // A soft shadow under it.
    painter.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i)
    {
        painter.setBrush(QColor(0, 0, 0, theme::isDark() ? 22 : 10));
        painter.drawRoundedRect(r.adjusted(-i * 0.8, -i * 0.4 + 2.0, i * 0.8, i * 1.2 + 2.0), kRadius + i, kRadius + i);
    }
    // The glow of a selected node.
    if (selected)
    {
        QColor glow = accent;
        glow.setAlpha(70);
        painter.setPen(QPen(glow, 6.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(r.adjusted(-2, -2, 2, 2), kRadius + 2, kRadius + 2);
    }
    QPainterPath shape;
    shape.addRoundedRect(r, kRadius, kRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(body);
    painter.drawPath(shape);

    // The header, its category's color, lit from above.
    {
        QPainterPath bar;
        bar.addRect(QRectF(r.left(), r.top(), r.width(), kHeader));
        const QPainterPath top = shape.intersected(bar);
        QLinearGradient light(r.topLeft(), QPointF(r.left(), r.top() + kHeader));
        light.setColorAt(0.0, header.lighter(118));
        light.setColorAt(1.0, header.darker(106));
        painter.setBrush(light);
        painter.drawPath(top);
        painter.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
        painter.drawLine(QPointF(r.left(), r.top() + kHeader), QPointF(r.right(), r.top() + kHeader));
    }

    const QColor onHeader(255, 255, 255, 240);
    const QString problem = _problems.value(node.id);
    double titleRight = r.right() - 10;
    if (detailed)
    {
        nodestyle::drawIcon(painter, QRectF(r.left() + 9, r.top() + 8, 14, 14), category, onHeader);
        // The eye: shown on the stage, or offered when hovered.
        if (hasOutput(node))
        {
            const QRectF eye = eyeRect(node);
            const bool viewed = _viewed == node.id;
            if (viewed)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 235));
                painter.drawRoundedRect(eye.adjusted(-2, -1, 2, 1), 6, 6);
                drawEye(painter, eye, header.darker(140));
            }
            else if (hovered || selected)
            {
                const bool overEye = _hover.kind == Hit::Eye && _hover.node == node.id;
                drawEye(painter, eye, QColor(255, 255, 255, overEye ? 255 : 150));
            }
            titleRight = eye.left() - 4;
        }
        if (!problem.isEmpty())
        {
            const QRectF badge(titleRight - 16, r.top() + 7, 16, 16);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(232, 72, 72));
            painter.drawEllipse(badge);
            QFont bold = painter.font();
            bold.setPixelSize(11);
            bold.setBold(true);
            painter.setFont(bold);
            painter.setPen(Qt::white);
            painter.drawText(badge, Qt::AlignCenter, "!");
            titleRight = badge.left() - 4;
        }
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(onHeader);
        const QRectF titleBox(r.left() + 29, r.top(), titleRight - r.left() - 29, kHeader);
        painter.drawText(titleBox, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetricsF(font).elidedText(QString::fromStdString(fla::nodegraph::title(node)), Qt::ElideRight,
                titleBox.width()));
    }

    // Inputs, named.
    const std::vector<std::string> inputs = fla::nodegraph::inputNames(node);
    if (detailed)
    {
        QFont font = painter.font();
        font.setPixelSize(11);
        font.setWeight(QFont::Normal);
        painter.setFont(font);
        painter.setPen(theme::color("nodes.dimText"));
        for (int i = 0; i < static_cast<int>(inputs.size()); ++i)
        {
            const QPointF pin = inputPin(node, i);
            painter.drawText(QRectF(r.left() + 13, pin.y() - kRow / 2, r.width() - 26, kRow), Qt::AlignLeft | Qt::AlignVCenter,
                QString::fromStdString(inputs[static_cast<size_t>(i)]));
        }
    }

    // The picture of what it gives.
    if (_showPictures && !node.collapsed)
    {
        const QRectF picture = pictureRect(node);
        QPainterPath frame;
        frame.addRoundedRect(picture, 5, 5);
        painter.save();
        painter.setClipPath(frame);
        drawChecker(painter, picture, 6.0);
        const QImage image = _pictures.value(node.id);
        if (!image.isNull() && detailed)
            painter.drawImage(picture, image);
        painter.restore();
        if (!problem.isEmpty() && detailed)
        {
            QFont font = painter.font();
            font.setPixelSize(10);
            painter.setFont(font);
            painter.setPen(QColor(240, 110, 110));
            painter.drawText(picture.adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap, problem);
        }
        painter.setPen(QPen(QColor(0, 0, 0, 70), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(frame);
    }

    // Its edge.
    QPen edge(selected ? accent : node.type() == "output" ? header : hovered ? border.lighter(130) : border,
        selected ? 2.0 : node.type() == "output" ? 1.6 : 1.0);
    painter.setPen(edge);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);

    // A bypassed node's line straight through.
    if (node.bypassed() && !inputs.empty() && hasOutput(node))
    {
        painter.setOpacity(1.0);
        QPen through(QColor(240, 180, 60), 2.0, Qt::DashLine);
        through.setCosmetic(true);
        painter.setPen(through);
        painter.drawLine(inputPin(node, 0), outputPin(node));
    }
    painter.restore();

    // Pins: filled when linked, hollow when not.
    const auto pin = [&](const QPointF& at, const QColor& color, bool linked, bool hot) {
        const double radius = hot ? kPin + 1.6 : kPin;
        if (hot)
        {
            QColor halo = color;
            halo.setAlpha(70);
            painter.setPen(Qt::NoPen);
            painter.setBrush(halo);
            painter.drawEllipse(at, radius + 4, radius + 4);
        }
        painter.setPen(QPen(color, 1.8));
        painter.setBrush(linked ? color : body);
        painter.drawEllipse(at, radius, radius);
    };
    for (int i = 0; i < static_cast<int>(inputs.size()); ++i)
    {
        const bool hot = (_hover.kind == Hit::Input && _hover.node == node.id && _hover.input == i) ||
                         (_drag == Drag::Link && _linkTo == node.id && _linkInput == i);
        pin(inputPin(node, i), secondaryInput(inputs[static_cast<size_t>(i)]) ? theme::color("nodes.matte") : pictureColor,
            _graph.linkInto(node.id, i) != nullptr, hot);
    }
    if (hasOutput(node))
    {
        bool linked = false;
        for (const fla::nodegraph::Link& link : _graph.links)
            linked = linked || link.from == node.id;
        const bool hot = (_hover.kind == Hit::Output && _hover.node == node.id) || (_drag == Drag::Link && _linkFrom == node.id);
        pin(outputPin(node), pictureColor, linked, hot);
    }
}

void NodeGraphView::drawMinimap(QPainter& painter)
{
    const QRectF box = minimapRect();
    const QRectF world = minimapWorld();
    const double scale = std::min(box.width() / world.width(), box.height() / world.height());
    const QPointF origin(box.center().x() - world.center().x() * scale, box.center().y() - world.center().y() * scale);
    const auto map = [&](const QRectF& r) {
        return QRectF(origin + r.topLeft() * scale, r.size() * scale);
    };
    painter.save();
    QColor back = theme::color("nodes.node");
    back.setAlpha(225);
    painter.setPen(QPen(theme::color("nodes.border"), 1.0));
    painter.setBrush(back);
    painter.drawRoundedRect(box, 8, 8);
    painter.setClipRect(box.adjusted(1, 1, -1, -1));
    for (const fla::nodegraph::Backdrop& backdrop : _graph.backdrops)
    {
        QColor fill = QColor::fromRgb(backdrop.color);
        fill.setAlpha(60);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(map(QRectF(backdrop.x, backdrop.y, backdrop.width, backdrop.height)), 2, 2);
    }
    for (const Node& node : _graph.nodes)
    {
        QColor fill = nodestyle::categoryColor(categoryOf(node));
        if (node.bypassed())
            fill.setAlpha(110);
        painter.setPen(_selected.contains(node.id) ? QPen(theme::color("accent"), 1.5) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(map(nodeRect(node)), 1.5, 1.5);
    }
    const QRectF visible = map(QRectF(toGraph(QPointF(0, 0)), toGraph(QPointF(width(), height()))));
    QColor seen = theme::color("accent");
    seen.setAlpha(36);
    painter.setPen(QPen(theme::color("accent"), 1.2));
    painter.setBrush(seen);
    painter.drawRoundedRect(visible, 3, 3);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Changes

void NodeGraphView::commit(const QString& name, bool merge)
{
    update();
    emit edited(_graph, name, merge);
}

void NodeGraphView::addNode(const std::string& type, const QPointF& at)
{
    const int id = _graph.newId();
    Node node = fla::nodegraph::make(type, id, 0, 0, 1 + static_cast<int>(id * 7919 % 9973));
    const QRectF r = nodeRect(node);
    node.x = std::round((at.x() - r.width() / 2.0) / 10.0) * 10.0;
    node.y = std::round((at.y() - kHeader / 2.0) / 10.0) * 10.0;
    _graph.nodes.push_back(node);
    _selected = {id};
    _selectedLink = -1;
    emit selectionChanged();
}

void NodeGraphView::connectTo(int from, int to, int input)
{
    if (_graph.connect(from, to, input))
        commit("Link Nodes");
    else
        update();
}

void NodeGraphView::deleteLink(int index)
{
    if (index < 0 || index >= static_cast<int>(_graph.links.size()))
        return;
    _graph.links.erase(_graph.links.begin() + index);
    _selectedLink = -1;
    commit("Unlink Nodes");
}

void NodeGraphView::insertIntoLink(int id, int index)
{
    if (index < 0 || index >= static_cast<int>(_graph.links.size()))
        return;
    const fla::nodegraph::Link link = _graph.links[static_cast<size_t>(index)];
    const Node* node = _graph.node(id);
    if (!node || !hasOutput(*node) || fla::nodegraph::inputNames(*node).empty() || link.from == id || link.to == id)
        return;
    _graph.links.erase(_graph.links.begin() + index);
    _graph.connect(link.from, id, 0);
    _graph.connect(id, link.to, link.input);
}

void NodeGraphView::openSearch(const QPoint& screen, int fromNode, int toNode, int toInput)
{
    std::function<bool(const fla::nodegraph::Type&)> accept;
    if (fromNode)
        accept = [](const fla::nodegraph::Type& type) { return !type.inputs.empty(); };
    else if (toNode)
        accept = [](const fla::nodegraph::Type& type) { return type.hasOutput; };
    auto* search = new NodeSearch(this, accept);
    const QPointF at = toGraph(screen);
    connect(search, &NodeSearch::chosen, this, [this, at, fromNode, toNode, toInput](const QString& type) {
        addNode(type.toStdString(), at);
        const int id = currentNode();
        if (fromNode)
            _graph.connect(fromNode, id, 0);
        if (toNode)
            _graph.connect(id, toNode, toInput);
        commit("Add Node");
    });
    connect(search, &NodeSearch::dismissed, this, [this]() { update(); });
    search->popup(mapToGlobal(screen));
}

void NodeGraphView::addNodeMenu()
{
    if (!_hasGraph)
        return;
    QPoint at = mapFromGlobal(QCursor::pos());
    if (!rect().contains(at))
        at = rect().center();
    openSearch(at, 0, 0, -1);
}

void NodeGraphView::deleteSelection()
{
    if (_selectedLink >= 0)
    {
        deleteLink(_selectedLink);
        return;
    }
    bool any = false;
    for (int id : QList<int>(_selected.begin(), _selected.end()))
    {
        const Node* node = _graph.node(id);
        // The Output stays: without it the timeline shows nothing.
        if (node && node->type() == "output")
            continue;
        // A reroute taken out joins what went through it.
        if (node && isDot(*node))
        {
            const fla::nodegraph::Link* in = _graph.linkInto(id, 0);
            const int source = in ? in->from : 0;
            std::vector<fla::nodegraph::Link> outs;
            for (const fla::nodegraph::Link& link : _graph.links)
                if (link.from == id)
                    outs.push_back(link);
            _graph.remove(id);
            if (source)
                for (const fla::nodegraph::Link& link : outs)
                    _graph.connect(source, link.to, link.input);
        }
        else
            _graph.remove(id);
        _selected.remove(id);
        any = true;
    }
    if (any)
    {
        emit selectionChanged();
        commit("Delete Nodes");
    }
}

void NodeGraphView::toggleBypass()
{
    bool any = false;
    for (Node& node : _graph.nodes)
        if (_selected.contains(node.id) && node.type() != "output" && !isDot(node))
        {
            node.applied.enabled = !node.applied.enabled;
            any = true;
        }
    if (any)
        commit("Bypass Nodes");
}

void NodeGraphView::toggleCollapsed()
{
    bool any = false;
    for (Node& node : _graph.nodes)
        if (_selected.contains(node.id) && !isDot(node))
        {
            node.collapsed = !node.collapsed;
            any = true;
        }
    if (any)
        commit("Hide Node Pictures");
}

void NodeGraphView::backdropSelection()
{
    QRectF bounds;
    for (const Node& node : _graph.nodes)
        if (_selected.contains(node.id))
            bounds = bounds.isNull() ? nodeRect(node) : bounds.united(nodeRect(node));
    if (bounds.isNull())
    {
        const QPointF c = toGraph(QPointF(width() / 2.0, height() / 2.0));
        bounds = QRectF(c - QPointF(140, 80), QSizeF(280, 160));
    }
    fla::nodegraph::Backdrop backdrop;
    backdrop.id = _graph.newId();
    backdrop.x = bounds.left() - 24;
    backdrop.y = bounds.top() - 48;
    backdrop.width = bounds.width() + 48;
    backdrop.height = bounds.height() + 72;
    backdrop.title = "Backdrop";
    static const uint32_t colors[] = {0x5B8CFF, 0x46C27A, 0xE5A548, 0xC86ADB, 0xE5564B, 0x3FB8B0};
    backdrop.color = colors[_graph.backdrops.size() % 6];
    // Behind everything else.
    _graph.backdrops.insert(_graph.backdrops.begin(), backdrop);
    commit("Add Backdrop");
    startRename(backdrop.id);
}

void NodeGraphView::copy()
{
    Graph part;
    for (const Node& node : _graph.nodes)
        if (_selected.contains(node.id) && node.type() != "output")
            part.nodes.push_back(node);
    for (const fla::nodegraph::Link& link : _graph.links)
        if (part.node(link.from) && part.node(link.to))
            part.links.push_back(link);
    for (const fla::nodegraph::Backdrop& backdrop : _graph.backdrops)
        if (_selected.contains(backdrop.id))
            part.backdrops.push_back(backdrop);
    if (part.nodes.empty() && part.backdrops.empty())
        return;
    auto* data = new QMimeData;
    data->setText(QString(kClipboardMark) + "\n" + QString::fromStdString(fla::nodegraph::encode(part)));
    QApplication::clipboard()->setMimeData(data);
}

void NodeGraphView::paste()
{
    const QString text = QApplication::clipboard()->text();
    if (!text.startsWith(kClipboardMark) || !_hasGraph)
        return;
    Graph part;
    if (!fla::nodegraph::decode(text.mid(text.indexOf('\n') + 1).toStdString(), part))
        return;
    // New ids, and the copies centered where the pointer is.
    QRectF bounds;
    for (const Node& node : part.nodes)
        bounds = bounds.isNull() ? nodeRect(node) : bounds.united(nodeRect(node));
    for (const fla::nodegraph::Backdrop& b : part.backdrops)
        bounds = bounds.isNull() ? QRectF(b.x, b.y, b.width, b.height) : bounds.united(QRectF(b.x, b.y, b.width, b.height));
    QPoint cursor = mapFromGlobal(QCursor::pos());
    if (!rect().contains(cursor))
        cursor = rect().center();
    const QPointF shift = toGraph(cursor) - bounds.center();
    const double dx = std::round(shift.x() / 10.0) * 10.0, dy = std::round(shift.y() / 10.0) * 10.0;
    std::map<int, int> ids;
    int next = _graph.newId();
    _selected.clear();
    for (fla::nodegraph::Backdrop b : part.backdrops)
    {
        b.id = next++;
        b.x += dx;
        b.y += dy;
        _graph.backdrops.push_back(b);
        _selected.insert(b.id);
    }
    for (Node node : part.nodes)
    {
        ids[node.id] = next;
        node.id = next++;
        node.x += dx;
        node.y += dy;
        _graph.nodes.push_back(node);
        _selected.insert(node.id);
    }
    for (const fla::nodegraph::Link& link : part.links)
        _graph.connect(ids[link.from], ids[link.to], link.input);
    emit selectionChanged();
    commit("Paste Nodes");
}

void NodeGraphView::duplicate()
{
    const QMimeData* before = QApplication::clipboard()->mimeData();
    const QString kept = before ? before->text() : QString();
    copy();
    paste();
    QApplication::clipboard()->setText(kept);
}

void NodeGraphView::tidy()
{
    // Columns by how far each node is from the end of the graph, the end on
    // the right; in each column, in the order of what they feed.
    QSet<int> moving = _selected;
    for (int id : QSet<int>(moving))
        if (!_graph.node(id))
            moving.remove(id);
    if (moving.size() < 2)
    {
        moving.clear();
        for (const Node& node : _graph.nodes)
            moving.insert(node.id);
    }
    if (moving.isEmpty())
        return;
    std::map<int, int> depth;
    std::function<int(int, int)> depthOf = [&](int id, int guard) -> int {
        const auto found = depth.find(id);
        if (found != depth.end())
            return found->second;
        int best = 0;
        if (guard < 256)
            for (const fla::nodegraph::Link& link : _graph.links)
                if (link.from == id && moving.contains(link.to))
                    best = std::max(best, depthOf(link.to, guard + 1) + 1);
        depth[id] = best;
        return best;
    };
    int deepest = 0;
    for (int id : moving)
        deepest = std::max(deepest, depthOf(id, 0));
    // Where the graph's right edge and top were, to keep it there.
    QRectF before;
    for (int id : moving)
        before = before.isNull() ? nodeRect(*_graph.node(id)) : before.united(nodeRect(*_graph.node(id)));
    std::map<int, std::vector<int>> columns;
    for (int id : moving)
        columns[depth[id]].push_back(id);
    std::map<int, double> placedY;
    for (int column = 0; column <= deepest; ++column)
    {
        std::vector<int>& ids = columns[column];
        // In the order of the inputs they feed, then of where they were.
        std::map<int, double> key;
        for (int id : ids)
        {
            double sum = 0;
            int count = 0;
            for (const fla::nodegraph::Link& link : _graph.links)
                if (link.from == id && placedY.count(link.to))
                {
                    sum += placedY[link.to] + link.input * kRow;
                    ++count;
                }
            key[id] = count ? sum / count : _graph.node(id)->y;
        }
        std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return key[a] < key[b]; });
        double y = before.top();
        double total = 0;
        for (int id : ids)
            total += nodeRect(*_graph.node(id)).height() + 28.0;
        // Centered on what they feed.
        double mid = 0;
        for (int id : ids)
            mid += key[id];
        mid = ids.empty() ? before.top() : mid / ids.size();
        if (column > 0)
            y = std::max(before.top() - 200.0, mid - total / 2.0 + kHeader);
        for (int id : ids)
        {
            Node* node = _graph.node(id);
            node->x = std::round((before.right() - kWidth - column * (kWidth + kColumn)) / 10.0) * 10.0;
            node->y = std::round(y / 10.0) * 10.0;
            placedY[id] = node->y;
            y += nodeRect(*node).height() + 28.0;
        }
    }
    commit("Line Up Nodes");
}

void NodeGraphView::startRename(int id)
{
    const Node* node = _graph.node(id);
    const fla::nodegraph::Backdrop* backdrop = node ? nullptr : _graph.backdrop(id);
    if ((!node || isDot(*node)) && !backdrop)
        return;
    delete _rename;
    _rename = new QLineEdit(this);
    QRectF box;
    if (node)
    {
        const QRectF r = nodeRect(*node);
        box = QRectF(toScreen(QPointF(r.left() + 26, r.top() + 4)), toScreen(QPointF(r.right() - 30, r.top() + kHeader - 4)));
        _rename->setText(QString::fromStdString(fla::nodegraph::title(*node)));
    }
    else
    {
        box = QRectF(toScreen(QPointF(backdrop->x + 8, backdrop->y + 3)),
            toScreen(QPointF(backdrop->x + std::max(160.0, backdrop->width - 16), backdrop->y + 25)));
        _rename->setText(QString::fromStdString(backdrop->title));
    }
    _rename->setGeometry(box.toRect().adjusted(0, 0, 0, 2));
    _rename->selectAll();
    _rename->show();
    _rename->setFocus();
    QLineEdit* field = _rename;
    connect(field, &QLineEdit::editingFinished, this, [this, field, id]() {
        if (_rename != field)
            return;
        _rename = nullptr;
        const std::string text = field->text().trimmed().toStdString();
        field->deleteLater();
        if (Node* named = _graph.node(id))
        {
            // Its own name, unless it is what it would be called anyway.
            Node plain = *named;
            plain.label.clear();
            const std::string label = text == fla::nodegraph::title(plain) ? std::string() : text;
            if (label != named->label)
            {
                named->label = label;
                commit("Rename Node");
            }
        }
        else if (fla::nodegraph::Backdrop* titled = _graph.backdrop(id))
        {
            if (titled->title != text)
            {
                titled->title = text;
                commit("Rename Backdrop");
            }
        }
        setFocus();
    });
}

// ---------------------------------------------------------------------------
// Moving about

void NodeGraphView::animateTo(const QPointF& pan, double zoom)
{
    _animation->stop();
    _animation->disconnect(this);
    const QPointF startPan = _pan;
    const double startZoom = _zoom;
    _animation->setStartValue(0.0);
    _animation->setEndValue(1.0);
    connect(_animation, &QVariantAnimation::valueChanged, this, [this, startPan, startZoom, pan, zoom](const QVariant& v) {
        const double t = v.toDouble();
        _zoom = startZoom + (zoom - startZoom) * t;
        _pan = startPan + (pan - startPan) * t;
        update();
    });
    _animation->start();
}

void NodeGraphView::frame(bool animate)
{
    QRectF bounds = graphBounds(true);
    if (bounds.isNull())
        bounds = graphBounds(false);
    if (bounds.isNull() || width() < 10 || height() < 10)
        return;
    bounds = bounds.adjusted(-48, -48, 48, 48);
    const double zoom = std::clamp(std::min(width() / bounds.width(), height() / bounds.height()), kMinZoom, 1.15);
    const QPointF pan = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * zoom;
    if (animate)
        animateTo(pan, zoom);
    else
        setView({pan, zoom});
}

void NodeGraphView::frameAll(bool animate)
{
    const QSet<int> kept = _selected;
    _selected.clear();
    frame(animate);
    _selected = kept;
}

void NodeGraphView::scrollToMinimap(const QPointF& screen)
{
    const QRectF box = minimapRect();
    const QRectF world = minimapWorld();
    const double scale = std::min(box.width() / world.width(), box.height() / world.height());
    const QPointF origin(box.center().x() - world.center().x() * scale, box.center().y() - world.center().y() * scale);
    const QPointF target = (screen - origin) / scale;
    _animation->stop();
    _pan = QPointF(width() / 2.0, height() / 2.0) - target * _zoom;
    update();
}

// ---------------------------------------------------------------------------
// Events

void NodeGraphView::resizeEvent(QResizeEvent*)
{
    if (_createButton)
    {
        _createButton->adjustSize();
        const QSize size = _createButton->sizeHint() + QSize(24, 8);
        _createButton->setGeometry(QRect(QPoint((width() - size.width()) / 2, height() / 2 + 40), size));
    }
}

bool NodeGraphView::event(QEvent* event)
{
    // The graph's own keys go to it, not to the window's tools.
    if (event->type() == QEvent::ShortcutOverride && _hasGraph && !_rename)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
        const int k = key->key();
        const bool plain = mods == Qt::NoModifier || mods == Qt::ShiftModifier;
        const bool ctrl = mods == Qt::ControlModifier;
        if ((plain && (k == Qt::Key_Tab || k == Qt::Key_Delete || k == Qt::Key_Backspace || k == Qt::Key_D ||
                          k == Qt::Key_H || k == Qt::Key_V || k == Qt::Key_F || k == Qt::Key_L || k == Qt::Key_B ||
                          k == Qt::Key_Home || k == Qt::Key_Escape || k == Qt::Key_Space || k == Qt::Key_A)) ||
            (ctrl && (k == Qt::Key_C || k == Qt::Key_V || k == Qt::Key_X || k == Qt::Key_D || k == Qt::Key_A)))
        {
            event->accept();
            return true;
        }
    }
    // Tab adds a node here rather than moving the focus on.
    if (event->type() == QEvent::KeyPress && _hasGraph && !_rename &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab)
    {
        addNodeMenu();
        return true;
    }
    if (event->type() == QEvent::ToolTip && _hasGraph)
    {
        auto* help = static_cast<QHelpEvent*>(event);
        const Hit hit = hitAt(help->pos());
        QString tip;
        if (const Node* node = _graph.node(hit.node))
        {
            const fla::nodegraph::Type* type = fla::nodegraph::type(node->type());
            if (hit.kind == Hit::Eye)
                tip = _viewed == node->id ? "Showing this node on the stage; click to show the output again (V)"
                                          : "Show this node on the stage (V)";
            else if (hit.kind == Hit::Input)
            {
                const std::vector<std::string> names = fla::nodegraph::inputNames(*node);
                tip = QString::fromStdString(names[static_cast<size_t>(hit.input)]) +
                      " - drag to a node's output to link it";
            }
            else if (hit.kind == Hit::Output)
                tip = "Drag to an input to link this node's picture to it";
            else if (type && (hit.kind == Hit::Node || hit.kind == Hit::Header))
            {
                tip = "<b>" + QString::fromStdString(type->name).toHtmlEscaped() + "</b><br>" +
                      QString::fromStdString(type->description).toHtmlEscaped();
                if (_problems.contains(node->id))
                    tip += "<br><span style='color:#E85050'>" + _problems.value(node->id).toHtmlEscaped() + "</span>";
                if (node->bypassed())
                    tip += "<br><i>Bypassed: its first input passes through (D).</i>";
            }
        }
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), tip, this);
        return true;
    }
    return QWidget::event(event);
}

void NodeGraphView::leaveEvent(QEvent*)
{
    if (_drag == Drag::None)
    {
        _hover = Hit();
        update();
    }
}

void NodeGraphView::mousePressEvent(QMouseEvent* event)
{
    if (_rename)
        _rename->clearFocus();
    setFocus();
    if (!_hasGraph)
        return;
    _animation->stop();
    _pressScreen = _lastScreen = event->position();
    _pressGraph = toGraph(event->position());
    _moved = false;
    _guideX.reset();
    _guideY.reset();
    _insertLink = -1;

    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton ||
        (event->button() == Qt::LeftButton && _spaceHeld))
    {
        _drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    const Hit hit = hitAt(event->position());
    const bool toggle = event->modifiers() & Qt::ControlModifier;
    const bool add = event->modifiers() & Qt::ShiftModifier;
    switch (hit.kind)
    {
    case Hit::Minimap:
        _drag = Drag::Minimap;
        scrollToMinimap(event->position());
        return;
    case Hit::Eye:
        emit viewRequested(_viewed == hit.node ? 0 : hit.node);
        return;
    case Hit::Output:
        _drag = Drag::Link;
        _linkFrom = hit.node;
        _linkTo = 0;
        _linkInput = -1;
        _linkEnd = _pressGraph;
        update();
        return;
    case Hit::Input:
    {
        _drag = Drag::Link;
        _linkEnd = _pressGraph;
        if (const fla::nodegraph::Link* linked = _graph.linkInto(hit.node, hit.input))
        {
            // Its link picked up, to put somewhere else or drop.
            _linkFrom = linked->from;
            _linkTo = 0;
            _linkInput = -1;
            _graph.links.erase(std::find(_graph.links.begin(), _graph.links.end(), *linked));
            _startPositions.clear();
            _startPositions.insert(-1, QPointF(hit.node, hit.input));
        }
        else
        {
            _linkFrom = 0;
            _linkTo = hit.node;
            _linkInput = hit.input;
            _startPositions.clear();
        }
        update();
        return;
    }
    case Hit::Link:
        if (event->modifiers() & Qt::AltModifier)
        {
            deleteLink(hit.link);
            return;
        }
        _selectedLink = hit.link;
        _selected.clear();
        emit selectionChanged();
        update();
        return;
    case Hit::Node:
    case Hit::Header:
    case Hit::BackdropTitle:
    {
        _selectedLink = -1;
        if (toggle)
        {
            if (_selected.contains(hit.node))
                _selected.remove(hit.node);
            else
                _selected.insert(hit.node);
        }
        else if (add)
            _selected.insert(hit.node);
        else if (!_selected.contains(hit.node))
            _selected = {hit.node};
        emit selectionChanged();
        // Moving: the selected nodes, and the nodes inside selected
        // backdrops.
        _startPositions.clear();
        QSet<int> moving = _selected;
        for (const fla::nodegraph::Backdrop& backdrop : _graph.backdrops)
            if (_selected.contains(backdrop.id))
            {
                const QRectF area(backdrop.x, backdrop.y, backdrop.width, backdrop.height);
                for (const Node& node : _graph.nodes)
                    if (area.contains(nodeRect(node)))
                        moving.insert(node.id);
            }
        for (int id : moving)
        {
            if (const Node* node = _graph.node(id))
                _startPositions.insert(id, QPointF(node->x, node->y));
            else if (const fla::nodegraph::Backdrop* backdrop = _graph.backdrop(id))
                _startPositions.insert(id, QPointF(backdrop->x, backdrop->y));
        }
        _linkFrom = hit.node; // the node under the pointer, which lines up
        _drag = _selected.contains(hit.node) ? Drag::Nodes : Drag::None;
        update();
        return;
    }
    case Hit::BackdropCorner:
        if (const fla::nodegraph::Backdrop* backdrop = _graph.backdrop(hit.node))
        {
            _selected = {hit.node};
            _startPositions.clear();
            _startPositions.insert(hit.node, QPointF(backdrop->width, backdrop->height));
            _linkFrom = hit.node;
            _drag = Drag::BackdropSize;
            emit selectionChanged();
        }
        return;
    case Hit::Backdrop:
    case Hit::None:
        _drag = Drag::Box;
        _box = QRectF();
        _selectedLink = -1;
        _startPositions.clear();
        if (!toggle && !add)
        {
            const bool had = !_selected.isEmpty();
            _selected.clear();
            if (had)
                emit selectionChanged();
        }
        // What was selected before the box, kept when adding to it.
        for (int id : _selected)
            _startPositions.insert(id, QPointF());
        update();
        return;
    }
}

void NodeGraphView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if ((pos - _pressScreen).manhattanLength() > 3)
        _moved = true;
    const QPointF graphPos = toGraph(pos);
    switch (_drag)
    {
    case Drag::None:
    {
        const Hit hit = hitAt(pos);
        if (hit.kind != _hover.kind || hit.node != _hover.node || hit.input != _hover.input || hit.link != _hover.link)
        {
            _hover = hit;
            update();
        }
        Qt::CursorShape shape = Qt::ArrowCursor;
        if (_spaceHeld)
            shape = Qt::OpenHandCursor;
        else if (hit.kind == Hit::Input || hit.kind == Hit::Output)
            shape = Qt::CrossCursor;
        else if (hit.kind == Hit::Eye || hit.kind == Hit::Link)
            shape = Qt::PointingHandCursor;
        else if (hit.kind == Hit::BackdropCorner)
            shape = Qt::SizeFDiagCursor;
        setCursor(shape);
        return;
    }
    case Drag::Pan:
        _pan += pos - _lastScreen;
        _lastScreen = pos;
        update();
        return;
    case Drag::Minimap:
        scrollToMinimap(pos);
        return;
    case Drag::Box:
    {
        _box = QRectF(_pressGraph, graphPos).normalized();
        QSet<int> chosen;
        for (auto it = _startPositions.begin(); it != _startPositions.end(); ++it)
            chosen.insert(it.key());
        for (const Node& node : _graph.nodes)
            if (_box.intersects(nodeRect(node)))
                chosen.insert(node.id);
        if (chosen != _selected)
        {
            _selected = chosen;
            emit selectionChanged();
        }
        update();
        return;
    }
    case Drag::Link:
    {
        _linkEnd = graphPos;
        Hit hit = hitAt(pos);
        // Only pins it could link to light up.
        if (!((_linkFrom && hit.kind == Hit::Input && !_graph.wouldLoop(_linkFrom, hit.node)) ||
                (_linkTo && hit.kind == Hit::Output && !_graph.wouldLoop(hit.node, _linkTo))))
            hit = Hit();
        _hover = hit;
        update();
        return;
    }
    case Drag::Nodes:
    {
        if (!_moved)
            return;
        double dx = graphPos.x() - _pressGraph.x();
        double dy = graphPos.y() - _pressGraph.y();
        // The node under the pointer lines up with others near it; or
        // with the grid.
        _guideX.reset();
        _guideY.reset();
        const Node* lead = _graph.node(_linkFrom);
        if (lead && _startPositions.contains(lead->id) && !(event->modifiers() & Qt::AltModifier))
        {
            const QPointF start = _startPositions.value(lead->id);
            double x = start.x() + dx, y = start.y() + dy;
            const double reach = 7.0 / _zoom;
            double bestX = reach, bestY = reach;
            for (const Node& other : _graph.nodes)
            {
                if (_startPositions.contains(other.id))
                    continue;
                const QRectF o = nodeRect(other);
                for (const double candidate : {o.left(), o.right() + kColumn, o.left() - kColumn - kWidth})
                    if (std::fabs(candidate - x) < bestX)
                    {
                        bestX = std::fabs(candidate - x);
                        _guideX = candidate == o.left() ? candidate : std::optional<double>();
                        dx = candidate - start.x();
                    }
                if (std::fabs(o.top() - y) < bestY)
                {
                    bestY = std::fabs(o.top() - y);
                    _guideY = o.top();
                    dy = o.top() - start.y();
                }
            }
            if (bestX >= reach)
                dx = std::round((start.x() + dx) / 10.0) * 10.0 - start.x();
            if (bestY >= reach)
                dy = std::round((start.y() + dy) / 10.0) * 10.0 - start.y();
        }
        for (auto it = _startPositions.begin(); it != _startPositions.end(); ++it)
        {
            if (Node* node = _graph.node(it.key()))
            {
                node->x = it.value().x() + dx;
                node->y = it.value().y() + dy;
            }
            else if (fla::nodegraph::Backdrop* backdrop = _graph.backdrop(it.key()))
            {
                backdrop->x = it.value().x() + dx;
                backdrop->y = it.value().y() + dy;
            }
        }
        // One node with no links, dragged over a link, goes into it.
        _insertLink = -1;
        if (_startPositions.size() == 1 && lead && hasOutput(*lead) && !fla::nodegraph::inputNames(*lead).empty())
        {
            bool linked = false;
            for (const fla::nodegraph::Link& link : _graph.links)
                linked = linked || link.from == lead->id || link.to == lead->id;
            if (!linked)
            {
                const QPointF centre(nodeRect(*lead).left(), nodeRect(*lead).top() + kHeader / 2.0);
                double best = 24.0 / _zoom;
                for (int i = 0; i < static_cast<int>(_graph.links.size()); ++i)
                {
                    const fla::nodegraph::Link& link = _graph.links[static_cast<size_t>(i)];
                    const Node* from = _graph.node(link.from);
                    const Node* to = _graph.node(link.to);
                    if (!from || !to)
                        continue;
                    const double d = distanceToPath(linkPath(outputPin(*from), inputPin(*to, link.input)), graphPos);
                    if (d < best)
                    {
                        best = d;
                        _insertLink = i;
                    }
                }
                Q_UNUSED(centre);
            }
        }
        update();
        return;
    }
    case Drag::BackdropSize:
        if (fla::nodegraph::Backdrop* backdrop = _graph.backdrop(_linkFrom))
        {
            const QPointF start = _startPositions.value(backdrop->id);
            backdrop->width = std::max(120.0, std::round((start.x() + graphPos.x() - _pressGraph.x()) / 10.0) * 10.0);
            backdrop->height = std::max(80.0, std::round((start.y() + graphPos.y() - _pressGraph.y()) / 10.0) * 10.0);
            update();
        }
        return;
    case Drag::Backdrop:
        return;
    }
}

void NodeGraphView::mouseReleaseEvent(QMouseEvent* event)
{
    const Drag drag = _drag;
    _drag = Drag::None;
    setCursor(_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
    switch (drag)
    {
    case Drag::Pan:
        if (event->button() == Qt::RightButton && !_moved)
        {
            QContextMenuEvent menu(QContextMenuEvent::Mouse, event->position().toPoint(), event->globalPosition().toPoint());
            contextMenuEvent(&menu);
        }
        return;
    case Drag::Nodes:
        _guideX.reset();
        _guideY.reset();
        if (_moved)
        {
            if (_insertLink >= 0)
            {
                insertIntoLink(_linkFrom, _insertLink);
                _insertLink = -1;
                commit("Insert Node");
            }
            else
                commit("Move Nodes");
        }
        else if (!(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
        {
            // A click on one of several selected: it alone.
            const Hit hit = hitAt(event->position());
            if (hit.node && _selected.size() > 1)
            {
                _selected = {hit.node};
                emit selectionChanged();
            }
        }
        update();
        return;
    case Drag::BackdropSize:
        commit("Size Backdrop");
        return;
    case Drag::Box:
        _box = QRectF();
        _startPositions.clear();
        update();
        return;
    case Drag::Link:
    {
        const Hit hit = hitAt(event->position());
        const bool pickedUp = _startPositions.contains(-1);
        _startPositions.clear();
        _hover = Hit();
        if (_linkFrom)
        {
            if (hit.kind == Hit::Input && hit.node != _linkFrom)
            {
                connectTo(_linkFrom, hit.node, hit.input);
                return;
            }
            if ((hit.kind == Hit::Node || hit.kind == Hit::Header) && hit.node != _linkFrom)
            {
                // Onto a node: its first free input.
                if (const Node* target = _graph.node(hit.node))
                {
                    const int count = static_cast<int>(fla::nodegraph::inputNames(*target).size());
                    for (int i = 0; i < count; ++i)
                        if (!_graph.linkInto(hit.node, i))
                        {
                            connectTo(_linkFrom, hit.node, i);
                            return;
                        }
                    if (count > 0)
                    {
                        connectTo(_linkFrom, hit.node, 0);
                        return;
                    }
                }
            }
            if (pickedUp)
            {
                // Dropped nowhere: the link is gone.
                commit("Unlink Nodes");
                return;
            }
            if (hit.kind == Hit::None || hit.kind == Hit::Backdrop)
            {
                openSearch(event->position().toPoint(), _linkFrom, 0, -1);
                return;
            }
        }
        else if (_linkTo)
        {
            if (hit.kind == Hit::Output && hit.node != _linkTo)
            {
                connectTo(hit.node, _linkTo, _linkInput);
                return;
            }
            if ((hit.kind == Hit::Node || hit.kind == Hit::Header) && hit.node != _linkTo)
            {
                if (const Node* source = _graph.node(hit.node); source && hasOutput(*source))
                {
                    connectTo(hit.node, _linkTo, _linkInput);
                    return;
                }
            }
            if (hit.kind == Hit::None || hit.kind == Hit::Backdrop)
            {
                openSearch(event->position().toPoint(), 0, _linkTo, _linkInput);
                return;
            }
        }
        update();
        return;
    }
    default:
        return;
    }
}

void NodeGraphView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!_hasGraph || event->button() != Qt::LeftButton)
        return;
    const Hit hit = hitAt(event->position());
    const Node* node = _graph.node(hit.node);
    if (node && (hit.kind == Hit::Header || hit.kind == Hit::Node))
    {
        if (node->type() == "layer" && hit.kind == Hit::Node)
            emit layerRequested(QString::fromStdString(node->applied.valueAt("layer", 0, std::string())));
        else
            startRename(node->id);
        return;
    }
    if (hit.kind == Hit::BackdropTitle)
    {
        startRename(hit.node);
        return;
    }
    if (hit.kind == Hit::Link)
    {
        // A reroute point where the link was clicked.
        const fla::nodegraph::Link link = _graph.links[static_cast<size_t>(hit.link)];
        addNode("dot", toGraph(event->position()));
        const int dot = currentNode();
        if (Node* made = _graph.node(dot))
        {
            made->x = std::round((toGraph(event->position()).x() - kDot / 2) / 10.0) * 10.0;
            made->y = toGraph(event->position()).y() - kDot / 2;
        }
        _graph.links.erase(_graph.links.begin() + hit.link);
        _graph.connect(link.from, dot, 0);
        _graph.connect(dot, link.to, link.input);
        commit("Add Reroute");
        return;
    }
    if (hit.kind == Hit::None || hit.kind == Hit::Backdrop)
        openSearch(event->position().toPoint(), 0, 0, -1);
}

void NodeGraphView::wheelEvent(QWheelEvent* event)
{
    if (!_hasGraph)
        return;
    _animation->stop();
    // A touchpad's two fingers pan; a wheel, or a pinch with Ctrl, zooms.
    if (!event->pixelDelta().isNull() && !(event->modifiers() & Qt::ControlModifier))
    {
        _pan += QPointF(event->pixelDelta());
        update();
        return;
    }
    const double steps = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    const double zoom = std::clamp(_zoom * std::pow(1.0016, steps), kMinZoom, kMaxZoom);
    const QPointF anchor = toGraph(event->position());
    _zoom = zoom;
    _pan = event->position() - anchor * _zoom;
    update();
}

void NodeGraphView::keyPressEvent(QKeyEvent* event)
{
    if (!_hasGraph)
    {
        QWidget::keyPressEvent(event);
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods == Qt::ControlModifier;
    switch (event->key())
    {
    case Qt::Key_Space:
        if (!event->isAutoRepeat())
        {
            _spaceHeld = true;
            setCursor(Qt::OpenHandCursor);
        }
        return;
    case Qt::Key_Tab:
        addNodeMenu();
        return;
    case Qt::Key_A:
        if (ctrl)
        {
            _selected.clear();
            for (const Node& node : _graph.nodes)
                _selected.insert(node.id);
            emit selectionChanged();
            update();
        }
        else if (mods == Qt::ShiftModifier)
            addNodeMenu();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return;
    case Qt::Key_D:
        if (ctrl)
            duplicate();
        else
            toggleBypass();
        return;
    case Qt::Key_C:
        if (ctrl)
            copy();
        return;
    case Qt::Key_X:
        if (ctrl)
        {
            copy();
            deleteSelection();
        }
        return;
    case Qt::Key_V:
        if (ctrl)
            paste();
        else if (const int id = currentNode())
            emit viewRequested(_viewed == id ? 0 : id);
        else if (_viewed)
            emit viewRequested(0);
        return;
    case Qt::Key_H:
        toggleCollapsed();
        return;
    case Qt::Key_F:
        frame();
        return;
    case Qt::Key_Home:
        frameAll();
        return;
    case Qt::Key_L:
        tidy();
        return;
    case Qt::Key_B:
        backdropSelection();
        return;
    case Qt::Key_Escape:
        if (_viewed)
            emit viewRequested(0);
        _selected.clear();
        _selectedLink = -1;
        emit selectionChanged();
        update();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void NodeGraphView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        _spaceHeld = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void NodeGraphView::contextMenuEvent(QContextMenuEvent* event)
{
    if (!_hasGraph)
        return;
    const Hit hit = hitAt(event->pos());
    QMenu menu(this);
    const Node* node = _graph.node(hit.node);
    if (node && (hit.kind == Hit::Node || hit.kind == Hit::Header || hit.kind == Hit::Eye))
    {
        if (!_selected.contains(node->id))
        {
            _selected = {node->id};
            emit selectionChanged();
            update();
        }
        if (hasOutput(*node) && !isDot(*node))
            menu.addAction(_viewed == node->id ? "Show the Output on the Stage" : "Show on the Stage", this,
                [this, id = node->id]() { emit viewRequested(_viewed == id ? 0 : id); })
                ->setShortcut(QKeySequence(Qt::Key_V));
        if (node->type() != "output" && !isDot(*node))
            menu.addAction(node->bypassed() ? "Turn On" : "Bypass", this, [this]() { toggleBypass(); })
                ->setShortcut(QKeySequence(Qt::Key_D));
        if (!isDot(*node))
        {
            menu.addAction(node->collapsed ? "Show Picture" : "Hide Picture", this, [this]() { toggleCollapsed(); })
                ->setShortcut(QKeySequence(Qt::Key_H));
            menu.addAction("Rename...", this, [this, id = node->id]() { startRename(id); });
        }
        menu.addSeparator();
        menu.addAction("Duplicate", this, [this]() { duplicate(); })->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        menu.addAction("Copy", this, [this]() { copy(); })->setShortcut(QKeySequence::Copy);
        menu.addAction("Put Behind a Backdrop", this, [this]() { backdropSelection(); })->setShortcut(QKeySequence(Qt::Key_B));
        menu.addAction("Unlink", this, [this, id = node->id]() {
            _graph.links.erase(std::remove_if(_graph.links.begin(), _graph.links.end(),
                                   [id](const fla::nodegraph::Link& l) { return l.from == id || l.to == id; }),
                _graph.links.end());
            commit("Unlink Nodes");
        });
        if (node->type() != "output")
            menu.addAction("Delete", this, [this]() { deleteSelection(); })->setShortcut(QKeySequence::Delete);
    }
    else if (hit.kind == Hit::Link)
    {
        const int index = hit.link;
        menu.addAction("Add Reroute Point", this, [this, index, at = toGraph(event->pos())]() {
            const fla::nodegraph::Link link = _graph.links[static_cast<size_t>(index)];
            addNode("dot", at);
            const int dot = currentNode();
            _graph.links.erase(_graph.links.begin() + index);
            _graph.connect(link.from, dot, 0);
            _graph.connect(dot, link.to, link.input);
            commit("Add Reroute");
        });
        menu.addAction("Delete Link", this, [this, index]() { deleteLink(index); });
    }
    else if (hit.kind == Hit::BackdropTitle || hit.kind == Hit::Backdrop || hit.kind == Hit::BackdropCorner)
    {
        const int id = hit.node;
        menu.addAction("Rename...", this, [this, id]() { startRename(id); });
        QMenu* colors = menu.addMenu("Color");
        static const std::pair<const char*, uint32_t> named[] = {{"Blue", 0x5B8CFF}, {"Green", 0x46C27A},
            {"Orange", 0xE5A548}, {"Purple", 0xC86ADB}, {"Red", 0xE5564B}, {"Teal", 0x3FB8B0}, {"Gray", 0x8A8F9C}};
        for (const auto& [name, color] : named)
        {
            QPixmap swatch(14, 14);
            swatch.fill(QColor::fromRgb(color));
            colors->addAction(QIcon(swatch), name, this, [this, id, color = color]() {
                if (fla::nodegraph::Backdrop* backdrop = _graph.backdrop(id))
                {
                    backdrop->color = color;
                    commit("Color Backdrop");
                }
            });
        }
        menu.addAction("Delete Backdrop", this, [this, id]() {
            _graph.remove(id);
            _selected.remove(id);
            commit("Delete Backdrop");
        });
    }
    else
    {
        // Every kind of node, by category, to add where the menu opened.
        const QPointF at = toGraph(event->pos());
        QMenu* add = menu.addMenu("Add Node");
        for (const std::string& category : fla::nodegraph::categories())
        {
            QMenu* sub = nullptr;
            for (const fla::nodegraph::Type& type : fla::nodegraph::types())
            {
                if (type.category != category || type.id == "output")
                    continue;
                if (!sub)
                {
                    sub = add->addMenu(QString::fromStdString(category));
                    QPixmap icon(16, 16);
                    icon.fill(Qt::transparent);
                    QPainter p(&icon);
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(Qt::NoPen);
                    p.setBrush(nodestyle::categoryColor(category));
                    p.drawRoundedRect(QRectF(1, 1, 14, 14), 3, 3);
                    nodestyle::drawIcon(p, QRectF(3, 3, 10, 10), category, Qt::white);
                    p.end();
                    sub->setIcon(QIcon(icon));
                }
                sub->addAction(QString::fromStdString(type.name), this, [this, id = type.id, at]() {
                    addNode(id, at);
                    commit("Add Node");
                })->setStatusTip(QString::fromStdString(type.description));
            }
        }
        menu.addAction("Search Nodes...", this, [this, pos = event->pos()]() { openSearch(pos, 0, 0, -1); })
            ->setShortcut(QKeySequence(Qt::Key_Tab));
        menu.addSeparator();
        QAction* pasteAction = menu.addAction("Paste", this, [this]() { paste(); });
        pasteAction->setShortcut(QKeySequence::Paste);
        pasteAction->setEnabled(QApplication::clipboard()->text().startsWith(kClipboardMark));
        menu.addAction("Add Backdrop", this, [this]() { backdropSelection(); })->setShortcut(QKeySequence(Qt::Key_B));
        menu.addSeparator();
        menu.addAction("Frame All", this, [this]() { frameAll(); })->setShortcut(QKeySequence(Qt::Key_Home));
        menu.addAction("Line Up Nodes", this, [this]() { tidy(); })->setShortcut(QKeySequence(Qt::Key_L));
    }
    menu.exec(event->globalPos());
}
