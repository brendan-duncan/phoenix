#include "swap_symbol_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

SwapSymbolDialog::SwapSymbolDialog(const QStringList& symbols, const QString& current, Preview preview,
    Duplicate duplicate, QWidget* parent)
    : QDialog(parent)
    , _previewOf(std::move(preview))
    , _duplicate(std::move(duplicate))
{
    setWindowTitle("Swap Symbol");

    _preview = new QLabel(this);
    _preview->setFixedSize(140, 140);
    _preview->setAlignment(Qt::AlignCenter);
    _preview->setFrameShape(QFrame::StyledPanel);

    _list = new QListWidget(this);
    _list->addItems(symbols);
    _list->setMinimumSize(260, 260);
    const QList<QListWidgetItem*> found = _list->findItems(current, Qt::MatchExactly);
    if (!found.isEmpty())
    {
        // The symbol the instance shows now is marked, as Animate dots it.
        QFont font = found.front()->font();
        font.setBold(true);
        found.front()->setFont(font);
        _list->setCurrentItem(found.front());
    }
    else if (_list->count() > 0)
    {
        _list->setCurrentRow(0);
    }
    connect(_list, &QListWidget::currentItemChanged, this, &SwapSymbolDialog::showPreview);
    connect(_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    QPushButton* duplicateButton = new QPushButton("Duplicate Symbol...", this);
    duplicateButton->setEnabled(static_cast<bool>(_duplicate));
    connect(duplicateButton, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = _list->currentItem();
        if (!item || !_duplicate)
            return;
        const QString copy = _duplicate(item->text());
        if (copy.isEmpty())
            return;
        QListWidgetItem* added = new QListWidgetItem(copy);
        _list->addItem(added);
        _list->setCurrentItem(added);
    });

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* side = new QVBoxLayout();
    side->addWidget(_preview);
    side->addWidget(duplicateButton);
    side->addStretch();

    QHBoxLayout* body = new QHBoxLayout();
    body->addLayout(side);
    body->addWidget(_list, 1);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(body);
    layout->addWidget(buttons);

    showPreview();
}

QString SwapSymbolDialog::chosen() const
{
    const QListWidgetItem* item = _list->currentItem();
    return item ? item->text() : QString();
}

void SwapSymbolDialog::showPreview()
{
    const QListWidgetItem* item = _list->currentItem();
    if (!item || !_previewOf)
    {
        _preview->clear();
        return;
    }
    _preview->setPixmap(QPixmap::fromImage(_previewOf(item->text(), _preview->size())));
}
