#pragma once

#include <QString>

#include <functional>

class QObject;

namespace fla {
class FLADocument;
namespace play {
class MovieRuntime;
}
} // namespace fla

/// Gives a movie played with its scripts (Test Movie, the command line's
/// play) what Phoenix has for them: the document's ActionScript version and
/// class path (its folder, its publish settings' source paths, the extra
/// paths in Preferences, and Phoenix's own classes), images read with Qt,
/// the web, and a folder for SharedObjects. \a file is the document's path,
/// empty for one not saved; \a owner holds what loads from the web;
/// \a notice tells the person playing of what the movie sent.
void connectMovie(fla::play::MovieRuntime& movie, const fla::FLADocument& document, const QString& file, QObject* owner,
    std::function<void(const QString&)> notice);

/// The extra ActionScript 3 class paths Preferences gives.
QStringList actionScriptClassPaths();
void setActionScriptClassPaths(const QStringList& paths);
