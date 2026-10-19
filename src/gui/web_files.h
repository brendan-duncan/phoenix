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

/// Offers the file at \a path to the browser as a download named after it.
/// False when it cannot be read.
bool download(const QString& path);

} // namespace web_files
