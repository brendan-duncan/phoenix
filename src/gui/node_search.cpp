#include "node_search.h"

#include "node_graph_style.h"
#include "theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Role
{
    TypeRole = Qt::UserRole,
    CategoryRole,
    HeaderRole,
    DescriptionRole,
    ShowCategoryRole
};

/// The kinds added lately, newest first, for the menu's top.
QStringList& recent()
{
    static QStringList list;
    return list;
}

/// Rows of the menu: category headings, and kinds with their icon.
class Delegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool header = index.data(HeaderRole).toBool();
        return QSize(option.rect.width(), header ? 24 : 28);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(option.rect).adjusted(4, 1, -4, -1);
        const QString category = index.data(CategoryRole).toString();
        const QColor color = nodestyle::categoryColor(category.toStdString());
        QColor text = theme::color("nodes.text");
        QColor dim = theme::color("nodes.dimText");
        if (index.data(HeaderRole).toBool())
        {
            QFont font = option.font;
            font.setPointSizeF(font.pointSizeF() * 0.82);
            font.setBold(true);
            font.setLetterSpacing(QFont::PercentageSpacing, 108);
            painter->setFont(font);
            painter->setPen(dim);
            painter->drawText(r.adjusted(8, 4, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, index.data().toString().toUpper());
            painter->restore();
            return;
        }
        if (option.state & QStyle::State_Selected)
        {
            QColor fill = theme::color("accent");
            fill.setAlpha(60);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(r, 5, 5);
        }
        else if (option.state & QStyle::State_MouseOver)
        {
            QColor fill = text;
            fill.setAlpha(18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(r, 5, 5);
        }
        // The category's icon on its color.
        const QRectF chip(r.left() + 6, r.center().y() - 9, 18, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRoundedRect(chip, 4, 4);
        nodestyle::drawIcon(*painter, chip.adjusted(3, 3, -3, -3), category.toStdString(), QColor(255, 255, 255, 235));
        painter->setPen(text);
        painter->setFont(option.font);
        QRectF label = r.adjusted(32, 0, -8, 0);
        if (index.data(ShowCategoryRole).toBool())
        {
            QFont small = option.font;
            small.setPointSizeF(small.pointSizeF() * 0.85);
            painter->setFont(small);
            painter->setPen(dim);
            const QRectF right = r.adjusted(0, 0, -10, 0);
            painter->drawText(right, Qt::AlignRight | Qt::AlignVCenter, category);
            label.setRight(label.right() - QFontMetricsF(small).horizontalAdvance(category) - 10);
            painter->setFont(option.font);
            painter->setPen(text);
        }
        painter->drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetricsF(option.font).elidedText(index.data().toString(), Qt::ElideRight, label.width()));
        painter->restore();
    }
};

} // namespace

NodeSearch::NodeSearch(QWidget* parent, std::function<bool(const fla::nodegraph::Type&)> accept)
    : QFrame(parent, Qt::Popup)
    , _accept(std::move(accept))
{
    setAttribute(Qt::WA_DeleteOnClose);
    // Rounded corners, with nothing showing past them.
    setAttribute(Qt::WA_TranslucentBackground);
    setFrameShape(QFrame::NoFrame);
    setObjectName("nodeSearch");
    const QColor back = theme::color("nodes.node");
    const QColor border = theme::color("nodes.border");
    const QColor text = theme::color("nodes.text");
    setStyleSheet(QString("#nodeSearch { background: %1; border: 1px solid %2; border-radius: 8px; }"
                          "QLineEdit { background: %3; border: 1px solid %2; border-radius: 6px; padding: 5px 8px;"
                          " color: %4; selection-background-color: %5; }"
                          "QListWidget { background: transparent; border: none; outline: none; }")
                      .arg(back.name(), border.name(), theme::color("nodes.background").name(), text.name(),
                          theme::color("accent").name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    _field = new QLineEdit(this);
    _field->setPlaceholderText(_accept ? "Search nodes that fit..." : "Search nodes...");
    _field->setClearButtonEnabled(true);
    _field->installEventFilter(this);
    layout->addWidget(_field);
    _list = new QListWidget(this);
    _list->setItemDelegate(new Delegate(_list));
    _list->setMouseTracking(true);
    _list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    _list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(_list, 1);
    _description = new QLabel(this);
    _description->setWordWrap(true);
    _description->setMinimumHeight(34);
    _description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    _description->setStyleSheet(QString("color: %1; padding: 2px 4px;").arg(theme::color("nodes.dimText").name()));
    layout->addWidget(_description);
    resize(320, 420);

    connect(_field, &QLineEdit::textChanged, this, [this]() { fill(); });
    connect(_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item && !item->data(HeaderRole).toBool())
        {
            _list->setCurrentItem(item);
            choose();
        }
    });
    connect(_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        _description->setText(item ? item->data(DescriptionRole).toString() : QString());
    });
    fill();
}

