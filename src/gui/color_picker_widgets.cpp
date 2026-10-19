#include "color_picker_widgets.h"

#include <QConicalGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
const char* const kRecentKey = "colors/recent";

/// The barycentric weights of \a p in the triangle \a a, \a b, \a c.
void weights(const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c, double& wa, double& wb, double& wc)
{
    const double det = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
    if (std::fabs(det) < 1e-9)
    {
        wa = 1.0;
        wb = wc = 0.0;
        return;
    }
    wa = ((b.y() - c.y()) * (p.x() - c.x()) + (c.x() - b.x()) * (p.y() - c.y())) / det;
    wb = ((c.y() - a.y()) * (p.x() - c.x()) + (a.x() - c.x()) * (p.y() - c.y())) / det;
    wc = 1.0 - wa - wb;
}

/// A light and dark check, behind colors that are partly transparent.
void paintChecks(QPainter& painter, const QRectF& rect)
{
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, Qt::white);
    const int size = 4;
    for (int y = 0; y * size < rect.height(); ++y)
        for (int x = 0; x * size < rect.width(); ++x)
            if ((x + y) % 2)
                painter.fillRect(QRectF(rect.left() + x * size, rect.top() + y * size, size, size), QColor(204, 204, 204));
    painter.restore();
}

} // namespace

// ---------------------------------------------------------------------------
// RecentColors

RecentColors& RecentColors::instance()
{
    static RecentColors* shared = new RecentColors();
    return *shared;
}

RecentColors::RecentColors()
    : _history(fla::ColorHistory::fromText(QSettings().value(kRecentKey).toString().toStdString()))
{
}

QList<QColor> RecentColors::qcolors() const
{
    QList<QColor> colors;
    for (const fla::ColorHistory::Color& color : _history.colors())
        colors << QColor(color[0], color[1], color[2], color[3]);
    return colors;
}

void RecentColors::add(const QColor& color)
{
    if (!color.isValid())
        return;
    if (_history.add({static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
            static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())}))
    {
        save();
        emit changed();
    }
}

void RecentColors::add(const uint8_t rgba[4])
{
    add(QColor(rgba[0], rgba[1], rgba[2], rgba[3]));
}

void RecentColors::clear()
{
    _history.clear();
    save();
    emit changed();
}

void RecentColors::save()
{
    QSettings().setValue(kRecentKey, QString::fromStdString(_history.toText()));
}

// ---------------------------------------------------------------------------
// HsvWheel

HsvWheel::HsvWheel(QWidget* parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setCursor(Qt::CrossCursor);
    setToolTip("Drag around the ring for the hue, in the triangle for how strong and how light the color is");
}

void HsvWheel::setColor(const QColor& color)
{
    if (!color.isValid())
        return;
    const fla::Hsv hsv = fla::hsvOf(static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.blue()));
    // A gray, or black, keeps the hue (and a black the saturation) shown.
    if (hsv.s > 0.0 && hsv.v > 0.0)
        _hsv.h = hsv.h;
    if (hsv.v > 0.0)
        _hsv.s = hsv.s;
    _hsv.v = hsv.v;
    _alpha = color.alpha();
    update();
}

QColor HsvWheel::color() const
{
    const auto rgb = fla::rgbOf(_hsv, static_cast<uint8_t>(_alpha));
    return QColor(rgb[0], rgb[1], rgb[2], rgb[3]);
}

QPointF HsvWheel::center() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

double HsvWheel::outerRadius() const
{
    return std::max(10.0, std::min(width(), height()) / 2.0 - 2.0);
}

double HsvWheel::innerRadius() const
{
    return outerRadius() * 0.8;
}

void HsvWheel::corners(QPointF& hue, QPointF& white, QPointF& black) const
{
    const QPointF c = center();
    const double r = innerRadius() - 2.0;
    const auto at = [&c, r](double degrees) {
        const double angle = degrees * kPi / 180.0;
        return QPointF(c.x() + r * std::cos(angle), c.y() - r * std::sin(angle));
    };
    hue = at(_hsv.h);
    white = at(_hsv.h + 120.0);
    black = at(_hsv.h + 240.0);
}

