#include "brush_properties_dialog.h"
#include "file_pick.h"
#include "modal.h"

#include "../edit/brush_texture.h"
#include "../edit/drawing_style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include "hot_text.h"
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace {

constexpr double kPi = 3.14159265358979323846;

/// A built-in texture's tile as an image, for previews.
QImage tileImage(const std::string& name)
{
    const fla::texture::Tile tile = fla::texture::builtInTile(name);
    if (tile.size <= 0)
        return QImage();
    QImage image(tile.size, tile.size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tile.size; ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < tile.size; ++x)
        {
            const int a = tile.coverage[static_cast<size_t>(y) * tile.size + x];
            line[x] = qPremultiply(qRgba(0, 0, 0, a));
        }
    }
    return image;
}

} // namespace

QImage BrushPropertiesDialog::strokePreview(const fla::BrushSettings& given, const fla::DrawingStyle& style,
    const QSize& size, bool bitmap)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // An S across the thumbnail, pressing harder in the middle and leaning
    // more as it goes, so pressure, tilt and tapers all show.
    fla::BrushSettings brush = given;
    brush.size = std::min(brush.size, size.height() * 0.4);
    brush.stabilizer = 0.0;
    std::vector<fla::BrushSample> samples;
    const int count = 80;
    for (int i = 0; i <= count; ++i)
    {
        const double t = static_cast<double>(i) / count;
        fla::BrushSample sample;
        sample.x = size.width() * (0.08 + 0.84 * t);
        sample.y = size.height() * (0.5 - 0.22 * std::sin(t * 2.0 * kPi));
        sample.pressure = 0.15 + 0.85 * std::sin(t * kPi);
        sample.tiltX = 50.0 * t;
        sample.rotation = 90.0 * t;
        sample.time = i * 12.0;
        samples.push_back(sample);
    }
    const std::vector<fla::BrushDab> dabs = fla::BrushStroke::dabs(samples, brush, 1.0, true);
    const QColor color = QColor(style.fillColor[0], style.fillColor[1], style.fillColor[2]).value() > 220
        ? QColor(40, 40, 40) : QColor(style.fillColor[0], style.fillColor[1], style.fillColor[2]);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    if (bitmap)
    {
        const double flow = std::clamp(style.bitmapFlow / 100.0, 0.01, 1.0);
        const double softness = std::clamp(style.bitmapSoftness / 100.0, 0.0, 0.99);
        for (size_t i = 0; i < dabs.size(); i += 2)
        {
            const fla::BrushDab& dab = dabs[i];
            QRadialGradient gradient(QPointF(0, 0), std::max(0.5, dab.width / 2.0));
            QColor solid = color;
            QColor clear = color;
            clear.setAlpha(0);
            gradient.setColorAt(0.0, solid);
            gradient.setColorAt(1.0 - softness, solid);
            gradient.setColorAt(1.0, clear);
            double alpha = dab.opacity * flow;
            if (style.bitmapOpacityPressure || style.bitmapFlowPressure)
                alpha *= dab.pressure;
            painter.save();
            painter.translate(dab.x, dab.y);
            painter.rotate(dab.angle * 180.0 / kPi);
            painter.scale(1.0, dab.width > 0.0 ? dab.height / dab.width : 1.0);
            painter.setOpacity(std::clamp(alpha, 0.02, 1.0));
            painter.setBrush(gradient);
            painter.drawEllipse(QPointF(0, 0), dab.width / 2.0, dab.width / 2.0);
            painter.restore();
        }
    }
    else
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (const std::vector<fla::Point>& polygon : fla::BrushStroke::sweep(dabs, 1.0))
        {
            QPolygonF ring;
            for (const fla::Point& point : polygon)
                ring << QPointF(point.x, point.y);
            path.addPolygon(ring);
            path.closeSubpath();
        }
        painter.setBrush(color);
        painter.drawPath(path);
    }
    // The grain, when there is one Phoenix makes.
    const QImage tile = tileImage(style.brushTexture);
    if (!tile.isNull())
    {
        QBrush grain(tile);
        QTransform placed;
        placed.rotate(style.brushTextureRotation);
        placed.scale(style.brushTextureScale / 100.0, style.brushTextureScale / 100.0);
        grain.setTransform(placed);
        // Where the grain is light, the paint is taken away by its strength.
        const double strength = std::clamp(style.brushTextureStrength / 100.0, 0.0, 1.0);
        QImage solid(size, QImage::Format_ARGB32_Premultiplied);
        solid.fill(QColor(0, 0, 0, static_cast<int>(255 * (1.0 - strength))));
        {
            QPainter add(&solid);
            add.setOpacity(strength);
            add.fillRect(solid.rect(), grain);
        }
        painter.setOpacity(1.0);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(0, 0, solid);
    }
    return image;
}

