#pragma once

// Files in the web build. The page's file system lives in memory and is
// gone on reload, and the browser hands over files only through its own
// picker and takes them back only as downloads. A file opened is copied
// into the page's file system from the picker, and Phoenix reads it from
// there as it would from a disk; a file saved is written there and then
// offered as a download.

#include <QString>

#include <functional>

class QWidget;

namespace web_files {

/// The folder files opened and saved are kept in while the page is open.
QString folder();

/// Asks for a file with the browser's picker (\a nameFilter as for
/// QFileDialog), copies it into folder(), and calls \a done with its path
/// there. Nothing is called when the picker is canceled.
void open(QWidget* parent, const QString& nameFilter, std::function<void(const QString& path)> done);

/// A file dropped on the page, which the browser hands over for the drop
/// only, moved into folder() under its name (over one there already). Its
/// path there; empty when it cannot be.
QString keepDropped(const QString& path);

/// Offers the file at \a path to the browser as a download named after it.
/// False when it cannot be read.
bool download(const QString& path);

/// A page Phoenix runs inside may keep documents itself, as a document
/// server does: it sets window.phoenixHost, whose owns(path) says which
/// files are its and whose save(path, bytes) takes them. Hands the saved
/// file at \a path to it; false when the page does not keep that file.
bool handToHost(const QString& path);

} // namespace web_files