void HsvWheel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF c = center();
    const double outer = outerRadius();
    const double inner = innerRadius();

    // The ring: the hues counterclockwise from red at the right.
    QConicalGradient hues(c, 0.0);
    for (int i = 0; i <= 6; ++i)
        hues.setColorAt(i / 6.0, QColor::fromHsvF(std::fmod(i / 6.0, 1.0), 1.0, 1.0));
    QPainterPath ring;
    ring.addEllipse(c, outer, outer);
    ring.addEllipse(c, inner, inner);
    painter.fillPath(ring, hues);
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    painter.drawEllipse(c, outer, outer);
    painter.drawEllipse(c, inner, inner);

    // The triangle, drawn again when the hue or the size changes.
    QPointF hue, white, black;
    corners(hue, white, black);
    const QSize size = this->size() * devicePixelRatioF();
    if (_triangle.size() != size || _triangleHue != _hsv.h)
    {
        _triangle = QImage(size, QImage::Format_ARGB32_Premultiplied);
        _triangle.fill(Qt::transparent);
        _triangleHue = _hsv.h;
        const double scale = devicePixelRatioF();
        const auto pure = fla::rgbOf({_hsv.h, 1.0, 1.0});
        for (int y = 0; y < size.height(); ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(_triangle.scanLine(y));
            for (int x = 0; x < size.width(); ++x)
            {
                double wa, wb, wc;
                weights(QPointF((x + 0.5) / scale, (y + 0.5) / scale), hue, white, black, wa, wb, wc);
                if (wa < -0.01 || wb < -0.01 || wc < -0.01)
                    continue;
                wa = std::max(0.0, wa);
                wb = std::max(0.0, wb);
                const auto channel = [&](uint8_t value) {
                    return static_cast<int>(std::clamp(std::lround(wa * value + wb * 255.0), 0L, 255L));
                };
                line[x] = qRgb(channel(pure[0]), channel(pure[1]), channel(pure[2]));
            }
        }
        _triangle.setDevicePixelRatio(scale);
    }
    painter.drawImage(QPointF(0, 0), _triangle);

    // The markers: the hue on the ring, the color in the triangle.
    const double angle = _hsv.h * kPi / 180.0;
    const double middle = (outer + inner) / 2.0;
    const QPointF onRing(c.x() + middle * std::cos(angle), c.y() - middle * std::sin(angle));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 2.0));
    painter.drawEllipse(onRing, (outer - inner) / 2.0, (outer - inner) / 2.0);
    painter.setPen(QPen(Qt::white, 1.0));
    painter.drawEllipse(onRing, (outer - inner) / 2.0 - 1.5, (outer - inner) / 2.0 - 1.5);
    const double a = _hsv.s * _hsv.v;
    const double b = _hsv.v * (1.0 - _hsv.s);
    const double k = 1.0 - _hsv.v;
    const QPointF inTriangle = hue * a + white * b + black * k;
    painter.setPen(QPen(_hsv.v > 0.6 && _hsv.s < 0.5 ? Qt::black : Qt::white, 1.5));
    painter.drawEllipse(inTriangle, 4.5, 4.5);
}

void HsvWheel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF p = event->position();
    const double distance = std::hypot(p.x() - center().x(), p.y() - center().y());
    _dragging = distance >= innerRadius() - 1.0 ? Part::Ring : Part::Triangle;
    pick(p, false);
}

void HsvWheel::mouseMoveEvent(QMouseEvent* event)
{
    if (_dragging != Part::None)
        pick(event->position(), false);
}

void HsvWheel::mouseReleaseEvent(QMouseEvent* event)
{
    if (_dragging == Part::None)
        return;
    pick(event->position(), true);
    _dragging = Part::None;
}

