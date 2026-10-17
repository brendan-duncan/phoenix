#pragma once

#include <QString>
#include <QStringList>

class PhoenixView;
class TimelineAudio;

namespace fla {
class Document;
}

/// File > Publish's HTML5 format: a page that plays the movie in the
/// browser with Phoenix's JavaScript runtime. Beside the page go the
/// runtime (phoenix.js, and phoenix-createjs.js for an HTML5 Canvas
/// document), the movie's content (<name>.content.js, see
/// fla::html5::content) and its pictures and sounds (<name>_assets). The
/// page loads <name>.as.js too when there is one: the movie's ActionScript
/// translated to JavaScript (resources/html5/README.md).
namespace Html5Publish {

struct Settings
{
    /// The page to write; the rest goes beside it.
    QString page;
    /// The page fits the stage to the window, or shows it at its own size.
    bool fit = true;
    /// The main timeline starts again after its last frame.
    bool loop = true;
};

/// Writes the page for \a document as \a view draws it (the view showing
/// that document); \a audio decodes its sounds, null for none. The files
/// written go in \a written (names relative to the page's folder), what
/// could not be published as it is in \a notes. False with \a error set
/// when the page could not be written.
bool run(const Settings& settings, PhoenixView& view, TimelineAudio* audio, const fla::Document& document,
    QStringList& written, QStringList& notes, QString& error);

/// The runtime as one script: resources/html5's files in order.
QByteArray runtime();
/// The CreateJS layer HTML5 Canvas documents' scripts run on.
QByteArray createJs();

} // namespace Html5Publish
