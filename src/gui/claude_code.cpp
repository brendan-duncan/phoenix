#include "claude_code.h"
#include "modal.h"

#include "mcp_server.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>


namespace claude_code {

namespace {

const char* const kInstallPage = "https://docs.claude.com/en/docs/claude-code/setup";

/// The MCP configuration Claude Code is started with: this Phoenix's
/// executable as a relay to the session's port, the token in its
/// environment. Kept where only this user can read it.
QString writeConfig(int port, const QString& token)
{
    const QJsonObject phoenix{{"type", "stdio"}, {"command", QCoreApplication::applicationFilePath()},
        {"args", QJsonArray{"--mcp", "--port", QString::number(port)}},
        {"env", QJsonObject{{"PHOENIX_MCP_TOKEN", token}}}};
    const QJsonObject config{{"mcpServers", QJsonObject{{"phoenix", phoenix}}}};
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(folder);
    const QString path =
        QDir(folder).filePath(QString("claude-code-%1.json").arg(QCoreApplication::applicationPid()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(config).toJson());
    // Its token is no use once this Phoenix has gone.
    static bool removing = false;
    if (!removing)
    {
        removing = true;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, [path]() { QFile::remove(path); });
    }
    return path;
}

} // namespace

QString find()
{
    const QString onPath = QStandardPaths::findExecutable("claude");
    if (!onPath.isEmpty())
        return onPath;
    // Where its installers put it, for a Phoenix started without the
    // shell's PATH (from the Start menu, Finder or a desktop file).
    const QString home = QDir::homePath();
    QStringList places;
#ifdef _WIN32
    places << home + "/.local/bin/claude.exe" << qEnvironmentVariable("APPDATA") + "/npm/claude.cmd";
#else
    places << home + "/.local/bin/claude" << home + "/.claude/local/claude" << "/opt/homebrew/bin/claude"
           << "/usr/local/bin/claude";
#endif
    for (const QString& place : places)
    {
        if (QFileInfo(place).isExecutable())
            return QDir::cleanPath(place);
    }
    return QString();
}

bool prepare(McpServer& server, QWidget* parent, QString& claude, QString& config)
{
    claude = find();
    if (claude.isEmpty())
    {
        QMessageBox* box = new QMessageBox(QMessageBox::Information, "Open Claude Code",
            "Claude Code is not installed, or Phoenix cannot find it.<br><br>Install it, sign in once in a "
            "terminal, then choose <b>Open Claude Code</b> again.",
            QMessageBox::Close, parent);
        QPushButton* install = box->addButton("How to Install", QMessageBox::ActionRole);
        modal::open(box, [box, install](QMessageBox::StandardButton) {
            if (box->clickedButton() == install)
                QDesktopServices::openUrl(QUrl(kInstallPage));
        });
        return false;
    }
    QString token;
    const int port = server.openSession(token);
    config = port > 0 ? writeConfig(port, token) : QString();
    if (config.isEmpty())
    {
        modal::warning(parent, "Open Claude Code", "Phoenix could not get ready for Claude Code to connect.");
        return false;
    }
    return true;
}

QString startFolder(const QString& folder)
{
    return QFileInfo(folder).isDir() ? folder : QDir::homePath();
}

} // namespace claude_code
