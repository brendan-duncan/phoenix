#pragma once

#include <QDialog>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class PhoenixView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class HotSpinBox;
class TimelineAudio;

/// What File > Export > Export Movie writes.
struct MovieExportSettings
{
    /// In the order the dialog lists them.
    enum class Format
    {
        PngSequence,
        Video,
        Gif,
        LayerSequences,
        Wav,
        /// PNG sequences of the line, color and matte passes.
        Passes,
        /// One multi-layer OpenEXR a frame: the picture and the passes.
        Exr,
        /// One SVG a frame, numbered as a PNG sequence is.
        SvgSequence
    };
    /// The video's encoding.
    enum class Codec
    {
        H264,
        ProRes422,
        /// With an alpha channel, for a transparent background.
        ProRes4444
    };
    /// Which scenes: the one shown, all one after another, or each to a
    /// file of its own named after it.
    enum class Scenes
    {
        Current,
        All,
        Each
    };

    Format format = Format::Video;
    /// The video file, or for a sequence the first image's name, numbered
    /// from there ("walk.png" gives walk0001.png, walk0002.png...).
    QString path;
    QSize size;
    /// The frames, from 0.
    int first = 0;
    int last = 0;
    bool transparent = false;
    bool audio = true;
    /// An animated GIF plays over and over.
    bool loop = true;
    /// Seen through the scene's camera, when it has one.
    bool camera = true;
    Codec codec = Codec::H264;
    /// An animated GIF's colors, 2 to 256, and whether they are dithered.
    int gifColors = 256;
    bool gifDither = false;
    /// A looping GIF plays this many times after the first; 0 for ever.
    int gifRepeat = 0;
    /// For a sequence per layer, the layers by name; empty for every
    /// visible one.
    QStringList layers;
    /// Everything on the frames, the pasteboard too, rather than the stage.
    bool everything = false;
    /// SVG: hidden layers drawn too, and images linked rather than
    /// embedded.
    bool svgHidden = true;
    bool svgLinked = false;
    Scenes scenes = Scenes::Current;
    /// Shows scene \a index for drawing and gives its frame count; set by
    /// whoever runs the export when there is more than one scene.
    std::function<int(int)> showScene;
    int sceneCount = 1;
    int currentScene = 0;
    QStringList sceneNames;
};

/// Asks how to export the movie: as a numbered PNG sequence or as an MP4
/// video, at what size and over which frames.
class MovieExportDialog : public QDialog
{
    Q_OBJECT

public:
    MovieExportDialog(const QSize& documentSize, int frameCount, const QString& suggestedPath, QWidget* parent);

    /// The document's scenes, to offer exporting them all or each; and the
    /// current scene's layers, to choose among for a sequence per layer.
    void setScenes(const QStringList& names, int current);
    void setLayers(const QStringList& names);

    MovieExportSettings settings() const;

private:
    void formatChanged();
    void browse();
    void widthChanged(int width);
    void heightChanged(int height);

    QSize _documentSize;
    QComboBox* _format = nullptr;
    QLineEdit* _path = nullptr;
    HotSpinBox* _width = nullptr;
    HotSpinBox* _height = nullptr;
    QCheckBox* _keepAspect = nullptr;
    HotSpinBox* _first = nullptr;
    HotSpinBox* _last = nullptr;
    QCheckBox* _transparent = nullptr;
    QCheckBox* _audio = nullptr;
    QCheckBox* _loop = nullptr;
    QCheckBox* _camera = nullptr;
    QComboBox* _codec = nullptr;
    HotSpinBox* _gifColors = nullptr;
    QCheckBox* _gifDither = nullptr;
    QComboBox* _area = nullptr;
    QComboBox* _scenes = nullptr;
    class QPushButton* _chooseLayers = nullptr;
    QCheckBox* _svgHidden = nullptr;
    QCheckBox* _svgLinked = nullptr;
    QStringList _layerNames;
    QStringList _chosenLayers;
    QStringList _sceneNames;
    int _currentScene = 0;
    QLabel* _encoderNote = nullptr;
    bool _syncing = false;
};

namespace MovieExport {

/// The ffmpeg that ships with Phoenix, beside its program; empty when it is
/// missing. Phoenix never uses another one, so video works the same on
/// every machine with nothing else installed.
QString ffmpegPath();

/// What to tell someone whose Phoenix has lost its ffmpeg.
QString missingFfmpeg();

/// Writes the movie, showing progress over \a parent and letting it be
/// cancelled. False with \a error set when it could not be written; false
/// with \a error empty when cancelled.
bool run(const MovieExportSettings& settings, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error);

} // namespace MovieExport
