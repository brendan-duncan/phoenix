#include "find_replace_dialog.h"

#include "../data/bitmap.h"
#include "../data/document.h"

#include "../data/document.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog(std::function<fla::Document*()> document, QWidget* parent)
    : QDialog(parent)
    , _document(std::move(document))
{
    setWindowTitle("Find and Replace");
    QVBoxLayout* layout = new QVBoxLayout(this);

    QFormLayout* top = new QFormLayout();
    _kind = new QComboBox(this);
    _kind->addItems({"Text", "Font", "Color", "Symbol", "Sound", "Bitmap"});
    top->addRow("Search for:", _kind);
    layout->addLayout(top);

    _pages = new QStackedWidget(this);
    layout->addWidget(_pages);

    // Text.
    QWidget* textPage = new QWidget(_pages);
    QFormLayout* textForm = new QFormLayout(textPage);
    textForm->setContentsMargins(0, 0, 0, 0);
    _findText = new QLineEdit(textPage);
    _replaceText = new QLineEdit(textPage);
    _matchCase = new QCheckBox("Match case", textPage);
    _wholeWord = new QCheckBox("Whole word", textPage);
    textForm->addRow("Find:", _findText);
    textForm->addRow("Replace with:", _replaceText);
    QHBoxLayout* options = new QHBoxLayout();
    options->addWidget(_matchCase);
    options->addWidget(_wholeWord);
    options->addStretch(1);
    textForm->addRow("", options);
    _pages->addWidget(textPage);

    // Font: the faces the document uses, replaced by one of those or any
    // installed family.
    QWidget* fontPage = new QWidget(_pages);
    QFormLayout* fontForm = new QFormLayout(fontPage);
    fontForm->setContentsMargins(0, 0, 0, 0);
    _findFont = new QComboBox(fontPage);
    _findFont->setEditable(true);
    _replaceFont = new QComboBox(fontPage);
    _replaceFont->setEditable(true);
    fontForm->addRow("Find:", _findFont);
    fontForm->addRow("Replace with:", _replaceFont);
    _pages->addWidget(fontPage);

    // Color.
    QWidget* colorPage = new QWidget(_pages);
    QFormLayout* colorForm = new QFormLayout(colorPage);
    colorForm->setContentsMargins(0, 0, 0, 0);
    _findColorButton = new QPushButton(colorPage);
    _replaceColorButton = new QPushButton(colorPage);
    colorForm->addRow("Find:", _findColorButton);
    colorForm->addRow("Replace with:", _replaceColorButton);
    colorForm->addRow("", new QLabel("Fills, strokes, gradient colors and text; alpha is kept.", colorPage));
    showColor(_findColorButton, _findColor);
    showColor(_replaceColorButton, _replaceColor);
    connect(_findColorButton, &QPushButton::clicked, this, [this]() { pickColor(_findColorButton, _findColor); });
    connect(_replaceColorButton, &QPushButton::clicked, this,
        [this]() { pickColor(_replaceColorButton, _replaceColor); });
    _pages->addWidget(colorPage);

    // Symbol: instances of one swapped for another.
    QWidget* symbolPage = new QWidget(_pages);
    QFormLayout* symbolForm = new QFormLayout(symbolPage);
    symbolForm->setContentsMargins(0, 0, 0, 0);
    _findSymbol = new QComboBox(symbolPage);
    _replaceSymbol = new QComboBox(symbolPage);
    symbolForm->addRow("Find:", _findSymbol);
    symbolForm->addRow("Replace with:", _replaceSymbol);
    _pages->addWidget(symbolPage);

    // Sound and bitmap: what keyframes play, and what instances and fills
    // show, swapped for another library item.
    const auto namePage = [this](QComboBox*& find, QComboBox*& replace, const QString& note) {
        QWidget* page = new QWidget(_pages);
        QFormLayout* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        find = new QComboBox(page);
        replace = new QComboBox(page);
        form->addRow("Find:", find);
        form->addRow("Replace with:", replace);
        form->addRow("", new QLabel(note, page));
        _pages->addWidget(page);
    };
    namePage(_findSound, _replaceSound, "The keyframes that play it.");
    namePage(_findBitmap, _replaceBitmap, "Its instances, and fills that paint with it.");

    connect(_kind, &QComboBox::currentIndexChanged, this, [this](int index) {
        _pages->setCurrentIndex(index);
        _results->clear();
        _matches.clear();
        _status->clear();
    });

    _results = new QTreeWidget(this);
    _results->setColumnCount(2);
    _results->setHeaderLabels({"Location", "Found"});
    _results->setRootIsDecorated(false);
    _results->header()->setStretchLastSection(false);
    _results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _results->setMinimumHeight(160);
    layout->addWidget(_results, 1);
    connect(_results, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const int row = _results->indexOfTopLevelItem(item);
        if (row >= 0 && row < static_cast<int>(_matches.size()))
            emit goToRequested(_matches[static_cast<size_t>(row)]);
    });

    _status = new QLabel(this);
    layout->addWidget(_status);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    QPushButton* find = buttons->addButton("Find All", QDialogButtonBox::ActionRole);
    QPushButton* replace = buttons->addButton("Replace All", QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    find->setDefault(true);
    layout->addWidget(buttons);
    connect(find, &QPushButton::clicked, this, &FindReplaceDialog::findAll);
    connect(replace, &QPushButton::clicked, this, [this]() {
        emit replaceRequested(query());
        // What was found has changed.
        findAll();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);

    resize(460, 440);
}

void FindReplaceDialog::refresh()
{
    fla::Document* document = _document ? _document() : nullptr;
    const QString font = _findFont->currentText();
    const QString replacementFont = _replaceFont->currentText();
    const QString symbol = _findSymbol->currentText();
    const QString replacementSymbol = _replaceSymbol->currentText();

    _findFont->clear();
    _replaceFont->clear();
    _findSymbol->clear();
    _replaceSymbol->clear();
    for (QComboBox* box : {_findSound, _replaceSound, _findBitmap, _replaceBitmap})
        box->clear();
    if (document)
    {
        QStringList sounds;
        for (const fla::Document::SoundInfo& sound : document->sounds)
            sounds << QString::fromStdString(sound.name);
        sounds.sort(Qt::CaseInsensitive);
        _findSound->addItems(sounds);
        _replaceSound->addItems(sounds);
        QStringList bitmaps;
        for (const fla::Resource* resource : document->resources)
            if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
                bitmaps << QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name);
        bitmaps.sort(Qt::CaseInsensitive);
        _findBitmap->addItems(bitmaps);
        _replaceBitmap->addItems(bitmaps);
        for (const std::string& face : fla::find::fontsUsed(*document))
        {
            _findFont->addItem(QString::fromStdString(face));
            _replaceFont->addItem(QString::fromStdString(face));
        }
        if (document->symbolList)
        {
            QStringList names;
            for (const fla::Symbol* item : document->symbolList->symbols)
                if (item)
                    names << QString::fromStdString(item->name);
            names.sort(Qt::CaseInsensitive);
            _findSymbol->addItems(names);
            _replaceSymbol->addItems(names);
        }
    }
    if (_replaceFont->count())
        _replaceFont->insertSeparator(_replaceFont->count());
    _replaceFont->addItems(QFontDatabase::families());
    if (!font.isEmpty())
        _findFont->setCurrentText(font);
    if (!replacementFont.isEmpty())
        _replaceFont->setCurrentText(replacementFont);
    if (_findSymbol->findText(symbol) >= 0)
        _findSymbol->setCurrentText(symbol);
    if (_replaceSymbol->findText(replacementSymbol) >= 0)
        _replaceSymbol->setCurrentText(replacementSymbol);

    _results->clear();
    _matches.clear();
    _status->clear();
}

