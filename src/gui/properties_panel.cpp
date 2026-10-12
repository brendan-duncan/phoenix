#include "properties_panel.h"

#include "color_panel.h"
#include "hot_text.h"
#include "phoenix_view.h"
#include "player.h"
#include "tool.h"

#include "../data/bitmap_instance.h"
#include "../data/document.h"
#include "../data/filter.h"
#include "../data/fla_document.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/rectangle_primitive.h"
#include "../data/oval_primitive.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/stroke_style.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/drawing_style.h"
#include "../edit/edit_context.h"
#include "../edit/element_clone.h"
#include "../edit/element_commands.h"
#include "../edit/frame_commands.h"
#include "../edit/frame_sound.h"
#include "../edit/property_command.h"
#include "../edit/selection.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace {

QTransform toQTransform(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

fla::Transform fromQTransform(const QTransform& q)
{
    return fla::Transform(q.m11(), q.m12(), q.m21(), q.m22(), q.dx(), q.dy());
}

QIcon swatchIcon(const uint8_t rgba[4])
{
    return PropertiesPanel::swatchIcon(rgba);
}

} // namespace

QIcon PropertiesPanel::swatchIcon(const uint8_t rgba[4])
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    // A checker behind the swatch so a transparent color reads as transparent
    // rather than as black.
    painter.fillRect(0, 0, 20, 20, QColor(120, 120, 120));
    painter.fillRect(0, 0, 10, 10, QColor(160, 160, 160));
    painter.fillRect(10, 10, 10, 10, QColor(160, 160, 160));
    painter.fillRect(1, 1, 18, 18, QColor(rgba[0], rgba[1], rgba[2], rgba[3]));
    painter.setPen(QColor(30, 30, 30));
    painter.drawRect(0, 0, 19, 19);

    return QIcon(pixmap);
}

namespace {

void applyColor(const QColor& color, uint8_t rgba[4])
{
    rgba[0] = static_cast<uint8_t>(color.red());
    rgba[1] = static_cast<uint8_t>(color.green());
    rgba[2] = static_cast<uint8_t>(color.blue());
    rgba[3] = static_cast<uint8_t>(color.alpha());
}

QColor toQColor(const uint8_t rgba[4])
{
    return QColor(rgba[0], rgba[1], rgba[2], rgba[3]);
}

/// A small glyph for the Object tab's header: what kind of thing is
/// selected, drawn rather than loaded so it matches the theme.
QPixmap objectGlyph(const QString& kind, const QPalette& palette)
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = palette.color(QPalette::Text);
    painter.setPen(QPen(ink, 1.4));

    if (kind == "shape")
    {
        QPainterPath blob;
        blob.moveTo(4, 14);
        blob.cubicTo(2, 6, 10, 2, 14, 6);
        blob.cubicTo(20, 10, 18, 18, 11, 18);
        blob.cubicTo(7, 18, 5, 17, 4, 14);
        painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 70));
        painter.drawPath(blob);
    }
    else if (kind == "symbol")
    {
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(3.5, 3.5, 15, 15));
        painter.setBrush(ink);
        painter.drawEllipse(QRectF(8, 8, 6, 6));
    }
    else if (kind == "text")
    {
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSizeF(13.0);
        painter.setFont(font);
        painter.drawText(QRectF(0, 0, 22, 22), Qt::AlignCenter, "T");
    }
    else if (kind == "group")
    {
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(3.5, 6.5, 9, 9));
        painter.drawRect(QRectF(9.5, 3.5, 9, 9));
    }
    else if (kind == "bitmap")
    {
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if ((x + y) % 2 == 0)
                    painter.fillRect(3 + x * 4, 3 + y * 4, 4, 4, ink);
            }
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(2.5, 2.5, 17, 17));
    }
    else
    {
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(3.5, 3.5, 15, 15));
        painter.drawRect(QRectF(7.5, 7.5, 7, 7));
    }
    return pixmap;
}

/// Animate's lock-aspect chain: two links, joined when locked.
QIcon chainIcon(bool linked, const QPalette& palette)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = linked ? palette.color(QPalette::Text) : palette.color(QPalette::Disabled, QPalette::Text);
    painter.setPen(QPen(ink, 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(6, 1.5, 6, 7), 3, 3);
    painter.drawRoundedRect(QRectF(6, 9.5, 6, 7), 3, 3);
    if (linked)
        painter.drawLine(QPointF(9, 6), QPointF(9, 12));
    return QIcon(pixmap);
}

const char* filterName(fla::Filter::Type type)
{
    switch (type)
    {
    case fla::Filter::Type::Blur: return "Blur";
    case fla::Filter::Type::DropShadow: return "Drop Shadow";
    case fla::Filter::Type::Glow: return "Glow";
    case fla::Filter::Type::Bevel: return "Bevel";
    case fla::Filter::Type::GradientGlow: return "Gradient Glow";
    case fla::Filter::Type::GradientBevel: return "Gradient Bevel";
    case fla::Filter::Type::AdjustColor: return "Adjust Color";
    }
    return "Filter";
}

const char* const kBlendModes[] = {"normal", "layer", "multiply", "screen", "lighten", "darken",
    "difference", "add", "subtract", "invert", "alpha", "erase", "overlay", "hardlight"};
const char* const kBlendModeTitles[] = {"Normal", "Layer", "Multiply", "Screen", "Lighten", "Darken",
    "Difference", "Add", "Subtract", "Invert", "Alpha", "Erase", "Overlay", "Hard Light"};

/// The Animate color effect a transform amounts to.
enum class ColorStyle
{
    None,
    Brightness,
    Tint,
    Alpha,
    Advanced
};

ColorStyle colorStyleOf(const fla::ColorTransform& color)
{
    if (color.isIdentity())
        return ColorStyle::None;
    const bool plainChannels = color.redMultiplier == 1.0 && color.greenMultiplier == 1.0 &&
        color.blueMultiplier == 1.0 && color.redOffset == 0 && color.greenOffset == 0 &&
        color.blueOffset == 0 && color.alphaOffset == 0;
    if (color.tintMultiplier != 0.0 && plainChannels && color.brightness == 0.0 && color.alphaMultiplier == 1.0)
        return ColorStyle::Tint;
    if (color.brightness != 0.0 && plainChannels && color.tintMultiplier == 0.0 && color.alphaMultiplier == 1.0)
        return ColorStyle::Brightness;
    if (plainChannels && color.tintMultiplier == 0.0 && color.brightness == 0.0)
        return ColorStyle::Alpha;
    return ColorStyle::Advanced;
}

void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        else if (QLayout* inner = item->layout())
        {
            clearLayout(inner);
        }
        delete item;
    }
}

QLabel* fieldLabel(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

} // namespace

// ---------------------------------------------------------------------------
// PropertySection

PropertySection::PropertySection(const QString& title, QWidget* parent)
    : QWidget(parent)
    , _header(new QToolButton(this))
    , _body(new QWidget(this))
    , _grid(new QGridLayout(_body))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    _header->setText(title);
    _header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _header->setArrowType(Qt::DownArrow);
    _header->setAutoRaise(true);
    _header->setCheckable(true);
    _header->setChecked(true);
    _header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _header->setStyleSheet("QToolButton { text-align: left; font-weight: bold; padding: 4px 2px; border: none; }"
                           "QToolButton:hover { background: rgba(128, 128, 128, 40); }");
    connect(_header, &QToolButton::toggled, this, [this](bool on) { setExpanded(on); });
    layout->addWidget(_header);

    _grid->setContentsMargins(22, 2, 8, 8);
    _grid->setHorizontalSpacing(8);
    _grid->setVerticalSpacing(6);
    _grid->setColumnStretch(1, 1);
    _grid->setColumnStretch(3, 1);
    layout->addWidget(_body);

    QFrame* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    rule->setStyleSheet("color: rgba(128, 128, 128, 60);");
    layout->addWidget(rule);
}

void PropertySection::addField(int row, int slot, const QString& label, QWidget* field)
{
    const int column = slot * 2;
    if (!label.isEmpty())
    {
        _grid->addWidget(fieldLabel(label, _body), row, column);
        _grid->addWidget(field, row, column + 1);
    }
    else
    {
        _grid->addWidget(field, row, column, 1, 2);
    }
}

void PropertySection::addWide(int row, QWidget* widget)
{
    _grid->addWidget(widget, row, 0, 1, 4);
}

void PropertySection::addLong(int row, const QString& label, QWidget* field)
{
    _grid->addWidget(fieldLabel(label, _body), row, 0);
    _grid->addWidget(field, row, 1, 1, 3);
}

void PropertySection::setExpanded(bool expanded)
{
    _expanded = expanded;
    _body->setVisible(expanded);
    _header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    if (_header->isChecked() != expanded)
        _header->setChecked(expanded);
}

// ---------------------------------------------------------------------------
// PropertiesPanel

PropertiesPanel::PropertiesPanel(const Links& links, QWidget* parent)
    : QWidget(parent)
    , _links(links)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildTabBar();
    layout->addWidget(_tabBarHolder);

    // Each tab is a page in a scroll area, so a long Object tab scrolls
    // rather than squeezing its sections.
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget* stack = new QWidget(scroll);
    QVBoxLayout* stackLayout = new QVBoxLayout(stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);
    for (int i = 0; i < 4; ++i)
    {
        _pages[i] = new QWidget(stack);
        _pageLayouts[i] = new QVBoxLayout(_pages[i]);
        _pageLayouts[i]->setContentsMargins(0, 0, 0, 0);
        _pageLayouts[i]->setSpacing(0);
        _pageLayouts[i]->setAlignment(Qt::AlignTop);
        stackLayout->addWidget(_pages[i]);
        _pages[i]->setVisible(i == 0);
    }
    stackLayout->addStretch();
    scroll->setWidget(stack);
    layout->addWidget(scroll, 1);

    buildToolTab();
    applyToolVisibility();
    rebuildObjectTab();
    rebuildFrameTab();
    rebuildDocTab();
}

void PropertiesPanel::buildTabBar()
{
    _tabBarHolder = new QWidget(this);
    QVBoxLayout* holderLayout = new QVBoxLayout(_tabBarHolder);
    holderLayout->setContentsMargins(0, 0, 0, 0);
    holderLayout->setSpacing(0);

    QWidget* bar = new QWidget(_tabBarHolder);
    QHBoxLayout* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 0);
    layout->setSpacing(2);

    QButtonGroup* group = new QButtonGroup(bar);
    group->setExclusive(true);
    const char* titles[4] = {"Tool", "Object", "Frame", "Doc"};
    for (int i = 0; i < 4; ++i)
    {
        QToolButton* button = new QToolButton(bar);
        button->setText(titles[i]);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setChecked(i == 0);
        button->setMinimumWidth(52);
        // Inactive tabs sit halfway between the text and the background.
        const QColor text = palette().color(QPalette::Text);
        const QColor back = palette().color(QPalette::Window);
        const QColor dim((text.red() + back.red()) / 2, (text.green() + back.green()) / 2, (text.blue() + back.blue()) / 2);
        button->setStyleSheet(QString(
            "QToolButton { padding: 5px 8px; border: none; border-bottom: 2px solid transparent; color: %1; }"
            "QToolButton:hover { color: palette(text); }"
            "QToolButton:checked { color: palette(text); font-weight: bold; border-bottom: 2px solid rgb(86, 156, 255); }")
            .arg(dim.name()));
        connect(button, &QToolButton::clicked, this, [this, i]() { setTab(static_cast<Tab>(i)); });
        group->addButton(button);
        layout->addWidget(button);
        _tabButtons[i] = button;
    }
    layout->addStretch();
    holderLayout->addWidget(bar);

    // A rule under the tabs, across the full width.
    QFrame* rule = new QFrame(_tabBarHolder);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    rule->setStyleSheet("color: rgba(128, 128, 128, 80);");
    holderLayout->addWidget(rule);
}

void PropertiesPanel::setTab(Tab tab)
{
    _tab = tab;
    for (int i = 0; i < 4; ++i)
    {
        _pages[i]->setVisible(i == static_cast<int>(tab));
        if (_tabButtons[i]->isChecked() != (i == static_cast<int>(tab)))
            _tabButtons[i]->setChecked(i == static_cast<int>(tab));
    }
}

void PropertiesPanel::refresh()
{
    if (_inEdit)
    {
        _refreshPending = true;
        return;
    }
    _refreshPending = false;
    rebuildObjectTab();
    rebuildFrameTab();
    rebuildDocTab();
}

void PropertiesPanel::flushRefresh()
{
    if (_refreshPending && !_inEdit)
        QTimer::singleShot(0, this, [this]() { refresh(); });
}

void PropertiesPanel::refreshStyleSwatches()
{
    if (_fillSwatch)
        _fillSwatch->setIcon(swatchIcon(_links.style->fillColor));
    if (_strokeSwatch)
        _strokeSwatch->setIcon(swatchIcon(_links.style->strokeColor));
    if (_cornerRadius)
        _cornerRadius->setValue(_links.style->cornerRadius);
}

fla::Document* PropertiesPanel::document() const
{
    fla::FLADocument* fla = _links.editContext ? _links.editContext->document() : nullptr;
    return fla ? fla->document : nullptr;
}

void PropertiesPanel::push(std::unique_ptr<fla::Command> command)
{
    if (_links.editContext)
        _links.editContext->commandStack().push(std::move(command));
}

// ---------------------------------------------------------------------------
// Building blocks

