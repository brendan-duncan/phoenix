#include "document_brushes_dialog.h"

#include "brush_library_store.h"
#include "modal.h"

#include "../data/document.h"
#include "../edit/paint_brushes.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

DocumentBrushesDialog::DocumentBrushesDialog(fla::Document& document, Hooks hooks, QWidget* parent)
    : QDialog(parent)
    , _document(document)
    , _hooks(std::move(hooks))
{
    setWindowTitle("Document Brushes");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* intro = new QLabel("The brushes the document's strokes are drawn with: its paint brushes, which the "
                               "Paint Brush tool draws with, and the graphic symbols drawn along strokes. More "
                               "paint brushes come from the brush library (Window > Brush Library).",
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    _list = new QTreeWidget(this);
    _list->setColumnCount(3);
    _list->setHeaderLabels({"Brush", "Kind", "Strokes"});
    _list->setRootIsDecorated(false);
    _list->setIconSize(QSize(96, 24));
    _list->setUniformRowHeights(true);
    _list->setSelectionMode(QAbstractItemView::SingleSelection);
    _list->header()->setStretchLastSection(false);
    _list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(_list, 1);
    _status = new QLabel(this);
    _status->setWordWrap(true);
    layout->addWidget(_status);

    QHBoxLayout* buttons = new QHBoxLayout;
    _draw = new QPushButton("Draw With It", this);
    _draw->setToolTip("Draw new strokes with this brush");
    _select = new QPushButton("Select Strokes", this);
    _select->setToolTip("Select what on the current frame is drawn with this brush");
    _rename = new QPushButton("Rename…", this);
    _rename->setToolTip("Give this paint brush another name");
    _keep = new QPushButton("Add to Brush Library", this);
    _keep->setToolTip("Keep this paint brush in the brush library, to draw with in other documents");
    _takeOff = new QPushButton("Take Off Strokes", this);
    _takeOff->setToolTip("Draw the strokes that use this brush as plain lines again");
    _removeUnused = new QPushButton("Remove Unused", this);
    _removeUnused->setToolTip("Take the paint brushes no stroke draws with out of the document");
    QPushButton* close = new QPushButton("Close", this);
    for (QPushButton* button : {_draw, _select, _rename, _keep, _takeOff})
        buttons->addWidget(button);
    buttons->addStretch(1);
    buttons->addWidget(_removeUnused);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(_list, &QTreeWidget::currentItemChanged, this, [this]() { updateButtons(); });
    connect(_list, &QTreeWidget::itemDoubleClicked, this, [this]() {
        if (const fla::DocumentBrush* brush = chosen(); brush && _hooks.drawWith)
            _hooks.drawWith(*brush);
    });
    connect(_draw, &QPushButton::clicked, this, [this]() {
        const fla::DocumentBrush* brush = chosen();
        if (!brush || !_hooks.drawWith)
            return;
        _hooks.drawWith(*brush);
        _status->setText(QString("New strokes are drawn with %1.").arg(QString::fromStdString(brush->name)));
    });
    connect(_select, &QPushButton::clicked, this, [this]() {
        const fla::DocumentBrush* brush = chosen();
        if (!brush || !_hooks.selectStrokes)
            return;
        const int count = _hooks.selectStrokes(*brush);
        _status->setText(count > 0 ? QString("Selected %1 object%2 on this frame drawn with %3.")
                                         .arg(count)
                                         .arg(count == 1 ? "" : "s")
                                         .arg(QString::fromStdString(brush->name))
                                   : QString("Nothing on this frame is drawn with %1; its strokes are on other "
                                             "frames or inside symbols.")
                                         .arg(QString::fromStdString(brush->name)));
    });
    connect(_rename, &QPushButton::clicked, this, &DocumentBrushesDialog::rename);
    connect(_keep, &QPushButton::clicked, this, &DocumentBrushesDialog::addToLibrary);
    connect(_takeOff, &QPushButton::clicked, this, [this]() {
        const fla::DocumentBrush* brush = chosen();
        if (!brush || !_hooks.takeOff)
            return;
        const fla::DocumentBrush taken = *brush;
        _hooks.takeOff(taken);
        refill();
        _status->setText(QString("The strokes drawn with %1 are plain lines now.").arg(QString::fromStdString(taken.name)));
    });
    connect(_removeUnused, &QPushButton::clicked, this, &DocumentBrushesDialog::removeUnused);

    refill();
    resize(600, 420);
}

void DocumentBrushesDialog::refill()
{
    const QString was = _list->currentItem() ? _list->currentItem()->text(0) : QString();
    _brushes = fla::documentBrushes(_document);
    _list->clear();
    const QColor ink = palette().color(QPalette::Text);
    for (size_t i = 0; i < _brushes.size(); ++i)
    {
        const fla::DocumentBrush& brush = _brushes[i];
        QTreeWidgetItem* item = new QTreeWidgetItem(_list);
        item->setText(0, QString::fromStdString(brush.name));
        if (brush.paintBrush)
            item->setIcon(0, QIcon(paintBrushPreview(brush.definition, QSize(96, 24), ink)));
        item->setText(1, brush.paintBrush ? (brush.pattern ? "Pattern brush" : "Art brush")
                                          : (brush.pattern ? "Symbol, repeated" : "Symbol, stretched"));
        item->setText(2, QString::number(brush.strokes));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        if (item->text(0) == was)
            _list->setCurrentItem(item);
    }
    if (_brushes.empty())
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(_list);
        item->setText(0, "No brushes: draw with one from the brush library, or choose a symbol under Brush.");
        item->setFlags(Qt::NoItemFlags);
    }
    else if (!_list->currentItem())
    {
        _list->setCurrentItem(_list->topLevelItem(0));
    }
    int unused = 0;
    for (const fla::DocumentBrush& brush : _brushes)
        unused += brush.paintBrush && brush.strokes == 0 ? 1 : 0;
    _removeUnused->setEnabled(unused > 0);
    _removeUnused->setText(unused > 0 ? QString("Remove %1 Unused").arg(unused) : QString("Remove Unused"));
    updateButtons();
}