void FindReplaceDialog::setFindColor(const QColor& color)
{
    _findColor = color;
    showColor(_findColorButton, _findColor);
}

fla::find::Query FindReplaceDialog::query() const
{
    fla::find::Query query;
    query.kind = static_cast<fla::find::Kind>(_kind->currentIndex());
    switch (query.kind)
    {
    case fla::find::Kind::Text:
        query.find = _findText->text().toStdString();
        query.replace = _replaceText->text().toStdString();
        query.matchCase = _matchCase->isChecked();
        query.wholeWord = _wholeWord->isChecked();
        break;
    case fla::find::Kind::Font:
        query.find = _findFont->currentText().trimmed().toStdString();
        query.replace = _replaceFont->currentText().trimmed().toStdString();
        break;
    case fla::find::Kind::Color:
        query.color[0] = static_cast<uint8_t>(_findColor.red());
        query.color[1] = static_cast<uint8_t>(_findColor.green());
        query.color[2] = static_cast<uint8_t>(_findColor.blue());
        query.replaceColor[0] = static_cast<uint8_t>(_replaceColor.red());
        query.replaceColor[1] = static_cast<uint8_t>(_replaceColor.green());
        query.replaceColor[2] = static_cast<uint8_t>(_replaceColor.blue());
        break;
    case fla::find::Kind::Symbol:
        query.find = _findSymbol->currentText().toStdString();
        query.replace = _replaceSymbol->currentText().toStdString();
        break;
    case fla::find::Kind::Sound:
        query.find = _findSound->currentText().toStdString();
        query.replace = _replaceSound->currentText().toStdString();
        break;
    case fla::find::Kind::Bitmap:
        query.find = _findBitmap->currentText().toStdString();
        query.replace = _replaceBitmap->currentText().toStdString();
        break;
    }
    return query;
}

void FindReplaceDialog::findAll()
{
    _results->clear();
    _matches.clear();
    fla::Document* document = _document ? _document() : nullptr;
    if (!document)
    {
        _status->setText("No document is open.");
        return;
    }
    _matches = fla::find::findAll(*document, query());
    int total = 0;
    for (const fla::find::Match& match : _matches)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(_results);
        item->setText(0, QString::fromStdString(match.where));
        item->setText(1, QString::number(match.count));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        total += match.count;
    }
    _status->setText(_matches.empty() ? QString("Nothing found.")
                                      : QString("Found %1 in %2 object%3; click one to go to it.")
                                            .arg(total)
                                            .arg(_matches.size())
                                            .arg(_matches.size() == 1 ? "" : "s"));
}

void FindReplaceDialog::pickColor(QPushButton* button, QColor& color)
{
    const QColor chosen = QColorDialog::getColor(color, this, "Color");
    if (!chosen.isValid())
        return;
    color = chosen;
    showColor(button, color);
}

void FindReplaceDialog::showColor(QPushButton* button, const QColor& color)
{
    QPixmap swatch(28, 14);
    swatch.fill(color);
    QPainter painter(&swatch);
    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    button->setIcon(QIcon(swatch));
    button->setIconSize(swatch.size());
    button->setText(color.name().toUpper());
}
