#include "color_panel.h"
#include "theme.h"

#include "../data/bitmap.h"
#include "../data/document.h"
#include "../data/fla_document.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include "hot_text.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

QColor toQColor(const uint8_t color[4])
{
    return QColor(color[0], color[1], color[2], color[3]);
}

void fromQColor(const QColor& color, uint8_t out[4])
{
    out[0] = static_cast<uint8_t>(color.red());
    out[1] = static_cast<uint8_t>(color.green());
    out[2] = static_cast<uint8_t>(color.blue());
    out[3] = static_cast<uint8_t>(color.alpha());
}

/// A light and dark check, behind colors that are partly transparent.
void paintChecks(QPainter& painter, const QRectF& rect)
{
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, Qt::white);
    const int size = 6;
    for (int y = 0; y * size < rect.height(); ++y)
    {
        for (int x = 0; x * size < rect.width(); ++x)
        {
            if ((x + y) % 2)
                painter.fillRect(QRectF(rect.left() + x * size, rect.top() + y * size, size, size), QColor(204, 204, 204));
        }
    }
    painter.restore();
}

QIcon swatchIcon(const QColor& color)
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintChecks(painter, QRectF(1, 1, 20, 20));
    painter.fillRect(QRectF(1, 1, 20, 20), color);
    painter.setPen(theme::color("swatchBorder"));
    painter.drawRect(QRectF(0.5, 0.5, 21, 21));
    return QIcon(pixmap);
}

QToolButton* makeSwatch(QWidget* parent)
{
    QToolButton* button = new QToolButton(parent);
    button->setIconSize(QSize(22, 22));
    button->setAutoRaise(true);
    return button;
}

QLineEdit* makeHex(QWidget* parent)
{
    QLineEdit* edit = new QLineEdit(parent);
    edit->setMaxLength(7);
    edit->setFixedWidth(80);
    edit->setInputMask(">\\#HHHHHH");
    return edit;
}

QString hexOf(const QColor& color)
{
    return color.name(QColor::HexRgb).toUpper();
}

} // namespace

// ---------------------------------------------------------------------------
// GradientBar

GradientBar::GradientBar(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(44);
    setMouseTracking(false);
    setToolTip("Click the bar to add a color; drag a pointer to move it, or down off the bar to remove it; "
        "double-click a pointer for its color");
}

void GradientBar::setStops(const std::vector<fla::DrawingStyle::Stop>& stops)
{
    _stops = stops;
    _current = std::clamp(_current, 0, std::max(0, static_cast<int>(_stops.size()) - 1));
    update();
}

void GradientBar::setCurrentColor(const QColor& color)
{
    if (_current < 0 || _current >= static_cast<int>(_stops.size()))
        return;
    fromQColor(color, _stops[static_cast<size_t>(_current)].color);
    update();
    emit stopsChanged(true);
}

QRectF GradientBar::barRect() const
{
    return QRectF(8.0, 2.0, width() - 16.0, 22.0);
}

int GradientBar::stopAt(const QPointF& point) const
{
    const QRectF bar = barRect();
    int found = -1;
    double best = 7.0;
    for (size_t i = 0; i < _stops.size(); ++i)
    {
        const double x = bar.left() + _stops[i].ratio * bar.width();
        const double distance = std::fabs(point.x() - x);
        if (point.y() >= bar.bottom() - 2.0 && distance <= best)
        {
            best = distance;
            found = static_cast<int>(i);
        }
    }
    return found;
}

