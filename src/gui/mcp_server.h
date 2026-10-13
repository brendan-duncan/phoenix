#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class MainWindow;
class ScriptApi;
class QLabel;
class QToolButton;

/// Lets AI assistants (Claude Desktop, Claude Code and other MCP clients)
/// read and change the open document through Phoenix's own operations
/// (script_api.h), by the Model Context Protocol: JSON-RPC messages, one to
/// a line.
///
/// Two ways in:
/// - Standard input and output, when the client starts Phoenix itself as
///   `Phoenix --mcp`. If a Phoenix already running lets clients connect,
///   that process only relays (see relayToRunning), so the client works
///   with the window already open.
/// - A localhost port in a running Phoenix, off until switched on in
///   Preferences. Only connections from this computer are taken.
///
/// Each client is asked about before it may work, the status bar shows
/// while one is connected (with a button that stops it), every call is
/// logged, and nothing is written outside the document without asking.
class McpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpServer(MainWindow* window);
    ~McpServer() override;

    /// Serves the client that started Phoenix, on standard input and output.
    void serveStdio();
    /// Starts or stops taking connections on the port, as Preferences says.
    void applySettings();

    /// The port, and whether a running Phoenix takes connections on it.
    static int port();
    static bool enabled();
    static void setEnabled(bool on, int port);

    /// For `Phoenix --mcp` before any window: when a running Phoenix takes
    /// connections, relays standard input and output to it until the client
    /// goes, and returns true; false when there is none to relay to.
    static bool relayToRunning();

    /// Handles one JSON-RPC message from a client; empty for a notification.
    /// Runs on the GUI thread.
    QJsonObject handle(const QJsonObject& message, int client);

private:
    struct Client
    {
        int id = 0;
        QString name;
        bool allowed = false;
    };

    void listen();
    void stopListening();
    void serveSocket(std::intptr_t socket, int client);
    /// Handles \a line from a reader thread, on the GUI thread; returns the
    /// reply line, or an empty one.
    std::string dispatch(const std::string& line, int client);
    bool allow(Client& client);
    void showActivity(const QString& what);
    void stopAll();
    void log(const QString& client, const QString& tool, const QJsonObject& arguments, bool ok);
    QJsonObject callTool(const QJsonObject& params, Client& client);

    MainWindow* _window;
    std::unique_ptr<ScriptApi> _api;
    std::vector<Client> _clients;
    int _nextClient = 1;
    QSet<QString> _allowedNames;
    QSet<QString> _allowedFolders;

    std::atomic<bool> _stopping{false};
    std::intptr_t _listener = -1;
    std::thread _accepting;
    std::mutex _socketsMutex;
    std::vector<std::intptr_t> _sockets;
    std::vector<std::thread> _readers;

    bool _stdio = false;
    std::thread _stdioReader;
    int _stdout = -1;
    std::mutex _stdoutMutex;

    QLabel* _status = nullptr;
    QToolButton* _stop = nullptr;
};