BrushPropertiesDialog::BrushPropertiesDialog(fla::DrawingStyle& style, const std::string& list, Library library,
    QWidget* parent)
    : QDialog(parent)
    , _style(style)
    , _list(list)
    , _library(std::move(library))
{
    setWindowTitle("Brush Properties");
    QVBoxLayout* layout = new QVBoxLayout(this);

    // Presets: the list, how it shows, and what can be done with it.
    QHBoxLayout* top = new QHBoxLayout;
    QVBoxLayout* presetColumn = new QVBoxLayout;
    QHBoxLayout* presetHeader = new QHBoxLayout;
    presetHeader->addWidget(new QLabel("<b>Presets</b>", this));
    presetHeader->addStretch(1);
    _view = new QComboBox(this);
    _view->addItems({"Small Thumbnails", "Large Thumbnails", "Stroke View"});
    _view->setCurrentIndex(std::clamp(QSettings().value("brushPresets/view", 2).toInt(), 0, 2));
    presetHeader->addWidget(_view);
    presetColumn->addLayout(presetHeader);
    _presets = new QListWidget(this);
    _presets->setMinimumSize(300, 220);
    presetColumn->addWidget(_presets, 1);
    QHBoxLayout* presetButtons = new QHBoxLayout;
    QPushButton* add = new QPushButton("New...", this);
    add->setToolTip("Keep the brush as it is now as a new preset");
    QPushButton* rename = new QPushButton("Rename...", this);
    QPushButton* remove = new QPushButton("Delete", this);
    QPushButton* importSet = new QPushButton("Import...", this);
    importSet->setToolTip("Add the presets of an exported list");
    QPushButton* exportSet = new QPushButton("Export...", this);
    exportSet->setToolTip("Write this list to a file to share");
    for (QPushButton* button : {add, rename, remove, importSet, exportSet})
        presetButtons->addWidget(button);
    presetColumn->addLayout(presetButtons);
    top->addLayout(presetColumn, 1);

    QVBoxLayout* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(new QLabel("<b>Stroke</b>", this));
    _preview = new QLabel(this);
    _preview->setFixedSize(240, 80);
    _preview->setStyleSheet("background: white; border: 1px solid #888;");
    previewColumn->addWidget(_preview);
    previewColumn->addStretch(1);
    top->addLayout(previewColumn);
    layout->addLayout(top);

    // The settings, a tab a group.
    QTabWidget* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);
    const auto page = [&](const QString& name) {
        QWidget* widget = new QWidget(tabs);
        QFormLayout* form = new QFormLayout(widget);
        tabs->addTab(widget, name);
        return form;
    };
    using Brush = fla::BrushSettings;
    const auto number = [&](QFormLayout* form, const QString& label, double low, double high, int decimals,
                            const QString& suffix, const QString& tip, std::function<double()> get,
                            std::function<void(double)> set) {
        HotDoubleSpinBox* box = new HotDoubleSpinBox(this);
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setSuffix(suffix);
        box->setToolTip(tip);
        connect(box, &HotDoubleSpinBox::valueChanged, this, [this, set](double value) {
            if (_refreshing)
                return;
            set(value);
            touched();
        });
        _refreshers.push_back([box, get]() {
            QSignalBlocker block(box);
            box->setValue(get());
        });
        form->addRow(label, box);
        return box;
    };
    const auto check = [&](QFormLayout* form, const QString& text, const QString& tip, std::function<bool()> get,
                           std::function<void(bool)> set) {
        QCheckBox* box = new QCheckBox(text, this);
        box->setToolTip(tip);
        connect(box, &QCheckBox::toggled, this, [this, set](bool on) {
            if (_refreshing)
                return;
            set(on);
            touched();
        });
        _refreshers.push_back([box, get]() {
            QSignalBlocker block(box);
            box->setChecked(get());
        });
        form->addRow(QString(), box);
        return box;
    };
    const auto choice = [&](QFormLayout* form, const QString& label, const QStringList& items, const QString& tip,
                            std::function<int()> get, std::function<void(int)> set) {
        QComboBox* box = new QComboBox(this);
        box->addItems(items);
        box->setToolTip(tip);
        connect(box, &QComboBox::currentIndexChanged, this, [this, set](int index) {
            if (_refreshing || index < 0)
                return;
            set(index);
            touched();
        });
        _refreshers.push_back([box, get]() {
            QSignalBlocker block(box);
            box->setCurrentIndex(get());
        });
        form->addRow(label, box);
        return box;
    };
    // A texture: none, Phoenix's own, or a library bitmap.
    const auto textureBox = [&](QFormLayout* form, const QString& label, const QString& tip,
                                std::function<std::string()> get, std::function<void(const std::string&)> set) {
        QComboBox* box = new QComboBox(this);
        box->setToolTip(tip);
        const auto fill = [this, box]() {
            box->clear();
            box->addItem("None", QString());
            for (const std::string& name : fla::texture::builtIn())
                box->addItem(QString::fromStdString(name), QString::fromStdString(name));
            if (_library.bitmapNames)
                for (const QString& name : _library.bitmapNames())
                    box->addItem("Bitmap: " + name, QString(fla::texture::kBitmapPrefix) + name);
        };
        connect(box, &QComboBox::currentIndexChanged, this, [this, box, set](int) {
            if (_refreshing)
                return;
            set(box->currentData().toString().toStdString());
            touched();
        });
        _refreshers.push_back([box, get, fill]() {
            QSignalBlocker block(box);
            fill();
            const int found = box->findData(QString::fromStdString(get()));
            box->setCurrentIndex(found >= 0 ? found : 0);
        });
        form->addRow(label, box);
    };
    fla::DrawingStyle& s = _style;

    // --- Tip ---
    QFormLayout* tip = page("Tip");
    choice(tip, "Shape", {"Ellipse", "Rectangle", "Triangle", "Diamond", "Hexagon", "Star", "Custom"},
        "The tip's shape; Custom is an outline taken from a library bitmap",
        [this]() { return static_cast<int>(brush().tip); },
        [this](int i) { brush().tip = static_cast<Brush::Tip>(i); });
    QPushButton* fromBitmap = new QPushButton("Tip from Library Bitmap...", this);
    fromBitmap->setToolTip("Trace a library bitmap's outline as a custom tip; the bitmap brush stamps the "
                           "image itself");
    connect(fromBitmap, &QPushButton::clicked, this, [this]() {
        const QStringList names = _library.bitmapNames ? _library.bitmapNames() : QStringList();
        if (names.isEmpty())
        {
            modal::information(this, "Tip from Library Bitmap", "The library has no bitmaps.");
            return;
        }
        modal::getItem(this, "Tip from Library Bitmap", "Bitmap", names, 0, false, [this](const QString& name) {
            if (name.isEmpty())
                return;
            const QImage source = _library.bitmapImage ? _library.bitmapImage(name) : QImage();
            if (source.isNull())
                return;
            // Coverage: the image's alpha where it has one, else its dark parts.
            const QImage small = source.convertToFormat(QImage::Format_ARGB32).scaled(96, 96, Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            bool transparent = false;
            for (int y = 0; y < small.height() && !transparent; ++y)
                for (int x = 0; x < small.width() && !transparent; ++x)
                    transparent = qAlpha(small.pixel(x, y)) < 250;
            std::vector<uint8_t> alpha(static_cast<size_t>(small.width()) * small.height());
            for (int y = 0; y < small.height(); ++y)
                for (int x = 0; x < small.width(); ++x)
                {
                    const QRgb pixel = small.pixel(x, y);
                    alpha[static_cast<size_t>(y) * small.width() + x] =
                        static_cast<uint8_t>(transparent ? qAlpha(pixel) : 255 - qGray(pixel));
                }
            std::vector<fla::Point> outline = fla::traceTip(alpha, small.width(), small.height());
            if (outline.size() < 3)
            {
                modal::information(this, "Tip from Library Bitmap", "Nothing in that bitmap makes an outline.");
                return;
            }
            brush().customTip = outline;
            brush().tipSource = name.toStdString();
            brush().tip = fla::BrushSettings::Tip::Custom;
            brush().roundness = 100.0;
            _style.bitmapTip = name.toStdString();
            refresh();
            touched();
        });
    });
    tip->addRow(QString(), fromBitmap);
    QPushButton* roundTip = new QPushButton("Plain Bitmap Tip", this);
    roundTip->setToolTip("The bitmap brush goes back to its round soft tip");
    connect(roundTip, &QPushButton::clicked, this, [this]() {
        _style.bitmapTip.clear();
        touched();
    });
    tip->addRow(QString(), roundTip);
    number(tip, "Size", 0.5, 500.0, 1, " px", "The tip's width at full pressure",
        [this]() { return brush().size; }, [this](double v) { brush().size = v; });
    number(tip, "Roundness", 1.0, 100.0, 0, " %", "The tip's height as a percentage of its width",
        [this]() { return brush().roundness; }, [this](double v) { brush().roundness = v; });
    number(tip, "Angle", -180.0, 180.0, 0, QString::fromUtf8("\xC2\xB0"), "The tip's turn, clockwise",
        [this]() { return brush().angle; }, [this](double v) { brush().angle = v; });
    choice(tip, "Angle follows", {"Nothing (constant)", "Stroke direction (heading)", "Pen lean (azimuth)",
                                     "Pen barrel (twist)"},
        "What turns the tip as the stroke goes: the line's direction, the way the pen leans, or the turn of "
        "an art pen's barrel",
        [this]() { return static_cast<int>(brush().effectiveAngleFunction()); },
        [this](int i) {
            brush().angleFunction = static_cast<Brush::AngleFunction>(i);
            // The Nib Brush's Use Tilt turning the tip is the azimuth
            // function; choosing another leaves tilt to the width.
            if (brush().tiltEffect == Brush::TiltEffect::Angle && i != 2)
                brush().useTilt = false;
        });
    number(tip, "Tilt stretch", 0.0, 300.0, 0, " %", "How much a full lean of the pen stretches the tip",
        [this]() { return brush().tiltStretch; }, [this](double v) { brush().tiltStretch = v; });
    check(tip, "Size follows the camera", "The tip keeps its size on screen under a camera zoom",
        [this]() { return brush().sizeFollowsCamera; }, [this](bool on) { brush().sizeFollowsCamera = on; });

    // --- Dynamics ---
    QFormLayout* dynamics = page("Dynamics");
    check(dynamics, "Pressure varies the width", "", [this]() { return brush().usePressure; },
        [this](bool on) { brush().usePressure = on; });
    check(dynamics, "Invert pressure", "Pressing harder gives a thinner line",
        [this]() { return brush().invertPressure; }, [this](bool on) { brush().invertPressure = on; });
    number(dynamics, "Min size", 1.0, 100.0, 0, " %", "The width at the lightest touch",
        [this]() { return brush().minSize; }, [this](double v) { brush().minSize = v; });
    number(dynamics, "Pressure curve", 0.2, 3.0, 2, QString(),
        "Below 1 a light touch gives more width; above 1 it takes a firmer press",
        [this]() { return brush().pressureCurve; }, [this](double v) { brush().pressureCurve = v; });
    check(dynamics, "Tilt broadens the line", "The further the pen leans, the wider the line",
        [this]() { return brush().useTilt && brush().tiltEffect == Brush::TiltEffect::Width; },
        [this](bool on) {
            brush().tiltEffect = Brush::TiltEffect::Width;
            brush().useTilt = on;
        });
    check(dynamics, "Invert tilt", "A steeper lean gives a thinner line",
        [this]() { return brush().invertTilt; }, [this](bool on) { brush().invertTilt = on; });
    choice(dynamics, "Speed", {"No effect", "Faster is thicker", "Faster is thinner"},
        "How the speed of the stroke changes its width",
        [this]() { return static_cast<int>(brush().velocity); },
        [this](int i) { brush().velocity = static_cast<Brush::Velocity>(i); });
    number(dynamics, "Speed sensitivity", 1.0, 100.0, 0, QString(),
        "How quickly speed has its full effect: higher reaches it at a slower stroke",
        [this]() { return brush().velocitySensitivity; }, [this](double v) { brush().velocitySensitivity = v; });
    choice(dynamics, "Taper by", {"Percentage of the stroke", "Distance in pixels"},
        "Whether the tapers are a share of each stroke or a set length",
        [this]() { return static_cast<int>(brush().taperUnits); },
        [this](int i) { brush().taperUnits = static_cast<Brush::TaperUnits>(i); });
    number(dynamics, "Taper start", 0.0, 2000.0, 0, QString(), "How much the start tapers over",
        [this]() { return brush().taperStart; }, [this](double v) { brush().taperStart = v; });
    number(dynamics, "Taper end", 0.0, 2000.0, 0, QString(), "How much the end tapers over",
        [this]() { return brush().taperEnd; }, [this](double v) { brush().taperEnd = v; });
    number(dynamics, "Smoothing", 0.0, 100.0, 0, QString(), "How much of the hand's wobble is taken out",
        [this]() { return static_cast<double>(brush().smoothing); },
        [this](double v) { brush().smoothing = static_cast<int>(std::lround(v)); });
    number(dynamics, "Precision", 0.0, 100.0, 0, QString(), "How closely the outline follows the stroke",
        [this]() { return static_cast<double>(brush().precision); },
        [this](double v) { brush().precision = static_cast<int>(std::lround(v)); });
    number(dynamics, "Stabilizer", 0.0, 60.0, 0, " px", "How far the pen leads the line on its string",
        [this]() { return brush().stabilizer; }, [this](double v) { brush().stabilizer = v; });

    // --- Painting ---
    QFormLayout* painting = page("Painting");
    choice(painting, "Mode",
        {"Paint Normal", "Paint Fills", "Paint Behind", "Paint Selection", "Paint Inside", "Repaint"},
        "Where a stroke lands; Repaint recolors only paint already there",
        [&s]() { return static_cast<int>(s.brushMode); },
        [&s](int i) { s.brushMode = static_cast<fla::DrawingStyle::BrushMode>(i); });
    check(painting, "Auto fill", "A stroke that closes on itself is filled in",
        [&s]() { return s.brushAutoFill; }, [&s](bool on) { s.brushAutoFill = on; });
    check(painting, "Snap to guides", "A stroke begun on a ruler guide keeps to it",
        [&s]() { return s.brushSnapGuides; }, [&s](bool on) { s.brushSnapGuides = on; });

    // --- Bitmap ---
    QFormLayout* bitmap = page("Bitmap");
    check(bitmap, "Bitmap brush", "Paint pixels into the frame's paint canvas; it has its own presets",
        [&s]() { return s.bitmapBrush; },
        [this, &s](bool on) {
            s.bitmapBrush = on;
            fillList();
        });
    number(bitmap, "Flow", 1.0, 100.0, 0, " %", "How much paint each dab lays; below 100 it builds up",
        [&s]() { return s.bitmapFlow; }, [&s](double v) { s.bitmapFlow = v; });
    number(bitmap, "Spacing", 1.0, 500.0, 0, " %", "How far apart the dabs are, as a share of the tip",
        [&s]() { return s.bitmapSpacing; }, [&s](double v) { s.bitmapSpacing = v; });
    number(bitmap, "Hardness", 0.0, 100.0, 0, " %", "How firmly the soft edge holds before it falls away",
        [&s]() { return s.bitmapHardness; }, [&s](double v) { s.bitmapHardness = v; });
    number(bitmap, "Softness", 0.0, 100.0, 0, " %", "How wide the soft edge is",
        [&s]() { return s.bitmapSoftness; }, [&s](double v) { s.bitmapSoftness = v; });
    number(bitmap, "Opacity", 1.0, 100.0, 0, " %", "How strong a stroke is",
        [&s]() { return s.bitmapOpacity; }, [&s](double v) { s.bitmapOpacity = v; });
    check(bitmap, "Pressure sets the opacity", "", [&s]() { return s.bitmapOpacityPressure; },
        [&s](bool on) { s.bitmapOpacityPressure = on; });
    check(bitmap, "Pressure sets the flow", "", [&s]() { return s.bitmapFlowPressure; },
        [&s](bool on) { s.bitmapFlowPressure = on; });
    number(bitmap, "Fade in", 0.0, 2000.0, 0, " px", "The paint fades in over this much of the stroke's start",
        [this]() { return brush().fadeStart; }, [this](double v) { brush().fadeStart = v; });
    number(bitmap, "Fade out", 0.0, 2000.0, 0, " px", "The paint fades out over this much of its end",
        [this]() { return brush().fadeEnd; }, [this](double v) { brush().fadeEnd = v; });
    choice(bitmap, "Blend", {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Add"},
        "How a stroke mixes with the paint already on the canvas",
        [&s]() { return static_cast<int>(s.bitmapBlend); },
        [&s](int i) { s.bitmapBlend = static_cast<fla::DrawingStyle::BitmapBlend>(i); });
    textureBox(bitmap, "Tip texture", "A grain on the tip itself, turning with it",
        [&s]() { return s.bitmapTipTexture; }, [&s](const std::string& v) { s.bitmapTipTexture = v; });
    check(bitmap, "Erase pixels", "The bitmap brush takes paint away instead",
        [&s]() { return s.bitmapErase; }, [&s](bool on) { s.bitmapErase = on; });

    // --- Texture ---
    QFormLayout* texture = page("Texture");
    textureBox(texture, "Paper grain", "The grain a stroke takes on, as chalk takes the paper's tooth",
        [&s]() { return s.brushTexture; }, [&s](const std::string& v) { s.brushTexture = v; });
    number(texture, "Grain size", 10.0, 1000.0, 0, " %", "", [&s]() { return s.brushTextureScale; },
        [&s](double v) { s.brushTextureScale = v; });
    number(texture, "Strength", 0.0, 100.0, 0, " %", "How much paint the grain takes away where it is lightest",
        [&s]() { return s.brushTextureStrength; }, [&s](double v) { s.brushTextureStrength = v; });
    check(texture, "Pressure sets the strength", "A light touch shows more grain, a firm one less",
        [&s]() { return s.brushTexturePressure; }, [&s](bool on) { s.brushTexturePressure = on; });
    number(texture, "Offset X", -2000.0, 2000.0, 0, " px", "Where the grain starts",
        [&s]() { return s.brushTextureOffsetX; }, [&s](double v) { s.brushTextureOffsetX = v; });
    number(texture, "Offset Y", -2000.0, 2000.0, 0, " px", "", [&s]() { return s.brushTextureOffsetY; },
        [&s](double v) { s.brushTextureOffsetY = v; });
    number(texture, "Rotation", -180.0, 180.0, 0, QString::fromUtf8("\xC2\xB0"), "The grain's turn",
        [&s]() { return s.brushTextureRotation; }, [&s](double v) { s.brushTextureRotation = v; });
    check(texture, "Fixed to the frame", "The grain keeps its size as the camera zooms or the art is scaled",
        [&s]() { return s.brushTextureFixed; }, [&s](bool on) { s.brushTextureFixed = on; });

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    // The presets' actions.
    connect(_presets, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!_refreshing)
            applyPreset(row);
    });
    connect(_view, &QComboBox::currentIndexChanged, this, [this](int index) {
        QSettings().setValue("brushPresets/view", index);
        fillList();
    });
    connect(add, &QPushButton::clicked, this, [this]() {
        modal::getText(this, "New Preset", "Name", QString("Brush %1").arg(_presets->count() + 1),
            [this](const QString& typed) {
                const QString name = typed.trimmed();
                if (name.isEmpty())
                    return;
                std::vector<fla::brushpresets::Preset> presets = loadList();
                presets.push_back({name.toStdString(), fla::brushpresets::presetText(brush(), _style)});
                saveList(presets);
                fillList();
                _refreshing = true;
                _presets->setCurrentRow(_presets->count() - 1);
                _refreshing = false;
            });
    });
    connect(rename, &QPushButton::clicked, this, [this]() {
        const int row = _presets->currentRow();
        std::vector<fla::brushpresets::Preset> presets = loadList();
        if (row < 0 || row >= static_cast<int>(presets.size()))
            return;
        modal::getText(this, "Rename Preset", "Name", QString::fromStdString(presets[static_cast<size_t>(row)].name),
            [this, row](const QString& typed) {
                const QString name = typed.trimmed();
                std::vector<fla::brushpresets::Preset> latest = loadList();
                if (name.isEmpty() || row >= static_cast<int>(latest.size()))
                    return;
                latest[static_cast<size_t>(row)].name = name.toStdString();
                saveList(latest);
                fillList();
            });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = _presets->currentRow();
        std::vector<fla::brushpresets::Preset> presets = loadList();
        if (row < 0 || row >= static_cast<int>(presets.size()))
            return;
        presets.erase(presets.begin() + row);
        saveList(presets);
        fillList();
    });
    connect(exportSet, &QPushButton::clicked, this, [this]() {
        file_pick::save(this, "Export Brush Presets", QString::fromStdString(_list) + "-brushes.phxbrushes",
            "Brush Presets (*.phxbrushes)", [this](const QString& path) {
                QFile file(path);
                const std::string text = fla::brushpresets::toSetText(loadList());
                if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) < 0)
                    modal::warning(this, "Export Brush Presets", "The presets could not be written.");
            });
    });
    connect(importSet, &QPushButton::clicked, this, [this]() {
        file_pick::open(this, "Import Brush Presets", QString(), "Brush Presets (*.phxbrushes);;All Files (*)",
            [this](const QString& path) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    return;
                const std::vector<fla::brushpresets::Preset> incoming =
                    fla::brushpresets::fromSetText(file.readAll().toStdString());
                if (incoming.empty())
                {
                    modal::information(this, "Import Brush Presets", "That file holds no brush presets.");
                    return;
                }
                std::vector<fla::brushpresets::Preset> presets = loadList();
                presets.insert(presets.end(), incoming.begin(), incoming.end());
                saveList(presets);
                fillList();
            });
    });

    refresh();
    resize(640, 640);
}

