#include "stroke_style_dialog.h"

#include "stroke_effects.h"

#include "../data/solid_color.h"
#include "../data/stroke_style.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include "hot_text.h"
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

StrokeStyleDialog::StrokeStyleDialog(const fla::StrokeLook& look, double weight, const QColor& color, QWidget* parent)
    : QDialog(parent)
    , _look(look)
    , _weight(std::max(0.1, weight))
    , _color(color)
{
    setWindowTitle("Custom Stroke Style");
    QVBoxLayout* layout = new QVBoxLayout(this);

    _preview = new QLabel(this);
    _preview->setMinimumSize(320, 110);
    _preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(_preview);

    using Style = fla::StrokeStyle::Style;
    QFormLayout* numbers = new QFormLayout;
    switch (_look.kind)
    {
    case Style::Dashed:
    {
        HotDoubleSpinBox* dash = new HotDoubleSpinBox(this);
        dash->setRange(0.25, 300.0);
        dash->setSuffix(" pt");
        dash->setValue(_look.dash1);
        connect(dash, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dash1 = value;
            showPreview();
        });
        HotDoubleSpinBox* gap = new HotDoubleSpinBox(this);
        gap->setRange(0.25, 300.0);
        gap->setSuffix(" pt");
        gap->setValue(_look.dash2);
        connect(gap, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dash2 = value;
            showPreview();
        });
        numbers->addRow("Dash", dash);
        numbers->addRow("Gap", gap);
        layout->addLayout(numbers);
        break;
    }
    case Style::Dotted:
    {
        HotDoubleSpinBox* space = new HotDoubleSpinBox(this);
        space->setRange(0.0, 300.0);
        space->setSuffix(" pt");
        space->setValue(_look.dotSpace);
        connect(space, &HotDoubleSpinBox::valueChanged, this, [this](double value) {
            _look.dotSpace = value;
            showPreview();
        });
        numbers->addRow("Dot spacing", space);
        layout->addLayout(numbers);
        break;
    }
    case Style::Ragged:
        addChoice(layout, "Pattern", "pattern",
            {"solid", "simple", "random", "dotted", "random dotted", "triple dotted", "random triple dotted"},
            "simple");
        addChoice(layout, "Wave height", "waveHeight", {"flat", "wavy", "very wavy", "wild"}, "wavy");
        addChoice(layout, "Wave length", "waveLength", {"very short", "short", "medium", "long"}, "short");
        break;
    case Style::Stipple:
        addChoice(layout, "Dot size", "dotSize", {"tiny", "small", "medium", "large"}, "small");
        addChoice(layout, "Dot variation", "variation",
            {"one size", "small variation", "varied sizes", "random sizes"}, "varied sizes");
        addChoice(layout, "Density", "density", {"very dense", "dense", "sparse", "very sparse"}, "dense");
        break;
    case Style::Hatched:
        addChoice(layout, "Thickness", "hatchThickness", {"hairline", "thin", "medium", "thick"}, "thin");
        addChoice(layout, "Space", "space", {"very close", "close", "distant", "very distant"}, "distant");
        addChoice(layout, "Jiggle", "jiggle", {"none", "bounce", "loose", "wild"}, "none");
        addChoice(layout, "Rotate", "rotate", {"none", "slight", "medium", "free"}, "none");
        addChoice(layout, "Curve", "curve", {"straight", "slight curve", "medium curve", "very curved"}, "straight");
        addChoice(layout, "Length", "length", {"equal", "slight variation", "medium variation", "random"}, "equal");
        break;
    case Style::Solid:
    {
        QLabel* none = new QLabel("A solid stroke has no settings of its own beyond its weight, cap and join.", this);
        none->setWordWrap(true);
        layout->addWidget(none);
        break;
    }
    case Style::PaintBrush:
    {
        QLabel* none = new QLabel("A paint brush stroke draws its brush's art along the line; choose the brush "
                                  "under Brush in the Properties panel.", this);
        none->setWordWrap(true);
        layout->addWidget(none);
        break;
    }
    }

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    showPreview();
}

void StrokeStyleDialog::addChoice(QVBoxLayout* layout, const QString& label, const char* attribute,
    const QStringList& values, const QString& fallback)
{
    QFormLayout* form = new QFormLayout;
    QComboBox* box = new QComboBox(this);
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
    layout->addLayout(form);
}

void StrokeStyleDialog::showPreview()
{
    const QSize size(320, 110);
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A gentle curve across the box, stroked as the drawing would be.
    QPainterPath line;
    line.moveTo(20, 70);
    line.cubicTo(110, 10, 210, 100, 300, 40);

    fla::SolidStroke base(nullptr);
    base.weight = _weight;
    std::unique_ptr<fla::StrokeStyle> stroke(_look.applyTo(base, nullptr));
    const QBrush brush(_color);
    if (stroke && StrokeEffects::isProcedural(*stroke))
    {
        painter.fillPath(StrokeEffects::outline(line, *stroke), brush);
    }
    else
    {
        QPen pen(brush, _weight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (_look.kind == fla::StrokeStyle::Style::Dashed)
        {
            pen.setCapStyle(Qt::FlatCap);
            pen.setDashPattern({_look.dash1 / _weight, _look.dash2 / _weight});
        }
        else if (_look.kind == fla::StrokeStyle::Style::Dotted)
        {
            pen.setDashPattern({0.01, (_look.dotSpace + _weight) / _weight});
        }
        painter.strokePath(line, pen);
    }
    painter.end();
    _preview->setPixmap(pixmap);
}
