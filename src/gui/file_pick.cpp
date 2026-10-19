#include "file_pick.h"
#include "modal.h"
#include "web_files.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace file_pick {

void open(QWidget* parent, const QString& caption, const QString& directory, const QString& filter,
    std::function<void(const QString&)> done)
{
#ifdef PHOENIX_WEB
    Q_UNUSED(caption);
    Q_UNUSED(directory);
    web_files::open(parent, filter, std::move(done));
#else
    const QString path = QFileDialog::getOpenFileName(parent, caption, directory, filter);
    if (!path.isEmpty() && done)
        done(path);
#endif
}

void openMany(QWidget* parent, const QString& caption, const QString& directory, const QString& filter,
    std::function<void(const QStringList&)> done)
{
#ifdef PHOENIX_WEB
    Q_UNUSED(caption);
    Q_UNUSED(directory);
    web_files::open(parent, filter, [done](const QString& path) {
        if (done)
            done(QStringList{path});
    });
#else
    const QStringList paths = QFileDialog::getOpenFileNames(parent, caption, directory, filter);
    if (!paths.isEmpty() && done)
        done(paths);
#endif
}

void save(QWidget* parent, const QString& caption, const QString& suggested, const QString& filter,
    std::function<void(const QString&)> done, QString* selectedFilter)
{
#ifdef PHOENIX_WEB
    Q_UNUSED(parent);
    Q_UNUSED(caption);
    Q_UNUSED(filter);
    Q_UNUSED(selectedFilter);
    // No save dialog: the suggested name, written in the page's file system
    // and downloaded once written.
    QString name = QFileInfo(suggested).fileName();
    if (name.isEmpty())
        name = "untitled";
    const QString path = QDir(web_files::folder()).filePath(name);
    // Only what \a done writes is downloaded: a caller that just takes the
    // path, to write it later, downloads it itself then.
    const QDateTime before = QFileInfo(path).lastModified();
    if (done)
        done(path);
    const QFileInfo written(path);
    if (written.isFile() && written.lastModified() != before)
        web_files::download(path);
#else
    const QString path = QFileDialog::getSaveFileName(parent, caption, suggested, filter, selectedFilter);
    if (!path.isEmpty() && done)
        done(path);
#endif
}

void directory(QWidget* parent, const QString& caption, const QString& start, std::function<void(const QString&)> done)
{
#ifdef PHOENIX_WEB
    Q_UNUSED(start);
    Q_UNUSED(done);
    modal::information(parent, caption, "Phoenix in a browser cannot choose folders. Open and save single files instead.");
#else
    const QString path = QFileDialog::getExistingDirectory(parent, caption, start);
    if (!path.isEmpty() && done)
        done(path);
#endif
}

} // namespace file_pick