QWidget* PropertiesPanel::makeHeader(const QPixmap& icon, const QString& title, QWidget* field)
{
    QWidget* header = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(10, 8, 10, 6);
    layout->setSpacing(8);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(icon);
    iconLabel->setFixedSize(24, 24);
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel(title, header);
    QFont font = titleLabel->font();
    font.setBold(true);
    titleLabel->setFont(font);
    layout->addWidget(titleLabel);

    if (field)
        layout->addWidget(field, 1);
    else
        layout->addStretch();

    return header;
}

PropertySection* PropertiesPanel::addSection(QVBoxLayout* layout, const QString& title)
{
    PropertySection* section = new PropertySection(title, this);
    layout->addWidget(section);
    return section;
}

HotText* PropertiesPanel::makeHotText(double minimum, double maximum, int decimals, const QString& suffix)
{
    HotText* hot = new HotText(this);
    hot->setRange(minimum, maximum);
    hot->setDecimals(decimals);
    hot->setSuffix(suffix);
    hot->setStep(decimals > 0 ? 0.5 : 1.0);
    return hot;
}

QToolButton* PropertiesPanel::makeSwatch(const uint8_t rgba[4], const QString& tip)
{
    QToolButton* swatch = new QToolButton(this);
    swatch->setIcon(swatchIcon(rgba));
    swatch->setIconSize(QSize(20, 20));
    swatch->setAutoRaise(true);
    swatch->setToolTip(tip);
    return swatch;
}

void PropertiesPanel::bindLive(HotText* hot, std::function<void()> start,
    std::function<void(double)> apply, std::function<void()> commit)
{
    // A drag applies each step live and commits once at the end; a typed
    // value is applied and committed in one go.
    std::shared_ptr<bool> dragged = std::make_shared<bool>(false);
    connect(hot, &HotText::editStarted, this, [=]() {
        *dragged = false;
        start();
    });
    connect(hot, &HotText::valueChanged, this, [=](double value, bool live) {
        _inEdit = true;
        apply(value);
        if (live)
            *dragged = true;
        else
            commit();
        _inEdit = false;
        if (!live)
            flushRefresh();
    });
    connect(hot, &HotText::editFinished, this, [=]() {
        _inEdit = true;
        if (*dragged)
        {
            *dragged = false;
            commit();
        }
        _inEdit = false;
        flushRefresh();
    });
}

// ---------------------------------------------------------------------------
// Tool tab

void PropertiesPanel::setActiveTool(Tool* tool, ToolIcons::Tool icon)
{
    _tool = tool;
    _toolIcon = icon;
    if (_toolIconLabel)
        _toolIconLabel->setPixmap(ToolIcons::icon(icon).pixmap(22, 22));
    if (_toolNameLabel)
        _toolNameLabel->setText(tool ? tool->name() + " Tool" : QString("No tool"));
    applyToolVisibility();
}

void PropertiesPanel::applyToolVisibility()
{
    bool anyColor = false;
    bool anyOption = false;
    for (const ToolRow& row : _toolRows)
    {
        const bool shown = row.tools.empty() ||
            std::find(row.tools.begin(), row.tools.end(), _toolIcon) != row.tools.end();
        if (row.label)
            row.label->setVisible(shown);
        row.field->setVisible(shown);
        if (!shown)
            continue;
        if (row.field->parentWidget() && _colorSection && row.field->parentWidget()->parentWidget() == _colorSection)
            anyColor = true;
        else
            anyOption = true;
    }
    if (_colorSection)
        _colorSection->setVisible(anyColor);
    if (_optionsSection)
        _optionsSection->setVisible(anyOption);
}

