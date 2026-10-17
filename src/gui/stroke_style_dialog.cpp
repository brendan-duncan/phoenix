#include "stroke_style_dialog.h"

#include "brush_library_store.h"
#include "stroke_effects.h"

#include "../data/document.h"
#include "../data/solid_color.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include "hot_text.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <memory>

namespace {

using Style = fla::StrokeStyle::Style;

/// The kinds the dialog offers, in its menu's order: the stroke styles,
/// hairline counted as one, then a graphic symbol drawn along the stroke.
enum Kind
{
    SolidKind,
    HairlineKind,
    DashedKind,
    DottedKind,
    RaggedKind,
    StippleKind,
    HatchedKind,
    PaintBrushKind,
    SymbolBrushKind
};

Kind kindOf(const fla::StrokeLook& look)
{
    if (!look.brush.empty())
        return SymbolBrushKind;
    switch (look.kind)
    {
    case Style::Solid: return look.hairline ? HairlineKind : SolidKind;
    case Style::Dashed: return DashedKind;
    case Style::Dotted: return DottedKind;
    case Style::Ragged: return RaggedKind;
    case Style::Stipple: return StippleKind;
    case Style::Hatched: return HatchedKind;
    case Style::PaintBrush: return PaintBrushKind;
    }
    return SolidKind;
}

/// How the dialog names a paint brush in its menu: a guid, or for a brush
/// with none, "#" and its index in the document.
QString keyOf(const fla::PaintBrushDefinition& definition)
{
    const std::string guid = definition.guid();
    return guid.empty() ? QString("#%1").arg(definition.index) : QString::fromStdString(guid);
}

/// A menu entry that only heads the ones after it.
void addHeading(QComboBox* box, const QString& text)
{
    if (box->count() > 0)
        box->insertSeparator(box->count());
    box->addItem(text);
    if (auto* model = qobject_cast<QStandardItemModel*>(box->model()))
        model->item(box->count() - 1)->setEnabled(false);
}

} // namespace

StrokeStyleDialog::StrokeStyleDialog(const fla::StrokeLook& look, double weight, const QColor& color,
    const fla::Document* document, QWidget* parent)
    : QDialog(parent)
    , _look(look)
    , _weight(std::max(0.1, weight))
    , _color(color)
    , _document(document)
{
    setWindowTitle("Custom Stroke Style");
    QVBoxLayout* layout = new QVBoxLayout(this);

    _preview = new QLabel(this);
    _preview->setMinimumSize(360, 120);
    _preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(_preview);

    // The kind, and what it is; each kind's settings below.
    QFormLayout* top = new QFormLayout;
    _kind = new QComboBox(this);
    _kind->addItems({"Solid", "Hairline", "Dashed", "Dotted", "Ragged", "Stipple", "Hatched", "Paint brush",
        "Symbol brush"});
    _kind->setToolTip("What the stroke is drawn as");
    _kind->setCurrentIndex(kindOf(_look));
    top->addRow("Style", _kind);
    HotDoubleSpinBox* weightBox = new HotDoubleSpinBox(this);
    weightBox->setRange(0.1, 200.0);
    weightBox->setDecimals(2);
    weightBox->setValue(_weight);
    weightBox->setToolTip("The stroke's weight, in pixels: a paint brush's art is this tall across the stroke");
    connect(weightBox, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
        _weight = std::max(0.1, value);
        showPreview();
    });
    top->addRow("Weight", weightBox);
    layout->addLayout(top);
    _about = new QLabel(this);
    _about->setWordWrap(true);
    layout->addWidget(_about);

    QWidget* holder = new QWidget(this);
    QVBoxLayout* holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(holder);
    connect(_kind, &QComboBox::currentIndexChanged, this, [this, holderLayout](int index) {
        const Kind kind = static_cast<Kind>(index);
        const Style was = _look.kind;
        _look.brush.clear();
        _look.hairline = kind == HairlineKind;
        switch (kind)
        {
        case SolidKind:
        case HairlineKind: _look.kind = Style::Solid; break;
        case DashedKind: _look.kind = Style::Dashed; break;
        case DottedKind: _look.kind = Style::Dotted; break;
        case RaggedKind: _look.kind = Style::Ragged; break;
        case StippleKind: _look.kind = Style::Stipple; break;
        case HatchedKind: _look.kind = Style::Hatched; break;
        case PaintBrushKind: _look.kind = Style::PaintBrush; break;
        case SymbolBrushKind:
            // A symbol drawn along a plain stroke.
            _look.kind = Style::Solid;
            break;
        }
        // Another kind starts with its own settings at their defaults.
        if (_look.kind != was)
            _look.styleAttributes.clear();
        if (_look.kind != Style::PaintBrush)
        {
            _look.paintBrush = 0;
            _look.paintBrushGuid.clear();
        }
        if (kind == SymbolBrushKind && _document && _document->symbolList)
        {
            for (const fla::Symbol* symbol : _document->symbolList->symbols)
                if (symbol && symbol->symbolType == fla::SymbolType::Graphic)
                {
                    _look.brush = symbol->name;
                    break;
                }
        }
        delete _settings;
        _settings = new QWidget(this);
        holderLayout->addWidget(_settings);
        rebuild();
    });

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    _settings = new QWidget(this);
    holderLayout->addWidget(_settings);
    rebuild();
}

