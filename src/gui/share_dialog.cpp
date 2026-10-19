#include "share_dialog.h"
#include "file_pick.h"
#include "modal.h"

#include "new_document_dialog.h"

#include "../util/apng_writer.h"
#include "../util/gif_writer.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include "process.h"
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {

/// A size in words: "1.2 MB", "340 KB".
QString sizeText(double bytes)
{
    if (bytes >= 1024.0 * 1024.0)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', bytes >= 10.0 * 1024.0 * 1024.0 ? 0 : 1);
    return QString("%1 KB").arg(std::max(1.0, std::round(bytes / 1024.0)), 0, 'f', 0);
}

std::vector<uint8_t> rgbaOf(const QImage& picture)
{
    const QImage image = picture.convertToFormat(QImage::Format_RGBA8888);
    std::vector<uint8_t> rgba(static_cast<size_t>(image.width()) * image.height() * 4);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(rgba.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
            static_cast<size_t>(image.width()) * 4);
    return rgba;
}

/// How many times the animation plays: forever (0), or so many times.
const struct
{
    const char* text;
    int plays;
} kPlays[] = {{"Forever", 0}, {"Once", 1}, {"Twice", 2}, {"3 times", 3}, {"5 times", 5}};

} // namespace

ShareDialog::ShareDialog(const QSize& documentSize, double frameRate, int frameCount, const QString& baseName,
    const Hooks& hooks, QWidget* parent)
    : QDialog(parent)
    , _documentSize(documentSize.isValid() ? documentSize : QSize(550, 400))
    , _frameRate(frameRate > 0.0 ? frameRate : 24.0)
    , _frameCount(std::max(1, frameCount))
    , _baseName(baseName.isEmpty() ? QString("animation") : baseName)
    , _hooks(hooks)
{
    setWindowTitle("Share");
    QSettings stored;
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* heading = new QLabel("<b>Share your animation</b><br>Pick a format, then Save. "
                                 "The file is ready to post, send or put on a web page.", this);
    heading->setWordWrap(true);
    layout->addWidget(heading);

    // The formats, as big buttons.
    QGridLayout* tiles = new QGridLayout;
    _formats = new QButtonGroup(this);
    _formats->setExclusive(true);
    int column = 0;
    for (share::Format format : share::formats())
    {
        QToolButton* tile = new QToolButton(this);
        tile->setCheckable(true);
        tile->setText(QString("%1\n.%2").arg(share::title(format), share::suffix(format)));
        tile->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tile->setMinimumSize(118, 58);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont font = tile->font();
        font.setBold(true);
        tile->setFont(font);
        tile->setObjectName(QString("share_%1").arg(share::suffix(format) == QString("png") ? "apng"
                                                                                           : share::suffix(format)));
        if (available(format))
            tile->setToolTip(share::blurb(format));
        else
        {
            tile->setEnabled(false);
            tile->setToolTip("This copy of Phoenix's ffmpeg cannot write this format.");
        }
        _formats->addButton(tile, static_cast<int>(format));
        tiles->addWidget(tile, 0, column++);
    }
    layout->addLayout(tiles);
    _blurb = new QLabel(this);
    _blurb->setWordWrap(true);
    layout->addWidget(_blurb);

    QFormLayout* form = new QFormLayout;
    layout->addLayout(form);

    // Sizes: the stage's, half of it, each social site's, or any.
    _size = new QComboBox(this);
    _size->addItem(QString("Same as the stage (%1 × %2)").arg(_documentSize.width()).arg(_documentSize.height()));
    _sizes.push_back(_documentSize);
    const QSize half(std::max(1, _documentSize.width() / 2), std::max(1, _documentSize.height() / 2));
    _size->addItem(QString("Half size (%1 × %2)").arg(half.width()).arg(half.height()));
    _sizes.push_back(half);
    for (const NewDocumentDialog::Preset& preset : NewDocumentDialog::presets())
    {
        if (QString(preset.category) != "Social")
            continue;
        _size->addItem(QString("%1 (%2 × %3)").arg(preset.name).arg(preset.width).arg(preset.height));
        _sizes.push_back(QSize(preset.width, preset.height));
    }
    _size->addItem("Custom");
    _sizes.push_back(QSize());
    _size->setToolTip("A site's size keeps your stage's shape inside it, with bars of the background color around");
    form->addRow("Size", _size);
    QHBoxLayout* customRow = new QHBoxLayout;
    _width = new QSpinBox(this);
    _width->setRange(16, 8192);
    _width->setValue(_documentSize.width());
    _height = new QSpinBox(this);
    _height->setRange(16, 8192);
    _height->setValue(_documentSize.height());
    customRow->addWidget(_width);
    customRow->addWidget(new QLabel("×", this));
    customRow->addWidget(_height);
    customRow->addWidget(new QLabel("pixels", this));
    customRow->addStretch(1);
    form->addRow("", customRow);

    _rate = new QDoubleSpinBox(this);
    _rate->setRange(1.0, 120.0);
    _rate->setDecimals(2);
    _rate->setValue(_frameRate);
    _rate->setSuffix(" frames a second");
    _rate->setToolTip("Fewer frames a second make a smaller file; the animation still lasts as long");
    form->addRow("Frame rate", _rate);

    _plays = new QComboBox(this);
    for (const auto& plays : kPlays)
        _plays->addItem(plays.text, plays.plays);
    _plays->setToolTip("How many times the animation plays before it stops");
    form->addRow("Plays", _plays);

    QHBoxLayout* qualityRow = new QHBoxLayout;
    _quality = new QSlider(Qt::Horizontal, this);
    _quality->setRange(1, 100);
    _quality->setValue(std::clamp(stored.value("share/quality", 80).toInt(), 1, 100));
    qualityRow->addWidget(new QLabel("Smaller file", this));
    qualityRow->addWidget(_quality, 1);
    qualityRow->addWidget(new QLabel("Better looking", this));
    form->addRow("Quality", qualityRow);

    _transparent = new QCheckBox("Transparent background", this);
    _transparent->setChecked(stored.value("share/transparent", false).toBool());
    form->addRow("", _transparent);
    _sound = new QCheckBox("Include sound", this);
    _sound->setChecked(true);
    form->addRow("", _sound);

    _estimate = new QLabel(this);
    form->addRow("File size", _estimate);

    _result = new QLabel(this);
    _result->setWordWrap(true);
    layout->addWidget(_result);

    QHBoxLayout* buttons = new QHBoxLayout;
    _show = new QPushButton("Show in Folder", this);
    _show->setEnabled(false);
    _save = new QPushButton("Save...", this);
    _save->setDefault(true);
    QPushButton* close = new QPushButton("Close", this);
    buttons->addWidget(_show);
    buttons->addStretch(1);
    buttons->addWidget(_save);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    _estimateTimer = new QTimer(this);
    _estimateTimer->setSingleShot(true);
    _estimateTimer->setInterval(400);
    connect(_estimateTimer, &QTimer::timeout, this, &ShareDialog::estimate);
    const auto later = [this]() {
        _estimate->setText("Working it out...");
        _estimateTimer->start();
    };

    connect(_formats, &QButtonGroup::idClicked, this, [this, later](int) {
        formatChanged();
        later();
    });
    connect(_size, &QComboBox::currentIndexChanged, this, [this, later](int) {
        sizeChanged();
        later();
    });
    connect(_width, &QSpinBox::valueChanged, this, later);
    connect(_height, &QSpinBox::valueChanged, this, later);
    connect(_rate, &QDoubleSpinBox::valueChanged, this, later);
    connect(_quality, &QSlider::valueChanged, this, later);
    connect(_transparent, &QCheckBox::toggled, this, later);
    connect(_save, &QPushButton::clicked, this, &ShareDialog::save);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(_show, &QPushButton::clicked, this, [this]() {
        if (_hooks.reveal && !_saved.isEmpty())
            _hooks.reveal(_saved);
    });

    // The format used last, when it can still be written.
    int chosen = stored.value("share/format", static_cast<int>(share::Format::Gif)).toInt();
    if (!_formats->button(chosen) || !_formats->button(chosen)->isEnabled())
        chosen = static_cast<int>(share::Format::Gif);
    _formats->button(chosen)->setChecked(true);
    _size->setCurrentIndex(std::clamp(stored.value("share/size", 0).toInt(), 0, _size->count() - 1));
    sizeChanged();
    formatChanged();
    later();
}

