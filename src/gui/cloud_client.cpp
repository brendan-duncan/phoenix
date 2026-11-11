#include "cloud_client.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

const char* const kDefaultServer = "https://cloud.phoenixanimate.com";

/// \a bytes random bytes, URL-safe base64 without padding.
QString randomText(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator* random = QRandomGenerator::system();
    for (int i = 0; i < bytes; ++i)
        data[i] = static_cast<char>(random->bounded(256));
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

/// The page the browser shows once it has handed Phoenix the code.
QByteArray browserPage(bool ok, const QString& text)
{
    const QByteArray body = QString("<!doctype html><html><head><meta charset=\"utf-8\"><title>Phoenix</title>"
                                    "<style>body{font:16px system-ui,sans-serif;background:#1b1b1e;color:#e9e9ec;display:grid;"
                                    "place-items:center;height:100vh;margin:0}div{max-width:420px;text-align:center}"
                                    "h1{font-size:20px;color:%1}</style></head><body><div><h1>%2</h1><p>%3</p></div></body></html>")
                                .arg(ok ? "#4cc9a0" : "#ff8a80", ok ? "Phoenix is signed in" : "Phoenix is not signed in",
                                    text.toHtmlEscaped())
                                .toUtf8();
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
           "Content-Length: " +
        QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

} // namespace

CloudClient& CloudClient::instance()
{
    static CloudClient* client = new CloudClient();
    return *client;
}

CloudClient::CloudClient()
    : _network(new QNetworkAccessManager(this))
{
    _network->setTransferTimeout(120000);
    load();
}

QString CloudClient::server() const
{
    QString url = qEnvironmentVariable("PHOENIX_CLOUD_URL");
    if (url.isEmpty())
        url = QSettings().value("cloud/server", kDefaultServer).toString();
    while (url.endsWith('/'))
        url.chop(1);
    return url.isEmpty() ? QString(kDefaultServer) : url;
}

void CloudClient::setServer(const QString& url)
{
    QSettings().setValue("cloud/server", url.trimmed());
    load();
    emit signedInChanged();
}

void CloudClient::load()
{
    // The token is the server's it was given by.
    QSettings settings;
    if (settings.value("cloud/tokenServer").toString() == server())
    {
        _token = settings.value("cloud/token").toString();
        _userName = settings.value("cloud/user").toString();
    }
    else
    {
        _token.clear();
        _userName.clear();
    }
}

void CloudClient::store()
{
    QSettings settings;
    if (_token.isEmpty())
    {
        settings.remove("cloud/token");
        settings.remove("cloud/user");
        settings.remove("cloud/tokenServer");
        return;
    }
    settings.setValue("cloud/token", _token);
    settings.setValue("cloud/user", _userName);
    settings.setValue("cloud/tokenServer", server());
}

QByteArray CloudClient::Reply::header(const QByteArray& name) const
{
    for (const auto& [key, value] : headers)
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    return QByteArray();
}

QString CloudClient::Reply::message() const
{
    const QString said = json.value("message").toString();
    if (!said.isEmpty())
        return said;
    if (status == 0)
        return "Phoenix Cloud could not be reached.";
    return QString("Phoenix Cloud answered %1.").arg(status);
}

QNetworkReply* CloudClient::request(const QByteArray& method, const QString& path, const QByteArray& body,
    const QByteArray& type, Done done, QObject* context)
{
    QNetworkRequest request(QUrl(server() + path));
    if (!_token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + _token.toLatin1());
    request.setRawHeader("Accept", "application/json");
    if (!type.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, type);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = _network->sendCustomRequest(request, method, body);
    ++_pending;
    const QString sentWith = _token;
    QPointer<QObject> guard = context;
    const bool guarded = context != nullptr;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done, guard, guarded, sentWith]() {
        --_pending;
        reply->deleteLater();
        Reply answer;
        answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        answer.body = reply->readAll();
        answer.headers = reply->rawHeaderPairs();
        const QJsonDocument json = QJsonDocument::fromJson(answer.body);
        if (json.isObject())
            answer.json = json.object();
        // The token was revoked or ran out: signed out.
        if (answer.status == 401 && !sentWith.isEmpty() && sentWith == _token)
        {
            _token.clear();
            _userName.clear();
            store();
            emit signedInChanged();
        }
        if (guarded && !guard)
            return;
        if (done)
            done(answer);
    });
    return reply;
}

QNetworkReply* CloudClient::get(const QString& path, Done done, QObject* context)
{
    return request("GET", path, QByteArray(), QByteArray(), std::move(done), context);
}

QNetworkReply* CloudClient::postJson(const QString& path, const QJsonObject& json, Done done, QObject* context)
{
    return request("POST", path, QJsonDocument(json).toJson(QJsonDocument::Compact), "application/json", std::move(done), context);
}

