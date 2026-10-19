#pragma once

// Asking for a file to read or write, answered through a callback.
//
// On the desktop these are QFileDialog's getOpenFileName, getSaveFileName
// and getExistingDirectory, waited for and answered before they return. In
// a browser (web_files.h) a file to open comes from the browser's picker,
// copied into the page's file system, and a file to save is written there
// under the name suggested and then downloaded; folders cannot be chosen.

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace file_pick {

/// A file to open; \a done is called with its path, and not at all when
/// nothing is chosen.
void open(QWidget* parent, const QString& caption, const QString& directory, const QString& filter,
    std::function<void(const QString& path)> done);

/// Files to open (one at a time in a browser).
void openMany(QWidget* parent, const QString& caption, const QString& directory, const QString& filter,
    std::function<void(const QStringList& paths)> done);

/// A file to write: \a done is called with its path and writes it there.
/// In a browser the file \a done wrote is then downloaded. \a selectedFilter,
/// when given, is the filter chosen, as QFileDialog sets it.
void save(QWidget* parent, const QString& caption, const QString& suggested, const QString& filter,
    std::function<void(const QString& path)> done, QString* selectedFilter = nullptr);

/// A folder; in a browser there is none to choose, so \a done is never called
/// and the person is told.
void directory(QWidget* parent, const QString& caption, const QString& start,
    std::function<void(const QString& path)> done);

} // namespace file_pick
