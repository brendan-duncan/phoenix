#include "video_import.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>

namespace VideoImport {

namespace {

const QStringList kVideoSuffixes = {"mp4", "mov", "m4v", "avi", "mkv", "webm", "flv", "wmv", "mpg", "mpeg"};

/// Runs ffmpeg with \a arguments, keeping the dialog up to date from how far
/// into the video it has got. False when it failed or was cancelled.
bool runFfmpeg(const QString& ffmpeg, const QStringList& arguments, QProgressDialog& progress,
    const QString& label, QString& error)
{
    progress.setLabelText(label);
    progress.setValue(0);
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(10000))
    {
        error = QString("Could not start %1.").arg(QDir::toNativeSeparators(ffmpeg));
        return false;
    }

    // ffmpeg reports the video's length, then how far it has got, on its
    // error output.
    static const QRegularExpression durationPattern("Duration: (\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
    static const QRegularExpression timePattern("time=(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
    const auto seconds = [](const QRegularExpressionMatch& match) {
        return match.captured(1).toDouble() * 3600.0 + match.captured(2).toDouble() * 60.0 + match.captured(3).toDouble();
    };
    double duration = 0.0;
    QString log;
    while (!process.waitForFinished(100))
    {
        if (process.state() == QProcess::NotRunning)
            break;
        const QString chunk = QString::fromUtf8(process.readAllStandardError());
        log += chunk;
        if (log.size() > 20000)
            log = log.right(20000);
        if (duration <= 0.0)
        {
            const QRegularExpressionMatch match = durationPattern.match(log);
            if (match.hasMatch())
                duration = seconds(match);
        }
        QRegularExpressionMatchIterator times = timePattern.globalMatch(chunk);
        double at = -1.0;
        while (times.hasNext())
            at = seconds(times.next());
        if (duration > 0.0 && at >= 0.0)
            progress.setValue(static_cast<int>(std::min(1.0, at / duration) * 1000.0));
        QApplication::processEvents();
        if (progress.wasCanceled())
        {
            process.kill();
            process.waitForFinished(5000);
            error.clear();
            return false;
        }
    }
    log += QString::fromUtf8(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        error = QString("ffmpeg could not read the video.\n\n%1").arg(log.right(1500).trimmed());
        return false;
    }
    progress.setValue(1000);
    return true;
}

} // namespace

bool isVideo(const QString& path)
{
    return kVideoSuffixes.contains(QFileInfo(path).suffix().toLower());
}

QString fileFilter()
{
    QStringList patterns;
    for (const QString& suffix : kVideoSuffixes)
        patterns << "*." + suffix;
    return QString("Videos (%1);;All files (*)").arg(patterns.join(' '));
}

bool extract(const QString& ffmpeg, const QString& video, const QString& folder, double frameRate, int maxWidth,
    bool withAudio, QWidget* parent, Result& result, QString& error)
{
    result = Result();
    QProgressDialog progress("Reading the video...", "Cancel", 0, 1000, parent);
    progress.setWindowTitle("Import Video");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    // Stills at the document's rate, shrunk to fit when the video is wider
    // than asked, never grown.
    QString filter = QString("fps=%1").arg(frameRate, 0, 'f', 3);
    if (maxWidth > 0)
        filter += QString(",scale='min(%1,iw)':-2").arg(maxWidth);
    const QString pattern = QDir(folder).filePath("frame%05d.jpg");
    if (!runFfmpeg(ffmpeg, {"-hide_banner", "-y", "-i", video, "-an", "-vf", filter, "-q:v", "3", pattern},
            progress, "Reading the video's frames...", error))
        return false;
    for (const QFileInfo& still : QDir(folder).entryInfoList({"frame*.jpg"}, QDir::Files, QDir::Name))
        result.frames << still.absoluteFilePath();
    if (result.frames.isEmpty())
    {
        error = "ffmpeg found no frames in the video.";
        return false;
    }

    if (withAudio)
    {
        // A video with no sound is not an error; it just has none.
        const QString wav = QDir(folder).filePath("audio.wav");
        QString audioError;
        if (runFfmpeg(ffmpeg, {"-hide_banner", "-y", "-i", video, "-vn", "-ac", "2", "-ar", "44100",
                "-c:a", "pcm_s16le", wav}, progress, "Reading the video's sound...", audioError) &&
            QFileInfo(wav).size() > 44)
            result.audio = wav;
        else if (progress.wasCanceled())
            return false;
    }
    return true;
}

} // namespace VideoImport