QString StrokeStyleDialog::kindDescription() const
{
    switch (kindOf(_look))
    {
    case SolidKind: return "A plain line. Its cap, join and width profile are set in the Properties panel.";
    case HairlineKind: return "A line one screen pixel wide at any zoom, whatever its weight.";
    case DashedKind: return "Dashes with gaps between them.";
    case DottedKind: return "Round dots, spaced along the line.";
    case RaggedKind: return "A rough, wavering line.";
    case StippleKind: return "A spray of dots along the line.";
    case HatchedKind: return "Short hatching lines across the stroke.";
    case PaintBrushKind:
        return "A brush's art laid along the line: stretched from end to end (an art brush) or repeated a tile at "
               "a time (a pattern brush). Choose one of the document's brushes or the brush library's.";
    case SymbolBrushKind:
        return "A graphic symbol of the library drawn along the line, stretched or repeated.";
    }
    return QString();
}

void StrokeStyleDialog::rebuild()
{
    _about->setText(kindDescription());
    QVBoxLayout* layout = new QVBoxLayout(_settings);
    layout->setContentsMargins(0, 0, 0, 0);
    // The kind's settings in one form, so their labels line up.
    QFormLayout* numbers = new QFormLayout;
    layout->addLayout(numbers);
    switch (kindOf(_look))
    {
    case DashedKind:
    {
        HotDoubleSpinBox* dash = new HotDoubleSpinBox(_settings);
        dash->setRange(0.25, 300.0);
        dash->setSuffix(" pt");
        dash->setValue(_look.dash1);
        connect(dash, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dash1 = value;
            showPreview();
        });
        HotDoubleSpinBox* gap = new HotDoubleSpinBox(_settings);
        gap->setRange(0.25, 300.0);
        gap->setSuffix(" pt");
        gap->setValue(_look.dash2);
        connect(gap, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dash2 = value;
            showPreview();
        });
        numbers->addRow("Dash", dash);
        numbers->addRow("Gap", gap);
        break;
    }
    case DottedKind:
    {
        HotDoubleSpinBox* space = new HotDoubleSpinBox(_settings);
        space->setRange(0.0, 300.0);
        space->setSuffix(" pt");
        space->setValue(_look.dotSpace);
        connect(space, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dotSpace = value;
            showPreview();
        });
        numbers->addRow("Dot spacing", space);
        break;
    }
    case RaggedKind:
        addChoice(numbers, "Pattern", "pattern",
            {"solid", "simple", "random", "dotted", "random dotted", "triple dotted", "random triple dotted"},
            "simple");
        addChoice(numbers, "Wave height", "waveHeight", {"flat", "wavy", "very wavy", "wild"}, "wavy");
        addChoice(numbers, "Wave length", "waveLength", {"very short", "short", "medium", "long"}, "short");
        break;
    case StippleKind:
        addChoice(numbers, "Dot size", "dotSize", {"tiny", "small", "medium", "large"}, "small");
        addChoice(numbers, "Dot variation", "variation",
            {"one size", "small variation", "varied sizes", "random sizes"}, "varied sizes");
        addChoice(numbers, "Density", "density", {"very dense", "dense", "sparse", "very sparse"}, "dense");
        break;
    case HatchedKind:
        addChoice(numbers, "Thickness", "hatchThickness", {"hairline", "thin", "medium", "thick"}, "thin");
        addChoice(numbers, "Space", "space", {"very close", "close", "distant", "very distant"}, "distant");
        addChoice(numbers, "Jiggle", "jiggle", {"none", "bounce", "loose", "wild"}, "none");
        addChoice(numbers, "Rotate", "rotate", {"none", "slight", "medium", "free"}, "none");
        addChoice(numbers, "Curve", "curve", {"straight", "slight curve", "medium curve", "very curved"}, "straight");
        addChoice(numbers, "Length", "length", {"equal", "slight variation", "medium variation", "random"}, "equal");
        break;
    case PaintBrushKind: addPaintBrushSettings(layout); break;
    case SymbolBrushKind: addSymbolBrushSettings(layout); break;
    case SolidKind:
    case HairlineKind: break;
    }
    showPreview();
    adjustSize();
}

