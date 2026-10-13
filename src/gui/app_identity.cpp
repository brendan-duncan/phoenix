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

/// Copies what the folder the old name had at \a location holds to where
/// the new name keeps it, file by file, leaving any file already there. A
/// copy, not a move: an older Phoenix still installed goes on using its own.
void copyFolder(QStandardPaths::StandardLocation location)
{
    const QString now = QStandardPaths::writableLocation(location);
    if (now.isEmpty())
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
        if (QFileInfo::exists(to))
            continue;
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
    // Once only: a recovery copy discarded under the new name must not come
    // back from the old folder on the next start.
    if (settings->value("identity/foldersAdopted", false).toBool())
        return;
    copyFolder(QStandardPaths::AppDataLocation);
    copyFolder(QStandardPaths::AppLocalDataLocation);
    settings->setValue("identity/foldersAdopted", true);
    settings->sync();
}

} // namespace app_identity