int NodeSearch::score(const QString& query, const QString& text)
{
    if (query.isEmpty())
        return 0;
    const QString q = query.toLower();
    const QString t = text.toLower();
    // Each letter in order; more for runs and for starts of words.
    int total = 0;
    int at = 0;
    int run = 0;
    for (const QChar c : q)
    {
        if (c.isSpace())
            continue;
        const int found = t.indexOf(c, at);
        if (found < 0)
            return -1;
        const bool wordStart = found == 0 || !t[found - 1].isLetterOrNumber();
        run = found == at && at > 0 ? run + 1 : 0;
        total += 10 + (wordStart ? 15 : 0) + run * 8 - std::min(8, found - at);
        at = found + 1;
    }
    if (t.startsWith(q))
        total += 40;
    return total - t.size() / 4;
}

void NodeSearch::fill()
{
    _list->clear();
    const QString query = _field->text().trimmed();
    const auto addItem = [this](const fla::nodegraph::Type& type, bool showCategory) {
        auto* item = new QListWidgetItem(QString::fromStdString(type.name), _list);
        item->setData(TypeRole, QString::fromStdString(type.id));
        item->setData(CategoryRole, QString::fromStdString(type.category));
        item->setData(DescriptionRole, QString::fromStdString(type.description));
        item->setData(ShowCategoryRole, showCategory);
    };
    const auto addHeader = [this](const QString& text) {
        auto* item = new QListWidgetItem(text, _list);
        item->setData(HeaderRole, true);
        item->setFlags(Qt::NoItemFlags);
    };
    const auto offered = [this](const fla::nodegraph::Type& type) {
        // One Output is enough; the menu never adds another.
        return type.id != "output" && (!_accept || _accept(type));
    };
    if (query.isEmpty())
    {
        bool any = false;
        for (const QString& id : recent())
        {
            const fla::nodegraph::Type* type = fla::nodegraph::type(id.toStdString());
            if (!type || !offered(*type))
                continue;
            if (!any)
                addHeader("Recent");
            any = true;
            addItem(*type, true);
        }
        for (const std::string& category : fla::nodegraph::categories())
        {
            bool headed = false;
            for (const fla::nodegraph::Type& type : fla::nodegraph::types())
            {
                if (type.category != category || !offered(type))
                    continue;
                if (!headed)
                    addHeader(QString::fromStdString(category));
                headed = true;
                addItem(type, false);
            }
        }
    }
    else
    {
        std::vector<std::pair<int, const fla::nodegraph::Type*>> ranked;
        for (const fla::nodegraph::Type& type : fla::nodegraph::types())
        {
            if (!offered(type))
                continue;
            const int byName = score(query, QString::fromStdString(type.name));
            const int byCategory = score(query, QString::fromStdString(type.category));
            const int byWords = QString::fromStdString(type.description).contains(query, Qt::CaseInsensitive) ? 5 : -1;
            const int best = std::max({byName >= 0 ? byName * 2 : -1, byCategory, byWords});
            if (best >= 0)
                ranked.emplace_back(best, &type);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [score, type] : ranked)
            addItem(*type, true);
    }
    for (int i = 0; i < _list->count(); ++i)
        if (!_list->item(i)->data(HeaderRole).toBool())
        {
            _list->setCurrentRow(i);
            break;
        }
}

void NodeSearch::stepBy(int step)
{
    int row = _list->currentRow();
    for (int tries = 0; tries < _list->count(); ++tries)
    {
        row += step;
        if (row < 0 || row >= _list->count())
            return;
        if (!_list->item(row)->data(HeaderRole).toBool())
        {
            _list->setCurrentRow(row);
            return;
        }
    }
}

void NodeSearch::choose()
{
    QListWidgetItem* item = _list->currentItem();
    if (!item || item->data(HeaderRole).toBool())
        return;
    const QString type = item->data(TypeRole).toString();
    recent().removeAll(type);
    recent().prepend(type);
    while (recent().size() > 5)
        recent().removeLast();
    _done = true;
    emit chosen(type);
    close();
}

bool NodeSearch::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _field && event->type() == QEvent::KeyPress)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key())
        {
        case Qt::Key_Down:
            stepBy(1);
            return true;
        case Qt::Key_Up:
            stepBy(-1);
            return true;
        case Qt::Key_PageDown:
            stepBy(8);
            return true;
        case Qt::Key_PageUp:
            stepBy(-8);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            choose();
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void NodeSearch::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    if (!_done)
    {
        _done = true;
        emit dismissed();
    }
}

void NodeSearch::popup(const QPoint& global)
{
    QPoint at = global;
    if (const QScreen* screen = QApplication::screenAt(global))
    {
        const QRect area = screen->availableGeometry();
        at.setX(std::min(at.x(), area.right() - width()));
        at.setY(std::min(at.y(), area.bottom() - height()));
        at.setX(std::max(at.x(), area.left()));
        at.setY(std::max(at.y(), area.top()));
    }
    QWidget::move(at);
    show();
    _field->setFocus();
}
