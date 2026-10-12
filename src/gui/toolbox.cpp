#include "toolbox.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

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

Toolbox::Layout Toolbox::Layout::fromJson(const QJsonObject& json)
{
    Layout layout;
    layout.columns = std::clamp(json["columns"].toInt(1), 1, 2);
    for (const QJsonValue& value : json["slots"].toArray())
    {
        Slot slot;
        for (const QJsonValue& id : value.toArray())
            if (id.isString())
                slot << id.toString();
        if (!slot.isEmpty())
            layout.entries.push_back(slot);
    }
    for (const QJsonValue& id : json["hidden"].toArray())
        if (id.isString())
            layout.hidden << id.toString();
    return layout;
}

Toolbox::Toolbox(QWidget* parent)
    : QWidget(parent)
{
    _outer = new QVBoxLayout(this);
    _outer->setContentsMargins(3, 3, 3, 3);
    _outer->setSpacing(2);
    _grid = new QWidget(this);
    _outer->addWidget(_grid);
    _outer->addStretch(1);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
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
    out.columns = std::clamp(layout.columns, 1, 2);
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

void Toolbox::setColumns(int columns)
{
    columns = std::clamp(columns, 1, 2);
    if (columns == _layout.columns)
        return;
    _layout.columns = columns;
    rebuild();
    emit layoutChanged();
}

void Toolbox::setExtras(QWidget* extras)
{
    if (_extras)
        _outer->removeWidget(_extras);
    _extras = extras;
    if (_extras)
    {
        _extras->setParent(this);
        _outer->insertWidget(1, _extras);
    }
}

void Toolbox::rebuild()
{
    // The buttons are made again; the actions stay, so keys and checked
    // states carry on.
    delete _grid;
    _grid = new QWidget(this);
    _outer->insertWidget(0, _grid);
    QGridLayout* grid = new QGridLayout(_grid);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    const int columns = _layout.columns;
    int row = 0;
    int column = 0;
    for (const Slot& slot : _layout.entries)
    {
        if (slot == Slot{"|"})
        {
            if (column > 0)
            {
                ++row;
                column = 0;
            }
            QFrame* line = new QFrame(_grid);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Plain);
            line->setEnabled(false);
            grid->addWidget(line, row++, 0, 1, columns);
            continue;
        }
        QToolButton* button = new QToolButton(_grid);
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
        grid->addWidget(button, row, column);
        if (++column == columns)
        {
            column = 0;
            ++row;
        }
    }
    _grid->show();
    adjustSize();
    updateGeometry();
}

void Toolbox::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* one = menu.addAction("One Column");
    one->setCheckable(true);
    one->setChecked(_layout.columns == 1);
    QAction* two = menu.addAction("Two Columns");
    two->setCheckable(true);
    two->setChecked(_layout.columns == 2);
    menu.addSeparator();
    QAction* edit = menu.addAction("Edit Toolbar...");
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == one)
        setColumns(1);
    else if (chosen == two)
        setColumns(2);
    else if (chosen == edit)
        emit editRequested();
}
