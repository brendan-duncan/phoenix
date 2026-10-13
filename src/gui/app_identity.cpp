#include "app_identity.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace app_identity {

namespace {

const char* const kOrganization = "Phoenix";
const char* const kApplication = "Phoenix";
const char* const kOldApplication = "FLAViewer";

/// The settings of \a application, in the format settings are kept in now
/// (an INI file when PHOENIX_SETTINGS_DIR asks for one).
QSettings* settingsOf(const char* application)
{
    return new QSettings(QSettings::defaultFormat(), QSettings::UserScope, kOrganization, application);
}

/// Copies the folder the old name had at \a location to where the new name
/// keeps it, unless that is there already. A copy, not a move: an older
/// Phoenix still installed goes on using its own.
void copyFolder(QStandardPaths::StandardLocation location)
{
    const QString now = QStandardPaths::writableLocation(location);
    if (now.isEmpty() || QFileInfo::exists(now))
        return;
    // The location ends in the application's name.
    const QDir old(QFileInfo(now).dir().filePath(kOldApplication));
    if (!old.exists())
        return;
    QDirIterator files(old.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        const QString from = files.next();
        const QString to = QDir(now).filePath(old.relativeFilePath(from));
        QDir().mkpath(QFileInfo(to).path());
        QFile::copy(from, to);
    }
}

} // namespace

void apply()
{
    QCoreApplication::setOrganizationName(kOrganization);
    QCoreApplication::setApplicationName(kApplication);
}

void adoptOldSettings()
{
    std::unique_ptr<QSettings> settings(settingsOf(kApplication));
    if (settings->allKeys().isEmpty())
    {
        const std::unique_ptr<QSettings> old(settingsOf(kOldApplication));
        const QStringList keys = old->allKeys();
        for (const QString& key : keys)
            settings->setValue(key, old->value(key));
        settings->sync();
    }
    // Settings kept apart (PHOENIX_SETTINGS_DIR, for tests and demos) leave
    // the user's own folders alone.
    if (!qEnvironmentVariableIsEmpty("PHOENIX_SETTINGS_DIR"))
        return;
    copyFolder(QStandardPaths::AppDataLocation);
    copyFolder(QStandardPaths::AppLocalDataLocation);
}

} // namespace app_identity