QColor GradientBar::colorAt(double ratio) const
{
    if (_stops.empty())
        return Qt::black;
    if (ratio <= _stops.front().ratio)
        return toQColor(_stops.front().color);
    for (size_t i = 1; i < _stops.size(); ++i)
    {
        const auto& a = _stops[i - 1];
        const auto& b = _stops[i];
        if (ratio <= b.ratio)
        {
            const double t = b.ratio > a.ratio ? (ratio - a.ratio) / (b.ratio - a.ratio) : 0.0;
            const auto mix = [t](uint8_t x, uint8_t y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
            return QColor(mix(a.color[0], b.color[0]), mix(a.color[1], b.color[1]), mix(a.color[2], b.color[2]),
                mix(a.color[3], b.color[3]));
        }
    }
    return toQColor(_stops.back().color);
}

void GradientBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bar = barRect();
    paintChecks(painter, bar);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    for (const auto& stop : _stops)
        gradient.setColorAt(std::clamp(stop.ratio, 0.0, 1.0), toQColor(stop.color));
    painter.fillRect(bar, gradient);
    painter.setPen(theme::color("swatchBorder"));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bar);

    // A pointer under the bar for each stop, filled with its color; the one
    // picked is outlined in the accent color.
    for (size_t i = 0; i < _stops.size(); ++i)
    {
        if (_removing && static_cast<int>(i) == _current)
            continue;
        const double x = bar.left() + _stops[i].ratio * bar.width();
        const double top = bar.bottom() + 1.0;
        QPainterPath pointer;
        pointer.moveTo(x, top);
        pointer.lineTo(x + 6.0, top + 6.0);
        pointer.lineTo(x + 6.0, top + 16.0);
        pointer.lineTo(x - 6.0, top + 16.0);
        pointer.lineTo(x - 6.0, top + 6.0);
        pointer.closeSubpath();
        painter.setBrush(toQColor(_stops[i].color));
        const bool picked = static_cast<int>(i) == _current;
        painter.setPen(QPen(picked ? palette().color(QPalette::Highlight) : theme::color("swatchBorder"), picked ? 2.0 : 1.0));
        painter.drawPath(pointer);
    }
}

void GradientBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QRectF bar = barRect();
    int index = stopAt(event->position());
    if (index < 0 && bar.adjusted(0, 0, 0, 4).contains(event->position()) && _stops.size() < 15)
    {
        // A new stop takes the color the gradient has there.
        const double ratio = std::clamp((event->position().x() - bar.left()) / bar.width(), 0.0, 1.0);
        fla::DrawingStyle::Stop stop;
        stop.ratio = ratio;
        fromQColor(colorAt(ratio), stop.color);
        const auto at = std::find_if(_stops.begin(), _stops.end(),
            [ratio](const fla::DrawingStyle::Stop& each) { return each.ratio > ratio; });
        index = static_cast<int>(_stops.insert(at, stop) - _stops.begin());
        _current = index;
        emit stopsChanged(true);
    }
    if (index < 0)
        return;
    _current = index;
    _dragging = true;
    emit currentChanged(_current);
    update();
}

void GradientBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!_dragging || _current < 0 || _current >= static_cast<int>(_stops.size()))
        return;
    const QRectF bar = barRect();
    // Pulled well below the bar, a stop is on its way out, as long as two
    // are left.
    _removing = _stops.size() > 2 && event->position().y() > bar.bottom() + 40.0;
    double ratio = std::clamp((event->position().x() - bar.left()) / bar.width(), 0.0, 1.0);
    fla::DrawingStyle::Stop moved = _stops[static_cast<size_t>(_current)];
    moved.ratio = ratio;
    _stops.erase(_stops.begin() + _current);
    const auto at = std::find_if(_stops.begin(), _stops.end(),
        [ratio](const fla::DrawingStyle::Stop& each) { return each.ratio > ratio; });
    _current = static_cast<int>(_stops.insert(at, moved) - _stops.begin());
    update();
    emit stopsChanged(false);
}

void GradientBar::mouseReleaseEvent(QMouseEvent*)
{
    if (!_dragging)
        return;
    _dragging = false;
    if (_removing && _current >= 0 && _current < static_cast<int>(_stops.size()))
    {
        _stops.erase(_stops.begin() + _current);
        _current = std::min(_current, static_cast<int>(_stops.size()) - 1);
    }
    _removing = false;
    update();
    emit currentChanged(_current);
    emit stopsChanged(true);
}

void GradientBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = stopAt(event->position());
    if (index >= 0)
        emit colorRequested(index);
}

// ---------------------------------------------------------------------------
// ColorPanel