void PropertiesPanel::buildToolTab()
{
    using IconTool = ToolIcons::Tool;
    QVBoxLayout* page = _pageLayouts[0];
    fla::DrawingStyle& style = *_links.style;

    // Header: the tool's icon and name.
    QWidget* header = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 8, 10, 6);
    headerLayout->setSpacing(8);
    _toolIconLabel = new QLabel(header);
    _toolIconLabel->setFixedSize(24, 24);
    _toolIconLabel->setAlignment(Qt::AlignCenter);
    _toolIconLabel->setPixmap(ToolIcons::icon(_toolIcon).pixmap(22, 22));
    headerLayout->addWidget(_toolIconLabel);
    _toolNameLabel = new QLabel("Selection Tool", header);
    QFont bold = _toolNameLabel->font();
    bold.setBold(true);
    _toolNameLabel->setFont(bold);
    headerLayout->addWidget(_toolNameLabel);
    headerLayout->addStretch();
    page->addWidget(header);

    const std::vector<IconTool> fillTools = {IconTool::Rectangle, IconTool::Oval, IconTool::PolyStar,
        IconTool::RectanglePrimitive, IconTool::OvalPrimitive, IconTool::Pen, IconTool::Brush,
        IconTool::FluidBrush, IconTool::InkBrush, IconTool::PaintBucket, IconTool::Text};
    const std::vector<IconTool> strokeTools = {IconTool::Rectangle, IconTool::Oval, IconTool::Line,
        IconTool::PolyStar, IconTool::RectanglePrimitive, IconTool::OvalPrimitive, IconTool::Pen,
        IconTool::Pencil, IconTool::InkBottle};

    // --- Color and Style ---
    _colorSection = addSection(page, "Color and Style");
    int row = 0;

    QWidget* fillRow = new QWidget(this);
    QHBoxLayout* fillLayout = new QHBoxLayout(fillRow);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(6);
    _fillSwatch = makeSwatch(style.fillColor, "Fill color for new shapes");
    connect(_fillSwatch, &QToolButton::clicked, this, [this]() {
        fla::DrawingStyle& current = *_links.style;
        const QColor chosen = QColorDialog::getColor(toQColor(current.fillColor), this, "Fill Color",
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        applyColor(chosen, current.fillColor);
        _fillSwatch->setIcon(swatchIcon(current.fillColor));
        emit toolSettingChanged("fillColor");
    });
    QCheckBox* fillEnabled = new QCheckBox("Fill", fillRow);
    fillEnabled->setChecked(style.hasFill);
    connect(fillEnabled, &QCheckBox::toggled, this, [this](bool on) { _links.style->hasFill = on; });
    fillLayout->addWidget(_fillSwatch);
    fillLayout->addWidget(fillEnabled);
    fillLayout->addStretch();
    _colorSection->addField(row, 0, QString(), fillRow);
    _toolRows.push_back({nullptr, fillRow, fillTools});
    ++row;

    QWidget* strokeRow = new QWidget(this);
    QHBoxLayout* strokeLayout = new QHBoxLayout(strokeRow);
    strokeLayout->setContentsMargins(0, 0, 0, 0);
    strokeLayout->setSpacing(6);
    _strokeSwatch = makeSwatch(style.strokeColor, "Stroke color for new shapes");
    connect(_strokeSwatch, &QToolButton::clicked, this, [this]() {
        fla::DrawingStyle& current = *_links.style;
        const QColor chosen = QColorDialog::getColor(toQColor(current.strokeColor), this, "Stroke Color",
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        applyColor(chosen, current.strokeColor);
        _strokeSwatch->setIcon(swatchIcon(current.strokeColor));
        emit toolSettingChanged("strokeColor");
    });
    QCheckBox* strokeEnabled = new QCheckBox("Stroke", strokeRow);
    strokeEnabled->setChecked(style.hasStroke);
    connect(strokeEnabled, &QCheckBox::toggled, this, [this](bool on) { _links.style->hasStroke = on; });
    strokeLayout->addWidget(_strokeSwatch);
    strokeLayout->addWidget(strokeEnabled);
    strokeLayout->addStretch();
    _colorSection->addField(row, 0, QString(), strokeRow);
    _toolRows.push_back({nullptr, strokeRow, strokeTools});
    ++row;

    HotText* weight = makeHotText(0.1, 200.0, 2);
    weight->setValue(style.strokeWeight);
    weight->setStep(0.25);
    weight->setToolTip("Stroke weight for new shapes, in pixels");
    connect(weight, &HotText::valueChanged, this, [this](double value, bool) { _links.style->strokeWeight = value; });
    QLabel* weightLabel = fieldLabel("Stroke size", this);
    _colorSection->grid()->addWidget(weightLabel, row, 0);
    _colorSection->grid()->addWidget(weight, row, 1);
    _toolRows.push_back({weightLabel, weight, strokeTools});
    ++row;

    for (const auto& fields : addStrokeLookFields(_colorSection, row, style.strokeLook,
             [this](const fla::StrokeLook& look) { _links.style->strokeLook = look; }))
        _toolRows.push_back({fields.first, fields.second, strokeTools});

    // --- Tool Options ---
    _optionsSection = addSection(page, "Tool Options");
    QGridLayout* grid = _optionsSection->grid();
    row = 0;

    const auto addOption = [&](const QString& label, QWidget* field, const std::vector<IconTool>& tools) {
        QLabel* labelWidget = nullptr;
        if (!label.isEmpty())
        {
            labelWidget = fieldLabel(label, this);
            grid->addWidget(labelWidget, row, 0);
            grid->addWidget(field, row, 1, 1, 3);
        }
        else
        {
            grid->addWidget(field, row, 0, 1, 4);
        }
        _toolRows.push_back({labelWidget, field, tools});
        ++row;
    };

    // Rectangle corner radius.
    _cornerRadius = makeHotText(-500.0, 500.0, 2);
    _cornerRadius->setValue(style.cornerRadius);
    _cornerRadius->setToolTip("Corner radius for rectangles; Up and Down change it while dragging");
    connect(_cornerRadius, &HotText::valueChanged, this, [this](double value, bool) { _links.style->cornerRadius = value; });
    addOption("Corner radius", _cornerRadius, {IconTool::Rectangle, IconTool::RectanglePrimitive});

    // Oval primitive arc and hole.
    HotText* startAngle = makeHotText(0.0, 360.0, 2, QString::fromUtf8("\xC2\xB0"));
    startAngle->setValue(style.ovalStartAngle);
    startAngle->setToolTip("Where the oval primitive's arc starts, in degrees clockwise from three o'clock");
    connect(startAngle, &HotText::valueChanged, this, [this](double value, bool) { _links.style->ovalStartAngle = value; });
    addOption("Start angle", startAngle, {IconTool::OvalPrimitive});

    HotText* endAngle = makeHotText(0.0, 360.0, 2, QString::fromUtf8("\xC2\xB0"));
    endAngle->setValue(style.ovalEndAngle);
    endAngle->setToolTip("Where the oval primitive's arc ends; the same as the start draws the whole oval");
    connect(endAngle, &HotText::valueChanged, this, [this](double value, bool) { _links.style->ovalEndAngle = value; });
    addOption("End angle", endAngle, {IconTool::OvalPrimitive});

    HotText* innerRadius = makeHotText(0.0, 99.0, 2, "%");
    innerRadius->setValue(style.ovalInnerRadius);
    innerRadius->setToolTip("The oval primitive's hole, as a percentage of its size");
    connect(innerRadius, &HotText::valueChanged, this, [this](double value, bool) { _links.style->ovalInnerRadius = value; });
    addOption("Inner radius", innerRadius, {IconTool::OvalPrimitive});

    // PolyStar.
    HotText* sides = makeHotText(3, 32, 0);
    sides->setValue(style.sides);
    sides->setStep(0.1);
    connect(sides, &HotText::valueChanged, this, [this](double value, bool) { _links.style->sides = static_cast<int>(value); });
    addOption("Sides", sides, {IconTool::PolyStar});

    QCheckBox* star = new QCheckBox("Star", this);
    star->setChecked(style.star);
    star->setToolTip("Draw a star rather than a regular polygon");
    connect(star, &QCheckBox::toggled, this, [this](bool on) { _links.style->star = on; });
    addOption(QString(), star, {IconTool::PolyStar});

    // Free transform.
    QComboBox* transformMode = new QComboBox(this);
    transformMode->addItems({"Free Transform", "Distort", "Envelope"});
    transformMode->setCurrentIndex(static_cast<int>(style.transformMode));
    transformMode->setToolTip("The free transform tool's mode; Distort and Envelope work on shapes");
    connect(transformMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->transformMode = static_cast<fla::DrawingStyle::TransformMode>(index);
        emit toolSettingChanged("transformMode");
    });
    addOption("Mode", transformMode, {IconTool::FreeTransform});

    // Magic Wand, as Animate's: threshold and smoothing.
    HotText* wandThreshold = makeHotText(0.0, 200.0, 0);
    wandThreshold->setValue(style.wandThreshold);
    wandThreshold->setToolTip("How far a color may be from the clicked one and still be taken, 0 to 200");
    connect(wandThreshold, &HotText::valueChanged, this, [this](double value, bool) {
        _links.style->wandThreshold = static_cast<int>(std::lround(value));
    });
    addOption("Threshold", wandThreshold, {IconTool::MagicWand});
    QComboBox* wandSmoothing = new QComboBox(this);
    wandSmoothing->addItems({"Pixels", "Rough", "Normal", "Smooth"});
    wandSmoothing->setCurrentIndex(style.wandSmoothing);
    wandSmoothing->setToolTip("How the outline of the area taken is tidied");
    connect(wandSmoothing, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            _links.style->wandSmoothing = index;
    });
    addOption("Smoothing", wandSmoothing, {IconTool::MagicWand});

    // Brushes. Each has its own settings, shown while it is the tool; the
    // brush mode is shared.
    using BrushMember = fla::BrushSettings fla::DrawingStyle::*;
    using Brush = fla::BrushSettings;
    const auto brushNumber = [&](BrushMember member, IconTool icon, const QString& label, double minimum,
        double maximum, int decimals, const QString& suffix, const QString& tip,
        std::function<double(const Brush&)> get, std::function<void(Brush&, double)> set) {
        HotText* field = makeHotText(minimum, maximum, decimals, suffix);
        field->setValue(get(style.*member));
        field->setToolTip(tip);
        connect(field, &HotText::valueChanged, this, [this, member, set](double value, bool) {
            set(_links.style->*member, value);
        });
        addOption(label, field, {icon});
    };
    const auto brushCheck = [&](BrushMember member, IconTool icon, const QString& text, const QString& tip,
        std::function<bool(const Brush&)> get, std::function<void(Brush&, bool)> set) {
        QCheckBox* box = new QCheckBox(text, this);
        box->setChecked(get(style.*member));
        box->setToolTip(tip);
        connect(box, &QCheckBox::toggled, this, [this, member, set](bool on) { set(_links.style->*member, on); });
        addOption(QString(), box, {icon});
    };
    const auto brushChoice = [&](BrushMember member, IconTool icon, const QString& label, const QStringList& items,
        const QString& tip, std::function<int(const Brush&)> get, std::function<void(Brush&, int)> set) {
        QComboBox* box = new QComboBox(this);
        box->addItems(items);
        box->setCurrentIndex(get(style.*member));
        box->setToolTip(tip);
        connect(box, &QComboBox::currentIndexChanged, this, [this, member, set](int index) {
            if (index >= 0)
                set(_links.style->*member, index);
        });
        addOption(label, box, {icon});
    };
    const auto sizeOf = [](const Brush& b) { return b.size; };
    const auto setSize = [](Brush& b, double v) { b.size = v; };
    const auto pressureOf = [](const Brush& b) { return b.usePressure; };
    const auto setPressure = [](Brush& b, bool on) { b.usePressure = on; };
    const auto minSizeOf = [](const Brush& b) { return b.minSize; };
    const auto setMinSize = [](Brush& b, double v) { b.minSize = v; };
    const auto smoothingOf = [](const Brush& b) { return static_cast<double>(b.smoothing); };
    const auto setSmoothing = [](Brush& b, double v) { b.smoothing = static_cast<int>(std::lround(v)); };
    const auto precisionOf = [](const Brush& b) { return static_cast<double>(b.precision); };
    const auto setPrecision = [](Brush& b, double v) { b.precision = static_cast<int>(std::lround(v)); };
    const auto taperStartOf = [](const Brush& b) { return b.taperStart; };
    const auto setTaperStart = [](Brush& b, double v) { b.taperStart = v; };
    const auto taperEndOf = [](const Brush& b) { return b.taperEnd; };
    const auto setTaperEnd = [](Brush& b, double v) { b.taperEnd = v; };
    const QString sizeTip = "The width of the tip at full pressure, in pixels";
    const QString minSizeTip = "The width at the lightest touch, as a percentage of the size";
    const QString smoothingTip = "How much of the hand's wobble is taken out of the line";
    const QString precisionTip = "How closely the finished outline follows the stroke; lower is smoother "
        "with fewer points";

    // Fluid Brush, as Animate's.
    const BrushMember fluid = &fla::DrawingStyle::fluidBrush;
    brushNumber(fluid, IconTool::FluidBrush, "Size", 1.0, 200.0, 1, " px", sizeTip, sizeOf, setSize);
    brushCheck(fluid, IconTool::FluidBrush, "Pressure", "Let a pen's pressure vary the width", pressureOf,
        setPressure);
    brushNumber(fluid, IconTool::FluidBrush, "Min size", 1.0, 100.0, 0, " %", minSizeTip, minSizeOf, setMinSize);
    brushChoice(fluid, IconTool::FluidBrush, "Velocity", {"None", "Thin to thick", "Thick to thin"},
        "How the speed of the stroke changes its width",
        [](const Brush& b) { return static_cast<int>(b.velocity); },
        [](Brush& b, int i) { b.velocity = static_cast<Brush::Velocity>(i); });
    brushNumber(fluid, IconTool::FluidBrush, "Taper start", 0.0, 50.0, 0, " %",
        "How much of the stroke its start tapers over", taperStartOf, setTaperStart);
    brushNumber(fluid, IconTool::FluidBrush, "Taper end", 0.0, 50.0, 0, " %",
        "How much of the stroke its end tapers over", taperEndOf, setTaperEnd);
    brushNumber(fluid, IconTool::FluidBrush, "Smoothing", 0.0, 100.0, 0, QString(), smoothingTip, smoothingOf,
        setSmoothing);
    brushNumber(fluid, IconTool::FluidBrush, "Precision", 0.0, 100.0, 0, QString(), precisionTip, precisionOf,
        setPrecision);
    brushNumber(fluid, IconTool::FluidBrush, "Roundness", 1.0, 100.0, 0, " %",
        "The tip's height as a percentage of its width",
        [](const Brush& b) { return b.roundness; }, [](Brush& b, double v) { b.roundness = v; });
    brushNumber(fluid, IconTool::FluidBrush, "Angle", -180.0, 180.0, 0, QString::fromUtf8("°"),
        "The tip's turn, clockwise", [](const Brush& b) { return b.angle; },
        [](Brush& b, double v) { b.angle = v; });

    // Classic Brush, as Animate's (Flash's): its tip shapes, Use Pressure and
    // Use Tilt.
    const BrushMember classic = &fla::DrawingStyle::classicBrush;
    struct TipShape
    {
        Brush::Tip tip;
        double roundness;
        double angle;
    };
    static const TipShape tipShapes[] = {
        {Brush::Tip::Ellipse, 100.0, 0.0}, {Brush::Tip::Ellipse, 50.0, 0.0},
        {Brush::Tip::Rectangle, 100.0, 0.0}, {Brush::Tip::Rectangle, 50.0, 0.0},
        {Brush::Tip::Rectangle, 20.0, 90.0}, {Brush::Tip::Rectangle, 20.0, -45.0},
        {Brush::Tip::Rectangle, 20.0, 45.0}};
    brushNumber(classic, IconTool::Brush, "Size", 1.0, 200.0, 1, " px", sizeTip, sizeOf, setSize);
    brushChoice(classic, IconTool::Brush, "Brush shape",
        {"Round", "Oval", "Square", "Rectangle", "Vertical line", "Diagonal /", "Diagonal \\"},
        "The shape of the tip, as Flash's brush offers them",
        [](const Brush& b) {
            for (int i = 0; i < 7; ++i)
            {
                if (tipShapes[i].tip == b.tip && tipShapes[i].roundness == b.roundness &&
                    tipShapes[i].angle == b.angle)
                {
                    return i;
                }
            }
            return 0;
        },
        [](Brush& b, int i) {
            b.tip = tipShapes[i].tip;
            b.roundness = tipShapes[i].roundness;
            b.angle = tipShapes[i].angle;
        });
    brushCheck(classic, IconTool::Brush, "Use pressure", "Let a pen's pressure vary the width", pressureOf,
        setPressure);
    brushCheck(classic, IconTool::Brush, "Use tilt", "Turn the tip to the direction the pen leans",
        [](const Brush& b) { return b.useTilt; }, [](Brush& b, bool on) { b.useTilt = on; });
    brushNumber(classic, IconTool::Brush, "Smoothing", 0.0, 100.0, 0, QString(), smoothingTip, smoothingOf,
        setSmoothing);

    // Ink Brush, Phoenix's own. Pressure is always on.
    const BrushMember ink = &fla::DrawingStyle::inkBrush;
    brushNumber(ink, IconTool::InkBrush, "Size", 1.0, 200.0, 1, " px", sizeTip, sizeOf, setSize);
    brushNumber(ink, IconTool::InkBrush, "Min size", 1.0, 100.0, 0, " %", minSizeTip, minSizeOf, setMinSize);
    brushChoice(ink, IconTool::InkBrush, "Pressure", {"Soft", "Linear", "Firm"},
        "Soft gives width at a light touch; firm needs a harder press",
        [](const Brush& b) { return b.pressureCurve < 0.8 ? 0 : (b.pressureCurve > 1.3 ? 2 : 1); },
        [](Brush& b, int i) {
            static const double curves[] = {0.6, 1.0, 1.7};
            b.pressureCurve = curves[i];
        });
    brushNumber(ink, IconTool::InkBrush, "Stabilizer", 0.0, 60.0, 0, " px",
        "How far the pen leads the line, in screen pixels: the line follows on a string, which takes "
        "out shake. 0 is off",
        [](const Brush& b) { return b.stabilizer; }, [](Brush& b, double v) { b.stabilizer = v; });
    brushNumber(ink, IconTool::InkBrush, "Smoothing", 0.0, 100.0, 0, QString(), smoothingTip, smoothingOf,
        setSmoothing);
    brushNumber(ink, IconTool::InkBrush, "Precision", 0.0, 100.0, 0, QString(), precisionTip, precisionOf,
        setPrecision);
    brushCheck(ink, IconTool::InkBrush, "Tilt shading", "Broaden the line the further the pen leans",
        [](const Brush& b) { return b.useTilt; }, [](Brush& b, bool on) { b.useTilt = on; });

    QComboBox* brushMode = new QComboBox(this);
    brushMode->addItems({"Paint Normal", "Paint Fills", "Paint Behind", "Paint Selection", "Paint Inside"});
    brushMode->setCurrentIndex(static_cast<int>(style.brushMode));
    brushMode->setToolTip("Where a brush stroke lands, as in Animate");
    connect(brushMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->brushMode = static_cast<fla::DrawingStyle::BrushMode>(index);
    });
    addOption("Brush mode", brushMode, {IconTool::FluidBrush, IconTool::Brush, IconTool::InkBrush});

    // Eraser.
    HotText* eraserSize = makeHotText(1.0, 200.0, 1, " px");
    eraserSize->setValue(style.eraserSize);
    connect(eraserSize, &HotText::valueChanged, this, [this](double value, bool) { _links.style->eraserSize = value; });
    addOption("Size", eraserSize, {IconTool::Eraser});

    QComboBox* eraserMode = new QComboBox(this);
    eraserMode->addItems({"Erase Normal", "Erase Fills", "Erase Lines", "Erase Selected Fills", "Erase Inside"});
    eraserMode->setCurrentIndex(static_cast<int>(style.eraserMode));
    eraserMode->setToolTip("What a pass of the eraser takes away, as in Animate");
    connect(eraserMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->eraserMode = static_cast<fla::DrawingStyle::EraserMode>(index);
    });
    addOption("Eraser mode", eraserMode, {IconTool::Eraser});

    QCheckBox* faucet = new QCheckBox("Faucet", this);
    faucet->setChecked(style.faucet);
    faucet->setToolTip("Click a fill or a stroke with the eraser to remove it whole");
    connect(faucet, &QCheckBox::toggled, this, [this](bool on) {
        _links.style->faucet = on;
        emit toolSettingChanged("faucet");
    });
    addOption(QString(), faucet, {IconTool::Eraser});

    // Paint bucket.
    QComboBox* gapSize = new QComboBox(this);
    gapSize->addItems({"Don't Close Gaps", "Close Small Gaps", "Close Medium Gaps", "Close Large Gaps"});
    gapSize->setCurrentIndex(static_cast<int>(style.gapSize));
    gapSize->setToolTip("How far apart an outline's ends may be for the paint bucket to fill it");
    connect(gapSize, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->gapSize = static_cast<fla::DrawingStyle::GapSize>(index);
    });
    addOption("Gap size", gapSize, {IconTool::PaintBucket});

    // Pencil.
    QComboBox* pencilMode = new QComboBox(this);
    pencilMode->addItems({"Smooth", "Straighten", "Ink"});
    pencilMode->setCurrentIndex(static_cast<int>(style.pencilMode));
    pencilMode->setToolTip("How the pencil tidies a freehand stroke");
    connect(pencilMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->pencilMode = static_cast<fla::DrawingStyle::PencilMode>(index);
    });
    addOption("Pencil mode", pencilMode, {IconTool::Pencil});

    HotText* smoothing = makeHotText(0, 100, 0);
    smoothing->setValue(style.pencilSmoothing);
    smoothing->setToolTip("How much the pencil smooths a stroke, 0 to 100, as in Animate");
    connect(smoothing, &HotText::valueChanged, this, [this](double value, bool) { _links.style->pencilSmoothing = static_cast<int>(value); });
    addOption("Smoothing", smoothing, {IconTool::Pencil});

    QCheckBox* connectLines = new QCheckBox("Connect lines", this);
    connectLines->setChecked(style.connectLines);
    connectLines->setToolTip("In Straighten mode, close a pencil stroke whose ends nearly meet");
    connect(connectLines, &QCheckBox::toggled, this, [this](bool on) { _links.style->connectLines = on; });
    addOption(QString(), connectLines, {IconTool::Pencil});

    QCheckBox* recognizeShapes = new QCheckBox("Recognize shapes", this);
    recognizeShapes->setChecked(style.recognizeShapes);
    recognizeShapes->setToolTip("In Straighten mode, turn a roughly drawn rectangle or oval into the real thing");
    connect(recognizeShapes, &QCheckBox::toggled, this, [this](bool on) { _links.style->recognizeShapes = on; });
    addOption(QString(), recognizeShapes, {IconTool::Pencil});

    // Text. The face is kept the way Animate writes it, the family without
    // its spaces.
    QFontComboBox* textFont = new QFontComboBox(this);
    textFont->setCurrentFont(QFont("Arial"));
    connect(textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        // Animate names a font by its PostScript name, so the common ones get
        // the suffix their foundry gave them; the rest just lose their spaces.
        static const QHash<QString, QString> postScriptNames = {
            {"Arial", "ArialMT"},
            {"Times New Roman", "TimesNewRomanPSMT"},
            {"Courier New", "CourierNewPSMT"},
        };
        const QString family = font.family();
        _pendingTextFace = postScriptNames.value(family, QString(family).remove(' ')).toStdString();
        emit toolSettingChanged("textFace");
    });
    addOption("Family", textFont, {IconTool::Text});

    HotText* textSize = makeHotText(1.0, 500.0, 1, " pt");
    textSize->setValue(style.textSize);
    textSize->setToolTip("Size of new text, in pixels");
    connect(textSize, &HotText::valueChanged, this, [this](double value, bool) {
        _pendingTextSize = value;
        emit toolSettingChanged("textSize");
    });
    addOption("Size", textSize, {IconTool::Text});

    QComboBox* textType = new QComboBox(this);
    textType->addItems({"Static Text", "Dynamic Text", "Input Text"});
    textType->setCurrentIndex(style.textType);
    textType->setToolTip("What kind of field the text is: artwork, scriptable, or typed into");
    connect(textType, &QComboBox::currentIndexChanged, this, [this](int index) {
        _links.style->textType = index;
        emit toolSettingChanged("textType");
    });
    addOption("Text type", textType, {IconTool::Text});
}

