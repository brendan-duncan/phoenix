#include "claude_code.h"

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
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <utility>

namespace claude_code {

namespace {

const char* const kInstallPage = "https://docs.claude.com/en/docs/claude-code/setup";

#ifndef _WIN32
/// \a text quoted for a POSIX shell.
QString shellQuoted(const QString& text)
{
    QString quoted = text;
    quoted.replace("'", "'\\''");
    return "'" + quoted + "'";
}
#endif

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

/// Starts \a claude with \a config in a new terminal window in \a folder.
bool startInTerminal(const QString& claude, const QString& config, const QString& folder)
{
#ifdef _WIN32
    // A console of its own, which Windows opens in the terminal chosen as
    // the default (Windows Terminal on Windows 11).
    QProcess process;
    process.setWorkingDirectory(folder);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NEW_CONSOLE;
        arguments->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
    });
    if (claude.endsWith(".exe", Qt::CaseInsensitive))
    {
        process.setProgram(claude);
        process.setArguments({"--mcp-config", config});
    }
    else
    {
        // An npm install's claude.cmd runs through cmd; the outer quotes are
        // the ones cmd takes off.
        process.setProgram(qEnvironmentVariable("COMSPEC", "cmd.exe"));
        process.setNativeArguments(QString("/k \"\"%1\" --mcp-config \"%2\"\"")
                                       .arg(QDir::toNativeSeparators(claude), QDir::toNativeSeparators(config)));
    }
    return process.startDetached();
#elif defined(__APPLE__)
    const QString command =
        QString("cd %1 && %2 --mcp-config %3").arg(shellQuoted(folder), shellQuoted(claude), shellQuoted(config));
    QString script = command;
    script.replace("\\", "\\\\").replace("\"", "\\\"");
    return QProcess::startDetached("osascript", {"-e", QString("tell application \"Terminal\" to do script \"%1\"").arg(script),
                                                    "-e", "tell application \"Terminal\" to activate"});
#else
    const QString command = QString("cd %1 && exec %2 --mcp-config %3")
                                .arg(shellQuoted(folder), shellQuoted(claude), shellQuoted(config));
    const std::pair<const char*, QStringList> terminals[] = {
        {"x-terminal-emulator", {"-e", "sh", "-c", command}},
        {"gnome-terminal", {"--", "sh", "-c", command}},
        {"konsole", {"-e", "sh", "-c", command}},
        {"xfce4-terminal", {"-x", "sh", "-c", command}},
        {"xterm", {"-e", "sh", "-c", command}},
    };
    for (const auto& [name, arguments] : terminals)
    {
        if (!QStandardPaths::findExecutable(name).isEmpty() && QProcess::startDetached(name, arguments, folder))
            return true;
    }
    return false;
#endif
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
        QMessageBox box(QMessageBox::Information, "Open Claude Code",
            "Claude Code is not installed, or Phoenix cannot find it.<br><br>Install it, sign in once in a "
            "terminal, then choose <b>Open Claude Code</b> again.",
            QMessageBox::Close, parent);
        QPushButton* install = box.addButton("How to Install", QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == install)
            QDesktopServices::openUrl(QUrl(kInstallPage));
        return false;
    }
    QString token;
    const int port = server.openSession(token);
    config = port > 0 ? writeConfig(port, token) : QString();
    if (config.isEmpty())
    {
        QMessageBox::warning(parent, "Open Claude Code", "Phoenix could not get ready for Claude Code to connect.");
        return false;
    }
    return true;
}

QString startFolder(const QString& folder)
{
    return QFileInfo(folder).isDir() ? folder : QDir::homePath();
}

void open(McpServer& server, const QString& folder, QWidget* parent)
{
    QString claude, config;
    if (!prepare(server, parent, claude, config))
        return;
    if (!startInTerminal(claude, config, startFolder(folder)))
        QMessageBox::warning(parent, "Open Claude Code",
            QString("No terminal window could be opened. To connect Claude Code yourself, run:\n\n%1 --mcp-config %2")
                .arg(QDir::toNativeSeparators(claude), QDir::toNativeSeparators(config)));
}

} // namespace claude_code