void HsvWheel::pick(const QPointF& point, bool done)
{
    const QPointF c = center();
    if (_dragging == Part::Ring)
    {
        double degrees = std::atan2(c.y() - point.y(), point.x() - c.x()) * 180.0 / kPi;
        if (degrees < 0.0)
            degrees += 360.0;
        _hsv.h = degrees >= 360.0 ? 0.0 : degrees;
        // A gray picked up by the ring takes on the hue it is turned to.
        if (_hsv.s <= 0.0)
            _hsv.s = 1.0;
        if (_hsv.v <= 0.0)
            _hsv.v = 1.0;
    }
    else if (_dragging == Part::Triangle)
    {
        QPointF hue, white, black;
        corners(hue, white, black);
        double wa, wb, wc;
        weights(point, hue, white, black, wa, wb, wc);
        // Outside, the nearest weights that are not.
        wa = std::max(0.0, wa);
        wb = std::max(0.0, wb);
        wc = std::max(0.0, wc);
        const double sum = wa + wb + wc;
        if (sum > 0.0)
        {
            wa /= sum;
            wb /= sum;
        }
        _hsv.v = std::clamp(wa + wb, 0.0, 1.0);
        _hsv.s = _hsv.v > 0.0 ? std::clamp(wa / _hsv.v, 0.0, 1.0) : _hsv.s;
    }
    update();
    emit colorChanged(color(), done);
}

// ---------------------------------------------------------------------------
// RecentColorsBar

namespace {
constexpr int kSwatch = 18;
constexpr int kGap = 3;
} // namespace

RecentColorsBar::RecentColorsBar(QWidget* parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMouseTracking(true);
    setToolTip("Colors drawn with lately: click one to use it");
    connect(&RecentColors::instance(), &RecentColors::changed, this, [this]() {
        updateGeometry();
        update();
    });
}

void RecentColorsBar::setColumns(int columns)
{
    _columns = columns;
    updateGeometry();
}

int RecentColorsBar::columnsFor(int width) const
{
    if (_columns > 0)
        return _columns;
    return std::max(1, (width + kGap) / (kSwatch + kGap));
}

QSize RecentColorsBar::sizeHint() const
{
    const int columns = _columns > 0 ? _columns : 8;
    return QSize(columns * (kSwatch + kGap) - kGap, heightForWidth(columns * (kSwatch + kGap) - kGap));
}

int RecentColorsBar::heightForWidth(int width) const
{
    const int count = std::max(1, static_cast<int>(RecentColors::instance().colors().size()));
    const int columns = columnsFor(width);
    const int rows = (count + columns - 1) / columns;
    return rows * (kSwatch + kGap) - kGap;
}

QRect RecentColorsBar::swatchRect(int index, int width) const
{
    const int columns = columnsFor(width);
    return QRect((index % columns) * (kSwatch + kGap), (index / columns) * (kSwatch + kGap), kSwatch, kSwatch);
}

int RecentColorsBar::swatchAt(const QPoint& point) const
{
    const int count = static_cast<int>(RecentColors::instance().colors().size());
    for (int i = 0; i < count; ++i)
        if (swatchRect(i, width()).contains(point))
            return i;
    return -1;
}

void RecentColorsBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QList<QColor> colors = RecentColors::instance().qcolors();
    if (colors.isEmpty())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, "Colors you draw with show here");
        return;
    }
    for (int i = 0; i < colors.size(); ++i)
    {
        const QRect box = swatchRect(i, width());
        if (colors[i].alpha() < 255)
            paintChecks(painter, box);
        painter.fillRect(box, colors[i]);
        painter.setPen(i == _hover ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid));
        painter.drawRect(box.adjusted(0, 0, -1, -1));
    }
}

void RecentColorsBar::mousePressEvent(QMouseEvent* event)
{
    const int index = swatchAt(event->position().toPoint());
    const QList<QColor> colors = RecentColors::instance().qcolors();
    if (event->button() == Qt::LeftButton && index >= 0 && index < colors.size())
        emit colorPicked(colors[index]);
}

void RecentColorsBar::mouseMoveEvent(QMouseEvent* event)
{
    const int index = swatchAt(event->position().toPoint());
    if (index != _hover)
    {
        _hover = index;
        const QList<QColor> colors = RecentColors::instance().qcolors();
        setToolTip(index >= 0 && index < colors.size() ? colors[index].name(QColor::HexRgb).toUpper()
                                                       : QString("Colors drawn with lately: click one to use it"));
        update();
    }
}

void RecentColorsBar::leaveEvent(QEvent*)
{
    _hover = -1;
    update();
}