ColorPanel::ColorPanel(fla::DrawingStyle* style, QWidget* parent)
    : QWidget(parent)
    , _style(style)
{
    _kind = new QComboBox(this);
    _kind->addItems({"None", "Solid Color", "Linear Gradient", "Radial Gradient", "Bitmap Fill"});
    _kind->setToolTip("What new art is filled with");

    _pages = new QStackedWidget(this);

    // None.
    QLabel* none = new QLabel("New art has no fill.", this);
    none->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    none->setEnabled(false);
    _pages->addWidget(none);

    // Solid color.
    QWidget* solid = new QWidget(this);
    QVBoxLayout* solidPage = new QVBoxLayout(solid);
    solidPage->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout* solidLayout = new QHBoxLayout();
    _solidSwatch = makeSwatch(solid);
    _solidSwatch->setToolTip("Fill color");
    _solidHex = makeHex(solid);
    solidLayout->addWidget(_solidSwatch);
    solidLayout->addWidget(_solidHex);
    solidLayout->addStretch();
    solidPage->addLayout(solidLayout);

    // The color by its channels: RGB or HSB, and alpha.
    QHBoxLayout* channelRow = new QHBoxLayout();
    _channelMode = new QComboBox(solid);
    _channelMode->addItems({"RGB", "HSB"});
    _channelMode->setToolTip("Edit the color as red, green and blue, or as hue, saturation and brightness");
    channelRow->addWidget(_channelMode);
    for (int i = 0; i < 3; ++i)
    {
        _channelLabels[i] = new QLabel(solid);
        _channels[i] = new HotSpinBox(solid);
        channelRow->addWidget(_channelLabels[i]);
        channelRow->addWidget(_channels[i]);
    }
    channelRow->addStretch();
    solidPage->addLayout(channelRow);
    QHBoxLayout* alphaRow = new QHBoxLayout();
    alphaRow->addWidget(new QLabel("Alpha", solid));
    _alpha = new HotSpinBox(solid);
    _alpha->setRange(0, 100);
    _alpha->setSuffix("%");
    alphaRow->addWidget(_alpha);
    alphaRow->addStretch();
    solidPage->addLayout(alphaRow);
    solidPage->addStretch();
    const auto fromChannels = [this]() {
        if (_updating)
            return;
        QColor color;
        if (_channelMode->currentIndex() == 0)
            color = QColor(_channels[0]->value(), _channels[1]->value(), _channels[2]->value());
        else
            color = QColor::fromHsv(_channels[0]->value() % 360, _channels[1]->value() * 255 / 100,
                _channels[2]->value() * 255 / 100);
        color.setAlpha(_alpha->value() * 255 / 100);
        fromQColor(color, _style->fillColor);
        refresh();
        emit styleChanged();
    };
    for (HotSpinBox* box : _channels)
        connect(box, &HotSpinBox::valueChanged, this, fromChannels);
    connect(_alpha, &HotSpinBox::valueChanged, this, fromChannels);
    connect(_channelMode, &QComboBox::currentIndexChanged, this, [this]() {
        const bool was = _updating;
        _updating = true;
        updateChannels();
        _updating = was;
    });
    _pages->addWidget(solid);
    connect(_solidSwatch, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(toQColor(_style->fillColor), this, "Fill Color",
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        fromQColor(chosen, _style->fillColor);
        refresh();
        emit styleChanged();
    });
    connect(_solidHex, &QLineEdit::editingFinished, this, [this]() {
        QColor color(_solidHex->text());
        if (!color.isValid())
            return;
        color.setAlpha(_style->fillColor[3]);
        fromQColor(color, _style->fillColor);
        refresh();
        emit styleChanged();
    });

    // Gradient.
    QWidget* gradient = new QWidget(this);
    QVBoxLayout* gradientLayout = new QVBoxLayout(gradient);
    gradientLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout* flowRow = new QHBoxLayout();
    flowRow->addWidget(new QLabel("Flow", gradient));
    _flow = new QComboBox(gradient);
    _flow->addItems({"Extend", "Reflect", "Repeat"});
    _flow->setToolTip("How the colors go on past the ends of the gradient");
    flowRow->addWidget(_flow);
    _linearRGB = new QCheckBox("Linear RGB", gradient);
    _linearRGB->setToolTip("Blend the colors in linear RGB, as SVG does");
    flowRow->addWidget(_linearRGB);
    flowRow->addStretch();
    gradientLayout->addLayout(flowRow);
    _bar = new GradientBar(gradient);
    gradientLayout->addWidget(_bar);
    QHBoxLayout* stopRow = new QHBoxLayout();
    _stopSwatch = makeSwatch(gradient);
    _stopSwatch->setToolTip("The picked pointer's color");
    _stopHex = makeHex(gradient);
    stopRow->addWidget(_stopSwatch);
    stopRow->addWidget(_stopHex);
    stopRow->addStretch();
    gradientLayout->addLayout(stopRow);
    gradientLayout->addStretch();
    _pages->addWidget(gradient);

    connect(_flow, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (_updating)
            return;
        static const char* const spreads[3] = {"", "reflect", "repeat"};
        _style->gradientSpread = spreads[std::clamp(index, 0, 2)];
        emit styleChanged();
    });
    connect(_linearRGB, &QCheckBox::toggled, this, [this](bool on) {
        if (_updating)
            return;
        _style->gradientLinearRGB = on;
        emit styleChanged();
    });
    connect(_bar, &GradientBar::stopsChanged, this, [this](bool done) {
        _style->gradient = _bar->stops();
        updateStopColor();
        if (done)
            emit styleChanged();
    });
    connect(_bar, &GradientBar::currentChanged, this, [this](int) { updateStopColor(); });
    const auto pickStopColor = [this]() {
        const int index = _bar->current();
        if (index < 0 || index >= static_cast<int>(_bar->stops().size()))
            return;
        const QColor chosen = QColorDialog::getColor(toQColor(_bar->stops()[static_cast<size_t>(index)].color), this,
            "Gradient Color", QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            setStopColor(chosen);
    };
    connect(_stopSwatch, &QToolButton::clicked, this, pickStopColor);
    connect(_bar, &GradientBar::colorRequested, this, pickStopColor);
    connect(_stopHex, &QLineEdit::editingFinished, this, [this]() {
        QColor color(_stopHex->text());
        const int index = _bar->current();
        if (!color.isValid() || index < 0 || index >= static_cast<int>(_bar->stops().size()))
            return;
        color.setAlpha(_bar->stops()[static_cast<size_t>(index)].color[3]);
        setStopColor(color);
    });

    // Bitmap.
    QWidget* bitmap = new QWidget(this);
    QVBoxLayout* bitmapLayout = new QVBoxLayout(bitmap);
    bitmapLayout->setContentsMargins(0, 0, 0, 0);
    _bitmaps = new QListWidget(bitmap);
    _bitmaps->setToolTip("The library bitmap new art is filled with");
    bitmapLayout->addWidget(_bitmaps);
    QPushButton* import = new QPushButton("Import...", bitmap);
    connect(import, &QPushButton::clicked, this, &ColorPanel::importBitmapRequested);
    QHBoxLayout* importRow = new QHBoxLayout();
    importRow->addStretch();
    importRow->addWidget(import);
    bitmapLayout->addLayout(importRow);
    _pages->addWidget(bitmap);
    connect(_bitmaps, &QListWidget::currentTextChanged, this, [this](const QString& name) {
        if (_updating || name.isEmpty())
            return;
        _style->fillBitmap = name.toStdString();
        emit styleChanged();
    });

    connect(_kind, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (_updating)
            return;
        _style->hasFill = index != 0;
        static const fla::DrawingStyle::FillKind kinds[5] = {fla::DrawingStyle::FillKind::Solid,
            fla::DrawingStyle::FillKind::Solid, fla::DrawingStyle::FillKind::Linear,
            fla::DrawingStyle::FillKind::Radial, fla::DrawingStyle::FillKind::Bitmap};
        _style->fillKind = kinds[std::clamp(index, 0, 4)];
        if (_style->fillKind == fla::DrawingStyle::FillKind::Bitmap && _style->fillBitmap.empty() &&
            _bitmaps->count() > 0)
            _style->fillBitmap = _bitmaps->item(0)->text().toStdString();
        refresh();
        emit styleChanged();
    });

    QPushButton* apply = new QPushButton("Apply to Selection", this);
    apply->setToolTip("Fill the selected shapes with this");
    connect(apply, &QPushButton::clicked, this, &ColorPanel::applyToSelectionRequested);
    QPushButton* strokes = new QPushButton("Apply to Strokes", this);
    strokes->setToolTip("Draw the selected shapes' strokes with this color, gradient or bitmap");
    connect(strokes, &QPushButton::clicked, this, &ColorPanel::applyToStrokesRequested);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    QHBoxLayout* kindRow = new QHBoxLayout();
    kindRow->addWidget(new QLabel("Fill", this));
    kindRow->addWidget(_kind, 1);
    layout->addLayout(kindRow);
    layout->addWidget(_pages);
    QHBoxLayout* applyRow = new QHBoxLayout();
    applyRow->addStretch();
    applyRow->addWidget(strokes);
    applyRow->addWidget(apply);
    layout->addLayout(applyRow);

    refresh();
}