fla::BrushSettings& BrushPropertiesDialog::brush()
{
    if (_list == fla::brushpresets::kNib)
        return _style.classicBrush;
    if (_list == fla::brushpresets::kInk)
        return _style.inkBrush;
    return _style.fluidBrush;
}

std::string BrushPropertiesDialog::listKey() const
{
    return _style.bitmapBrush ? std::string(fla::brushpresets::kBitmap) : _list;
}

std::vector<fla::brushpresets::Preset> BrushPropertiesDialog::loadList() const
{
    const QString key = "brushPresets/" + QString::fromStdString(listKey());
    QSettings settings;
    if (!settings.contains(key))
        return fla::brushpresets::builtIns(listKey());
    return fla::brushpresets::fromSetText(settings.value(key).toString().toStdString());
}

void BrushPropertiesDialog::saveList(const std::vector<fla::brushpresets::Preset>& presets) const
{
    QSettings().setValue("brushPresets/" + QString::fromStdString(listKey()),
        QString::fromStdString(fla::brushpresets::toSetText(presets)));
}

void BrushPropertiesDialog::fillList()
{
    _refreshing = true;
    _presets->clear();
    const int view = _view->currentIndex();
    const QSize thumb = view == 0 ? QSize(72, 36) : view == 1 ? QSize(144, 72) : QSize(200, 40);
    _presets->setViewMode(view == 2 ? QListView::ListMode : QListView::IconMode);
    _presets->setIconSize(thumb);
    _presets->setResizeMode(QListView::Adjust);
    _presets->setWordWrap(true);
    if (view != 2)
        _presets->setGridSize(QSize(thumb.width() + 16, thumb.height() + 34));
    else
        _presets->setGridSize(QSize());
    const bool bitmap = _style.bitmapBrush;
    for (const fla::brushpresets::Preset& preset : loadList())
    {
        fla::BrushSettings shown = brush();
        fla::DrawingStyle shownStyle = _style;
        fla::brushpresets::apply(preset.text, shown, shownStyle);
        const QImage image = strokePreview(shown, shownStyle, thumb, bitmap);
        QPixmap pixmap(thumb);
        pixmap.fill(Qt::white);
        {
            QPainter painter(&pixmap);
            painter.drawImage(0, 0, image);
        }
        new QListWidgetItem(QIcon(pixmap), QString::fromStdString(preset.name), _presets);
    }
    _refreshing = false;
}