bool ShareDialog::available(share::Format format)
{
    const std::string encoder = share::encoderFor(format);
    if (encoder.empty())
        return true;
    static std::map<std::string, bool> known;
    const auto found = known.find(encoder);
    if (found != known.end())
        return found->second;
    bool has = false;
    const QString ffmpeg = MovieExport::ffmpegPath();
    if (!ffmpeg.isEmpty())
    {
        Process process;
        process.start(ffmpeg, {"-hide_banner", "-encoders"});
        if (process.waitForFinished(10000))
        {
            const QString listed = QString::fromLocal8Bit(process.readAllStandardOutput());
            has = listed.contains(QString(" %1 ").arg(QString::fromStdString(encoder)));
        }
    }
    known[encoder] = has;
    return has;
}

share::Format ShareDialog::format() const
{
    const int id = _formats->checkedId();
    return id < 0 ? share::Format::Gif : static_cast<share::Format>(id);
}

QSize ShareDialog::outputSize() const
{
    const int index = _size->currentIndex();
    QSize size = index >= 0 && index < static_cast<int>(_sizes.size()) ? _sizes[static_cast<size_t>(index)] : QSize();
    if (!size.isValid())
        size = QSize(_width->value(), _height->value());
    int width = size.width();
    int height = size.height();
    share::frameSize(format(), width, height);
    return QSize(width, height);
}

