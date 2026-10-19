#include "movie_export.h"

#include "../util/exr_writer.h"
#include "../data/timeline.h"
#include "../data/layer.h"
#include "../data/document.h"
#include "../data/fla_document.h"
#include "../util/apng_writer.h"
#include "../util/gif_writer.h"
#include "../util/share_formats.h"

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
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include "process.h"
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include "hot_text.h"
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
constexpr int kPassesIndex = 5;
constexpr int kExrIndex = 6;
constexpr int kSvgIndex = 7;
constexpr int kLastIndex = kSvgIndex;

/// Each format's file extension.
const char* const kSuffixes[8] = {"png", "mp4", "gif", "png", "wav", "png", "exr", "svg"};

HotSpinBox* plainSpin(int low, int high, int value, QWidget* parent)
{
    HotSpinBox* spin = new HotSpinBox(parent);
    spin->setRange(low, high);
    spin->setValue(value);
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
    // A WAV's sizes are 32-bit: past about 6.7 hours of stereo, what does
    // not fit is left off rather than the header wrapping.
    const size_t count = std::min(samples.size(), static_cast<size_t>((0xFFFFFFFFull - 36) / 2));
    const quint32 dataBytes = static_cast<quint32>(count * 2);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4)
        << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (size_t i = 0; i < count; ++i)
        out << qint16(std::lround(std::max(-1.0f, std::min(1.0f, samples[i])) * 32767.0f));
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
    _format->addItems({"PNG Sequence", "Video", "Animated GIF", "PNG Sequence per Layer", "Sound (WAV)",
        "Compositing Passes (PNG)", "Multi-layer EXR", "SVG Sequence"});
    form->addRow("Format", _format);
    _codec = new QComboBox(this);
    _codec->addItems({"H.264 (MP4)", "ProRes 422 HQ (MOV)", "ProRes 4444 with alpha (MOV)"});
    _codec->setToolTip("ProRes is for editing and compositing; 4444 keeps a transparent background");
    form->addRow("Codec", _codec);

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
    _safeZones = new QCheckBox("Include safe zones", this);
    _safeZones->setToolTip("Draw the safe zones and aspect-ratio frames over every frame, for a review copy");
    form->addRow("", _safeZones);
    _audio = new QCheckBox("Include sound", this);
    _audio->setChecked(true);
    form->addRow("", _audio);
    _loop = new QCheckBox("Loop forever", this);
    _loop->setChecked(true);
    form->addRow("", _loop);
    _camera = new QCheckBox("Through the camera", this);
    _camera->setChecked(true);
    _camera->setToolTip("Render the scene as its camera sees it; off, the stage as drawn");
    form->addRow("", _camera);

    _area = new QComboBox(this);
    _area->addItems({"Stage", "Everything (the pasteboard too)"});
    _area->setToolTip("The stage, or all the artwork on the frames, wherever it is");
    form->addRow("Area", _area);

    QHBoxLayout* gifRow = new QHBoxLayout;
    _gifColors = plainSpin(2, 256, 256, this);
    _gifColors->setToolTip("How many colors each frame may use");
    _gifDither = new QCheckBox("Dither", this);
    _gifDither->setToolTip("Mix the colors there are to stand in for those there are not, for smoother gradients");
    gifRow->addWidget(_gifColors);
    gifRow->addWidget(_gifDither);
    gifRow->addStretch(1);
    form->addRow("Colors", gifRow);

    _svgHidden = new QCheckBox("Include hidden layers", this);
    _svgHidden->setChecked(true);
    _svgLinked = new QCheckBox("Link images (write each once beside the SVGs)", this);
    form->addRow("", _svgHidden);
    form->addRow("", _svgLinked);

    _chooseLayers = new QPushButton("All visible layers", this);
    _chooseLayers->setToolTip("Choose which layers get a sequence of their own");
    connect(_chooseLayers, &QPushButton::clicked, this, [this]() {
        QDialog chooser(this);
        chooser.setWindowTitle("Layers to Export");
        QVBoxLayout* list = new QVBoxLayout(&chooser);
        std::vector<QCheckBox*> boxes;
        for (const QString& name : _layerNames)
        {
            QCheckBox* box = new QCheckBox(name, &chooser);
            box->setChecked(_chosenLayers.isEmpty() || _chosenLayers.contains(name));
            list->addWidget(box);
            boxes.push_back(box);
        }
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &chooser);
        connect(buttons, &QDialogButtonBox::accepted, &chooser, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);
        list->addWidget(buttons);
        if (chooser.exec() != QDialog::Accepted)
            return;
        _chosenLayers.clear();
        for (QCheckBox* box : boxes)
            if (box->isChecked())
                _chosenLayers << box->text();
        if (_chosenLayers.size() == static_cast<int>(boxes.size()))
            _chosenLayers.clear();
        _chooseLayers->setText(_chosenLayers.isEmpty() ? QString("All visible layers")
                                                       : QString("%1 layer%2").arg(_chosenLayers.size()).arg(_chosenLayers.size() == 1 ? "" : "s"));
    });
    form->addRow("Layers", _chooseLayers);

    _scenes = new QComboBox(this);
    _scenes->addItems({"This scene", "All scenes, one after another", "Each scene to its own file"});
    _scenes->setEnabled(false);
    connect(_scenes, &QComboBox::currentIndexChanged, this, [this](int index) {
        // All scenes run from the first frame of the first to the last of
        // the last.
        _first->setEnabled(index == 0);
        _last->setEnabled(index == 0);
    });
    form->addRow("Scenes", _scenes);

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
    connect(_codec, &QComboBox::currentIndexChanged, this, &MovieExportDialog::formatChanged);
    connect(_width, &HotSpinBox::valueChanged, this, &MovieExportDialog::widthChanged);
    connect(_height, &HotSpinBox::valueChanged, this, &MovieExportDialog::heightChanged);

    QSettings settings;
    _codec->setCurrentIndex(std::clamp(settings.value("export/codec", 0).toInt(), 0, 2));
    _gifColors->setValue(std::clamp(settings.value("export/gifColors", 256).toInt(), 2, 256));
    _gifDither->setChecked(settings.value("export/gifDither", false).toBool());
    _area->setCurrentIndex(settings.value("export/everything", false).toBool() ? 1 : 0);
    _svgLinked->setChecked(settings.value("export/svgLinked", false).toBool());
    _format->setCurrentIndex(std::clamp(settings.value("export/movieFormat", kVideoIndex).toInt(), 0, kLastIndex));
    formatChanged();
}

