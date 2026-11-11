// MainWindow's part in Phoenix Cloud on the desktop: the File > Phoenix Cloud
// menu, and what CloudDocuments asks of the windows (which documents are
// open, a draft of one with unsaved changes).

#include "main_window.h"

#include "cloud_browser.h"
#include "cloud_client.h"
#include "cloud_documents.h"
#include "phoenix_view.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUrl>

QStringList MainWindow::tabPaths() const
{
    QStringList paths;
    // The document shown, tabs or none.
    if (!_documentPath.isEmpty())
        paths << QFileInfo(_documentPath).absoluteFilePath();
    for (const std::unique_ptr<DocumentTab>& tab : _tabs)
    {
        const QString path = tabPath(*tab);
        if (!path.isEmpty() && !paths.contains(QFileInfo(path).absoluteFilePath()))
            paths << QFileInfo(path).absoluteFilePath();
    }
    return paths;
}

bool MainWindow::writeDraftOf(const QString& path, const QString& to)
{
    const QString wanted = QFileInfo(path).absoluteFilePath();
    QString error;
    // The document shown.
    if (QFileInfo(_documentPath).absoluteFilePath() == wanted && !_documentPath.isEmpty())
    {
        if (!_flaDocument || !_flaDocument->document || !_editContext.isModified())
            return false;
        if (_phoenixView)
            _phoenixView->flushPendingMerge();
        return writeDocumentFile(*_flaDocument->document, to, error) && error.isEmpty();
    }
    // One waiting in a tab.
    for (size_t i = 0; i < _tabs.size(); ++i)
    {
        const DocumentTab& tab = *_tabs[i];
        if (static_cast<int>(i) == _activeTab || QFileInfo(tabPath(tab)).absoluteFilePath() != wanted)
            continue;
        const fla::Document* document = tabDocument(tab);
        if (!tabModified(tab) || !document)
            return false;
        return writeDocumentFile(*document, to, error) && error.isEmpty();
    }
    return false;
}

void MainWindow::addCloudMenu(QMenu* fileMenu, QMenu* importMenu)
{
    QMenu* menu = fileMenu->addMenu("Phoenix &Cloud");
    menu->menuAction()->setObjectName("cloudMenu");
    QAction* signIn = menu->addAction("Sign &In...");
    signIn->setObjectName("cloudSignIn");
    signIn->setStatusTip("Sign in to Phoenix Cloud through your browser, to open and save cloud documents here");
    QAction* signOut = menu->addAction("Sign &Out");
    signOut->setObjectName("cloudSignOut");
    menu->addSeparator();
    QAction* open = menu->addAction("&Open from Cloud...");
    open->setObjectName("cloudOpen");
    open->setStatusTip("Open a document from your Phoenix Cloud files, shared documents or groups");
    QAction* make = menu->addAction("&New Cloud Document...");
    make->setObjectName("cloudNew");
    make->setStatusTip("A new document in Phoenix Cloud: blank, from a template or for a lesson");
    QAction* copy = menu->addAction("Save a &Copy to Cloud...");
    copy->setObjectName("cloudSaveCopy");
    copy->setStatusTip("This document, as it is now, as a new document in Phoenix Cloud");
    menu->addSeparator();
    QAction* page = menu->addAction("Show in Phoenix Cloud");
    page->setObjectName("cloudShowPage");
    page->setStatusTip("This cloud document's page in your browser: its versions, sharing and details");
    QAction* site = menu->addAction("Open Phoenix Cloud in Browser");
    site->setStatusTip("Your Phoenix Cloud files in your browser");

    QAction* library = importMenu->addAction("Open Library from &Phoenix Cloud...");
    library->setObjectName("cloudLibrary");
    library->setStatusTip("Another of your cloud documents' library beside this one, to copy items from");

    // Signed in: the menu says as whom, and offers what needs it.
    const auto refresh = [=]() {
        CloudClient& client = CloudClient::instance();
        const bool in = client.signedIn();
        signIn->setVisible(!in);
        signOut->setVisible(in);
        signOut->setText(in ? QString("Sign &Out (%1)").arg(client.userName()) : QString("Sign &Out"));
        page->setEnabled(CloudDocuments::instance().owns(_documentPath));
        copy->setEnabled(_flaDocument && _flaDocument->document);
    };
    connect(menu, &QMenu::aboutToShow, this, refresh);
    refresh();

    // What needs signing in signs in first, then goes on.
    const auto signedIn = [this](std::function<void()> then) {
        if (CloudClient::instance().signedIn())
        {
            then();
            return;
        }
        QPointer<MainWindow> self = this;
        CloudClient::instance().signIn(this, [self, then](bool ok, const QString& error) {
            if (!self)
                return;
            if (ok)
            {
                self->statusBar()->showMessage(
                    QString("Signed in to Phoenix Cloud as %1").arg(CloudClient::instance().userName()), 6000);
                then();
            }
            else if (!error.isEmpty())
                QMessageBox::warning(self, "Phoenix Cloud", error);
        });
    };
    connect(signIn, &QAction::triggered, this, [signedIn]() { signedIn([]() {}); });
    connect(signOut, &QAction::triggered, this, [this]() {
        CloudClient::instance().signOut();
        statusBar()->showMessage("Signed out of Phoenix Cloud", 5000);
    });
    connect(open, &QAction::triggered, this, [this, signedIn]() {
        signedIn([this]() { (new CloudBrowser(this, CloudBrowser::Mode::Open))->show(); });
    });
    connect(make, &QAction::triggered, this, [this, signedIn]() { signedIn([this]() { CloudBrowser::newDocument(this); }); });
    connect(copy, &QAction::triggered, this, [this, signedIn]() { signedIn([this]() { saveCopyToCloud(); }); });
    connect(library, &QAction::triggered, this, [this, signedIn]() {
        signedIn([this]() { (new CloudBrowser(this, CloudBrowser::Mode::Library))->show(); });
    });
    connect(page, &QAction::triggered, this, [this]() {
        const QString url = CloudDocuments::instance().pageUrl(_documentPath);
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });
    connect(site, &QAction::triggered, this, []() { QDesktopServices::openUrl(QUrl(CloudClient::instance().server() + "/")); });

    // The status bar's part, for the cloud document shown.
    _cloudStatus = new CloudStatus(statusBar());
    statusBar()->addPermanentWidget(_cloudStatus);
}

