#include "image_export.h"

#include "phoenix_view.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../util/gif_writer.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QStandardItemModel>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include "hot_text.h"
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>

namespace {

const char* const kSuffixes[3] = {"png", "jpg", "gif"};

QIcon colorIcon(const QColor& color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(QColor(120, 120, 120));
    painter.drawRect(0, 0, 17, 17);
    return QIcon(pixmap);
}

/// \a picture's pixels as red, green, blue and alpha bytes.
std::vector<uint8_t> rgbaBytes(const QImage& picture)
{
    const QImage rgba = picture.convertToFormat(QImage::Format_RGBA8888);
    std::vector<uint8_t> pixels(static_cast<size_t>(rgba.width()) * rgba.height() * 4);
    for (int y = 0; y < rgba.height(); ++y)
        std::memcpy(pixels.data() + static_cast<size_t>(y) * rgba.width() * 4, rgba.constScanLine(y),
            static_cast<size_t>(rgba.width()) * 4);
    return pixels;
}

} // namespace

QImage toIndexed(const QImage& picture, bool transparent, int colors, bool dither)
{
    gif::Options options;
    options.colors = colors;
    options.dither = dither;
    const gif::Reduced reduced = gif::reduce(picture.width(), picture.height(), rgbaBytes(picture), transparent, options);
    QImage indexed(picture.size(), QImage::Format_Indexed8);
    QList<QRgb> table;
    for (size_t i = 0; i + 2 < reduced.palette.size(); i += 3)
        table << qRgb(reduced.palette[i], reduced.palette[i + 1], reduced.palette[i + 2]);
    if (reduced.clearIndex >= 0 && reduced.clearIndex < table.size())
        table[reduced.clearIndex] = qRgba(0, 0, 0, 0);
    indexed.setColorTable(table);
    for (int y = 0; y < indexed.height(); ++y)
        std::memcpy(indexed.scanLine(y), reduced.indices.data() + static_cast<size_t>(y) * indexed.width(),
            static_cast<size_t>(indexed.width()));
    return indexed;
}

ImageExportSettings::Format ImageExportDialog::formatOf(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "jpg" || suffix == "jpeg")
        return ImageExportSettings::Format::Jpeg;
    if (suffix == "gif")
        return ImageExportSettings::Format::Gif;
    return ImageExportSettings::Format::Png;
}

