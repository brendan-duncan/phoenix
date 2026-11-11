#include "command_palette.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const int kPathRole = Qt::UserRole + 1;
const int kKeyRole = Qt::UserRole + 2;
const int kIdRole = Qt::UserRole + 3;
const int kRecentKept = 12;

/// A row: the command's name, where it lives after it in a fainter color,
/// and its key at the right.
class RowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem plain = option;
        initStyleOption(&plain, index);
        const QString name = plain.text;
        plain.text.clear();
        // The search keeps the focus, so the row picked is drawn as a
        // focused list draws it, not in the faint color of an inactive one.
        const bool selected = option.state & QStyle::State_Selected;
        plain.state &= ~QStyle::State_Selected;
        QStyle* style = plain.widget ? plain.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &plain, painter, plain.widget);
        if (selected)
            painter->fillRect(option.rect, option.palette.color(QPalette::Active, QPalette::Highlight));

        const bool enabled = index.flags() & Qt::ItemIsEnabled;
        QColor text = option.palette.color(enabled ? QPalette::Active : QPalette::Disabled,
            selected ? QPalette::HighlightedText : QPalette::Text);
        QColor faint = text;
        faint.setAlphaF(0.55);
        const QRect area = option.rect.adjusted(10, 0, -10, 0);
        const QString key = index.data(kKeyRole).toString();
        const QString path = index.data(kPathRole).toString();

        painter->save();
        int right = area.right();
        if (!key.isEmpty())
        {
            painter->setPen(faint);
            const int width = option.fontMetrics.horizontalAdvance(key);
            painter->drawText(QRect(area.right() - width, area.top(), width, area.height()), Qt::AlignVCenter, key);
            right -= width + 16;
        }
        painter->setPen(text);
        const QRect nameRect(area.left(), area.top(), std::max(0, right - area.left()), area.height());
        const QString shownName = option.fontMetrics.elidedText(name, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignVCenter, shownName);
        const int after = area.left() + option.fontMetrics.horizontalAdvance(shownName) + 12;
        if (!path.isEmpty() && after < right)
        {
            painter->setPen(faint);
            const QRect pathRect(after, area.top(), right - after, area.height());
            painter->drawText(pathRect, Qt::AlignVCenter,
                option.fontMetrics.elidedText(path, Qt::ElideLeft, pathRect.width()));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), option.fontMetrics.height() + 10));
        return size;
    }
};

/// The command's name and the menus it is in, from its id ("Modify >
/// Transform > Rotate 90° CW").
void split(const QString& id, QString& name, QString& path)
{
    const int last = static_cast<int>(id.lastIndexOf(" > "));
    name = last < 0 ? id : id.mid(last + 3);
    path = last < 0 ? QString() : id.left(last);
}

/// Whether the action can be seen where it lives: itself, and every menu
/// on the way to it.
bool shown(const QAction* action, int depth = 0)
{
    if (!action->isVisible())
        return false;
    if (depth > 12)
        return true;
    // In a menu that shows, when it is in any menu at all.
    bool inMenu = false;
    for (const QObject* object : action->associatedObjects())
        if (const QMenu* menu = qobject_cast<const QMenu*>(object))
        {
            inMenu = true;
            if (!menu->menuAction() || shown(menu->menuAction(), depth + 1))
                return true;
        }
    return !inMenu;
}

} // namespace

CommandPalette::CommandPalette(QWidget* window)
    : QFrame(window, Qt::Popup)
{
    setObjectName("commandPalette");
    setFrameShape(QFrame::StyledPanel);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    _search = new QLineEdit(this);
    _search->setObjectName("commandPaletteSearch");
    _search->setPlaceholderText("Find a command or tool");
    _search->setClearButtonEnabled(true);
    _search->installEventFilter(this);
    layout->addWidget(_search);
    _list = new QListWidget(this);
    _list->setObjectName("commandPaletteList");
    _list->setItemDelegate(new RowDelegate(_list));
    _list->setUniformItemSizes(true);
    _list->setFocusPolicy(Qt::NoFocus);
    _list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(_list, 1);
    connect(_search, &QLineEdit::textChanged, this, [this]() { refill(); });
    connect(_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { run(item); });
}

void CommandPalette::open(const std::vector<ShortcutEntry>& commands)
{
    _commands = commands;
    _search->clear();
    refill();
    place();
    show();
    raise();
    activateWindow();
    _search->setFocus();
}

void CommandPalette::place()
{
    QWidget* window = parentWidget();
    if (!window)
        return;
    const int width = std::clamp(window->width() / 2, std::min(420, window->width() - 16), 640);
    const int height = std::clamp(window->height() / 2, 160, 460);
    const QPoint topLeft = window->mapToGlobal(QPoint((window->width() - width) / 2, std::min(80, window->height() / 8)));
    setGeometry(QRect(topLeft, QSize(width, height)));
}