// ---------------------------------------------------------------------------
// Object tab

void PropertiesPanel::addPositionAndSize(QVBoxLayout* layout, const std::vector<fla::Element*>& elements)
{
    PhoenixView* view = _links.view;
    if (!view || elements.empty())
        return;

    QRectF bounds;
    for (fla::Element* element : elements)
    {
        const QRectF each = view->elementBounds(element);
        if (!each.isValid())
            continue;
        bounds = bounds.isNull() ? each : bounds.united(each);
    }

    PropertySection* section = addSection(layout, "Position and Size");

    HotText* width = makeHotText(0.1, 1.0e6, 2);
    HotText* height = makeHotText(0.1, 1.0e6, 2);
    HotText* x = makeHotText(-1.0e6, 1.0e6, 2);
    HotText* y = makeHotText(-1.0e6, 1.0e6, 2);
    width->setValue(bounds.width());
    height->setValue(bounds.height());
    x->setValue(bounds.left());
    y->setValue(bounds.top());

    QToolButton* lock = new QToolButton(this);
    lock->setCheckable(true);
    lock->setChecked(_aspectLocked);
    lock->setAutoRaise(true);
    lock->setStyleSheet("QToolButton:checked { background: rgba(128, 128, 128, 50); border-radius: 3px; }");
    lock->setIcon(chainIcon(_aspectLocked, palette()));
    lock->setToolTip("Keep the width and height in proportion");
    connect(lock, &QToolButton::toggled, this, [this, lock](bool on) {
        _aspectLocked = on;
        lock->setIcon(chainIcon(on, palette()));
    });

    section->addField(0, 0, "W", width);
    section->addField(0, 1, "X", x);
    section->addField(1, 0, "H", height);
    section->addField(1, 1, "Y", y);
    section->grid()->addWidget(lock, 0, 4, 2, 1, Qt::AlignVCenter);

    // Each edit: snapshot the transforms, apply the new placement live, and
    // record one Move or Resize step at the end.
    struct Snapshot
    {
        std::vector<fla::Transform> before;
        QRectF bounds;
    };
    std::shared_ptr<Snapshot> snapshot = std::make_shared<Snapshot>();
    const auto start = [=]() {
        snapshot->before.clear();
        for (fla::Element* element : elements)
            snapshot->before.push_back(element->transform);
        snapshot->bounds = bounds;
    };
    const auto applyAdjust = [=](const QTransform& adjust) {
        for (size_t i = 0; i < elements.size() && i < snapshot->before.size(); ++i)
        {
            const QTransform result = toQTransform(snapshot->before[i]) * adjust;
            elements[i]->transform = fromQTransform(result);
        }
        _links.view->invalidateBounds();
        _links.view->update();
    };
    const auto commit = [=](const char* name) {
        fla::CommandStack& stack = _links.editContext->commandStack();
        const bool several = elements.size() > 1;
        if (several)
            stack.beginMacro(name);
        for (size_t i = 0; i < elements.size() && i < snapshot->before.size(); ++i)
        {
            stack.push(fla::CommandPtr(new fla::SetElementTransformCommand(
                elements[i], snapshot->before[i], elements[i]->transform, name)));
        }
        if (several)
            stack.endMacro();
    };

    bindLive(x, start,
        [=](double value) { applyAdjust(QTransform::fromTranslate(value - snapshot->bounds.left(), 0.0)); },
        [=]() { commit("Move"); });
    bindLive(y, start,
        [=](double value) { applyAdjust(QTransform::fromTranslate(0.0, value - snapshot->bounds.top())); },
        [=]() { commit("Move"); });

    const auto scaleAbout = [=](double sx, double sy) {
        const QPointF anchor = snapshot->bounds.topLeft();
        return QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(sx, sy) *
            QTransform::fromTranslate(anchor.x(), anchor.y());
    };
    bindLive(width, start,
        [=](double value) {
            const double sx = snapshot->bounds.width() > 0.0 ? value / snapshot->bounds.width() : 1.0;
            const double sy = _aspectLocked ? sx : 1.0;
            applyAdjust(scaleAbout(sx, sy));
            if (_aspectLocked)
                height->setValue(snapshot->bounds.height() * sy);
        },
        [=]() { commit("Resize"); });
    bindLive(height, start,
        [=](double value) {
            const double sy = snapshot->bounds.height() > 0.0 ? value / snapshot->bounds.height() : 1.0;
            const double sx = _aspectLocked ? sy : 1.0;
            applyAdjust(scaleAbout(sx, sy));
            if (_aspectLocked)
                width->setValue(snapshot->bounds.width() * sx);
        },
        [=]() { commit("Resize"); });
}

std::vector<std::pair<QWidget*, QWidget*>> PropertiesPanel::addStrokeLookFields(PropertySection* section, int& row,
    const fla::StrokeLook& look, std::function<void(const fla::StrokeLook&)> apply)
{
    std::vector<std::pair<QWidget*, QWidget*>> rows;
    const auto add = [&](const QString& text, QWidget* field) {
        QLabel* label = fieldLabel(text, this);
        section->grid()->addWidget(label, row, 0);
        section->grid()->addWidget(field, row, 1, 1, 3);
        rows.emplace_back(label, field);
        ++row;
    };
    auto current = std::make_shared<fla::StrokeLook>(look);

    // Style: the kinds Animate offers, hairline counted as one.
    QComboBox* style = new QComboBox(this);
    style->addItems({"Solid", "Hairline", "Dashed", "Dotted", "Ragged", "Stipple", "Hatched"});
    const fla::StrokeStyle::Style kinds[7] = {fla::StrokeStyle::Style::Solid, fla::StrokeStyle::Style::Solid,
        fla::StrokeStyle::Style::Dashed, fla::StrokeStyle::Style::Dotted, fla::StrokeStyle::Style::Ragged,
        fla::StrokeStyle::Style::Stipple, fla::StrokeStyle::Style::Hatched};
    int styleIndex = 0;
    for (int i = 0; i < 7; ++i)
    {
        if (kinds[i] == look.kind && (i == 1) == look.hairline)
            styleIndex = i;
    }
    style->setCurrentIndex(styleIndex);
    connect(style, &QComboBox::currentIndexChanged, this, [=](int index) {
        current->kind = kinds[std::clamp(index, 0, 6)];
        current->hairline = index == 1;
        apply(*current);
    });
    add("Style", style);

    // Caps and joins as the file spells them; empty is Animate's round.
    QComboBox* cap = new QComboBox(this);
    cap->addItems({"Round", "None", "Square"});
    const char* caps[3] = {"", "none", "square"};
    for (int i = 0; i < 3; ++i)
    {
        if (look.caps == caps[i])
            cap->setCurrentIndex(i);
    }
    connect(cap, &QComboBox::currentIndexChanged, this, [=](int index) {
        current->caps = caps[std::clamp(index, 0, 2)];
        apply(*current);
    });
    add("Cap", cap);

    QWidget* joinRow = new QWidget(this);
    QHBoxLayout* joinLayout = new QHBoxLayout(joinRow);
    joinLayout->setContentsMargins(0, 0, 0, 0);
    QComboBox* join = new QComboBox(joinRow);
    join->addItems({"Round", "Miter", "Bevel"});
    const char* joins[3] = {"", "miter", "bevel"};
    for (int i = 0; i < 3; ++i)
    {
        if (look.joints == joins[i])
            join->setCurrentIndex(i);
    }
    HotText* miter = makeHotText(1, 60, 0);
    miter->setValue(look.miterLimit);
    miter->setToolTip("How far a miter join may reach before it is cut off, in stroke widths");
    miter->setEnabled(look.joints == "miter");
    connect(join, &QComboBox::currentIndexChanged, this, [=](int index) {
        current->joints = joins[std::clamp(index, 0, 2)];
        miter->setEnabled(index == 1);
        apply(*current);
    });
    connect(miter, &HotText::editFinished, this, [=]() {
        current->miterLimit = miter->value();
        apply(*current);
    });
    joinLayout->addWidget(join, 1);
    joinLayout->addWidget(fieldLabel("Miter", joinRow));
    joinLayout->addWidget(miter);
    add("Join", joinRow);

    QComboBox* scale = new QComboBox(this);
    scale->addItems({"Normal", "Horizontal", "Vertical", "None"});
    scale->setToolTip("Which way the stroke's weight follows when its object is scaled");
    const char* scales[4] = {"", "horizontal", "vertical", "none"};
    for (int i = 0; i < 4; ++i)
    {
        if (look.scaleMode == scales[i] || (i == 0 && look.scaleMode == "normal"))
            scale->setCurrentIndex(i);
    }
    connect(scale, &QComboBox::currentIndexChanged, this, [=](int index) {
        current->scaleMode = scales[std::clamp(index, 0, 3)];
        apply(*current);
    });
    add("Scale", scale);
    return rows;
}

HotText* PropertiesPanel::bindDouble(double& field, double minimum, double maximum, int decimals, const QString& suffix,
    const char* name)
{
    HotText* hot = makeHotText(minimum, maximum, decimals, suffix);
    hot->setValue(field);
    double* target = &field;
    auto before = std::make_shared<double>(field);
    bindLive(hot,
        [=]() { *before = *target; },
        [=](double value) {
            *target = value;
            emit documentEdited();
        },
        [=]() {
            if (*target != *before)
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<double>(*target, *before, *target, name)));
        });
    return hot;
}

