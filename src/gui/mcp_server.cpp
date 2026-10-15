#include "mcp_server.h"

#include "main_window.h"
#include "script_api.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUuid>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fcntl.h>
#include <io.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstdint>

namespace {

const int kDefaultPort = 47290;
const char* const kProtocolVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

// Local sockets, plain: Qt's network module is not part of the Qt Phoenix
// ships with.
#ifdef _WIN32
using Socket = SOCKET;
const Socket kNoSocket = INVALID_SOCKET;
void closeSocket(Socket socket)
{
    closesocket(socket);
}
bool socketsReady()
{
    static const bool ready = []() {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
#else
using Socket = int;
const Socket kNoSocket = -1;
void closeSocket(Socket socket)
{
    close(socket);
}
bool socketsReady()
{
    return true;
}
#endif

void shutdownSocket(Socket socket)
{
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

sockaddr_in loopback(int port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

bool sendAll(Socket socket, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        const int n = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

/// Reads up to a newline from \a socket, keeping what comes after it in
/// \a pending. False when the connection is gone.
bool readLine(Socket socket, std::string& pending, std::string& line)
{
    for (;;)
    {
        const size_t newline = pending.find('\n');
        if (newline != std::string::npos)
        {
            line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        // A runaway client is cut off rather than growing without end.
        if (pending.size() > 64 * 1024 * 1024)
            return false;
        char buffer[16384];
        const int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n <= 0)
            return false;
        pending.append(buffer, static_cast<size_t>(n));
    }
}

void writeFd(int fd, const std::string& data)
{
#ifdef _WIN32
    _write(fd, data.data(), static_cast<unsigned int>(data.size()));
#else
    size_t written = 0;
    while (written < data.size())
    {
        const ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n <= 0)
            return;
        written += static_cast<size_t>(n);
    }
#endif
}

QJsonObject errorReply(const QJsonValue& id, int code, const QString& message)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject resultReply(const QJsonValue& id, const QJsonObject& result)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

} // namespace

McpServer::McpServer(MainWindow* window)
    : QObject(window)
    , _window(window)
    , _api(std::make_unique<ScriptApi>(*window))
{
    // Writing anywhere outside the document is asked about, a folder at a time.
    _api->approveWrite = [this](const QString& path) {
        const QString folder = QFileInfo(path).absolutePath();
        if (_allowedFolders.contains(folder) || QGuiApplication::platformName() == "offscreen")
            return true;
        const auto answer = QMessageBox::question(_window, "AI Assistant",
            QString("An AI assistant wants to write a file:\n\n%1\n\nAllow it to write files in this folder until "
                    "Phoenix closes?")
                .arg(QDir::toNativeSeparators(path)));
        if (answer != QMessageBox::Yes)
            return false;
        _allowedFolders.insert(folder);
        return true;
    };

    _status = new QLabel(window);
    _status->hide();
    _stop = new QToolButton(window);
    _stop->setText("Stop");
    _stop->setToolTip("Disconnect the AI assistant; it has to be allowed again to carry on");
    _stop->setAutoRaise(true);
    _stop->hide();
    connect(_stop, &QToolButton::clicked, this, &McpServer::stopAll);
    window->statusBar()->addPermanentWidget(_status);
    window->statusBar()->addPermanentWidget(_stop);
}

McpServer::~McpServer()
{
    _stopping = true;
    stopListening();
    // A reader blocked on standard input cannot be woken; the process is
    // ending, so it is let go.
    if (_stdioReader.joinable())
        _stdioReader.detach();
}

int McpServer::port()
{
    return std::clamp(QSettings().value("mcp/port", kDefaultPort).toInt(), 1024, 65535);
}

bool McpServer::enabled()
{
    return QSettings().value("mcp/enabled", false).toBool();
}

void McpServer::setEnabled(bool on, int port)
{
    QSettings settings;
    settings.setValue("mcp/enabled", on);
    settings.setValue("mcp/port", std::clamp(port, 1024, 65535));
}

void McpServer::applySettings()
{
    stopListening();
    _stopping = false;
    if (enabled())
        listen(port());
    else if (!_sessionToken.isEmpty())
        listen(0);
}

int McpServer::openSession(QString& token)
{
    if (_sessionToken.isEmpty())
        _sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (_listener == -1)
    {
        _stopping = false;
        if (!(enabled() && listen(port())) && !listen(0))
            return 0;
    }
    token = _sessionToken;
    return _listenPort;
}

bool McpServer::listen(int port)
{
    if (!socketsReady())
        return false;
    Socket listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kNoSocket)
        return false;
#ifdef _WIN32
    // No other program may take the port from under it.
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    // Bound to this computer only: nothing from the network reaches it.
    const sockaddr_in address = loopback(port);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 4) != 0)
    {
        closeSocket(listener);
        if (port != 0)
            _window->statusBar()->showMessage(
                QString("AI assistants cannot connect: port %1 is in use. Choose another in Preferences.").arg(port),
                8000);
        return false;
    }
    // The port asked for, or the free one the system chose.
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length);
    _listenPort = ntohs(bound.sin_port);
    _listener = static_cast<std::intptr_t>(listener);
    _accepting = std::thread([this, listener]() {
        while (!_stopping)
        {
            const Socket client = accept(listener, nullptr, nullptr);
            if (client == kNoSocket)
                break;
            std::lock_guard<std::mutex> lock(_socketsMutex);
            if (_stopping)
            {
                closeSocket(client);
                break;
            }
            _sockets.push_back(static_cast<std::intptr_t>(client));
            const int id = _nextClient++;
            _readers.emplace_back([this, client, id]() { serveSocket(static_cast<std::intptr_t>(client), id); });
        }
    });
    return true;
}