int CommandPalette::score(const QString& query, const QString& name, const QString& path)
{
    const QString q = query.simplified().toLower();
    if (q.isEmpty())
        return 0;
    QString plainName = name.toLower();
    plainName.remove('&');
    const QString plainPath = path.toLower();
    const QString all = plainName + " " + plainPath;

    // Every word somewhere in the name or the path.
    const QStringList words = q.split(' ', Qt::SkipEmptyParts);
    bool everyWord = true;
    int best = 0;
    for (const QString& word : words)
    {
        if (!all.contains(word))
        {
            everyWord = false;
            break;
        }
        const int at = static_cast<int>(plainName.indexOf(word));
        if (at == 0)
            best += 40;
        else if (at > 0 && !plainName.at(at - 1).isLetterOrNumber())
            best += 30;
        else if (at > 0)
            best += 15;
        else
            best += 5;
    }
    if (everyWord)
    {
        if (plainName == q)
            best += 100;
        else if (plainName.startsWith(q))
            best += 60;
        // Shorter names first among equals.
        return 1000 + best * 4 - static_cast<int>(plainName.size());
    }

    // The letters in order in the name ("cvs" finds Convert to Symbol),
    // starts of words counting more.
    const QString letters = QString(q).remove(' ');
    int at = 0;
    int points = 0;
    for (const QChar c : letters)
    {
        const int found = static_cast<int>(plainName.indexOf(c, at));
        if (found < 0)
            return -1;
        points += found == 0 || !plainName.at(found - 1).isLetterOrNumber() ? 8 : found == at ? 4 : 1;
        at = found + 1;
    }
    return points * 4 - static_cast<int>(plainName.size());
}

QStringList CommandPalette::recent()
{
    return QSettings().value("commandPalette/recent").toStringList();
}

void CommandPalette::refill()
{
    _list->clear();
    const QString query = _search->text();
    struct Found
    {
        const ShortcutEntry* entry;
        int score;
    };
    std::vector<Found> found;
    if (query.trimmed().isEmpty())
    {
        // The last ones run, then the rest as the menus have them.
        const QStringList last = recent();
        for (const QString& id : last)
            for (const ShortcutEntry& entry : _commands)
                if (entry.id == id && entry.action && shown(entry.action))
                    found.push_back({&entry, 0});
        for (const ShortcutEntry& entry : _commands)
            if (entry.action && shown(entry.action) && !last.contains(entry.id))
                found.push_back({&entry, 0});
    }
    else
    {
        for (const ShortcutEntry& entry : _commands)
        {
            if (!entry.action || !shown(entry.action))
                continue;
            QString name;
            QString path;
            split(entry.id, name, path);
            const int s = score(query, name, path);
            if (s >= 0)
                found.push_back({&entry, s});
        }
        std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.score > b.score; });
    }

    for (const Found& f : found)
    {
        QString name;
        QString path;
        split(f.entry->id, name, path);
        if (f.entry->action->isCheckable())
            name += f.entry->action->isChecked() ? "  (on)" : "  (off)";
        QListWidgetItem* item = new QListWidgetItem(name, _list);
        item->setData(kPathRole, path);
        item->setData(kKeyRole, f.entry->action->shortcut().toString(QKeySequence::NativeText));
        item->setData(kIdRole, f.entry->id);
        item->setToolTip(f.entry->action->statusTip().isEmpty() ? f.entry->id : f.entry->action->statusTip());
        if (!f.entry->action->isEnabled())
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    // The first that can be run.
    for (int i = 0; i < _list->count(); ++i)
        if (_list->item(i)->flags() & Qt::ItemIsEnabled)
        {
            _list->setCurrentRow(i);
            break;
        }
}

void CommandPalette::run(QListWidgetItem* item)
{
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const QString id = item->data(kIdRole).toString();
    QPointer<QAction> action;
    for (const ShortcutEntry& entry : _commands)
        if (entry.id == id)
            action = entry.action;
    if (!action)
        return;
    QStringList last = recent();
    last.removeAll(id);
    last.prepend(id);
    while (last.size() > kRecentKept)
        last.removeLast();
    QSettings().setValue("commandPalette/recent", last);
    hide();
    // After the box has gone, so a dialog the command opens comes up over
    // the window and the stage has the keys back.
    QTimer::singleShot(0, action, [action]() {
        if (action && action->isEnabled())
            action->trigger();
    });
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _search && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        const int count = _list->count();
        const auto step = [this, count](int by) {
            if (count == 0)
                return;
            int row = _list->currentRow();
            for (int i = 0; i < count; ++i)
            {
                row = std::clamp(row + by, 0, count - 1);
                if (_list->item(row)->flags() & Qt::ItemIsEnabled)
                    break;
            }
            _list->setCurrentRow(row);
        };
        switch (key->key())
        {
        case Qt::Key_Down:
            step(1);
            return true;
        case Qt::Key_Up:
            step(-1);
            return true;
        case Qt::Key_PageDown:
            step(10);
            return true;
        case Qt::Key_PageUp:
            step(-10);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            run(_list->currentItem());
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event)
{
    // Keys that reach the box itself go to the search, as when it has focus.
    QApplication::sendEvent(_search, event);
}

void CommandPalette::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    if (parentWidget())
        parentWidget()->activateWindow();
}
