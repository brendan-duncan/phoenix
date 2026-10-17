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
/// fla::html5::content) and its pictures and sounds (<name>_assets). An
/// ActionScript 3 movie's scripts go beside it as <name>.as.js (see
/// actionScript), which the page loads (resources/html5/README.md,
/// "Translated ActionScript").
namespace Html5Publish {

struct Settings
{
    /// The page to write; the rest goes beside it.
    QString page;
    /// The page fits the stage to the window, or shows it at its own size.
    bool fit = true;
    /// The main timeline starts again after its last frame.
    bool loop = true;
    /// The document's file, where its ActionScript 3 classes are looked
    /// for (with its publish settings' source paths and Phoenix's own);
    /// empty for a document not saved.
    QString source;
};

/// Writes the page for \a document as \a view draws it (the view showing
/// that document); \a audio decodes its sounds, null for none. The files
/// written go in \a written (names relative to the page's folder), what
/// could not be published as it is in \a notes. False with \a error set
/// when the page could not be written.
bool run(const Settings& settings, PhoenixView& view, TimelineAudio* audio, const fla::Document& document,
    QStringList& written, QStringList& notes, QString& error);

/// The runtime as one script: as3.js (libas3's runtime for translated
/// ActionScript), then resources/html5's files in order.
QByteArray runtime();

/// \a document's ActionScript 3 translated to JavaScript, as the page
/// loads it (<name>.as.js, \a name being the page's): the classes, then
/// what binds them to the movie (its document class, its symbols' classes).
/// Empty when the movie has no ActionScript 3, or it could not be
/// translated; \a map gets the source map, \a notes the errors and
/// warnings, with where they are.
QByteArray actionScript(const fla::Document& document, const QString& source, const QString& name, QByteArray& map,
    QStringList& notes);
/// The CreateJS layer HTML5 Canvas documents' scripts run on.
QByteArray createJs();
/// What ActionScript 1 and 2 translated to JavaScript runs on: libas3's
/// avm1.js, then the runtime's as2.js (phoenix-as2.js beside the page).
QByteArray actionScript2Runtime();

} // namespace Html5Publish