void ShareDialog::formatChanged()
{
    const share::Format chosen = format();
    _blurb->setText(share::blurb(chosen));
    _plays->setEnabled(share::canLoop(chosen));
    _transparent->setEnabled(share::canBeTransparent(chosen));
    _sound->setEnabled(chosen == share::Format::Mp4 || chosen == share::Format::WebM);
    // Lossless at the top of the scale.
    _quality->setEnabled(chosen != share::Format::Apng);
    _quality->setToolTip(chosen == share::Format::Gif   ? "More colors a frame"
                         : chosen == share::Format::Apng ? "An animated PNG keeps every pixel as drawn"
                         : chosen == share::Format::WebP ? "At the top, every pixel kept as drawn"
                                                         : "Sharper pictures, bigger files");
}

void ShareDialog::sizeChanged()
{
    const int index = _size->currentIndex();
    const bool custom = index == _size->count() - 1;
    _width->setEnabled(custom);
    _height->setEnabled(custom);
    if (!custom && index >= 0 && index < static_cast<int>(_sizes.size()))
    {
        _width->blockSignals(true);
        _height->blockSignals(true);
        _width->setValue(_sizes[static_cast<size_t>(index)].width());
        _height->setValue(_sizes[static_cast<size_t>(index)].height());
        _width->blockSignals(false);
        _height->blockSignals(false);
    }
}

MovieExportSettings ShareDialog::settings() const
{
    MovieExportSettings settings;
    const share::Format chosen = format();
    switch (chosen)
    {
    case share::Format::Gif: settings.format = MovieExportSettings::Format::Gif; break;
    case share::Format::WebP: settings.format = MovieExportSettings::Format::WebP; break;
    case share::Format::Apng: settings.format = MovieExportSettings::Format::Apng; break;
    case share::Format::WebM: settings.format = MovieExportSettings::Format::WebM; break;
    case share::Format::Mp4:
        settings.format = MovieExportSettings::Format::Video;
        settings.codec = MovieExportSettings::Codec::H264;
        break;
    }
    settings.size = outputSize();
    settings.first = 0;
    settings.last = _frameCount - 1;
    settings.transparent = share::canBeTransparent(chosen) && _transparent->isChecked();
    settings.audio = _sound->isEnabled() && _sound->isChecked();
    const int plays = share::canLoop(chosen) ? _plays->currentData().toInt() : 1;
    settings.loop = plays != 1;
    settings.gifRepeat = plays > 1 ? plays - 1 : 0;
    settings.quality = chosen == share::Format::Apng ? 100 : _quality->value();
    settings.gifColors = share::gifColors(settings.quality);
    settings.gifDither = false;
    settings.letterbox = true;
    settings.outputRate = _rate->value();
    return settings;
}

