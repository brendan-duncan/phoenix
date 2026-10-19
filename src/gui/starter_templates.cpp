#include "starter_templates.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace startertemplates {

namespace {

const char* const kRoot = ":/templates";

/// \a from's files and folders copied into \a to, which is made; writable,
/// as files out of the app are read-only.
bool copyTree(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString source = it.next();
        const QString relative = QDir(from).relativeFilePath(source);
        const QString target = QDir(to).filePath(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(source, target))
            return false;
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner);
    }
    return true;
}

} // namespace

QList<StarterTemplate> all()
{
    QList<std::pair<int, StarterTemplate>> found;
    for (const QFileInfo& folder : QDir(kRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        QFile file(QDir(folder.absoluteFilePath()).filePath("template.json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        if (!QFileInfo::exists(QDir(folder.absoluteFilePath()).filePath("DOMDocument.xml")))
            continue;
        StarterTemplate entry;
        entry.id = folder.fileName();
        entry.title = json["title"].toString(entry.id);
        entry.summary = json["summary"].toString();
        entry.folder = folder.absoluteFilePath();
        entry.thumbnail = QImage(QDir(folder.absoluteFilePath()).filePath("thumbnail.png"));
        found.append({json["order"].toInt(1000), entry});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QList<StarterTemplate> templates;
    for (const auto& [order, entry] : found)
        templates << entry;
    return templates;
}

QString copyToWork(const QString& id, QString& error)
{
    const QString source = QDir(kRoot).filePath(id);
    if (id.isEmpty() || !QFileInfo::exists(QDir(source).filePath("DOMDocument.xml")))
    {
        error = QString("There is no starter template called %1.").arg(id);
        return QString();
    }
    // Under the temporary folder, as lessons' documents are; old copies go.
    const QString root =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("phoenix-templates");
    const QDateTime old = QDateTime::currentDateTime().addDays(-2);
    for (const QFileInfo& stale : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (stale.lastModified() < old)
            QDir(stale.absoluteFilePath()).removeRecursively();
    const QString copy = QDir(root).filePath(QString("%1-%2").arg(id).arg(QDateTime::currentMSecsSinceEpoch()));
    if (!copyTree(source, copy))
    {
        error = QString("The starter template %1 could not be copied.").arg(id);
        return QString();
    }
    // Only the document goes with it, not the template's own notes.
    QFile::remove(QDir(copy).filePath("template.json"));
    QFile::remove(QDir(copy).filePath("thumbnail.png"));
    return QDir(copy).filePath("DOMDocument.xml");
}

} // namespace startertemplates