void PropertiesPanel::addPrimitiveSections(QVBoxLayout* layout, fla::Element* element)
{
    fla::FillStyle** fillSlot = nullptr;
    fla::StrokeStyle** strokeSlot = nullptr;

    if (element->elementType() == fla::Element::Type::Rectangle)
    {
        auto* rect = static_cast<fla::RectanglePrimitive*>(element);
        fillSlot = &rect->fillStyle;
        strokeSlot = &rect->strokeStyle;

        PropertySection* corners = addSection(layout, "Rectangle Options");
        double* radii[4] = {&rect->topLeftRadius, &rect->topRightRadius, &rect->bottomLeftRadius, &rect->bottomRightRadius};
        // Locked, one value sets all four corners, as Animate's lock does;
        // it starts locked when they are all alike.
        auto locked = std::make_shared<bool>(rect->topLeftRadius == rect->topRightRadius &&
            rect->topLeftRadius == rect->bottomLeftRadius && rect->topLeftRadius == rect->bottomRightRadius);
        auto before = std::make_shared<std::array<double, 4>>();
        const char* labels[4] = {"Top left", "Top right", "Bottom left", "Bottom right"};
        std::vector<HotText*> fields;
        for (int i = 0; i < 4; ++i)
            fields.push_back(makeHotText(-999, 999, 1));
        for (int i = 0; i < 4; ++i)
        {
            HotText* hot = fields[static_cast<size_t>(i)];
            hot->setValue(*radii[i]);
            hot->setToolTip("Corner radius, in pixels; negative curves the corner inward");
            bindLive(hot,
                [=]() {
                    for (int k = 0; k < 4; ++k)
                        (*before)[static_cast<size_t>(k)] = *radii[k];
                },
                [=](double value) {
                    for (int k = 0; k < 4; ++k)
                    {
                        if (*locked || k == i)
                        {
                            *radii[k] = value;
                            if (k != i)
                                fields[static_cast<size_t>(k)]->setValue(value);
                        }
                    }
                    emit documentEdited();
                },
                [=]() {
                    auto* command = new fla::CompoundCommand("Corner Radius");
                    for (int k = 0; k < 4; ++k)
                    {
                        command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<double>(
                            *radii[k], (*before)[static_cast<size_t>(k)], *radii[k], "Corner Radius")));
                    }
                    push(std::unique_ptr<fla::Command>(command));
                });
            corners->addField(i / 2, i % 2, labels[i], hot);
        }
        QCheckBox* lock = new QCheckBox("Lock corners together", this);
        lock->setChecked(*locked);
        connect(lock, &QCheckBox::toggled, this, [locked](bool on) { *locked = on; });
        corners->addWide(2, lock);
        QPushButton* reset = new QPushButton("Reset", this);
        reset->setToolTip("Square corners");
        connect(reset, &QPushButton::clicked, this, [=]() {
            auto* command = new fla::CompoundCommand("Reset Corners");
            for (int k = 0; k < 4; ++k)
                command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<double>(*radii[k], *radii[k], 0.0, "Reset Corners")));
            push(std::unique_ptr<fla::Command>(command));
            emit documentEdited();
            QTimer::singleShot(0, this, [this]() { refresh(); });
        });
        corners->addWide(3, reset);
    }
    else if (element->elementType() == fla::Element::Type::Oval)
    {
        auto* oval = static_cast<fla::OvalPrimitive*>(element);
        fillSlot = &oval->fillStyle;
        strokeSlot = &oval->strokeStyle;

        PropertySection* options = addSection(layout, "Oval Options");
        options->addField(0, 0, "Start", bindDouble(oval->startAngle, 0, 360, 1, QString::fromUtf8("°"), "Start Angle"));
        options->addField(0, 1, "End", bindDouble(oval->endAngle, 0, 360, 1, QString::fromUtf8("°"), "End Angle"));
        options->addField(1, 0, "Inner", bindDouble(oval->innerRadius, 0, 99, 0, "%", "Inner Radius"));

        // Close Path is an attribute Phoenix keeps rather than models: on
        // unless the file says false.
        bool closed = true;
        for (const auto& attribute : oval->extraAttributes)
        {
            if (attribute.first == "closePath")
                closed = attribute.second != "false";
        }
        QCheckBox* closePath = new QCheckBox("Close path", this);
        closePath->setChecked(closed);
        closePath->setToolTip("With part of the oval cut away: a closed, filled wedge, or just the open arc");
        connect(closePath, &QCheckBox::toggled, this, [this, oval](bool on) {
            auto after = oval->extraAttributes;
            after.erase(std::remove_if(after.begin(), after.end(),
                [](const std::pair<std::string, std::string>& a) { return a.first == "closePath"; }), after.end());
            if (!on)
                after.emplace_back("closePath", "false");
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                oval->extraAttributes, oval->extraAttributes, after, "Close Path")));
            emit documentEdited();
        });
        options->addField(1, 1, QString(), closePath);
        QPushButton* reset = new QPushButton("Reset", this);
        reset->setToolTip("A whole oval with no hole");
        connect(reset, &QPushButton::clicked, this, [this, oval]() {
            auto* command = new fla::CompoundCommand("Reset Oval");
            command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<double>(oval->startAngle, oval->startAngle, 0.0, "Reset Oval")));
            command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<double>(oval->endAngle, oval->endAngle, 0.0, "Reset Oval")));
            command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<double>(oval->innerRadius, oval->innerRadius, 0.0, "Reset Oval")));
            push(std::unique_ptr<fla::Command>(command));
            emit documentEdited();
            QTimer::singleShot(0, this, [this]() { refresh(); });
        });
        options->addWide(2, reset);
    }
    if (!fillSlot || !strokeSlot)
        return;

    // Fill and stroke, edited where they are.
    PropertySection* section = addSection(layout, "Color and Style");
    int row = 0;
    fla::FillStyle* fill = *fillSlot;
    if (fill && fill->type() == fla::FillStyle::Type::SolidColor)
    {
        uint8_t* color = static_cast<fla::SolidColor*>(fill)->color;
        QToolButton* swatch = makeSwatch(color, "Fill color");
        connect(swatch, &QToolButton::clicked, this, [=]() {
            const QColor chosen = QColorDialog::getColor(toQColor(color), this, "Fill Color", QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid())
                return;
            uint8_t after[4];
            applyColor(chosen, after);
            push(std::unique_ptr<fla::Command>(new fla::SetColorCommand(color, color, after, "Fill Color")));
            emit documentEdited();
            QTimer::singleShot(0, this, [this]() { refresh(); });
        });
        section->addField(row++, 0, "Fill", swatch);
    }
    fla::StrokeStyle* stroke = *strokeSlot;
    if (stroke)
    {
        if (stroke->fill && stroke->fill->type() == fla::FillStyle::Type::SolidColor)
        {
            uint8_t* color = static_cast<fla::SolidColor*>(stroke->fill)->color;
            QToolButton* swatch = makeSwatch(color, "Stroke color");
            connect(swatch, &QToolButton::clicked, this, [=]() {
                const QColor chosen = QColorDialog::getColor(toQColor(color), this, "Stroke Color", QColorDialog::ShowAlphaChannel);
                if (!chosen.isValid())
                    return;
                uint8_t after[4];
                applyColor(chosen, after);
                push(std::unique_ptr<fla::Command>(new fla::SetColorCommand(color, color, after, "Stroke Color")));
                emit documentEdited();
                QTimer::singleShot(0, this, [this]() { refresh(); });
            });
            section->addField(row, 0, "Stroke", swatch);
        }
        section->addField(row++, 1, "Size", bindDouble(stroke->weight, 0.1, 200.0, 2, QString(), "Stroke Size"));
        addStrokeLookFields(section, row, fla::StrokeLook::of(*stroke), [=](const fla::StrokeLook& look) {
            push(std::unique_ptr<fla::Command>(new fla::ReplaceOwnedCommand<fla::StrokeStyle>(
                *strokeSlot, look.applyTo(**strokeSlot, element), "Stroke Style")));
            emit documentEdited();
        });
    }
    if (row == 0)
        section->addWide(row++, fieldLabel("No fill or stroke", this));
}

void PropertiesPanel::addShapeSections(QVBoxLayout* layout, fla::Shape* shape)
{
    // The first solid fill and stroke stand for the shape, as Animate shows
    // the one color when a selection has one.
    fla::SolidColor* fill = nullptr;
    for (fla::FillStyle* style : shape->fills)
    {
        if (style && style->type() == fla::FillStyle::Type::SolidColor)
        {
            fill = static_cast<fla::SolidColor*>(style);
            break;
        }
    }
    fla::StrokeStyle* stroke = shape->strokes.empty() ? nullptr : shape->strokes.front();
    fla::SolidColor* strokeColor = nullptr;
    if (stroke && stroke->fill && stroke->fill->type() == fla::FillStyle::Type::SolidColor)
        strokeColor = static_cast<fla::SolidColor*>(stroke->fill);

    PropertySection* section = addSection(layout, "Color and Style");
    int row = 0;

    // Restyling the shape replaces its contents as one undo step, so every
    // solid fill (or stroke) takes the new color together.
    const auto restyle = [this, shape](const char* name, std::function<void(fla::Shape&)> change) {
        std::unique_ptr<fla::Shape> after(static_cast<fla::Shape*>(fla::cloneElement(*shape, nullptr)));
        change(*after);
        push(std::unique_ptr<fla::Command>(new fla::SetShapeContentsCommand(shape, *after, name)));
        emit documentEdited();
    };

    if (fill)
    {
        QWidget* fillRow = new QWidget(this);
        QHBoxLayout* fillLayout = new QHBoxLayout(fillRow);
        fillLayout->setContentsMargins(0, 0, 0, 0);
        fillLayout->setSpacing(6);
        QToolButton* swatch = makeSwatch(fill->color, "Fill color");
        connect(swatch, &QToolButton::clicked, this, [=]() {
            const QColor chosen = QColorDialog::getColor(toQColor(fill->color), this, "Fill Color",
                QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid())
                return;
            restyle("Fill Color", [&](fla::Shape& after) {
                for (fla::FillStyle* style : after.fills)
                {
                    if (style && style->type() == fla::FillStyle::Type::SolidColor)
                        applyColor(chosen, static_cast<fla::SolidColor*>(style)->color);
                }
            });
        });
        fillLayout->addWidget(swatch);
        fillLayout->addWidget(fieldLabel("Fill", fillRow));
        fillLayout->addStretch();
        HotText* alpha = makeHotText(0, 100, 0, "%");
        alpha->setValue(fill->color[3] * 100.0 / 255.0);
        alpha->setToolTip("The fill's opacity");
        bindLive(alpha, []() {}, [](double) {}, [=]() {
            const uint8_t a = static_cast<uint8_t>(std::lround(alpha->value() * 255.0 / 100.0));
            restyle("Fill Alpha", [&](fla::Shape& after) {
                for (fla::FillStyle* style : after.fills)
                {
                    if (style && style->type() == fla::FillStyle::Type::SolidColor)
                        static_cast<fla::SolidColor*>(style)->color[3] = a;
                }
            });
        });
        fillLayout->addWidget(alpha);
        section->addWide(row++, fillRow);
    }

    if (stroke)
    {
        QWidget* strokeRow = new QWidget(this);
        QHBoxLayout* strokeLayout = new QHBoxLayout(strokeRow);
        strokeLayout->setContentsMargins(0, 0, 0, 0);
        strokeLayout->setSpacing(6);
        static const uint8_t kNoColor[4] = {0, 0, 0, 0};
        QToolButton* swatch = makeSwatch(strokeColor ? strokeColor->color : kNoColor, "Stroke color");
        if (strokeColor)
        {
            connect(swatch, &QToolButton::clicked, this, [=]() {
                const QColor chosen = QColorDialog::getColor(toQColor(strokeColor->color), this, "Stroke Color",
                    QColorDialog::ShowAlphaChannel);
                if (!chosen.isValid())
                    return;
                restyle("Stroke Color", [&](fla::Shape& after) {
                    for (fla::StrokeStyle* style : after.strokes)
                    {
                        if (style && style->fill && style->fill->type() == fla::FillStyle::Type::SolidColor)
                            applyColor(chosen, static_cast<fla::SolidColor*>(style->fill)->color);
                    }
                });
            });
        }
        strokeLayout->addWidget(swatch);
        strokeLayout->addWidget(fieldLabel("Stroke", strokeRow));
        strokeLayout->addStretch();
        HotText* weight = makeHotText(0.1, 200.0, 2);
        weight->setValue(stroke->weight);
        weight->setStep(0.25);
        weight->setToolTip("The stroke's weight, in pixels");
        bindLive(weight, []() {}, [](double) {}, [=]() {
            const double w = weight->value();
            restyle("Stroke Size", [&](fla::Shape& after) {
                for (fla::StrokeStyle* style : after.strokes)
                {
                    if (style)
                        style->weight = w;
                }
            });
        });
        strokeLayout->addWidget(weight);
        section->addWide(row++, strokeRow);

        addStrokeLookFields(section, row, fla::StrokeLook::of(*stroke), [=](const fla::StrokeLook& look) {
            restyle("Stroke Style", [&](fla::Shape& after) {
                // Each stroke made anew in the look, kept where edges find it.
                for (fla::StrokeStyle*& style : after.strokes)
                {
                    if (!style)
                        continue;
                    fla::StrokeStyle* restyled = look.applyTo(*style, &after);
                    for (auto& entry : after.strokesMap)
                    {
                        if (entry.second == style)
                            entry.second = restyled;
                    }
                    delete style;
                    style = restyled;
                }
            });
        });
    }

    // A gradient or bitmap fill: what it is, and Edit to take it to the
    // Color panel, which applies it back to the selection.
    const fla::FillStyle* other = nullptr;
    for (const fla::FillStyle* style : shape->fills)
    {
        if (style && style->type() != fla::FillStyle::Type::SolidColor)
        {
            other = style;
            break;
        }
    }
    if (other)
    {
        QWidget* otherRow = new QWidget(this);
        QHBoxLayout* otherLayout = new QHBoxLayout(otherRow);
        otherLayout->setContentsMargins(0, 0, 0, 0);
        const char* kind = other->type() == fla::FillStyle::Type::LinearGradient ? "Linear gradient"
            : other->type() == fla::FillStyle::Type::RadialGradient ? "Radial gradient" : "Bitmap fill";
        otherLayout->addWidget(fieldLabel(kind, otherRow));
        otherLayout->addStretch();
        QPushButton* editFill = new QPushButton("Edit...", otherRow);
        editFill->setToolTip("Open this fill in the Color panel; Apply to Selection puts it back");
        connect(editFill, &QPushButton::clicked, this, [this, other]() { emit editFillRequested(other); });
        otherLayout->addWidget(editFill);
        section->addWide(row++, otherRow);
    }

    if (row == 0)
        section->addWide(row++, fieldLabel("No fill or stroke", this));

    PropertySection* options = addSection(layout, "Shape Options");
    QWidget* buttons = new QWidget(this);
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    QPushButton* smooth = new QPushButton("Smoothen", buttons);
    QPushButton* straighten = new QPushButton("Straighten", buttons);
    smooth->setEnabled(_links.smoothAction != nullptr);
    straighten->setEnabled(_links.straightenAction != nullptr);
    connect(smooth, &QPushButton::clicked, this, [this]() {
        if (_links.smoothAction)
            _links.smoothAction->trigger();
    });
    connect(straighten, &QPushButton::clicked, this, [this]() {
        if (_links.straightenAction)
            _links.straightenAction->trigger();
    });
    buttonLayout->addWidget(smooth);
    buttonLayout->addWidget(straighten);
    buttonLayout->addStretch();
    options->addWide(0, buttons);
}

