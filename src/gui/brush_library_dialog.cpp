#include "brush_library_dialog.h"

#include "brush_library_store.h"
#include "file_pick.h"
#include "modal.h"

#include "../data/document.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <map>

namespace {

/// The list's entry for every category at once.
const char* const kAll = "All Brushes";

} // namespace

BrushLibraryDialog::BrushLibraryDialog(BrushLibraryStore& store, Hooks hooks, QWidget* parent)
    : QDialog(parent)
    , _store(store)
    , _hooks(std::move(hooks))
{
    setWindowTitle("Brush Library");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* intro = new QLabel("Brushes to draw with in any document. Choose one to paint with it (Paint Brush "
                               "tool); a document keeps a copy of each brush its strokes use. Phoenix's own brushes "
                               "come with it; add others from a document that has them.",
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    _search = new QLineEdit(this);
    _search->setPlaceholderText("Search brushes");
    _search->setClearButtonEnabled(true);
    layout->addWidget(_search);

    QSplitter* split = new QSplitter(Qt::Horizontal, this);
    QWidget* left = new QWidget(split);
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    _categories = new QListWidget(left);
    leftLayout->addWidget(_categories, 1);
    QHBoxLayout* categoryButtons = new QHBoxLayout;
    _renameCategory = new QPushButton("Rename…", left);
    _renameCategory->setToolTip("Give this category another name, or file its brushes with another's");
    _deleteCategory = new QPushButton("Delete", left);
    _deleteCategory->setToolTip("Delete this category and its brushes from the library");
    categoryButtons->addWidget(_renameCategory);
    categoryButtons->addWidget(_deleteCategory);
    leftLayout->addLayout(categoryButtons);
    _brushes = new QListWidget(split);
    _brushes->setViewMode(QListView::IconMode);
    _brushes->setIconSize(QSize(132, 40));
    _brushes->setGridSize(QSize(150, 78));
    _brushes->setResizeMode(QListView::Adjust);
    _brushes->setMovement(QListView::Static);
    _brushes->setWordWrap(true);
    _brushes->setUniformItemSizes(true);
    split->addWidget(left);
    split->addWidget(_brushes);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({170, 480});
    layout->addWidget(split, 1);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    layout->addWidget(_status);

    QHBoxLayout* brushButtons = new QHBoxLayout;
    _draw = new QPushButton("Draw With It", this);
    _draw->setToolTip("Paint with this brush: the Paint Brush tool, drawing with it");
    _add = new QPushButton("Add to Document", this);
    _add->setToolTip("Copy this brush into the document without drawing with it yet");
    _rename = new QPushButton("Rename…", this);
    _move = new QPushButton("Move To…", this);
    _delete = new QPushButton("Delete", this);
    for (QPushButton* button : {_draw, _add, _rename, _move, _delete})
        brushButtons->addWidget(button);
    brushButtons->addStretch(1);
    layout->addLayout(brushButtons);

    QHBoxLayout* bottom = new QHBoxLayout;
    QPushButton* import = new QPushButton("Import Brushes from Document…", this);
    import->setToolTip("Add the brushes of an FLA or XFL document to the library, filed by their names");
    _fromThis = new QPushButton("Add Brushes from This Document", this);
    _fromThis->setToolTip("Add the open document's brushes to the library");
    QPushButton* close = new QPushButton("Close", this);
    bottom->addWidget(import);
    bottom->addWidget(_fromThis);
    bottom->addStretch(1);
    bottom->addWidget(close);
    layout->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(_search, &QLineEdit::textChanged, this, [this]() { fillBrushes(); });
    connect(_categories, &QListWidget::currentRowChanged, this, [this]() {
        fillBrushes();
        updateButtons();
    });
    connect(_brushes, &QListWidget::currentItemChanged, this, [this]() { updateButtons(); });
    const auto chosenDefinition = [this]() -> const fla::BrushLibraryEntry* { return _store.find(chosenGuid()); };
    const auto draw = [this, chosenDefinition]() {
        const fla::BrushLibraryEntry* entry = chosenDefinition();
        if (!entry || !_hooks.drawWith)
            return;
        _hooks.drawWith(entry->definition);
        _status->setText(QString("Painting with %1.").arg(QString::fromStdString(entry->definition.name)));
    };
    connect(_draw, &QPushButton::clicked, this, draw);
    connect(_brushes, &QListWidget::itemActivated, this, draw);
    connect(_add, &QPushButton::clicked, this, [this, chosenDefinition]() {
        const fla::BrushLibraryEntry* entry = chosenDefinition();
        if (!entry || !_hooks.addToDocument)
            return;
        _hooks.addToDocument(entry->definition);
        _status->setText(QString("The document has %1.").arg(QString::fromStdString(entry->definition.name)));
    });
    connect(_rename, &QPushButton::clicked, this, [this, chosenDefinition]() {
        const fla::BrushLibraryEntry* entry = chosenDefinition();
        if (!entry)
            return;
        const std::string guid = entry->definition.guid();
        modal::getText(this, "Rename Brush", "Name:", QString::fromStdString(entry->definition.name),
            [this, guid](const QString& name) { _store.renameBrush(guid, name); });
    });
    connect(_move, &QPushButton::clicked, this, [this, chosenDefinition]() {
        const fla::BrushLibraryEntry* entry = chosenDefinition();
        if (!entry)
            return;
        const std::string guid = entry->definition.guid();
        QStringList categories = _store.categories();
        categories.removeAll("Phoenix");
        modal::getItem(this, "Move Brush", "Category:", categories,
            static_cast<int>(std::max<qsizetype>(0, categories.indexOf(QString::fromStdString(entry->category)))), true,
            [this, guid](const QString& category) { _store.moveBrush(guid, category); });
    });
    connect(_delete, &QPushButton::clicked, this, [this]() { _store.removeBrush(chosenGuid()); });
    connect(_renameCategory, &QPushButton::clicked, this, [this]() {
        const QString category = chosenCategory();
        if (category.isEmpty())
            return;
        modal::getText(this, "Rename Category", "Name:", category, [this, category](const QString& typed) {
            const QString name = typed.trimmed();
            if (_store.renameCategory(category, name))
            {
                const QList<QListWidgetItem*> found = _categories->findItems(name, Qt::MatchStartsWith);
                if (!found.isEmpty())
                    _categories->setCurrentItem(found.front());
            }
        });
    });
    connect(_deleteCategory, &QPushButton::clicked, this, [this]() {
        const QString category = chosenCategory();
        if (category.isEmpty())
            return;
        modal::ask(this, QMessageBox::Question, "Delete Category",
            QString("Delete %1 and its brushes from the brush library? Documents that use them keep their "
                    "copies.")
                .arg(category),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::NoButton, [this, category](QMessageBox::StandardButton answer) {
                if (answer == QMessageBox::Yes)
                    _store.removeCategory(category);
            });
    });
    connect(import, &QPushButton::clicked, this, [this]() {
        file_pick::open(this, "Import Brushes from Document", QString(), "Documents (*.fla *.xfl);;All files (*)",
            [this](const QString& path) { importFrom(path); });
    });
    connect(_fromThis, &QPushButton::clicked, this, [this]() {
        const fla::Document* document = _hooks.document ? _hooks.document() : nullptr;
        const auto library = document ? fla::paintBrushesOf(*document) : nullptr;
        if (!library || library->definitions.empty())
        {
            _status->setText("This document has no paint brushes to add.");
            return;
        }
        const int added = _store.add(library->definitions);
        _status->setText(!_store.error().isEmpty() ? _store.error()
                : added > 0 ? QString("Added %1 brush%2 from this document.").arg(added).arg(added == 1 ? "" : "es")
                            : QString("The library has this document's brushes already."));
    });
    connect(&_store, &BrushLibraryStore::changed, this, &BrushLibraryDialog::refill);

    refill();
    resize(700, 520);
}

int BrushLibraryDialog::importFrom(const QString& path)
{
    QString error;
    const std::vector<fla::PaintBrushDefinition> definitions =
        _hooks.readDocument ? _hooks.readDocument(path, error) : std::vector<fla::PaintBrushDefinition>();
    if (definitions.empty())
    {
        _status->setText(error.isEmpty() ? QString("%1 has no paint brushes.").arg(QFileInfo(path).fileName()) : error);
        return 0;
    }
    const int added = _store.add(definitions);
    _status->setText(!_store.error().isEmpty() ? _store.error()
            : added > 0
            ? QString("Added %1 of the %2 brushes of %3.").arg(added).arg(definitions.size()).arg(QFileInfo(path).fileName())
            : QString("The library has the brushes of %1 already.").arg(QFileInfo(path).fileName()));
    return added;
}

void BrushLibraryDialog::chooseCategory(const QString& category)
{
    for (int i = 0; i < _categories->count(); ++i)
        if (_categories->item(i)->data(Qt::UserRole).toString() == category)
            _categories->setCurrentRow(i);
}

void BrushLibraryDialog::refill()
{
    const QString was = chosenCategory();
    _categories->blockSignals(true);
    _categories->clear();
    std::map<QString, int> counts;
    for (const fla::BrushLibraryEntry& entry : _store.entries())
        ++counts[QString::fromStdString(entry.category)];
    QListWidgetItem* all = new QListWidgetItem(QString("%1 (%2)").arg(kAll).arg(_store.entries().size()), _categories);
    all->setData(Qt::UserRole, QString());
    for (const QString& category : _store.categories())
    {
        QListWidgetItem* item = new QListWidgetItem(QString("%1 (%2)").arg(category).arg(counts[category]), _categories);
        item->setData(Qt::UserRole, category);
        if (category == was)
            _categories->setCurrentItem(item);
    }
    if (!_categories->currentItem())
        _categories->setCurrentRow(0);
    _categories->blockSignals(false);
    fillBrushes();
    updateButtons();
}

void BrushLibraryDialog::fillBrushes()
{
    const std::string was = chosenGuid();
    const QString category = chosenCategory();
    const QString search = _search->text().trimmed();
    _brushes->clear();
    const QColor ink = palette().color(QPalette::Text);
    for (const fla::BrushLibraryEntry& entry : _store.entries())
    {
        if (!category.isEmpty() && QString::fromStdString(entry.category) != category)
            continue;
        const QString name = QString::fromStdString(entry.definition.name);
        if (!search.isEmpty() && !name.contains(search, Qt::CaseInsensitive))
            continue;
        QListWidgetItem* item = new QListWidgetItem(QIcon(paintBrushPreview(entry.definition, QSize(132, 40), ink)),
            name, _brushes);
        item->setToolTip(QString("%1\n%2 brush, %3%4")
                             .arg(name)
                             .arg(entry.definition.pattern ? "Pattern" : "Art")
                             .arg(QString::fromStdString(entry.category))
                             .arg(entry.builtIn ? " (comes with Phoenix)" : ""));
        item->setData(Qt::UserRole, QString::fromStdString(entry.definition.guid()));
        if (entry.definition.guid() == was)
            _brushes->setCurrentItem(item);
    }
    if (!_brushes->currentItem() && _brushes->count() > 0)
        _brushes->setCurrentRow(0);
    updateButtons();
}

std::string BrushLibraryDialog::chosenGuid() const
{
    const QListWidgetItem* item = _brushes ? _brushes->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString().toStdString() : std::string();
}

QString BrushLibraryDialog::chosenCategory() const
{
    const QListWidgetItem* item = _categories ? _categories->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void BrushLibraryDialog::updateButtons()
{
    const fla::BrushLibraryEntry* entry = _store.find(chosenGuid());
    const bool own = entry && !entry->builtIn;
    const fla::Document* document = _hooks.document ? _hooks.document() : nullptr;
    _draw->setEnabled(entry && document);
    _add->setEnabled(entry && document);
    _rename->setEnabled(own);
    _move->setEnabled(own);
    _delete->setEnabled(own);
    const bool builtIn = entry && entry->builtIn;
    const QString kept = "Phoenix's own brushes come with it and stay as they are";
    _rename->setToolTip(builtIn ? kept : QString("Give this brush another name"));
    _move->setToolTip(builtIn ? kept : QString("File this brush under another category"));
    _delete->setToolTip(
        builtIn ? kept : QString("Delete this brush from the library; documents that use it keep their copy"));
    const QString category = chosenCategory();
    bool builtInCategory = false;
    for (const fla::BrushLibraryEntry& item : _store.entries())
        builtInCategory = builtInCategory || (item.builtIn && QString::fromStdString(item.category) == category);
    _renameCategory->setEnabled(!category.isEmpty() && !builtInCategory);
    _deleteCategory->setEnabled(!category.isEmpty() && !builtInCategory);
    _fromThis->setEnabled(document != nullptr);
}
