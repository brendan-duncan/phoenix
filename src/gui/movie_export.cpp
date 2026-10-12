#include "movie_export.h"
#include "../data/timeline.h"
#include "../data/layer.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../util/gif_writer.h"

#include "phoenix_view.h"
#include "timeline_audio.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDir>
#include <cstring>
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr int kPngIndex = 0;
constexpr int kVideoIndex = 1;
constexpr int kGifIndex = 2;
constexpr int kLayersIndex = 3;
constexpr int kWavIndex = 4;

/// Each format's file extension.
const char* const kSuffixes[5] = {"png", "mp4", "gif", "png", "wav"};

QSpinBox* plainSpin(int low, int high, int value, QWidget* parent)
{
    QSpinBox* spin = new QSpinBox(parent);
    spin->setRange(low, high);
    spin->setValue(value);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return spin;
}

/// The numbered name of one image of a sequence.
QString sequenceName(const QString& path, int number, int digits)
{
    const QFileInfo info(path);
    QString stem = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString("png") : info.suffix();
    return info.dir().filePath(QString("%1%2.%3").arg(stem).arg(number, digits, 10, QChar('0')).arg(suffix));
}

/// Interleaved stereo float samples as a 16-bit WAV file.
bool writeWav(QIODevice& device, const std::vector<float>& samples, int rate)
{
    QDataStream out(&device);
    out.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes = static_cast<quint32>(samples.size() * 2);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4)
        << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (float sample : samples)
        out << qint16(std::lround(std::max(-1.0f, std::min(1.0f, sample)) * 32767.0f));
    return out.status() == QDataStream::Ok;
}

} // namespace

MovieExportDialog::MovieExportDialog(const QSize& documentSize, int frameCount, const QString& suggestedPath,
    QWidget* parent)
    : QDialog(parent)
    , _documentSize(documentSize)
{
    setWindowTitle("Export Movie");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout;
    layout->addLayout(form);

    _format = new QComboBox(this);
    _format->addItems({"PNG Sequence", "Video (MP4, H.264)", "Animated GIF", "PNG Sequence per Layer", "Sound (WAV)"});
    form->addRow("Format", _format);

    QHBoxLayout* pathRow = new QHBoxLayout;
    _path = new QLineEdit(suggestedPath, this);
    QPushButton* browse = new QPushButton("Browse...", this);
    pathRow->addWidget(_path, 1);
    pathRow->addWidget(browse);
    form->addRow("File", pathRow);

    QHBoxLayout* sizeRow = new QHBoxLayout;
    _width = plainSpin(1, 16384, std::max(1, documentSize.width()), this);
    _height = plainSpin(1, 16384, std::max(1, documentSize.height()), this);
    _keepAspect = new QCheckBox("Keep proportions", this);
    _keepAspect->setChecked(true);
    sizeRow->addWidget(_width);
    sizeRow->addWidget(new QLabel("x", this));
    sizeRow->addWidget(_height);
    sizeRow->addWidget(_keepAspect);
    sizeRow->addStretch(1);
    form->addRow("Size", sizeRow);

    QHBoxLayout* rangeRow = new QHBoxLayout;
    _first = plainSpin(1, std::max(1, frameCount), 1, this);
    _last = plainSpin(1, std::max(1, frameCount), std::max(1, frameCount), this);
    rangeRow->addWidget(_first);
    rangeRow->addWidget(new QLabel("to", this));
    rangeRow->addWidget(_last);
    rangeRow->addStretch(1);
    form->addRow("Frames", rangeRow);

    _transparent = new QCheckBox("Transparent background", this);
    form->addRow("", _transparent);
    _audio = new QCheckBox("Include sound", this);
    _audio->setChecked(true);
    form->addRow("", _audio);
    _loop = new QCheckBox("Loop forever", this);
    _loop->setChecked(true);
    form->addRow("", _loop);

    _encoderNote = new QLabel(this);
    _encoderNote->setWordWrap(true);
    layout->addWidget(_encoderNote);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &MovieExportDialog::browse);
    connect(_format, &QComboBox::currentIndexChanged, this, &MovieExportDialog::formatChanged);
    connect(_width, &QSpinBox::valueChanged, this, &MovieExportDialog::widthChanged);
    connect(_height, &QSpinBox::valueChanged, this, &MovieExportDialog::heightChanged);

    QSettings settings;
    _format->setCurrentIndex(std::clamp(settings.value("export/movieFormat", kVideoIndex).toInt(), 0, kWavIndex));
    formatChanged();
}