void PropertiesPanel::addInstanceSections(QVBoxLayout* layout, fla::SymbolInstance* instance)
{
    // --- Color Effect ---
    PropertySection* effect = addSection(layout, "Color Effect");
    QComboBox* styleBox = new QComboBox(this);
    styleBox->addItems({"None", "Brightness", "Tint", "Alpha", "Advanced"});
    ColorStyle current = colorStyleOf(instance->colorTransform);
    if (current == ColorStyle::None && _colorStyleChoice.contains(instance))
        current = static_cast<ColorStyle>(_colorStyleChoice.value(instance));
    styleBox->setCurrentIndex(static_cast<int>(current));
    effect->addLong(0, "Style", styleBox);

    // Choosing a style resets the transform to that style's starting point,
    // as Animate does, keeping what carries over.
    connect(styleBox, &QComboBox::currentIndexChanged, this, [this, instance](int index) {
        _colorStyleChoice.insert(instance, index);
        fla::ColorTransform after;
        const fla::ColorTransform& before = instance->colorTransform;
        switch (static_cast<ColorStyle>(index))
        {
        case ColorStyle::None:
            break;
        case ColorStyle::Brightness:
            after.brightness = before.brightness;
            break;
        case ColorStyle::Tint:
            after.tintMultiplier = before.tintMultiplier > 0.0 ? before.tintMultiplier : 0.5;
            for (int i = 0; i < 4; ++i)
                after.tintColor[i] = before.tintColor[i];
            break;
        case ColorStyle::Alpha:
            after.alphaMultiplier = before.alphaMultiplier;
            break;
        case ColorStyle::Advanced:
            after = before;
            break;
        }
        push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<fla::ColorTransform>(
            instance->colorTransform, before, after, "Color Effect")));
        emit documentEdited();
    });

    // One helper for every hot text that sets part of the color transform:
    // the field is applied live and committed as one step.
    const auto bindColor = [this, instance](HotText* hot, std::function<void(fla::ColorTransform&, double)> set,
                               const char* name) {
        std::shared_ptr<fla::ColorTransform> before = std::make_shared<fla::ColorTransform>();
        bindLive(hot,
            [=]() { *before = instance->colorTransform; },
            [=](double value) {
                set(instance->colorTransform, value);
                emit documentEdited();
            },
            [=]() {
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<fla::ColorTransform>(
                    instance->colorTransform, *before, instance->colorTransform, name)));
            });
    };

    int row = 1;
    const fla::ColorTransform& color = instance->colorTransform;
    switch (current)
    {
    case ColorStyle::None:
        break;
    case ColorStyle::Brightness:
    {
        HotText* brightness = makeHotText(-100, 100, 0, "%");
        brightness->setValue(color.brightness * 100.0);
        bindColor(brightness, [](fla::ColorTransform& c, double v) { c.brightness = v / 100.0; }, "Brightness");
        effect->addField(row++, 0, "Bright", brightness);
        break;
    }
    case ColorStyle::Tint:
    {
        QToolButton* swatch = makeSwatch(color.tintColor, "Tint color");
        connect(swatch, &QToolButton::clicked, this, [=]() {
            const QColor chosen = QColorDialog::getColor(toQColor(instance->colorTransform.tintColor), this, "Tint Color");
            if (!chosen.isValid())
                return;
            fla::ColorTransform after = instance->colorTransform;
            applyColor(chosen, after.tintColor);
            after.tintColor[3] = 255;
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<fla::ColorTransform>(
                instance->colorTransform, instance->colorTransform, after, "Tint")));
            emit documentEdited();
        });
        effect->addField(row, 0, "Tint", swatch);
        HotText* amount = makeHotText(0, 100, 0, "%");
        amount->setValue(color.tintMultiplier * 100.0);
        bindColor(amount, [](fla::ColorTransform& c, double v) { c.tintMultiplier = v / 100.0; }, "Tint");
        effect->addField(row++, 1, "", amount);
        break;
    }
    case ColorStyle::Alpha:
    {
        HotText* alpha = makeHotText(0, 100, 0, "%");
        alpha->setValue(color.alphaMultiplier * 100.0);
        bindColor(alpha, [](fla::ColorTransform& c, double v) { c.alphaMultiplier = v / 100.0; }, "Alpha");
        effect->addField(row++, 0, "Alpha", alpha);
        break;
    }
    case ColorStyle::Advanced:
    {
        struct Channel
        {
            const char* label;
            double multiplier;
            int offset;
            std::function<void(fla::ColorTransform&, double)> setMultiplier;
            std::function<void(fla::ColorTransform&, double)> setOffset;
        };
        const Channel channels[4] = {
            {"Alpha", color.alphaMultiplier, color.alphaOffset,
                [](fla::ColorTransform& c, double v) { c.alphaMultiplier = v / 100.0; },
                [](fla::ColorTransform& c, double v) { c.alphaOffset = static_cast<int>(v); }},
            {"Red", color.redMultiplier, color.redOffset,
                [](fla::ColorTransform& c, double v) { c.redMultiplier = v / 100.0; },
                [](fla::ColorTransform& c, double v) { c.redOffset = static_cast<int>(v); }},
            {"Green", color.greenMultiplier, color.greenOffset,
                [](fla::ColorTransform& c, double v) { c.greenMultiplier = v / 100.0; },
                [](fla::ColorTransform& c, double v) { c.greenOffset = static_cast<int>(v); }},
            {"Blue", color.blueMultiplier, color.blueOffset,
                [](fla::ColorTransform& c, double v) { c.blueMultiplier = v / 100.0; },
                [](fla::ColorTransform& c, double v) { c.blueOffset = static_cast<int>(v); }},
        };
        for (const Channel& channel : channels)
        {
            HotText* multiplier = makeHotText(-100, 100, 0, "%");
            multiplier->setValue(channel.multiplier * 100.0);
            bindColor(multiplier, channel.setMultiplier, "Advanced Color");
            effect->addField(row, 0, channel.label, multiplier);
            HotText* offset = makeHotText(-255, 255, 0);
            offset->setValue(channel.offset);
            bindColor(offset, channel.setOffset, "Advanced Color");
            effect->addField(row++, 1, "+", offset);
        }
        break;
    }
    }

    // --- Display ---
    PropertySection* display = addSection(layout, "Display");
    QComboBox* blend = new QComboBox(this);
    int blendIndex = 0;
    for (size_t i = 0; i < sizeof(kBlendModes) / sizeof(kBlendModes[0]); ++i)
    {
        blend->addItem(kBlendModeTitles[i]);
        if (instance->blendMode == kBlendModes[i])
            blendIndex = static_cast<int>(i);
    }
    blend->setCurrentIndex(blendIndex);
    connect(blend, &QComboBox::currentIndexChanged, this, [this, instance](int index) {
        const std::string after = index == 0 ? std::string() : std::string(kBlendModes[index]);
        push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<std::string>(
            instance->blendMode, instance->blendMode, after, "Blending")));
        emit documentEdited();
    });
    display->addLong(0, "Blending", blend);

    // --- Looping (graphics) ---
    if (instance->symbolType == fla::SymbolType::Graphic)
    {
        PropertySection* looping = addSection(layout, "Looping");
        QComboBox* options = new QComboBox(this);
        options->addItems({"Loop", "Play Once", "Single Frame", "Ping Pong"});
        const fla::LoopType loopTypes[4] = {fla::LoopType::Loop, fla::LoopType::PlayOnce,
            fla::LoopType::SingleFrame, fla::LoopType::PingPong};
        for (int i = 0; i < 4; ++i)
        {
            if (loopTypes[i] == instance->loopType)
                options->setCurrentIndex(i);
        }
        connect(options, &QComboBox::currentIndexChanged, this, [this, instance, loopTypes](int index) {
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<fla::LoopType>(
                instance->loopType, instance->loopType, loopTypes[index], "Looping")));
            emit documentEdited();
        });
        looping->addLong(0, "Options", options);

        HotText* first = makeHotText(1, 1.0e6, 0);
        first->setValue(instance->firstFrame + 1);
        first->setStep(0.2);
        first->setToolTip("The symbol's frame shown on the first frame of this instance");
        std::shared_ptr<int> before = std::make_shared<int>(0);
        bindLive(first,
            [=]() { *before = instance->firstFrame; },
            [=](double value) {
                instance->firstFrame = static_cast<int>(value) - 1;
                emit documentEdited();
            },
            [=]() {
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<int>(
                    instance->firstFrame, *before, instance->firstFrame, "First Frame")));
            });
        looping->addField(1, 0, "First", first);
    }

    // --- Filters ---
    // Animate gives filters to movie clips and buttons; a graphic shows any
    // it has.
    if (instance->symbolType != fla::SymbolType::Graphic || !instance->filters.empty())
        addFilterSection(layout, instance);
}

void PropertiesPanel::addFilterSection(QVBoxLayout* layout, fla::SymbolInstance* instance)
{
    PropertySection* section = addSection(layout, "Filters");
    int row = 0;
    std::vector<fla::Filter>* filters = &instance->filters;

    // Every change replaces the list as one undo step; a drag edits it live
    // and records where it started.
    auto before = std::make_shared<std::vector<fla::Filter>>();
    const auto change = [this, filters](const std::vector<fla::Filter>& after, const char* name, bool rebuild) {
        push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<std::vector<fla::Filter>>(
            *filters, *filters, after, name)));
        emit documentEdited();
        if (rebuild)
            QTimer::singleShot(0, this, [this]() { refresh(); });
    };
    const auto hot = [&](size_t index, double fla::Filter::*member, double minimum, double maximum, int decimals,
                         const QString& suffix, double scale) {
        HotText* field = makeHotText(minimum, maximum, decimals, suffix);
        field->setValue((*filters)[index].*member * scale);
        bindLive(field,
            [=]() { *before = *filters; },
            [=](double value) {
                (*filters)[index].*member = value / scale;
                emit documentEdited();
            },
            [=]() {
                const std::vector<fla::Filter> after = *filters;
                *filters = *before;
                change(after, "Filter", false);
            });
        return field;
    };
    const auto colorButton = [&](size_t index, uint8_t (fla::Filter::*member)[4], const char* title) {
        QToolButton* swatch = makeSwatch((*filters)[index].*member, title);
        connect(swatch, &QToolButton::clicked, this, [=]() {
            const QColor chosen = QColorDialog::getColor(toQColor((*filters)[index].*member), this, title,
                QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid())
                return;
            std::vector<fla::Filter> after = *filters;
            applyColor(chosen, after[index].*member);
            change(after, "Filter Color", true);
        });
        return swatch;
    };
    const auto checkBox = [&](size_t index, bool fla::Filter::*member, const char* text) {
        QCheckBox* box = new QCheckBox(text, this);
        box->setChecked((*filters)[index].*member);
        connect(box, &QCheckBox::toggled, this, [=](bool on) {
            std::vector<fla::Filter> after = *filters;
            after[index].*member = on;
            change(after, "Filter", false);
        });
        return box;
    };

    for (size_t i = 0; i < filters->size(); ++i)
    {
        const fla::Filter& filter = (*filters)[i];

        // The filter's own row: on or off, its name, order, and remove.
        QWidget* head = new QWidget(this);
        QHBoxLayout* headLayout = new QHBoxLayout(head);
        headLayout->setContentsMargins(0, 6, 0, 0);
        QCheckBox* enabled = new QCheckBox(filterName(filter.type), head);
        QFont bold = enabled->font();
        bold.setBold(true);
        enabled->setFont(bold);
        enabled->setChecked(filter.enabled);
        connect(enabled, &QCheckBox::toggled, this, [=](bool on) {
            std::vector<fla::Filter> after = *filters;
            after[i].enabled = on;
            change(after, on ? "Enable Filter" : "Disable Filter", false);
        });
        headLayout->addWidget(enabled, 1);
        const auto small = [&](const QString& text, const QString& tip) {
            QToolButton* button = new QToolButton(head);
            button->setText(text);
            button->setToolTip(tip);
            button->setAutoRaise(true);
            headLayout->addWidget(button);
            return button;
        };
        QToolButton* up = small(QString::fromUtf8("\u25b2"), "Move up (drawn earlier)");
        up->setEnabled(i > 0);
        connect(up, &QToolButton::clicked, this, [=]() {
            std::vector<fla::Filter> after = *filters;
            std::swap(after[i], after[i - 1]);
            change(after, "Reorder Filters", true);
        });
        QToolButton* down = small(QString::fromUtf8("\u25bc"), "Move down (drawn later)");
        down->setEnabled(i + 1 < filters->size());
        connect(down, &QToolButton::clicked, this, [=]() {
            std::vector<fla::Filter> after = *filters;
            std::swap(after[i], after[i + 1]);
            change(after, "Reorder Filters", true);
        });
        QToolButton* remove = small(QString::fromUtf8("\u2715"), "Remove the filter");
        connect(remove, &QToolButton::clicked, this, [=]() {
            std::vector<fla::Filter> after = *filters;
            after.erase(after.begin() + static_cast<std::ptrdiff_t>(i));
            change(after, "Remove Filter", true);
        });
        section->addWide(row++, head);

        using T = fla::Filter::Type;
        if (filter.type == T::AdjustColor)
        {
            section->addField(row, 0, "Brightness", hot(i, &fla::Filter::brightness, -100, 100, 0, QString(), 1.0));
            section->addField(row++, 1, "Contrast", hot(i, &fla::Filter::contrast, -100, 100, 0, QString(), 1.0));
            section->addField(row, 0, "Saturation", hot(i, &fla::Filter::saturation, -100, 100, 0, QString(), 1.0));
            section->addField(row++, 1, "Hue", hot(i, &fla::Filter::hue, -180, 180, 0, QString(), 1.0));
            continue;
        }

        // Every other filter blurs.
        section->addField(row, 0, "Blur X", hot(i, &fla::Filter::blurX, 0, 255, 1, " px", 1.0));
        section->addField(row++, 1, "Blur Y", hot(i, &fla::Filter::blurY, 0, 255, 1, " px", 1.0));
        QComboBox* quality = new QComboBox(this);
        quality->addItems({"Low", "Medium", "High"});
        quality->setCurrentIndex(std::clamp(filter.quality, 1, 3) - 1);
        connect(quality, &QComboBox::currentIndexChanged, this, [=](int index) {
            std::vector<fla::Filter> after = *filters;
            after[i].quality = index + 1;
            change(after, "Filter Quality", false);
        });
        section->addField(row, 0, "Quality", quality);
        if (filter.type == T::Blur)
        {
            ++row;
            continue;
        }
        section->addField(row++, 1, "Strength", hot(i, &fla::Filter::strength, 0, 1000, 0, "%", 100.0));

        const bool shadowed = filter.type == T::DropShadow || filter.type == T::Bevel ||
            filter.type == T::GradientGlow || filter.type == T::GradientBevel;
        if (shadowed)
        {
            section->addField(row, 0, "Angle", hot(i, &fla::Filter::angle, -360, 360, 0, QString::fromUtf8("\u00b0"), 1.0));
            section->addField(row++, 1, "Distance", hot(i, &fla::Filter::distance, -255, 255, 1, " px", 1.0));
        }
        if (filter.type == T::DropShadow || filter.type == T::Glow)
            section->addField(row++, 0, "Color", colorButton(i, &fla::Filter::color, "Filter Color"));
        if (filter.type == T::Bevel)
        {
            section->addField(row, 0, "Shadow", colorButton(i, &fla::Filter::shadowColor, "Shadow Color"));
            section->addField(row++, 1, "Highlight", colorButton(i, &fla::Filter::highlightColor, "Highlight Color"));
        }
        if (filter.type == T::Bevel || filter.type == T::GradientGlow || filter.type == T::GradientBevel)
        {
            QComboBox* type = new QComboBox(this);
            type->addItems({"Inner", "Outer", "Full"});
            const char* types[3] = {"inner", "outer", "full"};
            for (int k = 0; k < 3; ++k)
            {
                if (filter.bevelType == types[k])
                    type->setCurrentIndex(k);
            }
            connect(type, &QComboBox::currentIndexChanged, this, [=](int index) {
                std::vector<fla::Filter> after = *filters;
                after[i].bevelType = index == 0 ? std::string() : types[std::clamp(index, 0, 2)];
                change(after, "Filter Type", false);
            });
            section->addField(row++, 0, "Type", type);
        }
        if (filter.type == T::GradientGlow || filter.type == T::GradientBevel)
        {
            // The gradient, on the Color panel's bar.
            GradientBar* bar = new GradientBar(this);
            std::vector<fla::DrawingStyle::Stop> stops;
            for (const fla::GradientEntry& entry : filter.entries)
            {
                fla::DrawingStyle::Stop stop;
                stop.ratio = entry.ratio;
                std::copy(entry.color, entry.color + 4, stop.color);
                stops.push_back(stop);
            }
            if (stops.size() < 2)
                stops = fla::DrawingStyle::defaultGradient();
            bar->setStops(stops);
            connect(bar, &GradientBar::stopsChanged, this, [=](bool done) {
                if (!done)
                    return;
                std::vector<fla::Filter> after = *filters;
                after[i].entries.clear();
                for (const fla::DrawingStyle::Stop& stop : bar->stops())
                {
                    fla::GradientEntry entry{stop.ratio, {}};
                    std::copy(stop.color, stop.color + 4, entry.color);
                    after[i].entries.push_back(entry);
                }
                change(after, "Filter Gradient", false);
            });
            connect(bar, &GradientBar::colorRequested, this, [=](int index) {
                const auto& current = bar->stops();
                if (index < 0 || index >= static_cast<int>(current.size()))
                    return;
                const QColor chosen = QColorDialog::getColor(toQColor(current[static_cast<size_t>(index)].color), this,
                    "Gradient Color", QColorDialog::ShowAlphaChannel);
                if (chosen.isValid())
                    bar->setCurrentColor(chosen);
            });
            section->addWide(row++, bar);
        }
        QWidget* options = new QWidget(this);
        QHBoxLayout* optionsLayout = new QHBoxLayout(options);
        optionsLayout->setContentsMargins(0, 0, 0, 0);
        optionsLayout->addWidget(checkBox(i, &fla::Filter::knockout, "Knockout"));
        if (filter.type == T::DropShadow || filter.type == T::Glow)
            optionsLayout->addWidget(checkBox(i, &fla::Filter::inner, filter.type == T::Glow ? "Inner glow" : "Inner shadow"));
        if (filter.type == T::DropShadow)
            optionsLayout->addWidget(checkBox(i, &fla::Filter::hideObject, "Hide object"));
        optionsLayout->addStretch();
        section->addWide(row++, options);
    }

    // Add: each kind with Animate's starting settings.
    QToolButton* add = new QToolButton(this);
    add->setText("Add Filter");
    add->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(add);
    using T = fla::Filter::Type;
    for (const T type : {T::DropShadow, T::Blur, T::Glow, T::Bevel, T::GradientGlow, T::GradientBevel, T::AdjustColor})
    {
        menu->addAction(filterName(type), this, [=]() {
            fla::Filter filter;
            filter.type = type;
            if (type == T::Glow)
            {
                const uint8_t red[4] = {255, 0, 0, 255};
                std::copy(red, red + 4, filter.color);
            }
            if (type == T::GradientGlow || type == T::GradientBevel)
            {
                // Animate starts these white to black, the white clear.
                filter.entries = {fla::GradientEntry{0.0, {255, 255, 255, 0}}, fla::GradientEntry{1.0, {0, 0, 0, 255}}};
            }
            std::vector<fla::Filter> after = *filters;
            after.push_back(filter);
            change(after, "Add Filter", true);
        });
    }
    add->setMenu(menu);
    section->addWide(row++, add);
}