void CloudClient::signIn(QWidget* parent, std::function<void(bool, const QString&)> done)
{
    // One sign-in at a time: a second ask takes over the first.
    if (_listener)
    {
        _listener->close();
        _listener->deleteLater();
        _listener = nullptr;
    }
    if (_waiting)
        _waiting->deleteLater();
    _signedIn = std::move(done);
    _listener = new QTcpServer(this);
    if (!_listener->listen(QHostAddress::LocalHost, 0))
    {
        finishSignIn(QString(), "Phoenix could not listen for the browser on this computer.");
        return;
    }
    _state = randomText(24);
    _verifier = randomText(48);
    const QString challenge = QString::fromLatin1(QCryptographicHash::hash(_verifier.toLatin1(), QCryptographicHash::Sha256)
                                                      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    connect(_listener, &QTcpServer::newConnection, this, [this]() {
        while (_listener && _listener->hasPendingConnections())
        {
            QTcpSocket* socket = _listener->nextPendingConnection();
            // Its own: the listener goes as soon as the code is in, before
            // the page has been written back to the browser.
            socket->setParent(this);
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                const QByteArray head = socket->peek(8192);
                if (!head.contains("\r\n\r\n") && head.size() < 8192)
                    return;
                const QList<QByteArray> line = head.left(head.indexOf("\r\n")).split(' ');
                const QUrl url(QString::fromLatin1(line.value(1)));
                if (line.value(0) != "GET" || url.path() != "/callback")
                {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                const QUrlQuery query(url);
                const QString state = query.queryItemValue("state");
                const QString code = query.queryItemValue("code");
                const bool cancelled = query.queryItemValue("error") == "cancelled";
                const bool matches = state == _state && !_state.isEmpty();
                socket->write(browserPage(matches && !code.isEmpty(),
                    !matches   ? QString("This sign-in was not the one Phoenix asked for. Sign in again from Phoenix.")
                        : cancelled ? QString("Signing in was cancelled.")
                                    : QString("You can close this tab and go back to Phoenix.")));
                socket->disconnectFromHost();
                if (!matches)
                    return;
                finishSignIn(code, cancelled ? QString("Signing in was cancelled.") : QString());
            });
        }
    });

    QUrl url(server() + "/desktop");
    QUrlQuery query;
    query.addQueryItem("port", QString::number(_listener->serverPort()));
    query.addQueryItem("state", _state);
    query.addQueryItem("challenge", challenge);
    query.addQueryItem("name", QSysInfo::machineHostName());
    url.setQuery(query);
    // Tests play the browser: the address goes to a file for them.
    const QString forTests = qEnvironmentVariable("PHOENIX_CLOUD_SIGNIN_URL_FILE");
    if (!forTests.isEmpty())
    {
        QFile out(forTests);
        if (out.open(QIODevice::WriteOnly))
            out.write(url.toEncoded());
    }
    else if (!QDesktopServices::openUrl(url))
    {
        finishSignIn(QString(), "Phoenix could not open your browser.");
        return;
    }
    _waiting = new QMessageBox(QMessageBox::Information, "Sign In to Phoenix Cloud",
        QString("Finish signing in in your browser, at %1.\n\nPhoenix carries on once you allow it there.").arg(server()),
        QMessageBox::Cancel, parent);
    _waiting->setAttribute(Qt::WA_DeleteOnClose);
    _waiting->setWindowModality(Qt::NonModal);
    connect(_waiting, &QMessageBox::rejected, this, [this]() { finishSignIn(QString(), "Signing in was cancelled."); });
    _waiting->show();
    // Not finished in ten minutes: given up.
    QTimer::singleShot(10 * 60 * 1000, _listener, [this]() { finishSignIn(QString(), "Signing in took too long."); });
}

void CloudClient::finishSignIn(const QString& code, const QString& error)
{
    if (_listener)
    {
        _listener->close();
        _listener->deleteLater();
        _listener = nullptr;
    }
    if (_waiting)
    {
        _waiting->blockSignals(true);
        _waiting->close();
    }
    auto done = std::move(_signedIn);
    _signedIn = nullptr;
    const QString verifier = _verifier;
    _state.clear();
    _verifier.clear();
    if (!done)
        return;
    if (code.isEmpty())
    {
        done(false, error);
        return;
    }
    QJsonObject body{{"code", code}, {"verifier", verifier}};
    // The trade is made without a token: none yet.
    _token.clear();
    postJson("/api/desktop/token", body, [this, done](const Reply& reply) {
        const QString token = reply.json.value("token").toString();
        if (!reply.ok() || token.isEmpty())
        {
            done(false, reply.message());
            return;
        }
        _token = token;
        _userName = reply.json.value("user").toObject().value("name").toString();
        store();
        emit signedInChanged();
        done(true, QString());
    });
}

void CloudClient::signOut()
{
    if (_token.isEmpty())
        return;
    // The server forgets the token; Phoenix forgets it whether or not the
    // server could be reached.
    postJson("/api/desktop/signout", QJsonObject(), [](const Reply&) {});
    _token.clear();
    _userName.clear();
    store();
    emit signedInChanged();
}