void McpServer::stopListening()
{
    _stopping = true;
    if (_listener != -1)
    {
        shutdownSocket(static_cast<Socket>(_listener));
        closeSocket(static_cast<Socket>(_listener));
        _listener = -1;
        _listenPort = 0;
    }
    if (_accepting.joinable())
        _accepting.join();
    std::vector<std::thread> readers;
    {
        std::lock_guard<std::mutex> lock(_socketsMutex);
        for (std::intptr_t socket : _sockets)
            shutdownSocket(static_cast<Socket>(socket));
        readers.swap(_readers);
    }
    // Readers waiting on the GUI thread would never finish while it waits
    // here, so they are let go: each one ends as soon as it sees the stop.
    for (std::thread& reader : readers)
    {
        if (reader.joinable())
            reader.detach();
    }
}

void McpServer::serveSocket(std::intptr_t handle, int client)
{
    const Socket socket = static_cast<Socket>(handle);
    std::string pending;
    std::string line;
    while (!_stopping && readLine(socket, pending, line))
    {
        if (line.empty())
            continue;
        const std::string reply = dispatch(line, client);
        if (!reply.empty() && !sendAll(socket, reply + "\n"))
            break;
    }
    {
        std::lock_guard<std::mutex> lock(_socketsMutex);
        _sockets.erase(std::remove(_sockets.begin(), _sockets.end(), handle), _sockets.end());
    }
    closeSocket(socket);
    if (!_stopping)
    {
        QMetaObject::invokeMethod(this, [this, client]() {
            _clients.erase(std::remove_if(_clients.begin(), _clients.end(),
                               [client](const Client& c) { return c.id == client; }),
                _clients.end());
            showActivity(QString());
        });
    }
}

void McpServer::serveStdio()
{
    if (_stdio)
        return;
    _stdio = true;
    // Replies go to the real standard output; anything else Phoenix prints
    // goes to standard error, so it cannot break a reply.
    std::fflush(stdout);
#ifdef _WIN32
    _stdout = _dup(_fileno(stdout));
    _dup2(_fileno(stderr), _fileno(stdout));
    _setmode(_stdout, _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#else
    _stdout = dup(fileno(stdout));
    dup2(fileno(stderr), fileno(stdout));
#endif
    const int id = _nextClient++;
    _stdioReader = std::thread([this, id]() {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            const std::string reply = dispatch(line, id);
            if (!reply.empty() && _stdout >= 0)
            {
                std::lock_guard<std::mutex> lock(_stdoutMutex);
                writeFd(_stdout, reply + "\n");
            }
        }
        // The client went: the window stays, with whatever was made.
        if (!_stopping)
            QMetaObject::invokeMethod(this, [this]() {
                _clients.clear();
                showActivity(QString());
            });
    });
}

std::string McpServer::dispatch(const std::string& line, int client)
{
    if (_stopping)
        return std::string();
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray::fromStdString(line), &error);
    QJsonObject reply;
    if (json.isNull() || !json.isObject())
        reply = errorReply(QJsonValue::Null, -32700, "That message is not a JSON object.");
    else
    {
        const QJsonObject message = json.object();
        QMetaObject::invokeMethod(this, [this, &reply, &message, client]() { reply = handle(message, client); },
            Qt::BlockingQueuedConnection);
    }
    if (reply.isEmpty())
        return std::string();
    return QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString();
}

