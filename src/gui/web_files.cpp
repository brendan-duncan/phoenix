#include "web_files.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>

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

bool download(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QFileDialog::saveFileContent(file.readAll(), QFileInfo(path).fileName());
    return true;
}

} // namespace web_files