void ColorPanel::setDocument(const fla::FLADocument* document)
{
    _document = document;
    refresh();
}

void ColorPanel::refresh()
{
    _updating = true;
    int kind = 1;
    if (!_style->hasFill)
        kind = 0;
    else if (_style->fillKind == fla::DrawingStyle::FillKind::Linear)
        kind = 2;
    else if (_style->fillKind == fla::DrawingStyle::FillKind::Radial)
        kind = 3;
    else if (_style->fillKind == fla::DrawingStyle::FillKind::Bitmap)
        kind = 4;
    _kind->setCurrentIndex(kind);
    _pages->setCurrentIndex(kind == 3 ? 2 : kind == 4 ? 3 : kind);

    const QColor fill = toQColor(_style->fillColor);
    _solidSwatch->setIcon(swatchIcon(fill));
    _solidHex->setText(hexOf(fill));
    updateChannels();

    if (_style->gradient.size() < 2)
        _style->gradient = fla::DrawingStyle::defaultGradient();
    _bar->setStops(_style->gradient);
    _flow->setCurrentIndex(_style->gradientSpread == "reflect" ? 1 : _style->gradientSpread == "repeat" ? 2 : 0);
    _linearRGB->setChecked(_style->gradientLinearRGB);
    updateStopColor();

    refreshBitmaps();
    _updating = false;
}