void MovieExportDialog::formatChanged()
{
    const int index = _format->currentIndex();
    const bool video = index == kVideoIndex;
    const bool sound = index == kWavIndex;
    _transparent->setEnabled(!video && !sound);
    _audio->setEnabled(video);
    _loop->setEnabled(index == kGifIndex);
    _width->setEnabled(!sound);
    _height->setEnabled(!sound);
    _keepAspect->setEnabled(!sound);
    const QFileInfo info(_path->text());
    const QString wanted = kSuffixes[std::clamp(index, 0, kWavIndex)];
    if (!_path->text().isEmpty() && info.suffix().toLower() != wanted && !(video && info.suffix().toLower() == "mov"))
        _path->setText(info.dir().filePath(info.completeBaseName() + "." + wanted));
    if (index == kGifIndex)
        _encoderNote->setText("Each frame with its own 256 colors, timed to the frame rate.");
    else if (index == kLayersIndex)
        _encoderNote->setText("A numbered sequence for each visible layer, named after it.");
    else if (sound)
        _encoderNote->setText("The sound of the frames, mixed as it plays.");
    else if (video)
    {
        const QString ffmpeg = MovieExport::ffmpegPath();
        _encoderNote->setText(ffmpeg.isEmpty()
            ? "Video is encoded with ffmpeg, which was not found. You will be asked where it is."
            : QString("Encoded with %1").arg(QDir::toNativeSeparators(ffmpeg)));
    }
    else
    {
        _encoderNote->setText("One image a frame, numbered from 0001.");
    }
}

void MovieExportDialog::browse()
{
    const int index = _format->currentIndex();
    const char* filters[5] = {"PNG Images (*.png)", "MP4 Video (*.mp4);;QuickTime Movie (*.mov)", "GIF Animation (*.gif)",
        "PNG Images (*.png)", "WAV Sound (*.wav)"};
    const QString file = QFileDialog::getSaveFileName(this, "Export Movie", _path->text(),
        filters[std::clamp(index, 0, kWavIndex)]);
    if (!file.isEmpty())
        _path->setText(file);
}

void MovieExportDialog::widthChanged(int width)
{
    if (_syncing || !_keepAspect->isChecked() || _documentSize.width() <= 0)
        return;
    _syncing = true;
    _height->setValue(std::max(1, static_cast<int>(std::lround(width * double(_documentSize.height()) / _documentSize.width()))));
    _syncing = false;
}

void MovieExportDialog::heightChanged(int height)
{
    if (_syncing || !_keepAspect->isChecked() || _documentSize.height() <= 0)
        return;
    _syncing = true;
    _width->setValue(std::max(1, static_cast<int>(std::lround(height * double(_documentSize.width()) / _documentSize.height()))));
    _syncing = false;
}

MovieExportSettings MovieExportDialog::settings() const
{
    MovieExportSettings settings;
    settings.format = static_cast<MovieExportSettings::Format>(std::clamp(_format->currentIndex(), 0, kWavIndex));
    settings.loop = _loop->isChecked();
    settings.path = _path->text();
    settings.size = QSize(_width->value(), _height->value());
    settings.first = std::min(_first->value(), _last->value()) - 1;
    settings.last = std::max(_first->value(), _last->value()) - 1;
    settings.transparent = _transparent->isChecked();
    settings.audio = _audio->isChecked();
    QSettings stored;
    stored.setValue("export/movieFormat", _format->currentIndex());
    return settings;
}

namespace MovieExport {

QString ffmpegPath()
{
    const QString stored = QSettings().value("export/ffmpeg").toString();
    if (!stored.isEmpty() && QFileInfo(stored).isExecutable())
        return stored;
    return QStandardPaths::findExecutable("ffmpeg");
}

void setFfmpegPath(const QString& path)
{
    QSettings().setValue("export/ffmpeg", path);
}

bool run(const MovieExportSettings& settings, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error)
{
    error.clear();
    const int count = settings.last - settings.first + 1;
    if (count <= 0 || settings.size.isEmpty() || settings.path.isEmpty())
    {
        error = "Nothing to export.";
        return false;
    }

    QProgressDialog progress("Exporting...", "Cancel", 0, count, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    if (settings.format == MovieExportSettings::Format::Wav)
    {
        // The sound of the frames, mixed as playback mixes it.
        const std::vector<float> samples = audio ? audio->mixdown(settings.first, settings.last) : std::vector<float>();
        bool audible = false;
        for (float sample : samples)
            audible = audible || sample != 0.0f;
        if (!audible)
        {
            error = "These frames have no sound.";
            return false;
        }
        QFile file(settings.path);
        if (!file.open(QIODevice::WriteOnly) || !writeWav(file, samples, PcmBuffer::kRate))
        {
            error = QString("Could not write %1.").arg(QDir::toNativeSeparators(settings.path));
            return false;
        }
        return true;
    }

    if (settings.format == MovieExportSettings::Format::Gif)
    {
        gif::AnimationWriter writer(settings.size.width(), settings.size.height(), settings.loop);
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
                return false;
            const QImage image = view.renderFrame(settings.first + i, settings.size, settings.transparent)
                                     .convertToFormat(QImage::Format_RGBA8888);
            std::vector<uint8_t> rgba(static_cast<size_t>(image.width()) * image.height() * 4);
            for (int y = 0; y < image.height(); ++y)
                std::memcpy(rgba.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
                    static_cast<size_t>(image.width()) * 4);
            writer.addFrame(rgba, gif::AnimationWriter::delayOf(i, frameRate), settings.transparent);
        }
        const std::vector<uint8_t> bytes = writer.finish();
        QFile file(settings.path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) !=
                static_cast<qint64>(bytes.size()))
        {
            error = QString("Could not write %1.").arg(QDir::toNativeSeparators(settings.path));
            return false;
        }
        progress.setValue(count);
        return true;
    }

