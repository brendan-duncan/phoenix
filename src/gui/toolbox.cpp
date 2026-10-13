#include "toolbox.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFrame>
#include <QJsonArray>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

#include <algorithm>

QJsonObject Toolbox::Layout::toJson() const
{
    QJsonArray list;
    for (const Slot& slot : entries)
        list.append(QJsonArray::fromStringList(slot));
    QJsonObject json;
    json["version"] = 1;
    json["columns"] = columns;
    json["slots"] = list;
    json["hidden"] = QJsonArray::fromStringList(hidden);
    return json;
}

namespace {

/// A tool's id as a layout saved before a rename gives it.
QString currentToolId(const QString& id)
{
    if (id == "Flow Brush")
        return "Brush";
    return id;
}

} // namespace

Toolbox::Layout Toolbox::Layout::fromJson(const QJsonObject& json)
{
    Layout layout;
    layout.columns = std::clamp(json["columns"].toInt(1), 1, 8);
    for (const QJsonValue& value : json["slots"].toArray())
    {
        Slot slot;
        for (const QJsonValue& id : value.toArray())
            if (id.isString())
                slot << currentToolId(id.toString());
        if (!slot.isEmpty())
            layout.entries.push_back(slot);
    }
    for (const QJsonValue& id : json["hidden"].toArray())
        if (id.isString())
            layout.hidden << currentToolId(id.toString());
    return layout;
}

Toolbox::Toolbox(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void Toolbox::addTool(const QString& id, QAction* action)
{
    if (!_tools.contains(id))
        _order << id;
    _tools[id] = action;
}

Toolbox::Layout Toolbox::completed(const Layout& layout) const
{
    Layout out;
    out.columns = std::clamp(layout.columns, 1, 8);
    QStringList placed;
    for (const Slot& slot : layout.entries)
    {
        if (slot == Slot{"|"})
        {
            // No line first, and never two in a row.
            if (!out.entries.empty() && out.entries.back() != Slot{"|"})
                out.entries.push_back(slot);
            continue;
        }
        Slot kept;
        for (const QString& id : slot)
            if (_tools.contains(id) && !placed.contains(id))
            {
                kept << id;
                placed << id;
            }
        if (!kept.isEmpty())
            out.entries.push_back(kept);
    }
    if (!out.entries.empty() && out.entries.back() == Slot{"|"})
        out.entries.pop_back();
    for (const QString& id : layout.hidden)
        if (_tools.contains(id) && !placed.contains(id))
        {
            out.hidden << id;
            placed << id;
        }
    // A tool the layout does not know of joins the slot the default gives
    // it, or comes last in one of its own.
    for (const QString& id : _order)
    {
        if (placed.contains(id))
            continue;
        bool joined = false;
        for (const Slot& home : _default.entries)
        {
            if (!home.contains(id))
                continue;
            for (Slot& slot : out.entries)
                for (const QString& sibling : home)
                    if (!joined && sibling != id && slot.contains(sibling))
                    {
                        slot << id;
                        joined = true;
                    }
        }
        if (!joined)
            out.entries.push_back(Slot{id});
        placed << id;
    }
    return out;
}

void Toolbox::setLayout(const Layout& layout)
{
    // An empty layout is the default, in the columns asked for.
    Layout base = layout;
    if (base.entries.empty() && base.hidden.isEmpty())
    {
        base = _default;
        base.columns = layout.columns;
    }
    _layout = completed(base);
    rebuild();
    emit layoutChanged();
}

void Toolbox::setExtras(const QList<QWidget*>& extras)
{
    for (QWidget* extra : _extras)
        if (extra)
            extra->hide();
    _extras = extras;
    for (QWidget* extra : _extras)
        if (extra)
            extra->setParent(this);
    rebuild();
}

void Toolbox::setOrientation(Qt::Orientation orientation)
{
    if (orientation == _orientation)
        return;
    _orientation = orientation;
    for (const Item& item : _items)
        if (item.line)
            item.line->setFrameShape(_orientation == Qt::Vertical ? QFrame::HLine : QFrame::VLine);
    updateGeometry();
    place(0, true);
}

void Toolbox::setFloating(bool floating)
{
    _floating = floating;
    if (!floating)
        return;
    setOrientation(width() > height() * 2 ? Qt::Horizontal : Qt::Vertical);
}

QFrame* Toolbox::makeLine()
{
    QFrame* line = new QFrame(this);
    line->setFrameShape(_orientation == Qt::Vertical ? QFrame::HLine : QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setEnabled(false);
    line->show();
    return line;
}

int Toolbox::cell() const
{
    // Every item sits in a square cell the size of the largest.
    int size = 24;
    for (const Item& item : _items)
        if (item.widget)
            size = std::max({size, item.widget->sizeHint().width(), item.widget->sizeHint().height()});
    return size;
}

namespace {

constexpr int kMargin = 3;
/// The room a line between sections takes along the flow.
constexpr int kLineRoom = 7;

} // namespace

int Toolbox::place(int across, bool move)
{
    // Down a side, items fill rows as wide as the panel; along the top or
    // bottom, columns as tall as it. A line ends the row or column and
    // runs across the panel.
    const bool vertical = _orientation == Qt::Vertical;
    const int c = cell();
    const int room = (vertical ? width() : height()) - 2 * kMargin;
    if (across <= 0)
        across = std::max(1, room / c);
    // The cells sit in the middle of the panel's breadth.
    const int start = kMargin + std::max(0, (room - across * c) / 2);
    int along = kMargin;
    int index = 0;
    for (const Item& item : _items)
    {
        if (item.line)
        {
            if (index > 0)
            {
                along += c;
                index = 0;
            }
            if (move)
            {
                const int breadth = std::max(c, across * c);
                if (vertical)
                    item.line->setGeometry(start, along + kLineRoom / 2, breadth, 1);
                else
                    item.line->setGeometry(along + kLineRoom / 2, start, 1, breadth);
            }
            along += kLineRoom;
            continue;
        }
        if (move)
        {
            const QSize size = item.widget->sizeHint().boundedTo(QSize(c, c));
            const int x = vertical ? start + index * c : along;
            const int y = vertical ? along : start + index * c;
            item.widget->setGeometry(x + (c - size.width()) / 2, y + (c - size.height()) / 2, size.width(),
                size.height());
        }
        if (++index == across)
        {
            index = 0;
            along += c;
        }
    }
    if (index > 0)
        along += c;
    return along + kMargin;
}

int Toolbox::widthFor(int columns) const
{
    return std::max(columns, 1) * cell() + 2 * kMargin;
}

QSize Toolbox::sizeHint() const
{
    Toolbox* self = const_cast<Toolbox*>(this);
    if (_orientation == Qt::Vertical)
        return QSize(widthFor(_layout.columns), self->place(_layout.columns, false));
    return QSize(self->place(1, false), cell() + 2 * kMargin);
}

QSize Toolbox::minimumSizeHint() const
{
    // One cell across; along the flow, it can be cut short.
    const int c = cell();
    if (_orientation == Qt::Vertical)
        return QSize(c + 2 * kMargin, 3 * c);
    return QSize(3 * c, c + 2 * kMargin);
}

void Toolbox::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_floating)
        setOrientation(width() > height() * 2 ? Qt::Horizontal : Qt::Vertical);
    place(0, true);
    // The columns it was given down a side are kept, for next time and in
    // workspaces.
    if (_orientation == Qt::Vertical && !_floating && isVisible())
    {
        const int columns = std::clamp((width() - 2 * kMargin) / cell(), 1, 8);
        if (columns != _layout.columns)
        {
            _layout.columns = columns;
            emit layoutChanged();
        }
    }
}

