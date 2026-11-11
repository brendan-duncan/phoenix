#include "cloud_documents.h"

#include "cloud_client.h"
#include "main_window.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>


namespace {

const int kLockEveryMs = 30 * 1000;
const int kDraftEveryMs = 2 * 60 * 1000;

QString documentPath(const QString& id, const QString& tail = QString())
{
    return "/api/documents/" + QString::fromLatin1(QUrl::toPercentEncoding(id)) + tail;
}

/// A document's name as a file name.
QString fileNameOf(const QString& name)
{
    QString safe = name;
    safe.replace(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]+"), "_");
    safe = safe.trimmed().left(100);
    return (safe.isEmpty() ? QString("Untitled") : safe) + ".phx";
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QString samePath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

CloudDocuments& CloudDocuments::instance()
{
    static CloudDocuments* documents = new CloudDocuments();
    return *documents;
}

CloudDocuments::CloudDocuments()
{
    _locks = new QTimer(this);
    connect(_locks, &QTimer::timeout, this, [this]() { keepLocks(); });
    _locks->start(kLockEveryMs);
    _drafts = new QTimer(this);
    connect(_drafts, &QTimer::timeout, this, [this]() { sendDrafts(); });
    // Tests wait less for drafts.
    const int draftSeconds = qEnvironmentVariableIntValue("PHOENIX_CLOUD_DRAFT_SECONDS");
    _drafts->start(draftSeconds > 0 ? draftSeconds * 1000 : kDraftEveryMs);
    // Signed out: what is open stays open, and says why it cannot save.
    connect(&CloudClient::instance(), &CloudClient::signedInChanged, this, [this]() {
        if (CloudClient::instance().signedIn())
            return;
        for (auto& [path, doc] : _docs)
            setStatus(doc, "Signed out of Phoenix Cloud: sign in to save here", 2, false, true);
    });
}

QString CloudDocuments::cacheFolder() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("Cloud");
}

void CloudDocuments::setStatus(Doc& doc, const QString& text, int kind, bool offerCopy, bool offerRetry)
{
    doc.status = text;
    doc.kind = kind;
    doc.offerCopy = offerCopy && doc.canDownload;
    doc.offerRetry = offerRetry;
    emit statusChanged(doc.path);
}

bool CloudDocuments::owns(const QString& path) const
{
    return !path.isEmpty() && _docs.count(samePath(path)) > 0;
}

const CloudDocuments::Doc* CloudDocuments::doc(const QString& path) const
{
    if (path.isEmpty())
        return nullptr;
    const auto found = _docs.find(samePath(path));
    return found == _docs.end() ? nullptr : &found->second;
}

QString CloudDocuments::cachedId(const QString& path) const
{
    const QFileInfo info(path);
    const QString cache = QFileInfo(cacheFolder()).absoluteFilePath();
    if (!info.absoluteFilePath().startsWith(cache + "/", Qt::CaseInsensitive))
        return QString();
    QFile sidecar(info.absoluteDir().filePath("cloud.json"));
    if (!sidecar.open(QIODevice::ReadOnly))
        return QString();
    const QJsonObject json = QJsonDocument::fromJson(sidecar.readAll()).object();
    if (json.value("server").toString() != CloudClient::instance().server())
        return QString();
    return json.value("id").toString();
}

QString CloudDocuments::pageUrl(const QString& path) const
{
    const Doc* found = doc(path);
    return found ? CloudClient::instance().server() + "/d/" + QString::fromLatin1(QUrl::toPercentEncoding(found->id))
                 : QString();
}

void CloudDocuments::writeSidecar(const Doc& doc)
{
    const QJsonObject json{{"server", CloudClient::instance().server()}, {"id", doc.id}, {"name", doc.name}};
    writeFile(QFileInfo(doc.path).absoluteDir().filePath("cloud.json"), QJsonDocument(json).toJson());
}

// -- Opening --------------------------------------------------------------

void CloudDocuments::open(const QString& id, MainWindow* window)
{
    CloudClient& client = CloudClient::instance();
    QPointer<MainWindow> target = window;
    if (!client.signedIn())
    {
        client.signIn(window, [this, id, target](bool ok, const QString& error) {
            if (!target)
                return;
            if (ok)
                open(id, target);
            else if (!error.isEmpty())
                QMessageBox::warning(target, "Phoenix Cloud", error);
        });
        return;
    }
    // Open already: its tab.
    for (const auto& [path, doc] : _docs)
        if (doc.id == id)
        {
            window->openInTab(path);
            return;
        }
    window->statusBar()->showMessage("Opening from Phoenix Cloud…");
    client.get(documentPath(id), [this, id, target](const CloudClient::Reply& reply) {
        if (!target)
            return;
        target->statusBar()->clearMessage();
        if (!reply.ok())
        {
            QMessageBox::warning(target, "Phoenix Cloud", "The document could not be opened: " + reply.message());
            return;
        }
        const QJsonObject document = reply.json.value("document").toObject();
        if (!document.value("revision").toInt())
        {
            QMessageBox::warning(target, "Phoenix Cloud", "This document has nothing saved in it yet.");
            return;
        }
        if (!document.value("canOpen").toBool())
        {
            // Only played: its player page in the browser.
            const auto answer = QMessageBox::question(target, "Phoenix Cloud",
                QString("You can play “%1” but not open it. Play it in your browser?").arg(document.value("name").toString()));
            if (answer == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(CloudClient::instance().server() + "/play/" + id));
            return;
        }
        // Unsaved work kept from last time comes back if they want it.
        CloudClient::instance().get(documentPath(id, "/draft"), [this, id, document, target](const CloudClient::Reply& reply) {
            if (!target)
                return;
            const QJsonObject draft = reply.json.value("draft").toObject();
            if (reply.ok() && !draft.isEmpty())
            {
                const QString when = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(draft.value("savedAt").toDouble()))
                                         .toString("d MMMM yyyy, h:mm ap");
                QMessageBox box(QMessageBox::Question, "Open your unsaved changes?",
                    QString("You made changes to “%1” on %2 that were not saved. Open them, or the last saved version?")
                        .arg(document.value("name").toString(), when),
                    QMessageBox::NoButton, target);
                QPushButton* saved = box.addButton("Last Saved Version", QMessageBox::RejectRole);
                QPushButton* changes = box.addButton("Unsaved Changes", QMessageBox::AcceptRole);
                box.setDefaultButton(changes);
                box.exec();
                if (box.clickedButton() == changes)
                {
                    fetchAndOpen(document, target, documentPath(id, "/draft/file"), draft.value("baseRevision").toInt());
                    return;
                }
                Q_UNUSED(saved);
                CloudClient::instance().request("DELETE", documentPath(id, "/draft"), QByteArray(), QByteArray(), nullptr);
            }
            fetchAndOpen(document, target, documentPath(id, "/file"), -1);
        }, target);
    }, target);
}

