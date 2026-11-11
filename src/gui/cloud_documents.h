#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <map>

class MainWindow;
class QLabel;
class QPushButton;
class QTimer;

/// Phoenix Cloud documents open on the desktop, as the web editor's page
/// keeps them (cloud/dashboard/editor-host.js): each is a .phx in a cache
/// folder, opened in a tab like any file. Saving it writes the file, then
/// uploads it as a new version, checked against the version it was opened
/// from; while it is open, Phoenix holds its edit lock, and sends unsaved
/// changes as a draft every two minutes.
class CloudDocuments : public QObject
{
    Q_OBJECT
public:
    static CloudDocuments& instance();

    /// An open cloud document.
    struct Doc
    {
        QString id;
        QString name;
        /// Its file in the cache.
        QString path;
        /// "owner", "editor", "viewer"...
        QString role;
        bool canDownload = false;
        /// The version its saves are checked against.
        int revision = 0;
        bool fromDraft = false;
        /// Someone else holding its edit lock, by name.
        QString lockedBy;
        bool saving = false;
        QByteArray queued;
        /// A save that did not reach the cloud, kept to try again or save as
        /// a copy.
        QByteArray unsent;
        QString status;
        /// 0 plain, 1 good, 2 bad.
        int kind = 0;
        bool offerCopy = false;
        bool offerRetry = false;
        /// Its tab is being made: not let go of for not being open yet.
        bool opening = false;
        /// The window it opened in (its tab may move to another).
        QPointer<MainWindow> window;

        bool editor() const { return role == "owner" || role == "editor"; }
    };

    /// Opens document \a id in a tab of \a window, offering unsaved changes
    /// kept from last time; signs in first when Phoenix is not.
    void open(const QString& id, MainWindow* window);
    /// A new cloud document: \a body as POST /api/documents takes it (blank,
    /// a template, a tutorial), in \a folderId or \a groupId, then opened.
    void create(const QJsonObject& body, MainWindow* window);
    /// A local file (.phx, or an FLA, SWF, SVG... the cloud converts) made a
    /// cloud document, then opened.
    void upload(const QString& file, const QString& name, const QString& folderId, const QString& groupId,
        MainWindow* window);
    /// Another cloud document's library, beside the document in \a window.
    void openLibrary(const QString& id, MainWindow* window);

    /// Whether \a path is an open cloud document's file, and that document.
    bool owns(const QString& path) const;
    const Doc* doc(const QString& path) const;
    /// The cloud document a file in the cache was, for opening it again
    /// from Recent Files; empty for any other file.
    QString cachedId(const QString& path) const;
    /// The document's page in Phoenix Cloud (details and versions).
    QString pageUrl(const QString& path) const;

    /// The file at \a path, a cloud document's, was saved: up it goes.
    void saved(const QString& path);
    /// The save that did not go, as a copy in the person's files.
    void saveCopy(const QString& path);
    /// The save that did not go, again.
    void retry(const QString& path);
    /// The tabs changed: documents no longer open let go of their locks.
    void tabsChanged();
    /// Saves still going up.
    bool busy() const;
    /// Waits (the events running) until no save is going, or \a ms pass.
    void waitForSaves(int ms);
    /// Waits (the events running) until every request to Phoenix Cloud is
    /// answered, or \a ms pass: locks let go of as Phoenix quits.
    void waitForRequests(int ms);

    /// The open cloud documents as JSON, for checks: id, name, file,
    /// role, version, status, who else is editing.
    QJsonArray report() const;

signals:
    /// \a path's status changed (empty: everything's, after signing out).
    void statusChanged(const QString& path);

private:
    CloudDocuments();
    QString cacheFolder() const;
    void setStatus(Doc& doc, const QString& text, int kind = 0, bool offerCopy = false, bool offerRetry = false);
    void startUpload(const QString& path, const QByteArray& bytes);
    void keepLock(const QString& path);
    void keepLocks();
    void sendDrafts();
    void fetchAndOpen(const QJsonObject& document, MainWindow* window, const QString& source, int draftBase);
    void writeSidecar(const Doc& doc);
    /// The windows to look in for open documents: Phoenix's, and those the
    /// documents opened in.
    QList<MainWindow*> windows() const;
    /// Every document open in those windows, by file.
    QStringList openPaths() const;

    std::map<QString, Doc> _docs;
    QTimer* _locks = nullptr;
    QTimer* _drafts = nullptr;
};

/// The status bar's part for a cloud document: where it is, whether the
/// last save reached the cloud, and Save a Copy or Try Again when it did
/// not. Hidden for other documents.
class CloudStatus : public QWidget
{
    Q_OBJECT
public:
    explicit CloudStatus(QWidget* parent = nullptr);
    /// Shows the status of the document at \a path (the window's).
    void setPath(const QString& path);

private:
    void refresh();

    QString _path;
    QLabel* _text = nullptr;
    QPushButton* _copy = nullptr;
    QPushButton* _retry = nullptr;
};