    if (settings.format == MovieExportSettings::Format::LayerSequences)
    {
        // Each visible layer of the scene that draws, on its own.
        const fla::FLADocument* document = view.flaDocument();
        const fla::Timeline* scene = document && document->document ? document->document->currentScene() : nullptr;
        if (!scene)
        {
            error = "Nothing to export.";
            return false;
        }
        std::vector<const fla::Layer*> layers;
        for (const fla::Layer* layer : scene->layers)
        {
            if (layer && layer->isVisible() &&
                (layer->layerType == fla::Layer::Type::Normal || layer->layerType == fla::Layer::Type::Masked))
                layers.push_back(layer);
        }
        progress.setMaximum(count * static_cast<int>(std::max<size_t>(1, layers.size())));
        const int digits = std::max(4, static_cast<int>(QString::number(count).size()));
        const QFileInfo info(settings.path);
        int done = 0;
        for (const fla::Layer* layer : layers)
        {
            // Named for the layer, with what a file name cannot hold changed.
            QString name = QString::fromStdString(layer->name);
            name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            const QString base = info.dir().filePath(info.completeBaseName() + "_" + name + ".png");
            view.setOnlyLayer(layer);
            for (int i = 0; i < count; ++i, ++done)
            {
                progress.setValue(done);
                if (progress.wasCanceled())
                {
                    view.setOnlyLayer(nullptr);
                    return false;
                }
                const QImage image = view.renderFrame(settings.first + i, settings.size, settings.transparent);
                const QString file = sequenceName(base, i + 1, digits);
                if (!image.save(file, "PNG"))
                {
                    view.setOnlyLayer(nullptr);
                    error = QString("Could not write %1.").arg(QDir::toNativeSeparators(file));
                    return false;
                }
            }
        }
        view.setOnlyLayer(nullptr);
        progress.setValue(progress.maximum());
        return true;
    }

    if (settings.format == MovieExportSettings::Format::PngSequence)
    {
        const int digits = std::max(4, static_cast<int>(QString::number(count).size()));
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
                return false;
            const QImage image = view.renderFrame(settings.first + i, settings.size, settings.transparent);
            const QString file = sequenceName(settings.path, i + 1, digits);
            if (!image.save(file, "PNG"))
            {
                error = QString("Could not write %1.").arg(QDir::toNativeSeparators(file));
                return false;
            }
        }
        progress.setValue(count);
        return true;
    }

    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty())
    {
        error = "ffmpeg was not found.";
        return false;
    }
    // H.264 in 4:2:0 wants even sides.
    const QSize size(std::max(2, settings.size.width() & ~1), std::max(2, settings.size.height() & ~1));

    QTemporaryFile wav(QDir::temp().filePath("phoenix_soundXXXXXX.wav"));
    bool withAudio = false;
    if (settings.audio && audio)
    {
        const std::vector<float> samples = audio->mixdown(settings.first, settings.last);
        bool audible = false;
        for (float sample : samples)
            audible = audible || sample != 0.0f;
        if (audible && wav.open())
        {
            withAudio = writeWav(wav, samples, PcmBuffer::kRate);
            wav.close();
        }
    }

    QStringList arguments{"-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgra", "-s",
        QString("%1x%2").arg(size.width()).arg(size.height()), "-framerate", QString::number(frameRate, 'g', 10),
        "-i", "-"};
    if (withAudio)
        arguments << "-i" << wav.fileName();
    arguments << "-map" << "0:v";
    if (withAudio)
        arguments << "-map" << "1:a" << "-c:a" << "aac" << "-b:a" << "192k" << "-shortest";
    arguments << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-pix_fmt" << "yuv420p"
              << "-movflags" << "+faststart" << settings.path;

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(10000))
    {
        error = QString("Could not start %1.").arg(QDir::toNativeSeparators(ffmpeg));
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        progress.setValue(i);
        if (progress.wasCanceled() || process.state() != QProcess::Running)
            break;
        const QImage image =
            view.renderFrame(settings.first + i, size, false).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y)
            process.write(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
        while (process.bytesToWrite() > 0 && process.state() == QProcess::Running)
        {
            process.waitForBytesWritten(100);
            QApplication::processEvents();
        }
    }
    const bool cancelled = progress.wasCanceled();
    process.closeWriteChannel();
    while (!process.waitForFinished(100))
    {
        if (process.state() == QProcess::NotRunning)
            break;
        QApplication::processEvents();
    }
    progress.setValue(count);
    if (cancelled)
    {
        QFile::remove(settings.path);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        error = QString("ffmpeg could not write the video.\n\n%1")
                    .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

} // namespace MovieExport