void CloudDocuments::fetchAndOpen(const QJsonObject& document, MainWindow* window, const QString& source, int draftBase)
{
    QPointer<MainWindow> target = window;
    const QString id = document.value("id").toString();
    window->statusBar()->showMessage(QString("Opening %1…").arg(document.value("name").toString()));
    CloudClient::instance().get(source, [this, document, target, id, draftBase](const CloudClient::Reply& reply) {
        if (!target)
            return;
        target->statusBar()->clearMessage();
        if (!reply.ok())
        {
            QMessageBox::warning(target, "Phoenix Cloud", "The document could not be read: " + reply.message());
            return;
        }
        Doc doc;
        doc.id = id;
        doc.name = document.value("name").toString();
        doc.role = document.value("role").toString();
        doc.canDownload = document.value("canDownload").toBool();
        doc.fromDraft = draftBase >= 0;
        doc.revision = doc.fromDraft ? draftBase
                                     : (reply.header("X-Phoenix-Revision").toInt() > 0 ? reply.header("X-Phoenix-Revision").toInt()
                                                                                       : document.value("revision").toInt());
        doc.path = samePath(QDir(QDir(cacheFolder()).filePath(id)).filePath(fileNameOf(doc.name)));
        doc.window = target;
        if (!writeFile(doc.path, reply.body))
        {
            QMessageBox::warning(target, "Phoenix Cloud", QString("Could not keep %1 on this computer.").arg(QDir::toNativeSeparators(doc.path)));
            return;
        }
        writeSidecar(doc);
        _docs[doc.path] = doc;
        Doc& kept = _docs[doc.path];
        if (kept.fromDraft)
            setStatus(kept, "Opened your unsaved changes: save to keep them", 2);
        else if (kept.editor())
            setStatus(kept, QString("Version %1").arg(kept.revision));
        else
            setStatus(kept, kept.canDownload ? "You can view it; save to keep a copy in your files" : "You can view it");
        CloudClient::instance().postJson(documentPath(id, "/opened"), QJsonObject(), nullptr);
        // Opening the tab asks which cloud documents are open before this
        // one's tab is there: it is not let go of meanwhile, and the tab is
        // given its own copy of the path.
        kept.opening = true;
        const QString path = kept.path;
        const bool editor = kept.editor();
        target->openInTab(path);
        const auto opened = _docs.find(path);
        if (opened == _docs.end())
            return;
        opened->second.opening = false;
        if (!openPaths().contains(path))
        {
            // It did not open (a file Phoenix could not read).
            _docs.erase(opened);
            return;
        }
        if (editor)
            keepLock(path);
    }, target);
}

