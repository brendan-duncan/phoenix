#include "popup_palette.h"

#include "color_picker_widgets.h"
#include "modal.h"

#include <QAction>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
/// The panel's size, and the radii of its rings: the tools around the
/// edge, the recent colors inside them, the wheel in the middle.
constexpr int kSize = 300;
constexpr double kToolRing = 122.0;
constexpr double kRecentRing = 92.0;
constexpr double kRecentDot = 8.5;
constexpr double kWheel = 74.0;
constexpr int kMostRecent = 16;

} // namespace

PopupPalette::PopupPalette(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setObjectName("popupPalette");
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kSize, kSize);
    setMouseTracking(true);
    _wheel = new HsvWheel(this);
    _wheel->setObjectName("popupPaletteWheel");
    _wheel->setGeometry(QRect(0, 0, static_cast<int>(kWheel * 2), static_cast<int>(kWheel * 2)));
    _wheel->move(static_cast<int>(kSize / 2 - kWheel), static_cast<int>(kSize / 2 - kWheel));
    connect(_wheel, &HsvWheel::colorChanged, this, [this](const QColor& color, bool done) {
        (_stroke ? _strokeColor : _fill) = color;
        update();
        emit colorPicked(color, _stroke, done);
    });
}

QStringList PopupPalette::defaultFavorites()
{
    return {"Brush", "Pencil", "Eraser", "Paint Bucket", "Eyedropper", "Selection", "Line", "Rectangle", "Oval"};
}

void PopupPalette::setTools(const QList<QAction*>& tools, const QStringList& favorites)
{
    _tools = tools;
    _favorites.clear();
    for (const QString& name : favorites)
        for (QAction* tool : tools)
            if (tool && tool->text() == name && !_favorites.contains(name) && _favorites.size() < mostFavorites())
                _favorites << name;
    arrange();
}

void PopupPalette::setColors(const QColor& fill, const QColor& stroke)
{
    _fill = fill;
    _strokeColor = stroke;
    _wheel->setColor(_stroke ? _strokeColor : _fill);
    update();
}

void PopupPalette::setEditsStroke(bool stroke)
{
    _stroke = stroke;
    _wheel->setColor(_stroke ? _strokeColor : _fill);
    update();
}

void PopupPalette::arrange()
{
    qDeleteAll(_buttons);
    _buttons.clear();
    const int count = static_cast<int>(_favorites.size());
    for (int i = 0; i < count; ++i)
    {
        QAction* action = nullptr;
        for (QAction* tool : _tools)
            if (tool && tool->text() == _favorites[i])
                action = tool;
        if (!action)
            continue;
        QToolButton* button = new QToolButton(this);
        button->setObjectName("popupTool" + QString(_favorites[i]).remove(' '));
        button->setIcon(action->icon());
        button->setIconSize(QSize(24, 24));
        button->setFixedSize(36, 36);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setChecked(action->isChecked());
        button->setToolTip(action->toolTip());
        connect(button, &QToolButton::clicked, this, [this, action]() {
            close();
            action->trigger();
        });
        // Around the edge from the top, clockwise.
        const double angle = -kPi / 2.0 + 2.0 * kPi * i / std::max(count, 1);
        const QPointF at = middle() + QPointF(kToolRing * std::cos(angle), kToolRing * std::sin(angle));
        button->move(static_cast<int>(at.x() - 18), static_cast<int>(at.y() - 18));
        button->show();
        _buttons << button;
    }
}

void PopupPalette::popupAt(const QPoint& globalPos)
{
    for (QToolButton* button : _buttons)
        for (QAction* tool : _tools)
            if (tool && button->objectName() == "popupTool" + QString(tool->text()).remove(' '))
                button->setChecked(tool->isChecked());
    QRect frame(globalPos - QPoint(kSize / 2, kSize / 2), QSize(kSize, kSize));
    if (const QScreen* screen = QGuiApplication::screenAt(globalPos))
    {
        const QRect room = screen->availableGeometry();
        frame.moveLeft(std::clamp(frame.left(), room.left(), std::max(room.left(), room.right() - kSize)));
        frame.moveTop(std::clamp(frame.top(), room.top(), std::max(room.top(), room.bottom() - kSize)));
    }
    move(frame.topLeft());
    show();
    raise();
    activateWindow();
    setFocus();
}

QPointF PopupPalette::middle() const
{
    return QPointF(kSize / 2.0, kSize / 2.0);
}

QRectF PopupPalette::recentRect(int index) const
{
    const double angle = -kPi / 2.0 + 2.0 * kPi * index / kMostRecent;
    const QPointF at = middle() + QPointF(kRecentRing * std::cos(angle), kRecentRing * std::sin(angle));
    return QRectF(at.x() - kRecentDot, at.y() - kRecentDot, kRecentDot * 2, kRecentDot * 2);
}

QRectF PopupPalette::fillChip() const
{
    return QRectF(4, kSize - 30, 58, 26);
}

QRectF PopupPalette::strokeChip() const
{
    return QRectF(kSize - 62, kSize - 30, 58, 26);
}

QRectF PopupPalette::editChip() const
{
    return QRectF(kSize - 50, 4, 46, 22);
}