void MovieExportDialog::setScenes(const QStringList& names, int current)
{
    _sceneNames = names;
    _currentScene = current;
    _scenes->setEnabled(names.size() > 1);
}

void MovieExportDialog::setLayers(const QStringList& names)
{
    _layerNames = names;
}

void MovieExportDialog::formatChanged()
{
    const int index = _format->currentIndex();
    const bool video = index == kVideoIndex;
    const bool sound = index == kWavIndex;
    const bool prores = _codec->currentIndex() > 0;
    _transparent->setEnabled((!video || _codec->currentIndex() == 2) && !sound);
    _audio->setEnabled(video);
    _loop->setEnabled(index == kGifIndex);
    _codec->setEnabled(video);
    _gifColors->setEnabled(index == kGifIndex);
    _gifDither->setEnabled(index == kGifIndex);
    _chooseLayers->setEnabled(index == kLayersIndex);
    _svgHidden->setEnabled(index == kSvgIndex);
    _svgLinked->setEnabled(index == kSvgIndex);
    _area->setEnabled(!sound);
    _width->setEnabled(!sound);
    _height->setEnabled(!sound);
    _keepAspect->setEnabled(!sound);
    const QFileInfo info(_path->text());
    const QString wanted = video && prores ? QString("mov") : QString(kSuffixes[std::clamp(index, 0, kLastIndex)]);
    if (!_path->text().isEmpty() && info.suffix().toLower() != wanted && !(video && !prores && info.suffix().toLower() == "mov"))
        _path->setText(info.dir().filePath(info.completeBaseName() + "." + wanted));
    if (index == kGifIndex)
        _encoderNote->setText("Each frame with its own 256 colors, timed to the frame rate.");
    else if (index == kLayersIndex)
        _encoderNote->setText("A numbered sequence for each visible layer, named after it.");
    else if (sound)
        _encoderNote->setText("The sound of the frames, mixed as it plays.");
    else if (index == kPassesIndex)
        _encoderNote->setText("Line, color and matte sequences, each numbered from 0001, for compositing.");
    else if (index == kExrIndex)
        _encoderNote->setText("One OpenEXR a frame: the picture, and the line, color and matte passes as layers.");
    else if (index == kSvgIndex)
        _encoderNote->setText("One SVG a frame, numbered from 0001: shapes, strokes, gradients and text as vectors.");
    else if (video)
        _encoderNote->setText(MovieExport::ffmpegPath().isEmpty() ? MovieExport::missingFfmpeg() : QString());
    else
    {
        _encoderNote->setText("One image a frame, numbered from 0001.");
    }
}