void CloudDocuments::create(const QJsonObject& body, MainWindow* window)
{
    QPointer<MainWindow> target = window;
    window->statusBar()->showMessage("Making the document in Phoenix Cloud…");
    CloudClient::instance().postJson("/api/documents", body, [this, target](const CloudClient::Reply& reply) {
        if (!target)
            return;
        target->statusBar()->clearMessage();
        if (!reply.ok())
        {
            QMessageBox::warning(target, "New Cloud Document", "The document could not be made: " + reply.message());
            return;
        }
        const QJsonObject document = reply.json.value("document").toObject();
        const QString tutorial = reply.json.value("tutorial").toString();
        fetchAndOpen(document, target, documentPath(document.value("id").toString(), "/file"), -1);
        // A tutorial's document: its lesson runs on it once it is open.
        if (!tutorial.isEmpty())
            QTimer::singleShot(0, target, [target, tutorial]() {
                if (target)
                    target->startTutorialOnDocument(tutorial);
            });
    }, target);
}

void CloudDocuments::upload(const QString& file, const QString& name, const QString& folderId, const QString& groupId,
    MainWindow* window)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(window, "Save to Phoenix Cloud", QString("Could not read %1.").arg(QDir::toNativeSeparators(file)));
        return;
    }
    const QByteArray bytes = in.readAll();
    QUrlQuery query;
    query.addQueryItem("name", name);
    if (!folderId.isEmpty())
        query.addQueryItem("folderId", folderId);
    if (!groupId.isEmpty())
        query.addQueryItem("groupId", groupId);
    QPointer<MainWindow> target = window;
    window->statusBar()->showMessage(QString("Saving %1 to Phoenix Cloud…").arg(name));
    CloudClient::instance().request("POST", "/api/documents/upload?" + query.toString(QUrl::FullyEncoded), bytes,
        "application/octet-stream", [this, target](const CloudClient::Reply& reply) {
            if (!target)
                return;
            target->statusBar()->clearMessage();
            if (!reply.ok())
            {
                QMessageBox::warning(target, "Save to Phoenix Cloud", "It could not be saved: " + reply.message());
                return;
            }
            const QJsonObject document = reply.json.value("document").toObject();
            fetchAndOpen(document, target, documentPath(document.value("id").toString(), "/file"), -1);
        }, target);
}