bool McpServer::allow(Client& client)
{
    if (client.allowed)
        return true;
    // Without a screen (QT_QPA_PLATFORM=offscreen, for automation) there is
    // no one to ask: whoever ran it that way chose to.
    if (QGuiApplication::platformName() == "offscreen")
    {
        client.allowed = true;
        return true;
    }
    if (_allowedNames.contains(client.name))
    {
        client.allowed = true;
        return true;
    }
    QMessageBox box(QMessageBox::Question, "AI Assistant",
        QString("<b>%1</b> wants to work with Phoenix: read the documents you have open and change them.<br><br>"
                "Its changes go into the undo history, and it asks before writing any file. The status bar shows "
                "while it is connected, with a button that stops it.<br><br>Allow it?")
            .arg(client.name.toHtmlEscaped()),
        QMessageBox::NoButton, _window);
    QPushButton* yes = box.addButton("Allow", QMessageBox::AcceptRole);
    box.addButton("Don't Allow", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != yes)
        return false;
    client.allowed = true;
    _allowedNames.insert(client.name);
    return true;
}

QJsonObject McpServer::handle(const QJsonObject& message, int id)
{
    const QJsonValue requestId = message.value("id");
    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();
    const bool notification = !message.contains("id");

    auto found = std::find_if(_clients.begin(), _clients.end(), [id](const Client& c) { return c.id == id; });
    if (found == _clients.end())
    {
        _clients.push_back({id, "An AI assistant", false});
        found = std::prev(_clients.end());
    }
    Client& client = *found;

    // The client this Phoenix started, through the relay that names its
    // session: let in without asking.
    if (message.contains("phoenixSession"))
    {
        if (!_sessionToken.isEmpty() && message.value("phoenixSession").toString() == _sessionToken)
            client.allowed = true;
        return QJsonObject();
    }
    if (notification)
        return QJsonObject();
    if (method == "ping")
        return resultReply(requestId, QJsonObject());
    if (method == "initialize")
    {
        const QString name = params.value("clientInfo").toObject().value("name").toString();
        if (!name.isEmpty())
            client.name = name;
        if (!allow(client))
        {
            log(client.name, "connect", {}, false);
            return errorReply(requestId, -32001, "The person using Phoenix did not allow this connection.");
        }
        log(client.name, "connect", {}, true);
        showActivity("connected");
        QString version = params.value("protocolVersion").toString();
        if (std::find(std::begin(kProtocolVersions), std::end(kProtocolVersions), version.toStdString()) ==
            std::end(kProtocolVersions))
            version = kProtocolVersions[0];
        return resultReply(requestId,
            QJsonObject{{"protocolVersion", version},
                {"capabilities", QJsonObject{{"tools", QJsonObject{}}, {"resources", QJsonObject{}}}},
                {"serverInfo", QJsonObject{{"name", "phoenix"}, {"title", "Phoenix"},
                                   {"version", QCoreApplication::applicationVersion()}}},
                {"instructions",
                    "Phoenix is a vector animation software. These tools read and change the document open in it. Frames "
                    "count from 1; layers are named or counted from 0 at the top. Look with get_document, "
                    "get_timeline, get_elements and render_frame before changing things, and wrap a request that "
                    "takes several changes in begin_edit and end_edit so it undoes in one step."}});
    }
    if (!client.allowed && !allow(client))
        return errorReply(requestId, -32001, "The person using Phoenix did not allow this connection.");

    if (method == "tools/list")
    {
        QJsonArray tools;
        for (const ScriptApi::Operation& operation : _api->operations())
        {
            tools.append(QJsonObject{{"name", operation.name}, {"title", operation.title},
                {"description", operation.description}, {"inputSchema", operation.schema},
                {"annotations", QJsonObject{{"title", operation.title}, {"readOnlyHint", !operation.writes},
                                    {"destructiveHint", operation.name.startsWith("delete") ||
                                                            operation.name == "tidy_library"},
                                    {"openWorldHint", false}}}});
        }
        return resultReply(requestId, QJsonObject{{"tools", tools}});
    }
    if (method == "tools/call")
        return resultReply(requestId, callTool(params, client));
    if (method == "resources/list")
        return resultReply(requestId, QJsonObject{{"resources", _api->resources()}});
    if (method == "resources/read")
    {
        const QString uri = params.value("uri").toString();
        const ScriptApi::Result result = _api->readResource(uri);
        if (!result.ok())
            return errorReply(requestId, -32002, result.error);
        return resultReply(requestId,
            QJsonObject{{"contents", QJsonArray{QJsonObject{{"uri", uri}, {"mimeType", "application/json"},
                                         {"text", QString::fromUtf8(QJsonDocument(result.value.toObject())
                                                                        .toJson(QJsonDocument::Compact))}}}}});
    }
    return errorReply(requestId, -32601, QString("Phoenix does not answer %1.").arg(method));
}

