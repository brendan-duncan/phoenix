#pragma once

#include <QString>
#include <QStringList>

class QWidget;

/// Bringing a video in as a reference to animate over: ffmpeg turns it into
/// a still for each frame at the document's frame rate, and its sound into
/// a WAV, which Import Video then lays on a guide layer, so the reference
/// shows while drawing but is left out of anything published or exported.
namespace VideoImport {

/// Whether a file is a video Import Video takes, by its extension.
bool isVideo(const QString& path);

/// The file filter for the Open dialog.
QString fileFilter();

struct Result
{
    /// The stills, in order, one per frame.
    QStringList frames;
    /// The sound as 16-bit WAV, or empty when there is none or it was not
    /// asked for.
    QString audio;
};

/// Runs \a ffmpeg on \a video into \a folder: JPEG stills at \a frameRate,
/// no wider than \a maxWidth (0 for the video's own size), and with
/// \a withAudio its sound. Shows progress over \a parent and can be
/// cancelled; false with \a error empty when it was.
bool extract(const QString& ffmpeg, const QString& video, const QString& folder, double frameRate, int maxWidth,
    bool withAudio, QWidget* parent, Result& result, QString& error);

} // namespace VideoImport
