#include "autosave.h"

#include "../phx/phx_document.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#endif

namespace {

/// Whether the process \a pid is still running.
bool running(qint64 pid)
{
    if (pid <= 0)
        return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

} // namespace

AutoSave::AutoSave(Links links, QObject* parent)
    : QObject(parent)
    , _links(std::move(links))
    , _id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    // A look every half minute; a copy when one is due and something changed.
    _timer = new QTimer(this);
    _timer->setInterval(30 * 1000);
    connect(_timer, &QTimer::timeout, this, [this]() {
        const int minutes = interval();
        if (minutes <= 0)
            return;
        if (_lastSave.isValid() && _lastSave.secsTo(QDateTime::currentDateTime()) < minutes * 60)
            return;
        saveNow();
    });
    _timer->start();
}

AutoSave::~AutoSave()
{
    // A clean exit leaves nothing to recover.
    discard();
}

int AutoSave::interval()
{
    return QSettings().value("autosaveMinutes", 2).toInt();
}

void AutoSave::setInterval(int minutes)
{
    QSettings().setValue("autosaveMinutes", minutes);
}

QString AutoSave::folder()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("recovery");
}

void AutoSave::saveNow()
{
    const fla::Document* document = _links.document ? _links.document() : nullptr;
    if (!document || !_links.modified || !_links.modified())
        return;
    if (_changes == _savedChanges && _lastSave.isValid())
        return;
    if (_links.busy && _links.busy())
    {
        // Not mid-gesture: a little later.
        QTimer::singleShot(5000, this, [this]() { saveNow(); });
        return;
    }
    QDir().mkpath(folder());
    const QString file = QDir(folder()).filePath(_id + ".phx");
    phx::SaveOptions options;
    options.application = QString("Phoenix %1 (recovery)").arg(QCoreApplication::applicationVersion()).toStdString();
    std::string error;
    if (!phx::save(*document, file.toStdString(), options, error))
        return;

    QJsonObject note;
    note["path"] = _links.path ? _links.path() : QString();
    note["name"] = _links.name ? _links.name() : QString();
    note["pid"] = QCoreApplication::applicationPid();
    note["saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile noteFile(QDir(folder()).filePath(_id + ".json"));
    if (noteFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        noteFile.write(QJsonDocument(note).toJson());
    _savedChanges = _changes;
    _lastSave = QDateTime::currentDateTime();
}

void AutoSave::discard()
{
    QFile::remove(QDir(folder()).filePath(_id + ".phx"));
    QFile::remove(QDir(folder()).filePath(_id + ".json"));
    _lastSave = QDateTime();
    _savedChanges = _changes;
}

QList<AutoSave::Orphan> AutoSave::orphans()
{
    QList<Orphan> found;
    const QDir dir(folder());
    for (const QFileInfo& noteInfo : dir.entryInfoList({"*.json"}, QDir::Files, QDir::Time))
    {
        QFile noteFile(noteInfo.absoluteFilePath());
        if (!noteFile.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject note = QJsonDocument::fromJson(noteFile.readAll()).object();
        const QString phx = dir.filePath(noteInfo.completeBaseName() + ".phx");
        if (!QFile::exists(phx) || running(note["pid"].toInteger()))
            continue;
        Orphan orphan;
        orphan.file = phx;
        orphan.note = noteInfo.absoluteFilePath();
        orphan.originalPath = note["path"].toString();
        orphan.name = note["name"].toString();
        orphan.saved = QDateTime::fromString(note["saved"].toString(), Qt::ISODate);
        found.push_back(orphan);
    }
    return found;
}

void AutoSave::remove(const Orphan& orphan)
{
    QFile::remove(orphan.file);
    QFile::remove(orphan.note);
}