void BrushPropertiesDialog::applyPreset(int row)
{
    const std::vector<fla::brushpresets::Preset> presets = loadList();
    if (row < 0 || row >= static_cast<int>(presets.size()))
        return;
    const bool bitmap = _style.bitmapBrush;
    fla::brushpresets::apply(presets[static_cast<size_t>(row)].text, brush(), _style);
    // A preset belongs to its list: choosing it does not switch brushes.
    _style.bitmapBrush = bitmap;
    refresh();
    emit changed();
}

void BrushPropertiesDialog::refresh()
{
    _refreshing = true;
    for (const auto& refresher : _refreshers)
        refresher();
    _refreshing = false;
    if (_presets->count() == 0)
        fillList();
    updatePreview();
}

void BrushPropertiesDialog::touched()
{
    updatePreview();
    emit changed();
}

void BrushPropertiesDialog::updatePreview()
{
    const QImage image = strokePreview(brush(), _style, _preview->size(), _style.bitmapBrush);
    QPixmap pixmap(_preview->size());
    pixmap.fill(Qt::white);
    {
        QPainter painter(&pixmap);
        painter.drawImage(0, 0, image);
    }
    _preview->setPixmap(pixmap);
}

void BrushPropertiesDialog::changeEvent(QEvent* event)
{
    // Back from the Tool tab, which may have changed the brush.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        refresh();
    QDialog::changeEvent(event);
}