void StrokeStyleDialog::addChoice(QFormLayout* form, const QString& label, const char* attribute,
    const QStringList& values, const QString& fallback)
{
    QComboBox* box = new QComboBox(_settings);
    for (const QString& value : values)
    {
        // Shown capitalized, kept as the file spells it.
        QString shown = value;
        shown[0] = shown[0].toUpper();
        box->addItem(shown, value);
    }
    const auto found = _look.styleAttributes.find(attribute);
    const QString current = found != _look.styleAttributes.end() ? QString::fromStdString(found->second) : fallback;
    box->setCurrentIndex(std::max<qsizetype>(0, values.indexOf(current)));
    const std::string name = attribute;
    connect(box, &QComboBox::currentIndexChanged, this, [this, box, name](int) {
        _look.styleAttributes[name] = box->currentData().toString().toStdString();
        showPreview();
    });
    form->addRow(label, box);
}

void StrokeStyleDialog::addPaintBrushSettings(QVBoxLayout* layout)
{
    // The brushes: the document's, then the brush library's that the
    // document has not got, by category.
    QComboBox* brushes = new QComboBox(_settings);
    brushes->setIconSize(QSize(72, 20));
    brushes->setToolTip("The brush whose art is drawn along the stroke");
    const QColor ink = palette().color(QPalette::Text);
    std::vector<std::string> own;
    const auto library = _document ? fla::paintBrushesOf(*_document) : nullptr;
    if (library && !library->definitions.empty())
    {
        addHeading(brushes, "In this document");
        for (const fla::PaintBrushDefinition& definition : library->definitions)
        {
            brushes->addItem(QIcon(paintBrushPreview(definition, QSize(72, 20), ink)),
                QString::fromStdString(definition.name), keyOf(definition));
            own.push_back(definition.guid());
        }
    }
    QString category;
    for (const fla::BrushLibraryEntry& entry : BrushLibraryStore::shared().entries())
    {
        if (std::find(own.begin(), own.end(), entry.definition.guid()) != own.end())
            continue;
        if (QString::fromStdString(entry.category) != category)
        {
            category = QString::fromStdString(entry.category);
            addHeading(brushes, QString("Brush library: %1").arg(category));
        }
        brushes->addItem(QIcon(paintBrushPreview(entry.definition, QSize(72, 20), ink)),
            QString::fromStdString(entry.definition.name), keyOf(entry.definition));
    }

    // Which is chosen: by guid, or by index in the document.
    QString key = QString::fromStdString(_look.paintBrushGuid);
    if (key.isEmpty() && library)
        if (const fla::PaintBrushDefinition* definition = library->find(_look.paintBrush))
            key = keyOf(*definition);
    int at = key.isEmpty() ? -1 : brushes->findData(key);
    if (at < 0)
        for (int i = 0; i < brushes->count() && at < 0; ++i)
            if (brushes->itemData(i).isValid())
                at = i;
    QFormLayout* form = new QFormLayout;
    form->addRow("Brush", brushes);
    layout->addLayout(form);
    QLabel* where = new QLabel(_settings);
    where->setWordWrap(true);
    layout->addWidget(where);

    // The brush's own options, built anew for each brush chosen.
    QGroupBox* options = new QGroupBox("Brush options", _settings);
    QVBoxLayout* optionsLayout = new QVBoxLayout(options);
    QLabel* note = new QLabel(options);
    note->setWordWrap(true);
    optionsLayout->addWidget(note);
    QWidget* fields = new QWidget(options);
    optionsLayout->addWidget(fields);
    QCheckBox* asNew = new QCheckBox(options);
    asNew->setToolTip("Leave the brush as it is for the strokes already drawn with it, and draw these strokes with "
                      "a copy of it that has the new options");
    optionsLayout->addWidget(asNew);
    layout->addWidget(options);
    connect(asNew, &QCheckBox::toggled, this, [this](bool on) { _asNew = on; });

    const auto showBrush = [this, where, note, fields, asNew, options, library]() {
        delete fields->layout();
        for (QWidget* child : fields->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            delete child;
        const std::string guid = _brush.guid();
        const bool inDocument = library && std::any_of(library->definitions.begin(), library->definitions.end(),
            [&](const fla::PaintBrushDefinition& definition) { return keyOf(definition) == keyOf(_brush); });
        options->setEnabled(!_brush.json.empty());
        if (_brush.json.empty())
        {
            where->setText("There are no brushes to choose from: add some in Window > Brush Library.");
            note->clear();
            asNew->setText(QString());
            return;
        }
        const QString name = QString::fromStdString(_brush.name);
        int uses = 0;
        if (inDocument && _document)
        {
            const std::map<int, int> counts = fla::paintBrushUses(*_document);
            const auto found = counts.find(_brush.index);
            uses = found != counts.end() ? found->second : 0;
        }
        where->setText(inDocument
                ? QString("%1 brush in this document, drawing %2 stroke%3.")
                      .arg(_brush.pattern ? "A pattern" : "An art")
                      .arg(uses)
                      .arg(uses == 1 ? "" : "s")
                : QString("%1 brush from the brush library: it is copied into the document when you click OK.")
                      .arg(_brush.pattern ? "A pattern" : "An art"));
        note->setText(inDocument
                ? QString("These belong to the brush: changing them changes every stroke drawn with “%1”.")
                      .arg(name)
                : QString("These belong to the brush: the document's copy of “%1” takes them.").arg(name));
        asNew->setText(QString("Keep the changes for these strokes only, as a new brush “%1 copy”").arg(name));
        asNew->setChecked(_asNew);
        asNew->setEnabled(_brushEdited);

        QFormLayout* grid = new QFormLayout(fields);
        grid->setContentsMargins(0, 0, 0, 0);
        for (const fla::PaintBrushOption& option : fla::paintBrushOptions(_brush.pattern))
        {
            const QString value = QString::fromStdString(fla::paintBrushOptionValue(_brush, option));
            const std::string key = option.key;
            QWidget* field = nullptr;
            switch (option.kind)
            {
            case fla::PaintBrushOption::Kind::Choice:
            {
                QComboBox* box = new QComboBox(fields);
                for (const auto& [spelled, shown] : option.choices)
                    box->addItem(QString::fromStdString(shown), QString::fromStdString(spelled));
                box->setCurrentIndex(std::max(0, box->findData(value)));
                connect(box, &QComboBox::currentIndexChanged, this, [this, box, key, asNew](int) {
                    const int index = _brush.index;
                    _brush = fla::withParam(_brush, key, box->currentData().toString().toStdString());
                    _brush.index = index;
                    _art.reset();
                    _brushEdited = true;
                    asNew->setEnabled(true);
                    showPreview();
                });
                field = box;
                break;
            }
            case fla::PaintBrushOption::Kind::Flag:
            {
                QCheckBox* box = new QCheckBox(fields);
                const std::string off = option.choices.size() > 0 ? option.choices[0].first : "0";
                const std::string on = option.choices.size() > 1 ? option.choices[1].first : "1";
                box->setChecked(value.toStdString() == on || value == "true" || value == "1");
                connect(box, &QCheckBox::toggled, this, [this, key, on, off, asNew](bool checked) {
                    const int index = _brush.index;
                    _brush = fla::withParam(_brush, key, checked ? on : off);
                    _brush.index = index;
                    _art.reset();
                    _brushEdited = true;
                    asNew->setEnabled(true);
                    showPreview();
                });
                field = box;
                break;
            }
            case fla::PaintBrushOption::Kind::Percent:
            {
                HotDoubleSpinBox* box = new HotDoubleSpinBox(fields);
                box->setRange(0.0, 1000.0);
                box->setDecimals(option.decimals);
                box->setSuffix(" %");
                box->setValue(value.toDouble());
                const int decimals = option.decimals;
                connect(box, &HotDoubleSpinBox::valueChanged, this, [this, key, decimals, asNew](double number) {
                    const int index = _brush.index;
                    _brush = fla::withParam(_brush, key, QString::number(number, 'f', decimals).toStdString());
                    _brush.index = index;
                    _art.reset();
                    _brushEdited = true;
                    asNew->setEnabled(true);
                    showPreview();
                });
                field = box;
                break;
            }
            }
            field->setToolTip(QString::fromStdString(option.tip));
            grid->addRow(QString::fromStdString(option.label), field);
        }
    };
    connect(brushes, &QComboBox::currentIndexChanged, this, [this, brushes, showBrush](int index) {
        choosePaintBrush(brushes->itemData(index).toString());
        showBrush();
        showPreview();
        adjustSize();
    });
    if (at >= 0)
    {
        QSignalBlocker block(brushes);
        brushes->setCurrentIndex(at);
        choosePaintBrush(brushes->itemData(at).toString());
    }
    showBrush();
}

void StrokeStyleDialog::choosePaintBrush(const QString& key)
{
    _brush = fla::PaintBrushDefinition();
    _art.reset();
    _brushEdited = false;
    _asNew = false;
    const auto library = _document ? fla::paintBrushesOf(*_document) : nullptr;
    if (key.startsWith('#'))
    {
        if (const fla::PaintBrushDefinition* definition = library ? library->find(key.mid(1).toInt()) : nullptr)
            _brush = *definition;
    }
    else if (!paintBrushNamed(_document, key.toStdString(), _brush))
    {
        return;
    }
    // In the document by index; from the library, by guid until it is
    // copied in.
    const bool inDocument = library && std::any_of(library->definitions.begin(), library->definitions.end(),
        [&](const fla::PaintBrushDefinition& definition) { return keyOf(definition) == keyOf(_brush); });
    _look.paintBrush = inDocument ? _brush.index : 0;
    _look.paintBrushGuid = _brush.guid();
}

void StrokeStyleDialog::addSymbolBrushSettings(QVBoxLayout* layout)
{
    QFormLayout* form = new QFormLayout;
    QComboBox* symbols = new QComboBox(_settings);
    symbols->setToolTip("A graphic symbol of the library, drawn along the stroke instead of a line");
    QStringList names;
    if (_document && _document->symbolList)
        for (const fla::Symbol* symbol : _document->symbolList->symbols)
            if (symbol && symbol->symbolType == fla::SymbolType::Graphic)
                names << QString::fromStdString(symbol->name);
    names.sort(Qt::CaseInsensitive);
    if (!_look.brush.empty() && !names.contains(QString::fromStdString(_look.brush)))
        names << QString::fromStdString(_look.brush);
    symbols->addItems(names);
    symbols->setCurrentIndex(std::max<qsizetype>(0, names.indexOf(QString::fromStdString(_look.brush))));
    connect(symbols, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        _look.brush = name.toStdString();
        showPreview();
    });
    QComboBox* mode = new QComboBox(_settings);
    mode->addItems({"Stretch", "Repeat"});
    mode->setToolTip("Stretch the symbol from end to end (an art brush), or repeat it along the stroke (a pattern "
                     "brush)");
    mode->setCurrentIndex(_look.brushPattern ? 1 : 0);
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _look.brushPattern = index == 1;
        showPreview();
    });
    form->addRow("Symbol", symbols);
    form->addRow("Drawn", mode);
    layout->addLayout(form);
    if (names.isEmpty())
    {
        QLabel* none = new QLabel("The library has no graphic symbols to draw with.", _settings);
        none->setWordWrap(true);
        layout->addWidget(none);
    }
}