void MovieExportDialog::browse()
{
    const int index = _format->currentIndex();
    const char* filters[8] = {"PNG Images (*.png)", "MP4 Video (*.mp4);;QuickTime Movie (*.mov)", "GIF Animation (*.gif)",
        "PNG Images (*.png)", "WAV Sound (*.wav)", "PNG Images (*.png)", "OpenEXR Images (*.exr)", "SVG Images (*.svg)"};
    const QString file = QFileDialog::getSaveFileName(this, "Export Movie", _path->text(),
        filters[std::clamp(index, 0, kLastIndex)]);
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
    settings.format = static_cast<MovieExportSettings::Format>(std::clamp(_format->currentIndex(), 0, kLastIndex));
    settings.loop = _loop->isChecked();
    settings.camera = _camera->isChecked();
    settings.path = _path->text();
    settings.size = QSize(_width->value(), _height->value());
    settings.first = std::min(_first->value(), _last->value()) - 1;
    settings.last = std::max(_first->value(), _last->value()) - 1;
    settings.transparent = _transparent->isChecked();
    settings.safeZones = _safeZones->isChecked();
    settings.audio = _audio->isChecked();
    settings.codec = static_cast<MovieExportSettings::Codec>(_codec->currentIndex());
    settings.gifColors = _gifColors->value();
    settings.gifDither = _gifDither->isChecked();
    settings.layers = _chosenLayers;
    settings.everything = _area->currentIndex() == 1;
    settings.svgHidden = _svgHidden->isChecked();
    settings.svgLinked = _svgLinked->isChecked();
    settings.scenes = _scenes->isEnabled() ? static_cast<MovieExportSettings::Scenes>(_scenes->currentIndex())
                                           : MovieExportSettings::Scenes::Current;
    settings.sceneNames = _sceneNames;
    settings.sceneCount = std::max(1, static_cast<int>(_sceneNames.size()));
    settings.currentScene = _currentScene;
    QSettings stored;
    stored.setValue("export/movieFormat", _format->currentIndex());
    stored.setValue("export/codec", _codec->currentIndex());
    stored.setValue("export/gifColors", settings.gifColors);
    stored.setValue("export/gifDither", settings.gifDither);
    stored.setValue("export/everything", settings.everything);
    stored.setValue("export/svgLinked", settings.svgLinked);
    return settings;
}

namespace MovieExport {

QString ffmpegPath()
{
#ifdef Q_OS_WIN
    const QString name = "ffmpeg.exe";
#else
    const QString name = "ffmpeg";
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo(path).isExecutable() ? path : QString();
}

QString missingFfmpeg()
{
    return QString("This copy of Phoenix is missing its ffmpeg, which it encodes and reads video with. "
                   "It belongs in %1; reinstalling Phoenix puts it back.")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
}

bool canEncodeMp3()
{
    static int known = -1;
    if (known < 0)
    {
        known = 0;
        const QString ffmpeg = ffmpegPath();
        Process process;
        if (!ffmpeg.isEmpty())
        {
            process.start(ffmpeg, {"-hide_banner", "-encoders"});
            if (process.waitForFinished(10000))
                known = process.readAllStandardOutput().contains("libmp3lame") ? 1 : 0;
        }
    }
    return known == 1;
}

/// \a samples through ffmpeg with \a arguments (the codec and its settings,
/// then the output's format), into \a encoded; \a what names the format for
/// an error.
static bool encodeSound(const std::vector<float>& samples, const QStringList& arguments, const QString& suffix,
    const QString& what, std::vector<uint8_t>& encoded, QString& error)
{
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty())
    {
        error = missingFfmpeg();
        return false;
    }
    QTemporaryFile wav(QDir::temp().filePath("phoenix_soundXXXXXX.wav"));
    QTemporaryFile out(QDir::temp().filePath("phoenix_soundXXXXXX." + suffix));
    if (!wav.open() || !writeWav(wav, samples, PcmBuffer::kRate) || !out.open())
    {
        error = "Could not write the sound to a temporary file.";
        return false;
    }
    wav.close();
    out.close();
    Process process;
    process.start(ffmpeg, QStringList{"-y", "-hide_banner", "-loglevel", "error", "-i", wav.fileName()} + arguments +
                              QStringList{out.fileName()});
    if (!process.waitForFinished(600000) || process.exitStatus() != Process::NormalExit || process.exitCode() != 0)
    {
        const QString said = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        error = said.isEmpty() ? QString("ffmpeg could not encode %1.").arg(what) : said;
        return false;
    }
    QFile file(out.fileName());
    if (!file.open(QIODevice::ReadOnly))
    {
        error = "Could not read the encoded sound.";
        return false;
    }
    const QByteArray bytes = file.readAll();
    encoded.assign(bytes.begin(), bytes.end());
    return true;
}