ImageExportDialog::ImageExportDialog(const QSize& documentSize, int frameCount, int currentFrame,
    const QString& suggestedPath, QWidget* parent)
    : QDialog(parent)
    , _documentSize(documentSize)
{
    setWindowTitle("Export Image");

    _path = new QLineEdit(suggestedPath, this);
    QPushButton* browseButton = new QPushButton("Browse...", this);
    connect(browseButton, &QPushButton::clicked, this, &ImageExportDialog::browse);
    QHBoxLayout* pathRow = new QHBoxLayout();
    pathRow->addWidget(_path, 1);
    pathRow->addWidget(browseButton);

    _format = new QComboBox(this);
    _format->addItems({"PNG", "JPEG", "GIF"});
    _format->setCurrentIndex(static_cast<int>(formatOf(suggestedPath)));
    connect(_format, &QComboBox::currentIndexChanged, this, &ImageExportDialog::formatChanged);
    // A typed name with another extension picks that format.
    connect(_path, &QLineEdit::editingFinished, this, [this]() {
        const QString suffix = QFileInfo(_path->text()).suffix().toLower();
        if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || suffix == "gif")
            _format->setCurrentIndex(static_cast<int>(formatOf(_path->text())));
    });

    _width = new HotSpinBox(this);
    _height = new HotSpinBox(this);
    for (HotSpinBox* box : {_width, _height})
    {
        box->setRange(1, 16384);
        box->setSuffix(" px");
    }
    _width->setValue(documentSize.width());
    _height->setValue(documentSize.height());
    _keepAspect = new QCheckBox("Keep proportions", this);
    _keepAspect->setChecked(true);
    connect(_width, &HotSpinBox::valueChanged, this, [this](int width) {
        if (_syncing || !_keepAspect->isChecked() || _documentSize.width() <= 0)
            return;
        _syncing = true;
        _height->setValue(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(width) * _documentSize.height() / _documentSize.width()))));
        _syncing = false;
    });
    connect(_height, &HotSpinBox::valueChanged, this, [this](int height) {
        if (_syncing || !_keepAspect->isChecked() || _documentSize.height() <= 0)
            return;
        _syncing = true;
        _width->setValue(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(height) * _documentSize.width() / _documentSize.height()))));
        _syncing = false;
    });
    QHBoxLayout* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(_width);
    sizeRow->addWidget(new QLabel(QString::fromUtf8("×"), this));
    sizeRow->addWidget(_height);
    sizeRow->addWidget(_keepAspect);
    sizeRow->addStretch();

    _frame = new HotSpinBox(this);
    _frame->setRange(1, std::max(1, frameCount));
    _frame->setValue(currentFrame + 1);

    _matte = new QComboBox(this);
    _matte->addItems({"Stage color", "Transparent", "Color"});
    _matte->setToolTip("What shows where nothing is drawn");
    _matteSwatch = new QToolButton(this);
    _matteSwatch->setIcon(colorIcon(_matteColor));
    _matteSwatch->setAutoRaise(true);
    connect(_matteSwatch, &QToolButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(_matteColor, this, "Matte Color");
        if (!chosen.isValid())
            return;
        _matteColor = chosen;
        _matteSwatch->setIcon(colorIcon(chosen));
        _matte->setCurrentIndex(2);
    });
    QHBoxLayout* matteRow = new QHBoxLayout();
    matteRow->addWidget(_matte, 1);
    matteRow->addWidget(_matteSwatch);

    _quality = new HotSpinBox(this);
    _quality->setRange(0, 100);
    _quality->setValue(90);
    _quality->setToolTip("JPEG quality: higher is better and larger");

    _depth = new QComboBox(this);
    _depth->addItems({"Full color (24/32-bit)", "8-bit (a palette)"});
    _depth->setToolTip("An 8-bit PNG is smaller, with at most 256 colors");
    connect(_depth, &QComboBox::currentIndexChanged, this, &ImageExportDialog::formatChanged);
    _colors = new HotSpinBox(this);
    _colors->setRange(2, 256);
    _colors->setValue(256);
    _colors->setToolTip("How many colors the palette has");
    _dither = new QCheckBox("Dither", this);
    _dither->setToolTip("Mix the palette's colors to stand in for the rest, for smoother gradients");
    QHBoxLayout* colorsRow = new QHBoxLayout();
    colorsRow->addWidget(_colors);
    colorsRow->addWidget(_dither);
    colorsRow->addStretch();

    QFormLayout* form = new QFormLayout();
    form->addRow("File:", pathRow);
    form->addRow("Format:", _format);
    form->addRow("Size:", sizeRow);
    form->addRow("Frame:", _frame);
    form->addRow("Matte:", matteRow);
    form->addRow("Quality:", _quality);
    form->addRow("PNG:", _depth);
    form->addRow("Colors:", colorsRow);
    _safeZones = new QCheckBox("Include safe zones", this);
    _safeZones->setToolTip("Draw the safe zones and aspect-ratio frames over the picture, for a review copy");
    form->addRow("", _safeZones);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    setMinimumWidth(480);
    formatChanged();
}

void ImageExportDialog::formatChanged()
{
    const auto format = static_cast<ImageExportSettings::Format>(_format->currentIndex());
    // JPEG has no transparency and is the one with a quality.
    _quality->setEnabled(format == ImageExportSettings::Format::Jpeg);
    _depth->setEnabled(format == ImageExportSettings::Format::Png);
    const bool palette = format == ImageExportSettings::Format::Gif ||
        (format == ImageExportSettings::Format::Png && _depth->currentIndex() == 1);
    _colors->setEnabled(palette);
    _dither->setEnabled(palette);
    auto* model = qobject_cast<QStandardItemModel*>(_matte->model());
    if (model && model->item(1))
        model->item(1)->setEnabled(format != ImageExportSettings::Format::Jpeg);
    if (format == ImageExportSettings::Format::Jpeg && _matte->currentIndex() == 1)
        _matte->setCurrentIndex(0);
    // The file's extension follows the format.
    QFileInfo info(_path->text());
    const QString suffix = kSuffixes[_format->currentIndex()];
    if (!_path->text().isEmpty() && formatOf(_path->text()) != format)
        _path->setText(QDir(info.path()).filePath(info.completeBaseName() + "." + suffix));
}

