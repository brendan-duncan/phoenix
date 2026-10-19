#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdint>

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
    /// goes, and returns true; false when there is none to relay to. With
    /// \a sessionPort, to the Phoenix that opened that port for a session
    /// (openSession), sending the session's token from PHOENIX_MCP_TOKEN
    /// first so the client it started is not asked about.
    static bool relayToRunning(int sessionPort = 0);

    /// Takes connections for a client this Phoenix starts itself (Claude
    /// Code): on the Preferences port when that is on, otherwise on a free
    /// one for as long as Phoenix runs. Gives the port, 0 when none could
    /// be opened, and the token that client sends to be let in unasked.
    int openSession(QString& token);

    /// Handles one JSON-RPC message from a client and gives \a reply its
    /// answer, empty for a notification: at once, or after the person is
    /// asked whether the client may work. Runs on the GUI thread.
    void handle(const QJsonObject& message, int client, std::function<void(const QJsonObject&)> reply);

private:
    struct Client
    {
        int id = 0;
        QString name;
        bool allowed = false;
    };

    /// What the reader threads share with the GUI thread, kept alive by
    /// them: once the server is closing, a reader touches it no more.
    struct Shared
    {
        std::mutex mutex;
        /// A reply came, or the readers are to stop waiting for one.
        std::condition_variable answered;
        /// The port's connections are being cut (Preferences, or closing).
        std::atomic<bool> stopping{false};
        /// The server is going: nothing may reach it any more.
        std::atomic<bool> closing{false};
        std::vector<std::intptr_t> sockets;
        int stdoutFd = -1;
        std::mutex stdoutMutex;
    };
    /// A message handed to the GUI thread, and its reply.
    struct Pending
    {
        bool done = false;
        bool abandoned = false;
        QJsonObject reply;
    };

    /// Takes connections on \a port, or on a free one for 0; false when it
    /// cannot.
    bool listen(int port);
    void stopListening();
    void setStopping(bool stopping);
    static void serveSocket(const std::shared_ptr<Shared>& shared, McpServer* server, std::intptr_t socket, int client);
    /// Hands \a line from a reader thread to the GUI thread and waits for
    /// the reply, without holding that thread up; returns the reply line,
    /// or an empty one (also when the reader is to stop).
    static std::string dispatch(const std::shared_ptr<Shared>& shared, McpServer* server, const std::string& line,
        int client, bool stdio);
    /// The client with \a id, or null: looked up again after anything that
    /// runs the event loop, as the clients may change meanwhile.
    Client* clientOf(int id);
    /// Calls \a done with whether \a client may work, asking the person the
    /// first time.
    void allow(int client, std::function<void(bool allowed)> done);
    /// The answer to a request from a client already let in.
    QJsonObject respond(const QJsonValue& requestId, const QString& method, const QJsonObject& params, int client);
    void showActivity(const QString& what);
    void stopAll();
    void log(const QString& client, const QString& tool, const QJsonObject& arguments, bool ok);
    QJsonObject callTool(const QJsonObject& params, int client);

    MainWindow* _window;
    std::unique_ptr<ScriptApi> _api;
    std::vector<Client> _clients;
    std::atomic<int> _nextClient{1};
    QSet<QString> _allowedNames;
    QSet<QString> _allowedFolders;

    std::shared_ptr<Shared> _shared = std::make_shared<Shared>();
    std::intptr_t _listener = -1;
    int _listenPort = 0;
    /// The token of the session openSession made; empty when there is none
    /// or Stop ended it.
    QString _sessionToken;
    std::thread _accepting;
    /// The socket readers, guarded by the shared mutex.
    std::vector<std::thread> _readers;

    bool _stdio = false;
    std::thread _stdioReader;

    QLabel* _status = nullptr;
    QToolButton* _stop = nullptr;
};