void ColorPanel::updateChannels()
{
    const QColor fill = toQColor(_style->fillColor);
    const bool rgb = _channelMode->currentIndex() == 0;
    const char* names[2][3] = {{"R", "G", "B"}, {"H", "S", "B"}};
    const int tops[2][3] = {{255, 255, 255}, {359, 100, 100}};
    const int values[2][3] = {{fill.red(), fill.green(), fill.blue()},
        {std::max(0, fill.hsvHue()), fill.hsvSaturation() * 100 / 255, fill.value() * 100 / 255}};
    const int mode = rgb ? 0 : 1;
    for (int i = 0; i < 3; ++i)
    {
        _channelLabels[i]->setText(names[mode][i]);
        _channels[i]->setRange(0, tops[mode][i]);
        _channels[i]->setSuffix(!rgb && i == 0 ? QString(QChar(0x00B0)) : (!rgb ? QString("%") : QString()));
        _channels[i]->setValue(values[mode][i]);
    }
    _alpha->setValue(static_cast<int>(std::lround(fill.alpha() * 100.0 / 255.0)));
}

void ColorPanel::refreshBitmaps()
{
    const bool was = _updating;
    _updating = true;
    QStringList names;
    const fla::Document* doc = _document ? _document->document : nullptr;
    if (doc)
    {
        for (const fla::Resource* resource : doc->resources)
        {
            if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
                names << QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name);
        }
    }
    QStringList shown;
    for (int i = 0; i < _bitmaps->count(); ++i)
        shown << _bitmaps->item(i)->text();
    if (shown != names)
    {
        _bitmaps->clear();
        _bitmaps->addItems(names);
    }
    const QList<QListWidgetItem*> found =
        _bitmaps->findItems(QString::fromStdString(_style->fillBitmap), Qt::MatchExactly);
    if (!found.isEmpty())
        _bitmaps->setCurrentItem(found.front());
    _updating = was;
}

void ColorPanel::updateStopColor()
{
    const int index = _bar->current();
    if (index < 0 || index >= static_cast<int>(_bar->stops().size()))
        return;
    const QColor color = toQColor(_bar->stops()[static_cast<size_t>(index)].color);
    _stopSwatch->setIcon(swatchIcon(color));
    _stopHex->setText(hexOf(color));
}

void ColorPanel::setStopColor(const QColor& color)
{
    _bar->setCurrentColor(color);
}