int PopupPalette::recentAt(const QPointF& point) const
{
    const int count = std::min(kMostRecent, static_cast<int>(RecentColors::instance().colors().size()));
    for (int i = 0; i < count; ++i)
        if (recentRect(i).adjusted(-2, -2, 2, 2).contains(point))
            return i;
    return -1;
}

void PopupPalette::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor back = palette().color(QPalette::Window);
    back.setAlpha(245);
    const QColor edge = palette().color(QPalette::Mid);
    painter.setPen(QPen(edge, 1.0));
    painter.setBrush(back);
    painter.drawEllipse(middle(), kSize / 2.0 - 1.0, kSize / 2.0 - 1.0);

    // The recent colors, round dots around the wheel.
    const QList<QColor> recent = RecentColors::instance().qcolors();
    for (int i = 0; i < std::min(kMostRecent, static_cast<int>(recent.size())); ++i)
    {
        const QRectF dot = recentRect(i);
        painter.setPen(QPen(i == _hoverRecent ? palette().color(QPalette::Highlight) : edge, i == _hoverRecent ? 2.0 : 1.0));
        painter.setBrush(recent[i]);
        painter.drawEllipse(dot);
    }

    // The fill and stroke chips, the one the wheel sets marked.
    const auto chip = [&](const QRectF& rect, const QString& label, const QColor& color, bool on, bool ring) {
        painter.setPen(QPen(on ? palette().color(QPalette::Highlight) : edge, on ? 2.0 : 1.0));
        painter.setBrush(back);
        painter.drawRoundedRect(rect, 6, 6);
        const QRectF swatch(rect.left() + 5, rect.top() + 5, rect.height() - 10, rect.height() - 10);
        if (ring)
        {
            painter.setPen(QPen(color, 4.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(swatch.adjusted(2, 2, -2, -2));
        }
        else
        {
            painter.setPen(QPen(edge, 1.0));
            painter.setBrush(color);
            painter.drawRect(swatch);
        }
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect.adjusted(rect.height() - 2, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    };
    chip(fillChip(), "Fill", _fill, !_stroke, false);
    chip(strokeChip(), "Stroke", _strokeColor, _stroke, true);
    painter.setPen(QPen(edge, 1.0));
    painter.setBrush(back);
    painter.drawRoundedRect(editChip(), 6, 6);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(editChip(), Qt::AlignCenter, "Edit");
}

void PopupPalette::mousePressEvent(QMouseEvent* event)
{
    const QPointF p = event->position();
    // Outside the disc and its chips is outside the palette.
    const bool onDisc = std::hypot(p.x() - middle().x(), p.y() - middle().y()) <= kSize / 2.0;
    if (fillChip().contains(p) || strokeChip().contains(p))
    {
        setEditsStroke(strokeChip().contains(p));
        return;
    }
    if (editChip().contains(p))
    {
        editFavorites();
        return;
    }
    const int index = recentAt(p);
    if (index >= 0)
    {
        const QList<QColor> recent = RecentColors::instance().qcolors();
        if (index < recent.size())
        {
            const QColor color = recent[index];
            const bool stroke = _stroke;
            close();
            emit colorPicked(color, stroke, true);
        }
        return;
    }
    if (!onDisc || !rect().contains(p.toPoint()))
        close();
}

void PopupPalette::mouseMoveEvent(QMouseEvent* event)
{
    const int index = recentAt(event->position());
    if (index != _hoverRecent)
    {
        _hoverRecent = index;
        const QList<QColor> recent = RecentColors::instance().qcolors();
        setToolTip(index >= 0 && index < recent.size() ? recent[index].name().toUpper() : QString());
        update();
    }
}

void PopupPalette::keyPressEvent(QKeyEvent* event)
{
    // Esc, or the key that opened it, closes it.
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_QuoteLeft)
    {
        close();
        return;
    }
    if (event->key() == Qt::Key_X)
    {
        setEditsStroke(!_stroke);
        return;
    }
    QWidget::keyPressEvent(event);
}

void PopupPalette::editFavorites()
{
    // Every tool, the favorites ticked; as many as fit around the edge.
    QMenu* menu = new QMenu(this);
    menu->setObjectName("popupPaletteFavorites");
    for (QAction* tool : _tools)
    {
        if (!tool)
            continue;
        QAction* item = menu->addAction(tool->icon(), tool->text());
        item->setCheckable(true);
        item->setChecked(_favorites.contains(tool->text()));
        item->setEnabled(item->isChecked() || _favorites.size() < mostFavorites());
    }
    menu->addSeparator();
    QAction* reset = menu->addAction("Reset Favorites");
    modal::popup(menu, mapToGlobal(editChip().bottomLeft().toPoint()), [this, reset](QAction* chosen) {
        if (!chosen)
            return;
        QStringList favorites = _favorites;
        if (chosen == reset)
            favorites = defaultFavorites();
        else if (chosen->isChecked())
            favorites << chosen->text();
        else
            favorites.removeAll(chosen->text());
        setTools(_tools, favorites);
        emit favoritesChanged(_favorites);
    });
}