bool encodeMp3(const std::vector<float>& samples, int rate, int channels, int kbps, std::vector<uint8_t>& mp3,
    QString& error)
{
    return encodeSound(samples,
        {"-c:a", "libmp3lame", "-ar", QString::number(rate), "-ac", QString::number(channels), "-b:a",
            QString("%1k").arg(kbps), "-write_xing", "0", "-id3v2_version", "0", "-f", "mp3"},
        "mp3", "MP3", mp3, error);
}

bool encodeAac(const std::vector<float>& samples, int rate, int channels, int kbps, std::vector<uint8_t>& m4a,
    QString& error)
{
    return encodeSound(samples,
        {"-c:a", "aac", "-ar", QString::number(rate), "-ac", QString::number(channels), "-b:a", QString("%1k").arg(kbps),
            "-movflags", "+faststart", "-f", "ipod"},
        "m4a", "AAC", m4a, error);
}

bool runOne(const MovieExportSettings& settings, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error);

bool run(const MovieExportSettings& given, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error)
{
    error.clear();
    // A review copy carries the safe zones on every frame; the view is put
    // back however the export ends.
    struct Zones
    {
        PhoenixView& view;
        bool was;
        ~Zones() { view.setExportSafeZones(was); }
    } zones{view, view.exportSafeZones()};
    view.setExportSafeZones(given.safeZones);
    if (given.scenes == MovieExportSettings::Scenes::Current || !given.showScene || given.sceneCount <= 1)
        return runOne(given, view, audio, frameRate, parent, error);
    // Each scene to a file named after it; the scene shown put back after.
    struct Back
    {
        const MovieExportSettings& settings;
        ~Back() { settings.showScene(settings.currentScene); }
    } back{given};
    if (given.scenes == MovieExportSettings::Scenes::Each)
    {
        const QFileInfo info(given.path);
        for (int scene = 0; scene < given.sceneCount; ++scene)
        {
            MovieExportSettings one = given;
            one.scenes = MovieExportSettings::Scenes::Current;
            const int frames = given.showScene(scene);
            one.first = 0;
            one.last = std::max(0, frames - 1);
            QString name = given.sceneNames.value(scene, QString("Scene %1").arg(scene + 1));
            name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            one.path = info.dir().filePath(info.completeBaseName() + "_" + name + "." + info.suffix());
            if (!runOne(one, view, audio, frameRate, parent, error))
                return false;
        }
        return true;
    }
    // All scenes, one after another: one file, the frames counted on
    // through every scene.
    MovieExportSettings all = given;
    all.scenes = MovieExportSettings::Scenes::All;
    return runOne(all, view, audio, frameRate, parent, error);
}