const fla::DocumentBrush* DocumentBrushesDialog::chosen() const
{
    const QTreeWidgetItem* item = _list->currentItem();
    if (!item || !item->data(0, Qt::UserRole).isValid())
        return nullptr;
    const int index = item->data(0, Qt::UserRole).toInt();
    return index >= 0 && index < static_cast<int>(_brushes.size()) ? &_brushes[static_cast<size_t>(index)] : nullptr;
}

void DocumentBrushesDialog::updateButtons()
{
    const fla::DocumentBrush* brush = chosen();
    _draw->setEnabled(brush != nullptr);
    _select->setEnabled(brush && brush->strokes > 0);
    _takeOff->setEnabled(brush && brush->strokes > 0);
    _rename->setEnabled(brush && brush->paintBrush);
    _keep->setEnabled(brush && brush->paintBrush && !BrushLibraryStore::shared().find(brush->definition.guid()));
}

void DocumentBrushesDialog::rename()
{
    const fla::DocumentBrush* brush = chosen();
    if (!brush || !brush->paintBrush || !_hooks.push)
        return;
    // What the callback needs of the brush, copied: the list is refilled
    // as the document changes.
    const std::string was = brush->name;
    const auto index = brush->definition.index;
    modal::getText(this, "Rename Brush", "Name:", QString::fromStdString(was), [this, was, index](const QString& text) {
        const QString name = text.trimmed();
        if (name.isEmpty() || name.toStdString() == was)
            return;
        const auto library = fla::paintBrushesOf(_document);
        if (!library)
            return;
        std::vector<fla::PaintBrushDefinition> definitions = library->definitions;
        for (fla::PaintBrushDefinition& definition : definitions)
            if (definition.index == index)
            {
                definition = fla::withParam(definition, "name", name.toStdString());
                definition.index = index;
            }
        if (fla::CommandPtr command = fla::setPaintBrushDefinitions(_document, definitions, "Rename Brush"))
            _hooks.push(std::move(command));
        refill();
    });
}

void DocumentBrushesDialog::removeUnused()
{
    const auto library = fla::paintBrushesOf(_document);
    if (!library || !_hooks.push)
        return;
    const std::map<int, int> uses = fla::paintBrushUses(_document);
    std::vector<fla::PaintBrushDefinition> kept;
    for (const fla::PaintBrushDefinition& definition : library->definitions)
        if (uses.count(definition.index))
            kept.push_back(definition);
    const size_t removed = library->definitions.size() - kept.size();
    if (removed == 0)
        return;
    if (fla::CommandPtr command = fla::setPaintBrushDefinitions(_document, kept, "Remove Unused Brushes"))
        _hooks.push(std::move(command));
    refill();
    _status->setText(QString("Removed %1 brush%2 no stroke drew with.").arg(removed).arg(removed == 1 ? "" : "es"));
}

void DocumentBrushesDialog::addToLibrary()
{
    const fla::DocumentBrush* brush = chosen();
    if (!brush || !brush->paintBrush)
        return;
    BrushLibraryStore& store = BrushLibraryStore::shared();
    const int added = store.add({brush->definition});
    _status->setText(!store.error().isEmpty() ? store.error()
            : added > 0
            ? QString("%1 is in the brush library, under %2.")
                  .arg(QString::fromStdString(brush->name))
                  .arg(QString::fromStdString(fla::paintBrushCategory(brush->definition)))
            : QString("The brush library has %1 already.").arg(QString::fromStdString(brush->name)));
    updateButtons();
}