void MainWindow::cloudDocumentShown()
{
    CloudDocuments::instance().tabsChanged();
    if (_cloudStatus)
        _cloudStatus->setPath(_documentPath);
}

bool MainWindow::openCloudCopy(const QString& path)
{
    if (CloudDocuments::instance().owns(path))
        return false;
    const QString id = CloudDocuments::instance().cachedId(path);
    if (id.isEmpty())
        return false;
    // A cloud document opened again (from Recent Files, say): the cloud's
    // version, not the copy left on this computer. Signed out, the browser
    // does not open unasked (tabs reopened as Phoenix starts).
    if (!CloudClient::instance().signedIn())
    {
        QPointer<MainWindow> self = this;
        const QString name = QFileInfo(path).completeBaseName();
        QMessageBox* box = new QMessageBox(QMessageBox::Question, "Phoenix Cloud",
            QString("“%1” is a Phoenix Cloud document. Sign in to open it from Phoenix Cloud?").arg(name),
            QMessageBox::Yes | QMessageBox::No, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QMessageBox::finished, this, [self, id](int result) {
            if (self && result == QMessageBox::Yes)
                CloudDocuments::instance().open(id, self);
        });
        box->show();
        return true;
    }
    CloudDocuments::instance().open(id, this);
    return true;
}

void MainWindow::finishCloudSaves()
{
    CloudDocuments& documents = CloudDocuments::instance();
    if (documents.busy())
    {
        statusBar()->showMessage("Finishing saves to Phoenix Cloud…");
        documents.waitForSaves(20000);
    }
}

void MainWindow::saveCopyToCloud()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    const QString base = _documentName.isEmpty() ? QString("Untitled") : QFileInfo(_documentName).completeBaseName();
    QPointer<MainWindow> self = this;
    (new CloudBrowser(this, CloudBrowser::Mode::SaveTo,
         [self](const QString& name, const QString& folderId, const QString& groupId) {
             if (!self || !self->_flaDocument || !self->_flaDocument->document)
                 return;
             if (self->_phoenixView)
                 self->_phoenixView->flushPendingMerge();
             // Written as a .phx in a folder of its own, then up it goes.
             QTemporaryDir folder;
             const QString file = QDir(folder.path()).filePath("copy.phx");
             QString error;
             if (!folder.isValid() || !self->writeDocumentFile(*self->_flaDocument->document, file, error) || !error.isEmpty())
             {
                 QMessageBox::warning(self, "Save a Copy to Cloud", error.isEmpty() ? QString("The copy could not be written.") : error);
                 return;
             }
             CloudDocuments::instance().upload(file, name.endsWith(".phx", Qt::CaseInsensitive) ? name : name + ".phx",
                 folderId, groupId, self);
         },
         base))
        ->show();
}
