#pragma once

#include <QDialog>
#include <QSize>
#include <QString>

class PhoenixView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
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
        Wav
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
};

/// Asks how to export the movie: as a numbered PNG sequence or as an MP4
/// video, at what size and over which frames.
class MovieExportDialog : public QDialog
{
    Q_OBJECT

public:
    MovieExportDialog(const QSize& documentSize, int frameCount, const QString& suggestedPath, QWidget* parent);

    MovieExportSettings settings() const;

private:
    void formatChanged();
    void browse();
    void widthChanged(int width);
    void heightChanged(int height);

    QSize _documentSize;
    QComboBox* _format = nullptr;
    QLineEdit* _path = nullptr;
    QSpinBox* _width = nullptr;
    QSpinBox* _height = nullptr;
    QCheckBox* _keepAspect = nullptr;
    QSpinBox* _first = nullptr;
    QSpinBox* _last = nullptr;
    QCheckBox* _transparent = nullptr;
    QCheckBox* _audio = nullptr;
    QCheckBox* _loop = nullptr;
    QCheckBox* _camera = nullptr;
    QLabel* _encoderNote = nullptr;
    bool _syncing = false;
};

namespace MovieExport {

/// Where ffmpeg is: the one set in Phoenix's settings, or one on the PATH.
/// Empty when there is none.
QString ffmpegPath();

/// Remembers \a path as the ffmpeg to use.
void setFfmpegPath(const QString& path);

/// Writes the movie, showing progress over \a parent and letting it be
/// cancelled. False with \a error set when it could not be written; false
/// with \a error empty when cancelled.
bool run(const MovieExportSettings& settings, PhoenixView& view, TimelineAudio* audio, double frameRate,
    QWidget* parent, QString& error);

} // namespace MovieExport