void PropertiesPanel::rebuildObjectTab()
{
    QVBoxLayout* page = _pageLayouts[1];
    clearLayout(page);

    std::vector<fla::Element*> elements;
    if (_links.selection)
    {
        for (fla::DOMElement* selected : _links.selection->elements())
        {
            if (fla::Element* element = dynamic_cast<fla::Element*>(selected))
                elements.push_back(element);
        }
    }

    if (elements.empty())
    {
        page->addWidget(makeHeader(objectGlyph("none", palette()), "Nothing selected"));
        QLabel* hint = new QLabel("Select something on the stage to see its properties.", this);
        hint->setWordWrap(true);
        hint->setContentsMargins(12, 4, 12, 8);
        hint->setEnabled(false);
        page->addWidget(hint);
        return;
    }

    if (elements.size() > 1)
    {
        page->addWidget(makeHeader(objectGlyph("group", palette()),
            QString("Mixed selection (%1 objects)").arg(elements.size())));
        addPositionAndSize(page, elements);
        return;
    }

    fla::Element* element = elements.front();
    switch (element->elementType())
    {
    case fla::Element::Type::Shape:
    {
        fla::Shape* shape = static_cast<fla::Shape*>(element);
        page->addWidget(makeHeader(objectGlyph("shape", palette()),
            shape->isDrawingObject ? "Drawing Object" : "Shape"));
        addPositionAndSize(page, elements);
        addShapeSections(page, shape);
        break;
    }
    case fla::Element::Type::Rectangle:
    case fla::Element::Type::Oval:
    {
        // The primitives are elements of their own, not shapes, so the shape
        // sections (which read a shape's fill and stroke lists) are not theirs.
        page->addWidget(makeHeader(objectGlyph("shape", palette()),
            element->elementType() == fla::Element::Type::Rectangle ? "Rectangle Primitive" : "Oval Primitive"));
        addPositionAndSize(page, elements);
        addPrimitiveSections(page, element);
        break;
    }
    case fla::Element::Type::SymbolInstance:
    {
        fla::SymbolInstance* instance = static_cast<fla::SymbolInstance*>(element);

        // The instance name, where scripts can reach it; graphics have none.
        QLineEdit* nameEdit = nullptr;
        if (instance->symbolType != fla::SymbolType::Graphic)
        {
            nameEdit = new QLineEdit(this);
            nameEdit->setPlaceholderText("<Instance Name>");
            nameEdit->setText(QString::fromStdString(instance->name));
            connect(nameEdit, &QLineEdit::editingFinished, this, [this, instance, nameEdit]() {
                const std::string after = nameEdit->text().trimmed().toStdString();
                if (after == instance->name)
                    return;
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<std::string>(
                    instance->name, instance->name, after, "Instance Name")));
            });
        }
        QComboBox* kind = new QComboBox(this);
        kind->addItems({"Graphic", "Movie Clip", "Button"});
        const fla::SymbolType kinds[3] = {fla::SymbolType::Graphic, fla::SymbolType::MovieClip, fla::SymbolType::Button};
        for (int i = 0; i < 3; ++i)
        {
            if (kinds[i] == instance->symbolType)
                kind->setCurrentIndex(i);
        }
        kind->setToolTip("How this instance behaves: artwork in step with the timeline, its own timeline, or a button");
        connect(kind, &QComboBox::currentIndexChanged, this, [this, instance, kinds](int index) {
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<fla::SymbolType>(
                instance->symbolType, instance->symbolType, kinds[index], "Instance Behavior")));
            emit documentEdited();
        });

        page->addWidget(makeHeader(objectGlyph("symbol", palette()), kind->currentText(), nameEdit));

        PropertySection* symbol = addSection(page, "Instance");
        QWidget* symbolRow = new QWidget(this);
        QHBoxLayout* symbolLayout = new QHBoxLayout(symbolRow);
        symbolLayout->setContentsMargins(0, 0, 0, 0);
        symbolLayout->addWidget(fieldLabel(QString::fromStdString(instance->libraryItemName), this), 1);
        QPushButton* swap = new QPushButton("Swap...", this);
        swap->setToolTip("Show another library symbol in this instance");
        connect(swap, &QPushButton::clicked, this, &PropertiesPanel::swapSymbolRequested);
        symbolLayout->addWidget(swap);
        symbol->addLong(0, "Symbol", symbolRow);
        symbol->addLong(1, "Type", kind);

        addPositionAndSize(page, elements);
        addInstanceSections(page, instance);
        break;
    }
    case fla::Element::Type::StaticText:
    {
        fla::StaticText* text = static_cast<fla::StaticText*>(element);
        const char* kinds[3] = {"Static Text", "Dynamic Text", "Input Text"};
        page->addWidget(makeHeader(objectGlyph("text", palette()), kinds[static_cast<int>(text->textType)]));
        addPositionAndSize(page, elements);

        PropertySection* character = addSection(page, "Character");
        const fla::TextRun* run = text->runs.empty() ? nullptr : &text->runs.front();
        character->addField(0, 0, "Family", fieldLabel(run ? QString::fromStdString(run->face) : QString("-"), this));
        character->addField(1, 0, "Size", fieldLabel(run ? QString::number(run->size, 'f', 1) + " pt" : QString("-"), this));
        if (run)
        {
            QToolButton* swatch = makeSwatch(run->fillColor, "Text color");
            swatch->setEnabled(false);
            character->addField(2, 0, "Color", swatch);
        }
        QComboBox* type = new QComboBox(this);
        type->addItems({"Static Text", "Dynamic Text", "Input Text"});
        type->setCurrentIndex(static_cast<int>(text->textType));
        connect(type, &QComboBox::currentIndexChanged, this, [this](int index) {
            _links.style->textType = index;
            emit toolSettingChanged("textType");
        });
        character->addLong(3, "Type", type);
        break;
    }
    case fla::Element::Type::Group:
        page->addWidget(makeHeader(objectGlyph("group", palette()), "Group"));
        addPositionAndSize(page, elements);
        break;
    case fla::Element::Type::BitmapInstance:
    {
        fla::BitmapInstance* bitmap = static_cast<fla::BitmapInstance*>(element);
        page->addWidget(makeHeader(objectGlyph("bitmap", palette()), "Bitmap"));
        PropertySection* source = addSection(page, "Bitmap");
        source->addField(0, 0, "Item", fieldLabel(QString::fromStdString(bitmap->libraryItemName), this));
        addPositionAndSize(page, elements);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Frame tab

fla::Frame* PropertiesPanel::currentFrame(fla::Layer** layerOut) const
{
    if (layerOut)
        *layerOut = nullptr;
    if (!_links.view || !_links.player)
        return nullptr;
    // The symbol being edited, else the document's main timeline.
    fla::Timeline* timeline = _links.view->scopeTimeline();
    if (!timeline)
    {
        fla::Document* doc = document();
        if (doc)
            timeline = doc->currentScene();
    }
    if (!timeline)
        return nullptr;

    // The current layer is the one new drawings go into; failing that, the
    // first layer that holds frames.
    fla::Layer* layer = nullptr;
    for (fla::Layer* candidate : timeline->layers)
    {
        if (candidate && candidate->selected && candidate->layerType != fla::Layer::Type::Folder)
        {
            layer = candidate;
            break;
        }
    }
    if (!layer)
    {
        for (fla::Layer* candidate : timeline->layers)
        {
            if (candidate && candidate->layerType != fla::Layer::Type::Folder && !candidate->frames.empty())
            {
                layer = candidate;
                break;
            }
        }
    }
    if (!layer)
        return nullptr;
    if (layerOut)
        *layerOut = layer;
    return fla::InsertKeyframeCommand::spanAt(*layer, _links.player->currentFrame());
}

void PropertiesPanel::rebuildFrameTab()
{
    QVBoxLayout* page = _pageLayouts[2];
    clearLayout(page);

    fla::Layer* layer = nullptr;
    fla::Frame* frame = currentFrame(&layer);
    const int shown = _links.player ? _links.player->currentFrame() : 0;

    if (!frame)
    {
        page->addWidget(makeHeader(objectGlyph("none", palette()), QString("Frame %1").arg(shown + 1)));
        QLabel* hint = new QLabel(layer ? "No keyframe spans this frame." : "No layer is current.", this);
        hint->setContentsMargins(12, 4, 12, 8);
        hint->setEnabled(false);
        page->addWidget(hint);
        return;
    }

    const bool keyframe = frame->index == shown;
    QString title = QString("Frame %1").arg(shown + 1);
    if (!keyframe)
        title += QString(" (keyframe %1)").arg(frame->index + 1);
    page->addWidget(makeHeader(objectGlyph("none", palette()), title));

    QLabel* where = fieldLabel(QString("Layer: %1").arg(QString::fromStdString(layer->name)), this);
    where->setContentsMargins(12, 0, 12, 4);
    where->setEnabled(false);
    page->addWidget(where);

    // Edits of a frame's fields: one command per change, named for the Edit
    // menu, and the stage told the frame changed.
    const auto setField = [this](auto& field, const auto& after, const char* name) {
        using T = std::decay_t<decltype(field)>;
        if (field == after)
            return;
        push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<T>(field, field, after, name)));
        emit documentEdited();
    };

    // --- Label ---
    PropertySection* label = addSection(page, "Label");
    QLineEdit* nameEdit = new QLineEdit(this);
    nameEdit->setText(QString::fromStdString(frame->name));
    nameEdit->setPlaceholderText("Name");
    connect(nameEdit, &QLineEdit::editingFinished, this, [=]() {
        setField(frame->name, nameEdit->text().trimmed().toStdString(), "Frame Label");
    });
    label->addLong(0, "Name", nameEdit);

    QComboBox* labelType = new QComboBox(this);
    labelType->addItems({"Name", "Comment", "Anchor"});
    const char* labelTypes[3] = {"name", "comment", "anchor"};
    for (int i = 0; i < 3; ++i)
    {
        if (frame->labelType == labelTypes[i])
            labelType->setCurrentIndex(i);
    }
    connect(labelType, &QComboBox::currentIndexChanged, this, [=](int index) {
        setField(frame->labelType, std::string(index == 0 ? "" : labelTypes[index]), "Label Type");
    });
    label->addLong(1, "Type", labelType);

    // --- Tweening ---
    PropertySection* tweening = addSection(page, "Tweening");
    QComboBox* tween = new QComboBox(this);
    tween->addItems({"No Tween", "Classic Tween", "Shape Tween"});
    tween->setCurrentIndex(static_cast<int>(frame->tweenType));
    connect(tween, &QComboBox::currentIndexChanged, this, [=](int index) {
        setField(frame->tweenType, static_cast<fla::TweenType>(index), "Tween");
    });
    tweening->addLong(0, "Tween", tween);

    if (frame->tweenType != fla::TweenType::None)
    {
        HotText* ease = makeHotText(-100, 100, 0);
        ease->setValue(frame->acceleration);
        ease->setToolTip("Negative eases in (starts slowly), positive eases out");
        // A custom curve or an ease preset takes the place of the value, so
        // the value is shown but not edited while one is set.
        const bool preset = !frame->easeMethod.empty() && frame->easeMethod != "none" &&
            frame->easeMethod != "classic";
        ease->setEnabled(!frame->hasCustomEase && !preset);
        std::shared_ptr<int> before = std::make_shared<int>(0);
        bindLive(ease,
            [=]() { *before = frame->acceleration; },
            [=](double value) {
                frame->acceleration = static_cast<int>(value);
                emit documentEdited();
            },
            [=]() {
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<int>(
                    frame->acceleration, *before, frame->acceleration, "Ease")));
            });
        const QString label = frame->hasCustomEase ? QString("Ease (custom)")
            : preset ? QString("Ease (%1)").arg(QString::fromStdString(frame->easeMethod)) : QString("Ease");
        tweening->addField(1, 0, label, ease);
    }

    if (frame->tweenType == fla::TweenType::Motion)
    {
        QComboBox* rotate = new QComboBox(this);
        rotate->addItems({"Auto", "None", "Clockwise", "Counter Clockwise"});
        const char* rotations[4] = {"auto", "none", "clockwise", "counter-clockwise"};
        for (int i = 0; i < 4; ++i)
        {
            if (frame->motionTweenRotate == rotations[i])
                rotate->setCurrentIndex(i);
        }
        connect(rotate, &QComboBox::currentIndexChanged, this, [=](int index) {
            setField(frame->motionTweenRotate, std::string(index == 0 ? "" : rotations[index]), "Rotate");
        });
        tweening->addField(2, 0, "Rotate", rotate);

        HotText* times = makeHotText(0, 999, 0);
        times->setValue(frame->motionTweenRotateTimes);
        times->setStep(0.2);
        times->setToolTip("Extra whole turns during the tween");
        std::shared_ptr<int> before = std::make_shared<int>(0);
        bindLive(times,
            [=]() { *before = frame->motionTweenRotateTimes; },
            [=](double value) {
                frame->motionTweenRotateTimes = static_cast<int>(value);
                emit documentEdited();
            },
            [=]() {
                push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<int>(
                    frame->motionTweenRotateTimes, *before, frame->motionTweenRotateTimes, "Rotate Times")));
            });
        tweening->addField(2, 1, "x", times);

        QCheckBox* snap = new QCheckBox("Snap", this);
        snap->setChecked(frame->motionTWeenSnap);
        snap->setToolTip("Snap the tweened object to the motion guide");
        connect(snap, &QCheckBox::toggled, this, [=](bool on) { setField(frame->motionTWeenSnap, on, "Snap"); });
        tweening->addField(3, 0, QString(), snap);

        QCheckBox* sync = new QCheckBox("Sync", this);
        sync->setChecked(frame->motionTweenSync);
        sync->setToolTip("Keep a graphic instance's frames in step with the timeline");
        connect(sync, &QCheckBox::toggled, this, [=](bool on) { setField(frame->motionTweenSync, on, "Sync"); });
        tweening->addField(3, 1, QString(), sync);
    }

    // --- Sound ---
    if (keyframe)
    {
        PropertySection* soundSection = addSection(page, "Sound");
        const fla::frame_sound::Settings sound = fla::frame_sound::read(*frame);
        const auto setSound = [this, frame](const fla::frame_sound::Settings& settings, const char* name) {
            const fla::Document* doc = document();
            auto attributes = frame->extraAttributes;
            auto children = frame->extraChildren;
            fla::frame_sound::write(settings, doc ? fla::frame_sound::sampleCount(*doc, settings.name) : 0,
                attributes, children);
            if (attributes == frame->extraAttributes && children == frame->extraChildren)
                return;
            auto* command = new fla::CompoundCommand(name);
            command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                frame->extraAttributes, frame->extraAttributes, attributes, name)));
            command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
                frame->extraChildren, frame->extraChildren, children, name)));
            push(std::unique_ptr<fla::Command>(command));
            emit documentEdited();
            // What the section shows depends on whether there is a sound.
            QTimer::singleShot(0, this, [this]() { refresh(); });
        };

        QComboBox* soundName = new QComboBox(this);
        soundName->addItem("None");
        if (const fla::Document* doc = document())
        {
            for (const fla::Document::SoundInfo& info : doc->sounds)
                soundName->addItem(QString::fromStdString(info.name));
        }
        if (!sound.name.empty())
        {
            const int found = soundName->findText(QString::fromStdString(sound.name));
            if (found < 0)
                soundName->addItem(QString::fromStdString(sound.name));
            soundName->setCurrentIndex(found < 0 ? soundName->count() - 1 : found);
        }
        connect(soundName, &QComboBox::currentIndexChanged, this, [=](int index) {
            fla::frame_sound::Settings settings = sound;
            settings.name = index <= 0 ? std::string() : soundName->itemText(index).toStdString();
            setSound(settings, "Sound");
        });
        soundSection->addLong(0, "Name", soundName);

        if (!sound.name.empty())
        {
            QComboBox* effect = new QComboBox(this);
            effect->addItems({"None", "Left Channel", "Right Channel", "Fade Left to Right", "Fade Right to Left",
                "Fade In", "Fade Out", "Custom"});
            for (int i = 0; i < 8; ++i)
            {
                if (sound.effect == fla::frame_sound::kEffects[i])
                    effect->setCurrentIndex(i);
            }
            // A custom envelope is drawn in Animate's Edit Envelope; here it
            // is only kept.
            if (sound.effect != "custom")
                qobject_cast<QStandardItemModel*>(effect->model())->item(7)->setEnabled(false);
            connect(effect, &QComboBox::currentIndexChanged, this, [=](int index) {
                fla::frame_sound::Settings settings = sound;
                settings.effect = fla::frame_sound::kEffects[std::clamp(index, 0, 7)];
                setSound(settings, "Sound Effect");
            });
            soundSection->addLong(1, "Effect", effect);

            QComboBox* sync = new QComboBox(this);
            sync->addItems({"Event", "Start", "Stop", "Stream"});
            for (int i = 0; i < 4; ++i)
            {
                if (sound.sync == fla::frame_sound::kSyncs[i])
                    sync->setCurrentIndex(i);
            }
            connect(sync, &QComboBox::currentIndexChanged, this, [=](int index) {
                fla::frame_sound::Settings settings = sound;
                settings.sync = fla::frame_sound::kSyncs[std::clamp(index, 0, 3)];
                setSound(settings, "Sound Sync");
            });
            soundSection->addLong(2, "Sync", sync);

            QComboBox* repeat = new QComboBox(this);
            repeat->addItems({"Repeat", "Loop"});
            repeat->setCurrentIndex(sound.loop ? 1 : 0);
            connect(repeat, &QComboBox::currentIndexChanged, this, [=](int index) {
                fla::frame_sound::Settings settings = sound;
                settings.loop = index == 1;
                setSound(settings, "Sound Loop");
            });
            soundSection->addField(3, 0, QString(), repeat);

            if (!sound.loop)
            {
                HotText* times = makeHotText(1, 65535, 0);
                times->setValue(sound.loops);
                times->setToolTip("How many times the sound plays");
                connect(times, &HotText::editFinished, this, [=]() {
                    fla::frame_sound::Settings settings = sound;
                    settings.loops = static_cast<int>(times->value());
                    setSound(settings, "Sound Repeat");
                });
                soundSection->addField(3, 1, "x", times);
            }
        }
    }

    // --- Span ---
    PropertySection* span = addSection(page, "Span");
    span->addField(0, 0, "Starts", fieldLabel(QString::number(frame->index + 1), this));
    span->addField(0, 1, "Lasts", fieldLabel(QString("%1 frames").arg(frame->duration), this));
    span->addField(1, 0, "Objects", fieldLabel(QString::number(frame->elements.size()), this));
}

