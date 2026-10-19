#include "toolbox.h"

#include "color_well.h"
#include "modal.h"
#include "toolbox_flow.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFrame>
#include <QJsonArray>
#include <QMenu>
#include <QPainter>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

QToolButton* Toolbox::buttonFor(const QString& id) const
{
    QAction* action = _tools.value(id);
    if (!action)
        return nullptr;
    for (const Item& item : _items)
    {
        QToolButton* button = qobject_cast<QToolButton*>(item.widget);
        if (!button)
            continue;
        if (button->defaultAction() == action || (button->menu() && button->menu()->actions().contains(action)))
            return button;
    }
    return nullptr;
}

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
    // Whether it needs more rows or columns waits for the size the dock
    // gives it there.
    _forced = 0;
    updateGeometry();
    place(0, true);
}

void Toolbox::setFloating(bool floating)
{
    _floating = floating;
    if (!floating)
        return;
    // Floating, it is the shape it is left, with nothing taken to fit.
    updateForced();
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
    // Every item sits in a square cell the size of the largest; the color
    // well spans the panel instead.
    int size = 24;
    for (const Item& item : _items)
        if (item.widget && !qobject_cast<ColorWell*>(item.widget))
            size = std::max({size, item.widget->sizeHint().width(), item.widget->sizeHint().height()});
    return size;
}

using toolboxflow::kMargin;

std::vector<toolboxflow::Kind> Toolbox::kinds() const
{
    std::vector<toolboxflow::Kind> kinds;
    for (const Item& item : _items)
        kinds.push_back(item.line ? toolboxflow::Kind::Line
                : qobject_cast<ColorWell*>(item.widget) ? toolboxflow::Kind::Span
                                                       : toolboxflow::Kind::Cell);
    return kinds;
}

int Toolbox::place(int across, bool move)
{
    // Down a side, items fill rows as wide as the panel; along the top or
    // bottom, columns as tall as it. A line ends the row or column and
    // runs across the panel, and so does the color well.
    const bool vertical = _orientation == Qt::Vertical;
    const int c = cell();
    const int room = (vertical ? width() : height()) - 2 * kMargin;
    if (across <= 0)
        across = std::max(1, room / c);
    // The cells sit in the middle of the panel's breadth.
    const int start = kMargin + std::max(0, (room - across * c) / 2);
    const Qt::Orientation flow = _orientation;
    std::vector<toolboxflow::Box> boxes;
    const int length = toolboxflow::place(kinds(), across, c, start, vertical,
        [flow](int breadth) { return ColorWell::lengthFor(breadth, flow); }, move ? &boxes : nullptr);
    if (!move)
        return length;
    for (size_t i = 0; i < _items.size() && i < boxes.size(); ++i)
    {
        const Item& item = _items[i];
        const toolboxflow::Box& box = boxes[i];
        if (item.line)
        {
            item.line->setGeometry(box.x, box.y, box.width, box.height);
            continue;
        }
        if (qobject_cast<ColorWell*>(item.widget))
        {
            item.widget->setGeometry(box.x, box.y, box.width, box.height);
            continue;
        }
        const QSize size = item.widget->sizeHint().boundedTo(QSize(c, c));
        item.widget->setGeometry(box.x + (c - size.width()) / 2, box.y + (c - size.height()) / 2, size.width(),
            size.height());
    }
    return length;
}

int Toolbox::widthFor(int columns) const
{
    return std::max(columns, 1) * cell() + 2 * kMargin;
}

int Toolbox::acrossFor(int length) const
{
    const Qt::Orientation flow = _orientation;
    return toolboxflow::acrossNeeded(kinds(), cell(), length, _orientation == Qt::Vertical ? _layout.columns : 1,
        [flow](int breadth) { return ColorWell::lengthFor(breadth, flow); });
}

int Toolbox::preferredAcross() const
{
    return std::max(_orientation == Qt::Vertical ? _layout.columns : 1, _forced);
}

QSize Toolbox::sizeHint() const
{
    Toolbox* self = const_cast<Toolbox*>(this);
    const int across = preferredAcross();
    if (_orientation == Qt::Vertical)
        return QSize(widthFor(across), self->place(across, false));
    return QSize(self->place(across, false), widthFor(across));
}

QSize Toolbox::minimumSizeHint() const
{
    // One cell across, or as many as the tools need to fit; along the
    // flow, it can be cut short.
    const int c = cell();
    const int across = widthFor(std::max(_forced, 1));
    if (_orientation == Qt::Vertical)
        return QSize(across, 3 * c);
    return QSize(3 * c, across);
}

bool Toolbox::updateForced()
{
    // Docked and too short (or narrow) for the columns (or rows) chosen,
    // the panel takes as many more as it needs, so no tool is cut off;
    // given the room again, it goes back.
    int forced = 0;
    if (!_floating && isVisible())
    {
        const int length = _orientation == Qt::Vertical ? height() : width();
        const int needed = acrossFor(length);
        if (needed > (_orientation == Qt::Vertical ? _layout.columns : 1))
            forced = needed;
    }
    if (forced == _forced)
        return false;
    // Going back, the panel is still as wide as it was made until the
    // window narrows it again; those columns are not the user's either.
    if (forced == 0)
    {
        _returning = true;
        QTimer::singleShot(400, this, [this]() { _returning = false; });
    }
    _forced = forced;
    updateGeometry();
    emit preferredSizeChanged();
    return true;
}

void Toolbox::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (_floating)
        setOrientation(width() > height() * 2 ? Qt::Horizontal : Qt::Vertical);
    place(0, true);
    // The columns it was given down a side are kept, for next time and in
    // workspaces; not the ones it took to fit a short window, nor while it
    // goes back from those.
    if (updateForced() || _forced > 0)
        return;
    if (_orientation == Qt::Vertical && !_floating && isVisible())
    {
        const int columns = std::clamp((width() - 2 * kMargin) / cell(), 1, toolboxflow::kMostAcross);
        if (_returning)
        {
            if (columns == _layout.columns)
                _returning = false;
            return;
        }
        if (columns != _layout.columns)
        {
            _layout.columns = columns;
            emit layoutChanged();
            updateForced();
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
    updateForced();
}

void Toolbox::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = new QMenu(this);
    QAction* edit = menu->addAction("Edit Toolbar...");
    modal::popup(menu, event->globalPos(), [this, edit](QAction* chosen) {
        if (chosen == edit)
            emit editRequested();
    });
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
