#include "web_files.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>

#include <emscripten.h>

namespace web_files {

QString folder()
{
    const QString path = QDir::homePath() + "/Documents";
    QDir().mkpath(path);
    return path;
}

void open(QWidget* parent, const QString& nameFilter, std::function<void(const QString&)> done)
{
    QFileDialog::getOpenFileContent(nameFilter,
        [done](const QString& fileName, const QByteArray& content) {
            if (fileName.isEmpty())
                return;
            const QString path = QDir(folder()).filePath(QFileInfo(fileName).fileName());
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(content) != content.size())
                return;
            file.close();
            if (done)
                done(path);
        },
        parent);
}

QString keepDropped(const QString& path)
{
    const QString kept = QDir(folder()).filePath(QFileInfo(path).fileName());
    if (QFileInfo(kept).absoluteFilePath() == QFileInfo(path).absoluteFilePath())
        return kept;
    QFile::remove(kept);
    // Moved rather than copied: the page's memory holds it once.
    if (!QFile::rename(path, kept) && !QFile::copy(path, kept))
        return QString();
    return kept;
}

bool download(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QFileDialog::saveFileContent(file.readAll(), QFileInfo(path).fileName());
    return true;
}

bool handToHost(const QString& path)
{
    const QByteArray name = path.toUtf8();
    const int owned = EM_ASM_INT({
        const host = window.phoenixHost;
        return host && typeof host.owns === "function" && host.owns(UTF8ToString($0)) ? 1 : 0;
    }, name.constData());
    if (!owned)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    // A copy of the bytes: the page's memory can move, and the browser
    // sends no shared memory over the network.
    EM_ASM({ window.phoenixHost.save(UTF8ToString($0), HEAPU8.slice($1, $1 + $2)); }, name.constData(), bytes.constData(),
        bytes.size());
    return true;
}

} // namespace web_files
