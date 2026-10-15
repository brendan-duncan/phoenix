#include "behavior_library.h"

#include "script_runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

BehaviorLibrary& BehaviorLibrary::instance()
{
    static BehaviorLibrary* library = new BehaviorLibrary();
    return *library;
}

BehaviorLibrary::BehaviorLibrary()
    : _engine(std::make_unique<fla::behaviors::Engine>())
{
    _engine->addBuiltIns();
    reload();
}

QString BehaviorLibrary::folder()
{
    const QString path = QDir(ScriptRunner::configFolder()).filePath("Behaviors");
    QDir().mkpath(path);
    return path;
}

void BehaviorLibrary::reload()
{
    auto engine = std::make_unique<fla::behaviors::Engine>();
    engine->addBuiltIns();
    const QString path = folder();
    const QFileInfoList files = QDir(path).entryInfoList({"*.js"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    QStringList watched{path};
    for (const QFileInfo& file : files)
    {
        watched << file.absoluteFilePath();
        QFile in(file.absoluteFilePath());
        if (!in.open(QIODevice::ReadOnly))
            continue;
        std::string error;
        if (!engine->add(file.completeBaseName().toStdString(), in.readAll().toStdString(),
                file.absoluteFilePath().toStdString(), false, error))
        {
            if (report)
                report(QString("Behavior %1: %2").arg(file.fileName(), QString::fromStdString(error)));
        }
    }
    _engine = std::move(engine);
    _told.clear();

    // Read again when a file there is added, changed or removed; a moment
    // later, as editors write a file in more than one step.
    if (!_watcher)
    {
        _watcher = new QFileSystemWatcher(this);
        const auto later = [this]() {
            QTimer::singleShot(300, this, [this]() {
                reload();
                emit changed();
            });
        };
        connect(_watcher, &QFileSystemWatcher::directoryChanged, this, later);
        connect(_watcher, &QFileSystemWatcher::fileChanged, this, later);
    }
    if (!_watcher->files().isEmpty())
        _watcher->removePaths(_watcher->files());
    if (!_watcher->directories().isEmpty())
        _watcher->removePaths(_watcher->directories());
    _watcher->addPaths(watched);
}

void BehaviorLibrary::reportOnce(const QString& message)
{
    if (message.isEmpty() || _told.contains(message))
        return;
    _told.insert(message);
    if (report)
        report(message);
}