void StrokeStyleDialog::showPreview()
{
    const QSize size(360, 120);
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A gentle curve across the box, stroked as the drawing would be.
    QPainterPath line;
    line.moveTo(24, 78);
    line.cubicTo(124, 12, 236, 110, 336, 44);

    // At most a third of the box tall, so a heavy stroke still shows whole.
    const double weight = std::min(_weight, 40.0);
    if (kindOf(_look) == PaintBrushKind)
    {
        if (!_art && !_brush.json.empty())
            _art = std::make_shared<const fla::PaintBrushArt>(fla::paintBrushArt(_brush));
        if (_art)
        {
            painter.setPen(Qt::NoPen);
            for (const auto& [area, shade] : layPaintBrushPath(_brush, *_art, line, weight, _color, _look.widthProfile))
                painter.fillPath(area, shade);
        }
        painter.end();
        _preview->setPixmap(pixmap);
        return;
    }
    if (kindOf(_look) == SymbolBrushKind)
    {
        // The symbol is drawn on the stage; here, where it goes.
        painter.strokePath(line, QPen(QColor(0, 0, 0, 60), weight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setPen(QColor(90, 90, 90));
        painter.drawText(QRect(0, 0, size.width(), 22), Qt::AlignCenter,
            _look.brush.empty() ? QString("No symbol chosen")
                                : QString("“%1” %2 along the stroke")
                                      .arg(QString::fromStdString(_look.brush))
                                      .arg(_look.brushPattern ? "repeated" : "stretched"));
        painter.end();
        _preview->setPixmap(pixmap);
        return;
    }

    fla::SolidStroke base(nullptr);
    base.weight = weight;
    std::unique_ptr<fla::StrokeStyle> stroke(_look.applyTo(base, nullptr));
    const QBrush brush(_color);
    if (stroke && StrokeEffects::isProcedural(*stroke))
    {
        painter.fillPath(StrokeEffects::outline(line, *stroke), brush);
    }
    else
    {
        QPen pen(brush, _look.hairline ? 1.0 : weight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (_look.kind == Style::Dashed)
        {
            pen.setCapStyle(Qt::FlatCap);
            pen.setDashPattern({_look.dash1 / weight, _look.dash2 / weight});
        }
        else if (_look.kind == Style::Dotted)
        {
            pen.setDashPattern({0.01, (_look.dotSpace + weight) / weight});
        }
        painter.strokePath(line, pen);
    }
    painter.end();
    _preview->setPixmap(pixmap);
}