QJsonObject McpServer::callTool(const QJsonObject& params, Client& client)
{
    const QString name = params.value("name").toString();
    const QJsonObject arguments = params.value("arguments").toObject();
    QString title = name;
    for (const ScriptApi::Operation& operation : _api->operations())
    {
        if (operation.name == name)
            title = operation.title;
    }
    showActivity(title);
    const ScriptApi::Result result = _api->call(name, arguments);
    log(client.name, name, arguments, result.ok());
    if (!result.ok())
        return QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", result.error}}}},
            {"isError", true}};
    QJsonArray content;
    const QByteArray text = result.value.isObject() ? QJsonDocument(result.value.toObject()).toJson(QJsonDocument::Compact)
        : result.value.isArray()                    ? QJsonDocument(result.value.toArray()).toJson(QJsonDocument::Compact)
                                                    : QByteArray("{}");
    content.append(QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(text)}});
    if (!result.image.isNull())
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        result.image.save(&buffer, "PNG");
        content.append(QJsonObject{{"type", "image"}, {"data", QString::fromLatin1(png.toBase64())},
            {"mimeType", "image/png"}});
    }
    return QJsonObject{{"content", content}, {"isError", false}};
}

void McpServer::showActivity(const QString& what)
{
    QStringList names;
    for (const Client& client : _clients)
    {
        if (client.allowed)
            names << client.name;
    }
    if (names.isEmpty())
    {
        _status->hide();
        _stop->hide();
        return;
    }
    _status->setText(QString("AI: %1%2").arg(names.join(", "), what.isEmpty() ? QString() : QString(" ") + QChar(0x2014) + " " + what));
    _status->setToolTip("An AI assistant is connected through MCP and can read and change the open document");
    _status->show();
    _stop->show();
}

void McpServer::stopAll()
{
    // Every client is cut off and has to be allowed again.
    for (Client& client : _clients)
        client.allowed = false;
    _allowedNames.clear();
    _allowedFolders.clear();
    // Stop ends the session too: a client started for it is asked about
    // from now on, like any other.
    _sessionToken.clear();
    {
        std::lock_guard<std::mutex> lock(_socketsMutex);
        for (std::intptr_t socket : _sockets)
            shutdownSocket(static_cast<Socket>(socket));
    }
    _clients.clear();
    showActivity(QString());
    _window->statusBar()->showMessage("The AI assistant was disconnected.", 5000);
}

void McpServer::log(const QString& client, const QString& tool, const QJsonObject& arguments, bool ok)
{
    // Beside settings kept apart (PHOENIX_SETTINGS_DIR), or in Phoenix's
    // own local data folder.
    const QString apart = qEnvironmentVariable("PHOENIX_SETTINGS_DIR");
    const QString folder = !apart.isEmpty() ? apart : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(folder);
    const QString path = QDir(folder).filePath("mcp.log");
    // Kept to a few megabytes: the older half goes.
    if (QFileInfo(path).size() > 4 * 1024 * 1024)
    {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QByteArray args = QJsonDocument(arguments).toJson(QJsonDocument::Compact);
    if (args.size() > 400)
        args = args.left(400) + "...";
    file.write(QString("%1\t%2\t%3\t%4\t%5\n")
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate), client, tool, ok ? "ok" : "failed",
                       QString::fromUtf8(args))
                   .toUtf8());
}

bool McpServer::relayToRunning(int sessionPort)
{
    if ((sessionPort == 0 && !enabled()) || !socketsReady())
        return false;
    Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kNoSocket)
        return false;
    const sockaddr_in address = loopback(sessionPort != 0 ? sessionPort : port());
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        closeSocket(socket);
        return false;
    }
    const QByteArray token = qgetenv("PHOENIX_MCP_TOKEN");
    if (sessionPort != 0 && !token.isEmpty())
        sendAll(socket, QJsonDocument(QJsonObject{{"phoenixSession", QString::fromLatin1(token)}})
                                .toJson(QJsonDocument::Compact)
                                .toStdString() +
                            "\n");
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    // The client's messages to Phoenix, line by line...
    std::thread out([socket]() {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!sendAll(socket, line + "\n"))
                break;
        }
        shutdownSocket(socket);
    });
    out.detach();
    // ...and Phoenix's replies back, until either side goes.
    char buffer[16384];
    for (;;)
    {
        const int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        std::fwrite(buffer, 1, static_cast<size_t>(n), stdout);
        std::fflush(stdout);
    }
    closeSocket(socket);
    return true;
}