bool runOne(const MovieExportSettings& settings, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error)
{
    error.clear();
    // The frames to draw: of the scene shown, or of every scene in turn.
    struct Segment
    {
        int scene;
        int first;
        int last;
    };
    std::vector<Segment> segments;
    if (settings.scenes == MovieExportSettings::Scenes::All && settings.showScene)
    {
        for (int scene = 0; scene < settings.sceneCount; ++scene)
            segments.push_back({scene, 0, std::max(0, settings.showScene(scene) - 1)});
        settings.showScene(segments.front().scene);
    }
    else
    {
        segments.push_back({-1, settings.first, settings.last});
    }
    int count = 0;
    for (const Segment& segment : segments)
        count += segment.last - segment.first + 1;
    // At another frame rate, frame i of the movie shows the document's frame
    // that far into it.
    const int sourceCount = count;
    double stride = 1.0;
    if (settings.outputRate > 0.0 && frameRate > 0.0 && std::fabs(settings.outputRate - frameRate) > 1.0e-6)
    {
        stride = frameRate / settings.outputRate;
        count = std::max(1, static_cast<int>(std::ceil(sourceCount / stride - 1.0e-9)));
        frameRate = settings.outputRate;
    }
    int shownScene = segments.front().scene;
    // Frame \a i of the export: its scene shown, then its frame.
    const auto locate = [&](int i, int& frame) {
        for (const Segment& segment : segments)
        {
            const int length = segment.last - segment.first + 1;
            if (i < length)
            {
                if (segment.scene >= 0 && segment.scene != shownScene && settings.showScene)
                {
                    settings.showScene(segment.scene);
                    shownScene = segment.scene;
                }
                frame = segment.first + i;
                return;
            }
            i -= length;
        }
        frame = segments.back().last;
    };
    // Everything: the artwork's bounds over the frames, at the scale the
    // size gives the stage.
    QRectF area;
    QSize size = settings.size;
    if (settings.everything && view.flaDocument() && view.flaDocument()->document)
    {
        const fla::Document* doc = view.flaDocument()->document;
        for (const Segment& segment : segments)
        {
            if (segment.scene >= 0 && settings.showScene)
                settings.showScene(segment.scene);
            const QRectF here = view.framesBounds(segment.first, segment.last);
            area = area.isValid() ? area.united(here) : here;
        }
        if (segments.front().scene >= 0 && settings.showScene)
        {
            settings.showScene(segments.front().scene);
            shownScene = segments.front().scene;
        }
        double scale = doc->width > 0 ? static_cast<double>(settings.size.width()) / doc->width : 1.0;
        area = area.toAlignedRect();
        // Art spread far over the pasteboard can make an image no one can
        // hold: the longer side is kept to 16384 pixels.
        const double longest = std::max(area.width(), area.height()) * scale;
        if (longest > 16384.0)
            scale *= 16384.0 / longest;
        size = QSize(std::max(1, static_cast<int>(std::lround(std::min(area.width() * scale, 16384.0)))),
            std::max(1, static_cast<int>(std::lround(std::min(area.height() * scale, 16384.0)))));
    }
    const QString tooLarge = QString("A frame at %1 × %2 pixels could not be drawn: there is not enough "
                                     "memory for it. Export at a smaller size.")
                                 .arg(size.width())
                                 .arg(size.height());
    const auto render = [&](int i, const QSize& at, bool transparent) {
        int frame = 0;
        locate(std::min(sourceCount - 1, static_cast<int>(std::floor(i * stride + 1.0e-9))), frame);
        const fla::Document* doc = view.flaDocument() ? view.flaDocument()->document : nullptr;
        if (!settings.letterbox || !doc || doc->width <= 0 || doc->height <= 0 || area.isValid())
            return view.renderFrame(frame, at, transparent, area);
        // The stage fitted inside, the rest the background (or clear).
        const double scale = std::min(at.width() / doc->width, at.height() / doc->height);
        const QSize fit(std::clamp(static_cast<int>(std::lround(doc->width * scale)), 1, at.width()),
            std::clamp(static_cast<int>(std::lround(doc->height * scale)), 1, at.height()));
        if (std::abs(fit.width() - at.width()) <= 1 && std::abs(fit.height() - at.height()) <= 1)
            return view.renderFrame(frame, at, transparent, area);
        const QImage stage = view.renderFrame(frame, fit, transparent, area);
        QImage framed(at, QImage::Format_ARGB32_Premultiplied);
        if (stage.isNull() || framed.isNull())
            return QImage();
        framed.fill(transparent ? QColor(Qt::transparent)
                                : QColor(doc->backgroundColor[0], doc->backgroundColor[1], doc->backgroundColor[2]));
        QPainter painter(&framed);
        painter.drawImage((at.width() - fit.width()) / 2, (at.height() - fit.height()) / 2, stage);
        painter.end();
        return framed;
    };
    const auto mixdown = [&]() {
        std::vector<float> samples;
        if (!audio)
            return samples;
        for (const Segment& segment : segments)
        {
            if (segment.scene >= 0 && settings.showScene)
                settings.showScene(segment.scene);
            const std::vector<float> part = audio->mixdown(segment.first, segment.last);
            samples.insert(samples.end(), part.begin(), part.end());
        }
        if (segments.front().scene >= 0 && settings.showScene)
        {
            settings.showScene(segments.front().scene);
            shownScene = segments.front().scene;
        }
        return samples;
    };
    if (count <= 0 || size.isEmpty() || settings.path.isEmpty())
    {
        error = "Nothing to export.";
        return false;
    }

    // With or without the camera, as asked; the view as it was afterwards.
    struct CameraView
    {
        PhoenixView& view;
        bool was;
        ~CameraView() { view.setCameraView(was); }
    } cameraView{view, view.cameraView()};
    view.setCameraView(settings.camera);

    QProgressDialog progress("Exporting...", "Cancel", 0, count, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    if (settings.format == MovieExportSettings::Format::Wav)
    {
        // The sound of the frames, mixed as playback mixes it.
        const std::vector<float> samples = mixdown();
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
        gif::Options options;
        options.colors = settings.gifColors;
        options.dither = settings.gifDither;
        options.repeat = settings.gifRepeat;
        gif::AnimationWriter writer(size.width(), size.height(), settings.loop, options);
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
                return false;
            const QImage image = render(i, size, settings.transparent).convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull())
            {
                error = tooLarge;
                return false;
            }
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

    if (settings.format == MovieExportSettings::Format::Apng)
    {
        // Written here, so it needs nothing of ffmpeg.
        const bool loops = settings.loop;
        apng::AnimationWriter writer(size.width(), size.height(), frameRate,
            loops ? (settings.gifRepeat > 0 ? settings.gifRepeat + 1 : 0) : 1);
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
                return false;
            const QImage image = render(i, size, settings.transparent).convertToFormat(QImage::Format_RGBA8888);
            if (image.isNull())
            {
                error = tooLarge;
                return false;
            }
            std::vector<uint8_t> rgba(static_cast<size_t>(image.width()) * image.height() * 4);
            for (int y = 0; y < image.height(); ++y)
                std::memcpy(rgba.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
                    static_cast<size_t>(image.width()) * 4);
            writer.addFrame(rgba);
        }
        const std::vector<uint8_t> bytes = writer.finish();
        QFile file(settings.path);
        if (bytes.empty() || !file.open(QIODevice::WriteOnly) ||
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
                (layer->layerType == fla::Layer::Type::Normal || layer->layerType == fla::Layer::Type::Masked) &&
                (settings.layers.isEmpty() || settings.layers.contains(QString::fromStdString(layer->name))))
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
                const QImage image = render(i, size, settings.transparent);
                if (image.isNull())
                {
                    view.setOnlyLayer(nullptr);
                    error = tooLarge;
                    return false;
                }
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

    if (settings.format == MovieExportSettings::Format::Passes || settings.format == MovieExportSettings::Format::Exr)
    {
        // Each frame drawn as a whole, then its lines alone and its color
        // alone, all with no stage behind them; the matte is the whole's
        // coverage.
        const bool exrOut = settings.format == MovieExportSettings::Format::Exr;
        const int digits = std::max(4, static_cast<int>(QString::number(count).size()));
        const QFileInfo info(settings.path);
        const auto named = [&](const QString& pass, int number) {
            return sequenceName(info.dir().filePath(info.completeBaseName() + (pass.isEmpty() ? QString() : "_" + pass) +
                                    (exrOut ? ".exr" : ".png")),
                number, digits);
        };
        const auto linear = [](int value) {
            const double c = value / 255.0;
            return static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        };
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
            {
                view.setRenderPass(PhoenixView::RenderPass::All);
                return false;
            }
            view.setRenderPass(PhoenixView::RenderPass::All);
            const QImage whole = render(i, size, true).convertToFormat(QImage::Format_ARGB32);
            view.setRenderPass(PhoenixView::RenderPass::Lines);
            const QImage lines = render(i, size, true).convertToFormat(QImage::Format_ARGB32);
            view.setRenderPass(PhoenixView::RenderPass::Color);
            const QImage color = render(i, size, true).convertToFormat(QImage::Format_ARGB32);
            view.setRenderPass(PhoenixView::RenderPass::All);
            if (whole.isNull() || lines.isNull() || color.isNull())
            {
                error = tooLarge;
                return false;
            }
            QImage matte(whole.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < whole.height(); ++y)
            {
                const QRgb* in = reinterpret_cast<const QRgb*>(whole.constScanLine(y));
                uchar* out = matte.scanLine(y);
                for (int x = 0; x < whole.width(); ++x)
                    out[x] = static_cast<uchar>(qAlpha(in[x]));
            }
            if (!exrOut)
            {
                const std::pair<QString, const QImage*> passes[] = {{"line", &lines}, {"color", &color}, {"matte", &matte}};
                for (const auto& [pass, image] : passes)
                {
                    const QString file = named(pass, i + 1);
                    if (!image->save(file, "PNG"))
                    {
                        error = QString("Could not write %1.").arg(QDir::toNativeSeparators(file));
                        return false;
                    }
                }
                continue;
            }
            // The picture in the default layer (R, G, B, A), the passes as
            // layers of their own: linear light, premultiplied, as
            // compositors expect.
            const int w = whole.width();
            const int h = whole.height();
            std::vector<exr::Channel> channels;
            const auto addLayer = [&](const QString& prefix, const QImage& image) {
                exr::Channel r{(prefix + "R").toStdString(), {}}, g{(prefix + "G").toStdString(), {}},
                    b{(prefix + "B").toStdString(), {}}, a{(prefix + "A").toStdString(), {}};
                for (exr::Channel* c : {&r, &g, &b, &a})
                    c->values.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
                for (int y = 0; y < h; ++y)
                {
                    const QRgb* in = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                    for (int x = 0; x < w; ++x)
                    {
                        const size_t at = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                        const float alpha = qAlpha(in[x]) / 255.0f;
                        r.values[at] = linear(qRed(in[x])) * alpha;
                        g.values[at] = linear(qGreen(in[x])) * alpha;
                        b.values[at] = linear(qBlue(in[x])) * alpha;
                        a.values[at] = alpha;
                    }
                }
                for (exr::Channel* c : {&r, &g, &b, &a})
                    channels.push_back(std::move(*c));
            };
            addLayer(QString(), whole);
            addLayer("line.", lines);
            addLayer("color.", color);
            exr::Channel matteChannel{"matte.Y", std::vector<float>(static_cast<size_t>(w) * static_cast<size_t>(h))};
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    matteChannel.values[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                        matte.constScanLine(y)[x] / 255.0f;
            channels.push_back(std::move(matteChannel));
            std::string exrError;
            const QString file = named(QString(), i + 1);
            if (!exr::write(QDir::toNativeSeparators(file).toStdString(), w, h, std::move(channels), exrError))
            {
                error = QString::fromStdString(exrError);
                return false;
            }
        }
        progress.setValue(count);
        return true;
    }

    if (settings.format == MovieExportSettings::Format::SvgSequence)
    {
        const int digits = std::max(4, static_cast<int>(QString::number(count).size()));
        QStringList written;
        for (int i = 0; i < count; ++i)
        {
            progress.setValue(i);
            if (progress.wasCanceled())
                return false;
            int frame = 0;
            locate(i, frame);
            const QString file = sequenceName(settings.path, i + 1, digits);
            if (!view.exportFrameSvg(file, frame, settings.transparent, settings.svgHidden, settings.svgLinked, area, &written))
            {
                error = QString("Could not write %1.").arg(QDir::toNativeSeparators(file));
                return false;
            }
        }
        progress.setValue(count);
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
            const QImage image = render(i, size, settings.transparent);
            if (image.isNull())
            {
                error = tooLarge;
                return false;
            }
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
        error = missingFfmpeg();
        return false;
    }
    // File > Share's WebP, WebM and MP4, encoded as share::encoderArguments
    // says.
    const bool shared = settings.format == MovieExportSettings::Format::WebP ||
                        settings.format == MovieExportSettings::Format::WebM ||
                        (settings.format == MovieExportSettings::Format::Video && settings.quality >= 0 &&
                            settings.codec == MovieExportSettings::Codec::H264);
    const share::Format sharedFormat = settings.format == MovieExportSettings::Format::WebP ? share::Format::WebP
                                       : settings.format == MovieExportSettings::Format::WebM ? share::Format::WebM
                                                                                              : share::Format::Mp4;
    // H.264 in 4:2:0, and ProRes, want even sides.
    QSize even(std::max(2, size.width() & ~1), std::max(2, size.height() & ~1));
    if (shared)
    {
        int width = size.width();
        int height = size.height();
        share::frameSize(sharedFormat, width, height);
        even = QSize(width, height);
    }
    const bool alpha = settings.transparent && (settings.codec == MovieExportSettings::Codec::ProRes4444 ||
                                                   (shared && share::canBeTransparent(sharedFormat)));

    QTemporaryFile wav(QDir::temp().filePath("phoenix_soundXXXXXX.wav"));
    bool withAudio = false;
    if (settings.audio && audio && settings.format != MovieExportSettings::Format::WebP)
    {
        const std::vector<float> samples = mixdown();
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
        QString("%1x%2").arg(even.width()).arg(even.height()), "-framerate", QString::number(frameRate, 'g', 10),
        "-i", "-"};
    if (withAudio)
        arguments << "-i" << wav.fileName();
    if (shared)
    {
        const int plays = settings.loop ? (settings.gifRepeat > 0 ? settings.gifRepeat + 1 : 0) : 1;
        for (const std::string& argument : share::encoderArguments(sharedFormat, settings.quality > 0 ? settings.quality : 80, alpha,
                 plays, withAudio, settings.path.toStdString()))
            arguments << QString::fromStdString(argument);
    }
    else
    {
        arguments << "-map" << "0:v";
        const bool prores = settings.codec != MovieExportSettings::Codec::H264;
        if (withAudio)
            arguments << "-map" << "1:a" << "-c:a" << (prores ? "pcm_s16le" : "aac") << "-shortest";
        if (withAudio && !prores)
            arguments << "-b:a" << "192k";
        if (settings.codec == MovieExportSettings::Codec::ProRes422)
            arguments << "-c:v" << "prores_ks" << "-profile:v" << "3" << "-pix_fmt" << "yuv422p10le" << settings.path;
        else if (settings.codec == MovieExportSettings::Codec::ProRes4444)
            arguments << "-c:v" << "prores_ks" << "-profile:v" << "4" << "-pix_fmt"
                      << (alpha ? "yuva444p10le" : "yuv444p10le") << settings.path;
        else
            arguments << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-pix_fmt" << "yuv420p"
                      << "-movflags" << "+faststart" << settings.path;
    }

    Process process;
    process.setProcessChannelMode(Process::SeparateChannels);
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(10000))
    {
        error = QString("Could not start %1.").arg(QDir::toNativeSeparators(ffmpeg));
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        progress.setValue(i);
        if (progress.wasCanceled() || process.state() != Process::Running)
            break;
        QImage image = render(i, even, alpha).convertToFormat(QImage::Format_ARGB32);
        if (image.isNull())
        {
            // A frame of no bytes would shift every later one: stopped.
            process.kill();
            process.waitForFinished(5000);
            error = tooLarge;
            return false;
        }
        // ffmpeg was told the size: every frame is exactly that.
        if (image.size() != even)
            image = image.copy(0, 0, even.width(), even.height());
        for (int y = 0; y < image.height(); ++y)
            process.write(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
        while (process.bytesToWrite() > 0 && process.state() == Process::Running)
        {
            process.waitForBytesWritten(100);
            QApplication::processEvents();
        }
    }
    const bool cancelled = progress.wasCanceled();
    process.closeWriteChannel();
    while (!process.waitForFinished(100))
    {
        if (process.state() == Process::NotRunning)
            break;
        QApplication::processEvents();
    }
    progress.setValue(count);
    if (cancelled)
    {
        QFile::remove(settings.path);
        return false;
    }
    if (process.exitStatus() != Process::NormalExit || process.exitCode() != 0)
    {
        error = QString("ffmpeg could not write the video.\n\n%1")
                    .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

} // namespace MovieExport