void Toolbox::rebuild()
{
    // The buttons and lines are made again; the actions stay, so keys and
    // checked states carry on. The extras are kept.
    for (QWidget* button : _buttons)
        delete button;
    _buttons.clear();
    for (const Item& item : _items)
        delete item.line;
    _items.clear();

    for (const Slot& slot : _layout.entries)
    {
        if (slot == Slot{"|"})
        {
            _items.push_back({nullptr, makeLine()});
            continue;
        }
        QToolButton* button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setIconSize(QSize(22, 22));
        QAction* first = _tools.value(slot.front());
        // The slot shows whichever of its tools is in use, or the first.
        for (const QString& id : slot)
            if (_tools.value(id)->isChecked())
                first = _tools.value(id);
        button->setDefaultAction(first);
        if (slot.size() > 1)
        {
            button->setPopupMode(QToolButton::DelayedPopup);
            QMenu* flyout = new QMenu(button);
            for (const QString& id : slot)
            {
                QAction* action = _tools.value(id);
                flyout->addAction(action);
                connect(action, &QAction::triggered, button, [button, action]() { button->setDefaultAction(action); });
            }
            button->setMenu(flyout);
        }
        button->show();
        _buttons << button;
        _items.push_back({button, nullptr});
    }
    if (!_extras.isEmpty())
    {
        _items.push_back({nullptr, makeLine()});
        for (QWidget* extra : _extras)
        {
            if (extra)
            {
                extra->show();
                _items.push_back({extra, nullptr});
            }
            else
            {
                _items.push_back({nullptr, makeLine()});
            }
        }
    }
    updateGeometry();
    place(0, true);
}

void Toolbox::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* edit = menu.addAction("Edit Toolbar...");
    if (menu.exec(event->globalPos()) == edit)
        emit editRequested();
}

DockGrip::DockGrip(QWidget* parent)
    : QWidget(parent)
{
    setToolTip("Drag to move the panel; double-click to float or dock it");
    setCursor(Qt::SizeAllCursor);
}

void DockGrip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    QColor dot = palette().color(QPalette::WindowText);
    dot.setAlpha(110);
    painter.setBrush(dot);
    const bool wide = width() >= height();
    const int length = wide ? width() : height();
    const int span = std::min(length - 8, 28);
    const double middle = (wide ? height() : width()) / 2.0;
    for (int at = (length - span) / 2; at <= (length + span) / 2; at += 4)
        painter.drawEllipse(wide ? QPointF(at, middle) : QPointF(middle, at), 1.0, 1.0);
}