void CloudDocuments::openLibrary(const QString& id, MainWindow* window)
{
    QPointer<MainWindow> target = window;
    CloudClient::instance().get(documentPath(id), [this, id, target](const CloudClient::Reply& info) {
        if (!target)
            return;
        const QString name = info.json.value("document").toObject().value("name").toString();
        target->statusBar()->showMessage(QString("Opening the library of %1…").arg(name));
        CloudClient::instance().get(documentPath(id, "/file"), [this, id, name, target](const CloudClient::Reply& reply) {
            if (!target)
                return;
            target->statusBar()->clearMessage();
            if (!reply.ok())
            {
                QMessageBox::warning(target, "Phoenix Cloud", QString("Could not open %1's library: %2").arg(name, reply.message()));
                return;
            }
            const QString path = QDir(QDir(cacheFolder()).filePath("libraries/" + id)).filePath(fileNameOf(name));
            if (!writeFile(path, reply.body))
                return;
            target->openExternalLibraryFile(path);
        }, target);
    }, target);
}

// -- The edit lock ---------------------------------------------------------

void CloudDocuments::keepLock(const QString& path)
{
    const auto found = _docs.find(path);
    if (found == _docs.end() || !found->second.editor() || !CloudClient::instance().signedIn())
        return;
    CloudClient::instance().postJson(documentPath(found->second.id, "/lock"), QJsonObject(),
        [this, path](const CloudClient::Reply& reply) {
            const auto it = _docs.find(path);
            if (it == _docs.end() || !reply.ok())
                return;
            Doc& doc = it->second;
            const QString holder = reply.json.value("held").toBool()
                ? QString()
                : reply.json.value("holder").toObject().value("name").toString("Someone");
            if (holder == doc.lockedBy)
                return;
            doc.lockedBy = holder;
            if (!holder.isEmpty())
                setStatus(doc, QString("%1 is editing it now: %2").arg(holder,
                    doc.canDownload ? "your saves go to a copy" : "you cannot save until they finish"), 2);
            else if (!doc.saving && doc.unsent.isEmpty())
                setStatus(doc, QString("Version %1 · you can save here now").arg(doc.revision), 1);
        });
}

void CloudDocuments::keepLocks()
{
    tabsChanged();
    for (const auto& [path, doc] : _docs)
        keepLock(path);
}

void CloudDocuments::tabsChanged()
{
    const QStringList open = openPaths();
    QSet<QString> still;
    for (const QString& path : open)
        still.insert(samePath(path));
    for (auto it = _docs.begin(); it != _docs.end();)
    {
        Doc& doc = it->second;
        // Closed, and nothing still going up: let go of its lock.
        if (still.contains(it->first) || doc.opening || doc.saving || !doc.queued.isEmpty())
        {
            ++it;
            continue;
        }
        if (doc.editor() && doc.lockedBy.isEmpty() && CloudClient::instance().signedIn())
            CloudClient::instance().request("DELETE", documentPath(doc.id, "/lock"), QByteArray(), QByteArray(), nullptr);
        it = _docs.erase(it);
    }
}

// -- Saves and drafts --------------------------------------------------------