// ---------------------------------------------------------------------------
// Doc tab

void PropertiesPanel::rebuildDocTab()
{
    QVBoxLayout* page = _pageLayouts[3];
    clearLayout(page);

    fla::Document* doc = document();
    if (!doc)
    {
        page->addWidget(makeHeader(objectGlyph("none", palette()), "No document"));
        return;
    }

    page->addWidget(makeHeader(objectGlyph("none", palette()), "Document"));

    PropertySection* settings = addSection(page, "Document Settings");

    HotText* width = makeHotText(1, 10000, 0, " px");
    width->setValue(doc->width);
    HotText* height = makeHotText(1, 10000, 0, " px");
    height->setValue(doc->height);
    std::shared_ptr<int> beforeWidth = std::make_shared<int>(0);
    std::shared_ptr<int> beforeHeight = std::make_shared<int>(0);
    bindLive(width,
        [=]() { *beforeWidth = doc->width; },
        [=](double value) {
            doc->width = static_cast<int>(value);
            emit documentEdited();
        },
        [=]() {
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<int>(
                doc->width, *beforeWidth, doc->width, "Stage Size")));
        });
    bindLive(height,
        [=]() { *beforeHeight = doc->height; },
        [=](double value) {
            doc->height = static_cast<int>(value);
            emit documentEdited();
        },
        [=]() {
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<int>(
                doc->height, *beforeHeight, doc->height, "Stage Size")));
        });
    settings->addField(0, 0, "W", width);
    settings->addField(0, 1, "H", height);

    HotText* frameRate = makeHotText(0.01, 120.0, 2, " fps");
    frameRate->setValue(doc->frameRate);
    std::shared_ptr<double> beforeRate = std::make_shared<double>(0.0);
    bindLive(frameRate,
        [=]() { *beforeRate = doc->frameRate; },
        [=](double value) {
            doc->frameRate = value;
            emit documentEdited();
        },
        [=]() {
            push(std::unique_ptr<fla::Command>(new fla::SetPropertyCommand<double>(
                doc->frameRate, *beforeRate, doc->frameRate, "Frame Rate")));
        });
    settings->addField(1, 0, "FPS", frameRate);

    QToolButton* stage = makeSwatch(doc->backgroundColor, "Stage color");
    connect(stage, &QToolButton::clicked, this, [this, doc]() {
        const QColor chosen = QColorDialog::getColor(toQColor(doc->backgroundColor), this, "Stage Color");
        if (!chosen.isValid())
            return;
        uint8_t after[4];
        applyColor(chosen, after);
        after[3] = 255;
        push(std::unique_ptr<fla::Command>(new fla::SetColorCommand(
            doc->backgroundColor, doc->backgroundColor, after, "Stage Color")));
        emit documentEdited();
    });
    settings->addField(1, 1, "Stage", stage);

    PropertySection* about = addSection(page, "Document Info");
    about->addLong(0, "Version", fieldLabel(QString::fromStdString(doc->xflVersion), this));
    about->addLong(1, "Made with", fieldLabel(QString::fromStdString(doc->creatorInfo), this));
}
