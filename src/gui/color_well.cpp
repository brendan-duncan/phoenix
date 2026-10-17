#include "color_well.h"

#include "theme.h"
#include "toolbox_flow.h"

#include <QGradient>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <functional>

namespace {

QRect rectOf(const toolboxflow::Box& box)
{
    return QRect(box.x, box.y, box.width, box.height);
}

/// Whether \a brush lets what is under it show through.
bool seeThrough(const QBrush& brush)
{
    if (const QGradient* gradient = brush.gradient())
    {
        for (const QGradientStop& stop : gradient->stops())
            if (stop.second.alpha() < 255)
                return true;
        return false;
    }
    return brush.color().alpha() < 255;
}

/// A gray checker over \a area, so a see-through color reads as one.
void drawChecker(QPainter& painter, const QRect& area)
{
    painter.fillRect(area, QColor(120, 120, 120));
    const int step = 4;
    for (int y = area.top(); y <= area.bottom(); y += step)
        for (int x = area.left() + ((y - area.top()) / step % 2) * step; x <= area.right(); x += 2 * step)
            painter.fillRect(QRect(x, y, step, step).intersected(area), QColor(160, 160, 160));
}

/// The red line across a swatch that has no color.
void drawNone(QPainter& painter, const QRect& area)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(area);
    painter.setPen(QPen(QColor(220, 30, 30), 2.0));
    painter.drawLine(QPointF(area.left() + 1, area.bottom()), QPointF(area.right(), area.top() + 1));
    painter.restore();
}

/// A small button's glyph, drawn on a 16-unit square in \a draw, kept
/// sharp at any scale.
QIcon smallIcon(const std::function<void(QPainter&)>& draw)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(4.0, 4.0);
    draw(painter);
    painter.end();
    pixmap.setDevicePixelRatio(4.0);
    return QIcon(pixmap);
}

} // namespace

ColorSwatch::ColorSwatch(bool stroke, QWidget* parent)
    : QAbstractButton(parent)
    , _stroke(stroke)
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    const QString name = stroke ? "Stroke color" : "Fill color";
    setToolTip(name);
    setAccessibleName(name);
    setAccessibleDescription("Click to choose a color");
    connect(theme::notifier(), &theme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

void ColorSwatch::setBrush(const QBrush& brush)
{
    _brush = brush;
    update();
}

void ColorSwatch::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // Square edges on whole pixels, so the outlines stay sharp at any
    // scale.
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRect outer = rect();
    // An outline that stands out from the panel (light on a dark theme,
    // dark on a light one), so the swatch is found even when its color is
    // the panel's own; the accent under the mouse or with the focus.
    const QColor outline = underMouse() || hasFocus() ? theme::color("accent") : theme::color("icon");
    const bool none = _brush.style() == Qt::NoBrush;
    QRect hollow;
    if (_stroke)
    {
        // A ring: a thick band of the color around a hollow middle.
        const int band = std::max(3, (outer.width() + 2) / 4);
        hollow = outer.adjusted(band, band, -band, -band);
        QPainterPath ring;
        ring.setFillRule(Qt::OddEvenFill);
        ring.addRect(outer);
        ring.addRect(hollow);
        painter.setClipPath(ring);
    }
    if (none)
        painter.fillRect(outer, theme::color("base"));
    else
    {
        if (seeThrough(_brush))
            drawChecker(painter, outer);
        painter.fillRect(outer, _brush);
    }
    painter.setClipping(false);
    if (_stroke)
        painter.fillRect(hollow, theme::color("window").darker(theme::isDark() ? 130 : 112));
    if (none)
        drawNone(painter, outer);
    if (isDown())
        painter.fillRect(outer, QColor(0, 0, 0, 60));
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(outer.adjusted(0, 0, -1, -1));
    if (_stroke)
        painter.drawRect(hollow.adjusted(-1, -1, 0, 0));
}

ColorWell::ColorWell(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Stroke and fill colors");
    // The fill starts on top; whichever is clicked comes forward.
    _fillSwatch = new ColorSwatch(false, this);
    _strokeSwatch = new ColorSwatch(true, this);
    connect(_strokeSwatch, &QAbstractButton::clicked, this, [this]() {
        setStrokeSelected(true);
        emit strokeClicked();
    });
    connect(_fillSwatch, &QAbstractButton::clicked, this, [this]() {
        setStrokeSelected(false);
        emit fillClicked();
    });

    const auto smallButton = [this](const QString& tip, const std::function<void(QPainter&)>& draw) {
        QToolButton* button = new QToolButton(this);
        theme::themedIcon(button, [draw]() { return smallIcon(draw); });
        button->setAutoRaise(true);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        return button;
    };
    _blackWhite = smallButton("Black and white", [](QPainter& p) {
        p.setPen(QPen(theme::color("icon"), 0.8));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(5.5, 5.5, 9, 9));
        p.setBrush(Qt::black);
        p.drawRect(QRectF(1.5, 1.5, 9, 9));
    });
    connect(_blackWhite, &QToolButton::clicked, this, &ColorWell::blackWhiteClicked);
    _swap = smallButton("Swap stroke and fill colors", [](QPainter& p) {
        p.setPen(QPen(theme::color("icon"), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(2.5, 2.5, 11, 11), 0, 90 * 16);
        p.setBrush(theme::color("icon"));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(8, 0.5), QPointF(8, 5.5), QPointF(5, 3)}));
        p.drawPolygon(QPolygonF({QPointF(15.5, 8), QPointF(10.5, 8), QPointF(13, 11)}));
    });
    connect(_swap, &QToolButton::clicked, this, &ColorWell::swapClicked);
    arrange();
}

void ColorWell::setStroke(const QBrush& brush)
{
    _strokeSwatch->setBrush(brush);
}

void ColorWell::setFill(const QBrush& brush)
{
    _fillSwatch->setBrush(brush);
}

void ColorWell::setStrokeSelected(bool stroke)
{
    _strokeSelected = stroke;
    (stroke ? _strokeSwatch : _fillSwatch)->raise();
    // The buttons sit clear of the swatches, but stay over them anyway.
    _blackWhite->raise();
    _swap->raise();
}

int ColorWell::lengthFor(int breadth, Qt::Orientation flow)
{
    return toolboxflow::colorWellLength(breadth, flow == Qt::Vertical);
}

QSize ColorWell::sizeHint() const
{
    return QSize(30, lengthFor(30, Qt::Vertical));
}

QSize ColorWell::minimumSizeHint() const
{
    return QSize(24, 24);
}

int ColorWell::heightForWidth(int width) const
{
    return lengthFor(width, Qt::Vertical);
}

void ColorWell::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    arrange();
}

void ColorWell::arrange()
{
    const toolboxflow::ColorWellParts parts = toolboxflow::colorWellParts(width(), height());
    _fillSwatch->setGeometry(rectOf(parts.fill));
    _strokeSwatch->setGeometry(rectOf(parts.stroke));
    _blackWhite->setGeometry(rectOf(parts.blackWhite));
    _swap->setGeometry(rectOf(parts.swap));
    const int icon = std::max(8, parts.swap.width - 4);
    _blackWhite->setIconSize(QSize(icon, icon));
    _swap->setIconSize(QSize(icon, icon));
    setStrokeSelected(_strokeSelected);
}