void ShareDialog::estimate()
{
    if (!_hooks.render)
    {
        _estimate->clear();
        return;
    }
    const MovieExportSettings chosen = settings();
    const share::Format kind = format();
    const double rate = chosen.outputRate > 0.0 ? chosen.outputRate : _frameRate;
    const int frames = std::max(1, static_cast<int>(std::ceil(_frameCount * rate / _frameRate - 1.0e-9)));
    // A few frames from a third of the way in, written for real: the first
    // frame's bytes, and what each later one adds.
    const int samples = std::min(4, frames);
    const int start = (frames - samples) / 3;
    std::vector<QImage> pictures;
    for (int i = 0; i < samples; ++i)
    {
        const int frame = std::min(_frameCount - 1, static_cast<int>(std::floor((start + i) * _frameRate / rate + 1.0e-9)));
        pictures.push_back(_hooks.render(frame, chosen.size, chosen.transparent));
        if (pictures.back().isNull())
        {
            _estimate->setText("Too large to draw");
            return;
        }
    }
    const auto written = [&](int count) -> double {
        if (kind == share::Format::Gif)
        {
            gif::Options options;
            options.colors = chosen.gifColors;
            gif::AnimationWriter writer(chosen.size.width(), chosen.size.height(), chosen.loop, options);
            for (int i = 0; i < count; ++i)
                writer.addFrame(rgbaOf(pictures[static_cast<size_t>(i)]), 4, chosen.transparent);
            return static_cast<double>(writer.finish().size());
        }
        if (kind == share::Format::Apng)
        {
            apng::AnimationWriter writer(chosen.size.width(), chosen.size.height(), rate, 0);
            for (int i = 0; i < count; ++i)
                writer.addFrame(rgbaOf(pictures[static_cast<size_t>(i)]));
            return static_cast<double>(writer.finish().size());
        }
        const QString ffmpeg = MovieExport::ffmpegPath();
        QTemporaryDir folder;
        if (ffmpeg.isEmpty() || !folder.isValid())
            return -1.0;
        const QString out = folder.filePath(QString("estimate.") + share::suffix(kind));
        QStringList arguments{"-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra", "-s",
            QString("%1x%2").arg(chosen.size.width()).arg(chosen.size.height()), "-framerate",
            QString::number(rate, 'g', 10), "-i", "-"};
        for (const std::string& argument : share::encoderArguments(kind, chosen.quality, chosen.transparent, 0, false,
                 out.toStdString()))
            arguments << QString::fromStdString(argument);
        Process process;
        process.start(ffmpeg, arguments);
        if (!process.waitForStarted(5000))
            return -1.0;
        for (int i = 0; i < count; ++i)
        {
            const QImage image = pictures[static_cast<size_t>(i)].convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y)
                process.write(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
        }
        process.closeWriteChannel();
        if (!process.waitForFinished(20000) || process.exitCode() != 0)
            return -1.0;
        return static_cast<double>(QFileInfo(out).size());
    };
    const double one = written(1);
    const double some = samples > 1 ? written(samples) : one;
    if (one <= 0.0 || some <= 0.0)
    {
        _estimate->setText("Unknown");
        return;
    }
    const double each = samples > 1 ? std::max(0.0, (some - one) / (samples - 1)) : one;
    const double total = one + each * (frames - 1);
    const double seconds = frames / rate;
    _estimate->setText(QString("About %1 (%2 frames, %3 seconds)")
                           .arg(sizeText(total))
                           .arg(frames)
                           .arg(seconds, 0, 'f', seconds < 10.0 ? 1 : 0));
}

void ShareDialog::save()
{
    QSettings stored;
    const share::Format chosen = format();
    QString folder = stored.value("share/folder").toString();
    if (folder.isEmpty() || !QDir(folder).exists())
        folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString suggested = QDir(folder).filePath(_baseName + "." + share::suffix(chosen));
    const QString filter = QString("%1 (*.%2)").arg(share::title(chosen), share::suffix(chosen));
    file_pick::save(this, "Share", suggested, filter, [this](const QString& path) { saveTo(path); });
}

bool ShareDialog::saveTo(const QString& given)
{
    const share::Format chosen = format();
    QString path = given;
    if (QFileInfo(path).suffix().isEmpty())
        path += QString(".") + share::suffix(chosen);
    QSettings stored;
    stored.setValue("share/folder", QFileInfo(path).absolutePath());
    stored.setValue("share/format", static_cast<int>(chosen));
    stored.setValue("share/size", _size->currentIndex());
    stored.setValue("share/quality", _quality->value());
    stored.setValue("share/transparent", _transparent->isChecked());

    MovieExportSettings chosenSettings = settings();
    chosenSettings.path = QFileInfo(path).absoluteFilePath();
    QString error;
    if (!_hooks.write || !_hooks.write(chosenSettings, error))
    {
        if (!error.isEmpty())
            modal::warning(this, "Share", error);
        _result->setText(error.isEmpty() ? "Cancelled." : "Not saved.");
        return false;
    }
    _saved = chosenSettings.path;
    _result->setText(QString("Saved <b>%1</b>, %2.")
                         .arg(QFileInfo(_saved).fileName().toHtmlEscaped(), sizeText(QFileInfo(_saved).size())));
    _show->setEnabled(true);
    return true;
}