void CloudDocuments::saved(const QString& path)
{
    const auto found = _docs.find(samePath(path));
    if (found == _docs.end())
        return;
    Doc& doc = found->second;
    QFile file(doc.path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    if (!CloudClient::instance().signedIn())
    {
        doc.unsent = bytes;
        setStatus(doc, "Not saved: Phoenix is signed out of Phoenix Cloud", 2, false, true);
        return;
    }
    // A save while someone else holds the lock goes to a copy.
    if (!doc.lockedBy.isEmpty())
    {
        doc.unsent = bytes;
        setStatus(doc, QString("Not saved: %1 is editing it now.").arg(doc.lockedBy), 2, true);
        return;
    }
    // One at a time; a save made while one goes waits, only the latest.
    if (doc.saving)
    {
        doc.queued = bytes;
        return;
    }
    startUpload(doc.path, bytes);
}

void CloudDocuments::startUpload(const QString& path, const QByteArray& bytes)
{
    Doc& doc = _docs[path];
    doc.saving = true;
    setStatus(doc, "Saving to the cloud…");
    CloudClient::instance().request("PUT", documentPath(doc.id, QString("/file?base=%1").arg(doc.revision)), bytes,
        "application/octet-stream", [this, path, bytes](const CloudClient::Reply& reply) {
            const auto it = _docs.find(path);
            if (it == _docs.end())
                return;
            Doc& doc = it->second;
            doc.saving = false;
            if (reply.ok())
            {
                doc.revision = reply.json.value("revision").toInt(doc.revision + 1);
                doc.unsent.clear();
                doc.fromDraft = false;
                setStatus(doc, QString("Saved · version %1").arg(doc.revision), 1);
            }
            else if (reply.status == 409 || reply.status == 403 || reply.status == 423)
            {
                doc.unsent = bytes;
                const QString why = reply.status == 409 ? QString("Someone else saved it since you opened it.")
                    : reply.status == 423             ? reply.message()
                                                      : QString("You can view this document but not save over it.");
                setStatus(doc, "Not saved: " + why, 2, true);
            }
            else
            {
                doc.unsent = bytes;
                const QString why = reply.status == 401 ? QString("Phoenix is signed out of Phoenix Cloud") : reply.message();
                setStatus(doc, "Not saved: " + why, 2, false, true);
            }
            if (!doc.queued.isEmpty())
            {
                const QByteArray next = doc.queued;
                doc.queued.clear();
                startUpload(path, next);
                return;
            }
            // A tab closed while its save went up lets go now.
            tabsChanged();
        });
}

void CloudDocuments::retry(const QString& path)
{
    const auto found = _docs.find(samePath(path));
    if (found == _docs.end() || found->second.unsent.isEmpty())
        return;
    const QString key = found->first;
    if (!CloudClient::instance().signedIn())
    {
        CloudClient::instance().signIn(QApplication::activeWindow(), [this, key](bool ok, const QString& error) {
            if (ok)
                retry(key);
            else if (!error.isEmpty())
                QMessageBox::warning(QApplication::activeWindow(), "Phoenix Cloud", error);
        });
        return;
    }
    Doc& doc = found->second;
    if (doc.saving)
        return;
    const QByteArray bytes = doc.unsent;
    startUpload(key, bytes);
}

void CloudDocuments::saveCopy(const QString& path)
{
    const auto found = _docs.find(samePath(path));
    if (found == _docs.end() || found->second.unsent.isEmpty() || !found->second.canDownload)
        return;
    const QString key = found->first;
    Doc& doc = found->second;
    setStatus(doc, "Saving a copy…");
    QUrlQuery query;
    query.addQueryItem("name", doc.name + " copy.phx");
    const QByteArray bytes = doc.unsent;
    CloudClient::instance().request("POST", "/api/documents/upload?" + query.toString(QUrl::FullyEncoded), bytes,
        "application/octet-stream", [this, key](const CloudClient::Reply& reply) {
            const auto it = _docs.find(key);
            if (it == _docs.end())
                return;
            Doc& doc = it->second;
            if (!reply.ok())
            {
                setStatus(doc, "Not saved: " + reply.message(), 2, true);
                return;
            }
            if (doc.editor() && doc.lockedBy.isEmpty())
                CloudClient::instance().request("DELETE", documentPath(doc.id, "/lock"), QByteArray(), QByteArray(), nullptr);
            // The tab is the copy now, kept where it is.
            const QJsonObject copy = reply.json.value("document").toObject();
            doc.id = copy.value("id").toString();
            doc.name = copy.value("name").toString();
            doc.revision = copy.value("revision").toInt();
            doc.role = "owner";
            doc.canDownload = true;
            doc.unsent.clear();
            doc.lockedBy.clear();
            doc.fromDraft = false;
            writeSidecar(doc);
            setStatus(doc, "Saved as a copy in your files", 1);
            keepLock(key);
        });
}

QList<MainWindow*> CloudDocuments::windows() const
{
    QList<MainWindow*> all = MainWindow::windows();
    for (const auto& [path, doc] : _docs)
        if (doc.window && !all.contains(doc.window.data()))
            all << doc.window.data();
    return all;
}

QStringList CloudDocuments::openPaths() const
{
    QStringList paths;
    for (MainWindow* window : windows())
        paths << window->tabPaths();
    return paths;
}

void CloudDocuments::sendDrafts()
{
    if (!CloudClient::instance().signedIn())
        return;
    for (const auto& [path, doc] : _docs)
    {
        // Written beside the document, never over it: a draft is not a save.
        const QString draft = QFileInfo(path).absoluteDir().filePath(".draft.phx");
        bool written = false;
        for (MainWindow* window : windows())
            if (window->writeDraftOf(path, draft))
            {
                written = true;
                break;
            }
        if (!written)
            continue;
        QFile file(draft);
        if (file.open(QIODevice::ReadOnly))
            CloudClient::instance().request("PUT", documentPath(doc.id, "/draft"), file.readAll(), "application/octet-stream", nullptr);
        file.close();
        QFile::remove(draft);
    }
}

QJsonArray CloudDocuments::report() const
{
    QJsonArray out;
    for (const auto& [path, doc] : _docs)
        out.append(QJsonObject{{"id", doc.id}, {"name", doc.name}, {"path", path}, {"role", doc.role},
            {"revision", doc.revision}, {"status", doc.status}, {"kind", doc.kind}, {"lockedBy", doc.lockedBy},
            {"saving", doc.saving}, {"unsent", !doc.unsent.isEmpty()}, {"offerCopy", doc.offerCopy}});
    return out;
}

bool CloudDocuments::busy() const
{
    for (const auto& [path, doc] : _docs)
        if (doc.saving || !doc.queued.isEmpty())
            return true;
    return false;
}

void CloudDocuments::waitForSaves(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (busy() && clock.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
}

void CloudDocuments::waitForRequests(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (CloudClient::instance().pending() > 0 && clock.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
}

// -- The status bar's part ---------------------------------------------------

CloudStatus::CloudStatus(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("cloudStatus");
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);
    _text = new QLabel(this);
    _text->setObjectName("cloudStatusText");
    _text->setTextFormat(Qt::PlainText);
    _copy = new QPushButton("Save a Copy", this);
    _copy->setObjectName("cloudSaveCopy");
    _copy->setToolTip("Save your changes as a new document in your Phoenix Cloud files");
    _retry = new QPushButton("Try Again", this);
    _retry->setObjectName("cloudRetry");
    layout->addWidget(_text);
    layout->addWidget(_copy);
    layout->addWidget(_retry);
    connect(_copy, &QPushButton::clicked, this, [this]() { CloudDocuments::instance().saveCopy(_path); });
    connect(_retry, &QPushButton::clicked, this, [this]() { CloudDocuments::instance().retry(_path); });
    connect(&CloudDocuments::instance(), &CloudDocuments::statusChanged, this, [this](const QString& path) {
        if (path.isEmpty() || QFileInfo(path).absoluteFilePath() == QFileInfo(_path).absoluteFilePath())
            refresh();
    });
    refresh();
}

void CloudStatus::setPath(const QString& path)
{
    _path = path;
    refresh();
}

void CloudStatus::refresh()
{
    const CloudDocuments::Doc* doc = CloudDocuments::instance().doc(_path);
    setVisible(doc != nullptr);
    if (!doc)
        return;
    _text->setText(QString::fromUtf8("\xE2\x98\x81 ") + doc->name + QString::fromUtf8(" \xC2\xB7 ") + doc->status);
    _text->setToolTip(QString("In Phoenix Cloud: %1").arg(CloudDocuments::instance().pageUrl(_path)));
    const QColor color = doc->kind == 1 ? QColor(76, 201, 160) : doc->kind == 2 ? QColor(255, 138, 128) : palette().color(QPalette::WindowText);
    _text->setStyleSheet(QString("color: %1;").arg(color.name()));
    _copy->setVisible(doc->offerCopy);
    _retry->setVisible(doc->offerRetry);
}
