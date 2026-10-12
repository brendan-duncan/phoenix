#include "toolbar_editor.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int KindRole = Qt::UserRole;
constexpr int IdRole = Qt::UserRole + 1;

enum Kind
{
    SlotKind,
    LineKind,
    ToolKind,
    HiddenKind
};

int kindOf(const QTreeWidgetItem* item)
{
    return item->data(0, KindRole).toInt();
}

} // namespace

ToolbarEditor::ToolbarEditor(Toolbox* toolbox, QWidget* parent)
    : QDialog(parent)
    , _toolbox(toolbox)
{
    setWindowTitle("Edit Toolbar");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* help = new QLabel(
        "Drag a tool to move it: into a slot to group it there, between slots to give it a slot of its own, "
        "or under Hidden to take it off the panel (its key still works). The first tool in a slot is the "
        "one shown.",
        this);
    help->setWordWrap(true);
    layout->addWidget(help);

    _tree = new QTreeWidget(this);
    _tree->setHeaderHidden(true);
    _tree->setDragDropMode(QAbstractItemView::InternalMove);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setDefaultDropAction(Qt::MoveAction);
    _tree->setIndentation(18);
    _tree->setMinimumHeight(380);
    layout->addWidget(_tree, 1);
    // After a drop the tree is put back into shape, once the move is done.
    connect(_tree->model(), &QAbstractItemModel::rowsInserted, this, [this]() {
        if (!_tidying)
            QTimer::singleShot(0, this, [this]() { tidy(); });
    });

    QHBoxLayout* row = new QHBoxLayout();
    QPushButton* line = new QPushButton("Add Line", this);
    line->setToolTip("A line between sections; drag it where it goes");
    QPushButton* reset = new QPushButton("Reset", this);
    reset->setToolTip("Phoenix's own layout");
    row->addWidget(line);
    row->addWidget(reset);
    row->addStretch(1);
    layout->addLayout(row);
    connect(line, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* item = slotItem(Toolbox::Slot{"|"});
        _tree->insertTopLevelItem(_tree->indexOfTopLevelItem(_hidden), item);
        _tree->setCurrentItem(item);
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        // The panel keeps its width.
        Toolbox::Layout layout = _toolbox->defaultLayout();
        layout.columns = _columns;
        load(layout);
    });

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    load(_toolbox->layout());
    resize(380, 560);
}

QTreeWidgetItem* ToolbarEditor::toolItem(const QString& id)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    QAction* action = _toolbox->tool(id);
    item->setText(0, id);
    if (action)
    {
        item->setIcon(0, action->icon());
        if (!action->shortcut().isEmpty())
            item->setText(0, QString("%1  (%2)").arg(id, action->shortcut().toString(QKeySequence::NativeText)));
    }
    item->setData(0, KindRole, ToolKind);
    item->setData(0, IdRole, id);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    return item;
}

QTreeWidgetItem* ToolbarEditor::slotItem(const Toolbox::Slot& slot)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();
    if (slot == Toolbox::Slot{"|"})
    {
        item->setText(0, QString(18, QChar(0x2014)));
        item->setData(0, KindRole, LineKind);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        return item;
    }
    item->setText(0, "Slot");
    item->setData(0, KindRole, SlotKind);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    for (const QString& id : slot)
        item->addChild(toolItem(id));
    if (QAction* first = _toolbox->tool(slot.value(0)))
        item->setIcon(0, first->icon());
    return item;
}

void ToolbarEditor::load(const Toolbox::Layout& layout)
{
    _tidying = true;
    _tree->clear();
    _columns = layout.columns;
    for (const Toolbox::Slot& slot : layout.entries)
        _tree->addTopLevelItem(slotItem(slot));
    _hidden = new QTreeWidgetItem();
    _hidden->setText(0, "Hidden");
    _hidden->setData(0, KindRole, HiddenKind);
    _hidden->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    QFont font = _hidden->font(0);
    font.setItalic(true);
    _hidden->setFont(0, font);
    for (const QString& id : layout.hidden)
        _hidden->addChild(toolItem(id));
    _tree->addTopLevelItem(_hidden);
    _tree->expandAll();
    _tidying = false;
}

Toolbox::Layout ToolbarEditor::layout() const
{
    Toolbox::Layout layout;
    layout.columns = _columns;
    // Whatever depth a drop left them at, the tools under each top item
    // belong to it.
    std::function<void(QTreeWidgetItem*, QStringList&)> tools = [&](QTreeWidgetItem* item, QStringList& into) {
        if (kindOf(item) == ToolKind)
            into << item->data(0, IdRole).toString();
        for (int i = 0; i < item->childCount(); ++i)
            tools(item->child(i), into);
    };
    for (int i = 0; i < _tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = _tree->topLevelItem(i);
        switch (kindOf(item))
        {
        case LineKind:
            layout.entries.push_back(Toolbox::Slot{"|"});
            break;
        case HiddenKind:
            tools(item, layout.hidden);
            break;
        default:
        {
            // A tool dropped between slots is a slot of its own; a line
            // dropped into a slot comes out after it.
            Toolbox::Slot slot;
            tools(item, slot);
            if (!slot.isEmpty())
                layout.entries.push_back(slot);
            bool line = false;
            for (int k = 0; k < item->childCount(); ++k)
                line = line || kindOf(item->child(k)) == LineKind;
            if (line)
                layout.entries.push_back(Toolbox::Slot{"|"});
            break;
        }
        }
    }
    return layout;
}

void ToolbarEditor::tidy()
{
    if (_tidying)
        return;
    QTreeWidgetItem* current = _tree->currentItem();
    const QString id = current ? current->data(0, IdRole).toString() : QString();
    // Anything dropped below Hidden goes into it.
    Toolbox::Layout tidied = layout();
    const int hiddenAt = _tree->indexOfTopLevelItem(_hidden);
    for (int i = hiddenAt + 1; i < _tree->topLevelItemCount(); ++i)
    {
        QStringList moved;
        std::function<void(QTreeWidgetItem*)> tools = [&](QTreeWidgetItem* item) {
            if (kindOf(item) == ToolKind)
                moved << item->data(0, IdRole).toString();
            for (int k = 0; k < item->childCount(); ++k)
                tools(item->child(k));
        };
        tools(_tree->topLevelItem(i));
        for (const QString& tool : moved)
        {
            for (auto it = tidied.entries.begin(); it != tidied.entries.end(); ++it)
                it->removeAll(tool);
            tidied.hidden << tool;
        }
    }
    tidied.entries.erase(std::remove_if(tidied.entries.begin(), tidied.entries.end(),
                           [](const Toolbox::Slot& slot) { return slot.isEmpty(); }),
        tidied.entries.end());
    load(tidied);
    // The tool moved stays picked.
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
        if (!id.isEmpty() && (*it)->data(0, IdRole).toString() == id)
            _tree->setCurrentItem(*it);
}