void ImageExportDialog::browse()
{
    const QString chosen = QFileDialog::getSaveFileName(this, "Export Image", _path->text(),
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;GIF Image (*.gif)");
    if (chosen.isEmpty())
        return;
    _path->setText(chosen);
    _format->setCurrentIndex(static_cast<int>(formatOf(chosen)));
}

ImageExportSettings ImageExportDialog::settings() const
{
    ImageExportSettings settings;
    settings.format = static_cast<ImageExportSettings::Format>(_format->currentIndex());
    settings.path = _path->text();
    settings.size = QSize(_width->value(), _height->value());
    settings.frame = _frame->value() - 1;
    settings.matte = static_cast<ImageExportSettings::Matte>(_matte->currentIndex());
    settings.matteColor = _matteColor;
    settings.quality = _quality->value();
    settings.png8 = _depth->currentIndex() == 1;
    settings.colors = _colors->value();
    settings.dither = _dither->isChecked();
    settings.safeZones = _safeZones->isChecked();
    return settings;
}

namespace ImageExport {

bool run(const ImageExportSettings& settings, PhoenixView& view, QString& error)
{
    // Drawn on nothing, then put on the matte: the stage's own color, one
    // chosen, or left clear.
    const bool clear = settings.matte != ImageExportSettings::Matte::Stage;
    const bool zonesWere = view.exportSafeZones();
    view.setExportSafeZones(settings.safeZones);
    QImage picture = view.renderFrame(settings.frame, settings.size, clear);
    view.setExportSafeZones(zonesWere);
    if (picture.isNull())
    {
        error = "The frame could not be drawn.";
        return false;
    }
    if (settings.matte == ImageExportSettings::Matte::Color)
    {
        QImage matted(picture.size(), QImage::Format_ARGB32_Premultiplied);
        matted.fill(settings.matteColor);
        QPainter painter(&matted);
        painter.drawImage(0, 0, picture);
        painter.end();
        picture = matted;
    }
    // A see-through stage color keeps its alpha in the image too.
    const fla::Document* document = view.flaDocument() ? view.flaDocument()->document : nullptr;
    const bool clearStage = settings.matte == ImageExportSettings::Matte::Stage && document &&
        document->backgroundColor[3] < 255;
    const bool transparent = settings.matte == ImageExportSettings::Matte::Transparent || clearStage;

    switch (settings.format)
    {
    case ImageExportSettings::Format::Png:
    {
        QImageWriter writer(settings.path, "png");
        const QImage out = settings.png8 ? toIndexed(picture, transparent, settings.colors, settings.dither)
                                         : picture.convertToFormat(transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        if (!writer.write(out))
        {
            error = writer.errorString();
            return false;
        }
        return true;
    }
    case ImageExportSettings::Format::Jpeg:
    {
        QImageWriter writer(settings.path, "jpeg");
        writer.setQuality(settings.quality);
        if (!writer.write(picture.convertToFormat(QImage::Format_RGB32)))
        {
            error = writer.errorString();
            return false;
        }
        return true;
    }
    case ImageExportSettings::Format::Gif:
    {
        gif::Options options;
        options.colors = settings.colors;
        options.dither = settings.dither;
        const std::vector<uint8_t> file =
            gif::encode(picture.width(), picture.height(), rgbaBytes(picture), transparent, options);
        QFile out(settings.path);
        if (file.empty() || !out.open(QIODevice::WriteOnly) ||
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<qint64>(file.size())) !=
                static_cast<qint64>(file.size()))
        {
            error = "The GIF could not be written.";
            return false;
        }
        return true;
    }
    }
    return false;
}

} // namespace ImageExport
