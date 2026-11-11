#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QMessageBox;
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QWidget;

/// Phoenix Cloud from the desktop: who is signed in, and requests to its API
/// with the token Phoenix was given (cloud/lib/desktop.mjs).
///
/// Signing in goes through the browser: Phoenix listens on a port of this
/// computer, opens <server>/desktop in the browser, where the person (signed
/// in to Phoenix Cloud there) allows it, and the browser comes back to the
/// port with a one-time code, which Phoenix trades for a token with the
/// secret its challenge was made from (PKCE). The token is kept in the
/// settings, for that server.
class CloudClient : public QObject
{
    Q_OBJECT
public:
    static CloudClient& instance();

    /// The server: PHOENIX_CLOUD_URL, else the settings' cloud/server, else
    /// https://cloud.phoenixanimate.com. No slash at the end.
    QString server() const;
    void setServer(const QString& url);

    bool signedIn() const { return !_token.isEmpty(); }
    /// The name of the person signed in.
    QString userName() const { return _userName; }

    /// Signs in through the browser; \a done gets whether it worked, and why
    /// not. \a parent shows a "finish in your browser" box meanwhile.
    void signIn(QWidget* parent, std::function<void(bool ok, const QString& error)> done);
    void signOut();

    /// What a request came back with.
    struct Reply
    {
        int status = 0;
        QByteArray body;
        QJsonObject json;
        /// A header of the answer, by name.
        QByteArray header(const QByteArray& name) const;
        QList<QPair<QByteArray, QByteArray>> headers;
        /// The server's own words when it said no, else a sentence for the
        /// failure.
        QString message() const;
        bool ok() const { return status >= 200 && status < 300; }
    };
    using Done = std::function<void(const Reply&)>;

    /// \a method on \a path ("/api/documents"), with the token. \a body goes
    /// as \a type. A 401 signs Phoenix out (the token was revoked or ran
    /// out), and signedOut() tells of it.
    QNetworkReply* request(const QByteArray& method, const QString& path, const QByteArray& body, const QByteArray& type,
        Done done, QObject* context = nullptr);
    QNetworkReply* get(const QString& path, Done done, QObject* context = nullptr);
    QNetworkReply* postJson(const QString& path, const QJsonObject& json, Done done, QObject* context = nullptr);

    /// Requests made and not answered yet.
    int pending() const { return _pending; }

signals:
    void signedInChanged();

private:
    CloudClient();
    void load();
    void store();
    void finishSignIn(const QString& code, const QString& error);

    QNetworkAccessManager* _network = nullptr;
    QString _token;
    QString _userName;
    int _pending = 0;

    // Signing in.
    QTcpServer* _listener = nullptr;
    QString _state;
    QString _verifier;
    QPointer<QMessageBox> _waiting;
    std::function<void(bool, const QString&)> _signedIn;
};
