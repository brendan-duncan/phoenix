#include "updater.h"
#include "file_association.h"

#include <mutex>
#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

#include <exception>
#include <optional>
#include <thread>

#ifdef PHOENIX_UPDATER
#include "Velopack.hpp"
#endif

namespace {

/// Where releases are published; the release workflow uploads each
/// version's installer, packages and feed there.
const char* RepositoryUrl = "https://github.com/brendan-duncan/phoenix";

/// Whether the updater, and so the application, is still there: the
/// network thread posts only while it is. Checked and cleared under the
/// lock, so a post never reaches an application already gone.
struct Mailbox
{
    std::mutex mutex;
    bool open = true;
};

/// Runs f on the GUI thread, if the updater is still there by then.
template <typename F>
void onGuiThread(const std::shared_ptr<Mailbox>& mailbox, QPointer<Updater> updater, F f)
{
    std::lock_guard<std::mutex> lock(mailbox->mutex);
    if (!mailbox->open)
        return;
    QMetaObject::invokeMethod(qApp, [updater, f]() {
        if (updater)
            f(updater.data());
    }, Qt::QueuedConnection);
}

} // namespace

struct Updater::State
{
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
#ifdef PHOENIX_UPDATER
    std::unique_ptr<Velopack::UpdateManager> manager;
    /// The downloaded update waiting to be installed.
    std::optional<Velopack::UpdateInfo> ready;
#endif
};

void Updater::handleStartup()
{
#ifdef PHOENIX_UPDATER
    try
    {
        // An update downloaded but not installed is installed as Phoenix
        // quits instead (applyOnExit), rather than holding up a start.
        // The installer's hooks: Phoenix's file types come and go with it.
        Velopack::VelopackApp::Build()
            .SetAutoApplyOnStartup(false)
            .OnAfterInstall([](void*, const char*) { FileAssociation::registerTypes(); })
            .OnAfterUpdate([](void*, const char*) { FileAssociation::registerTypes(); })
            .OnBeforeUninstall([](void*, const char*) { FileAssociation::unregisterTypes(); })
            .Run();
    }
    catch (const std::exception&)
    {
        // A copy run from the build folder has nothing to handle.
    }
#endif
}

QString Updater::currentVersion()
{
    return QStringLiteral(PHOENIX_VERSION);
}

Updater::Updater(QWidget* window)
    : QObject(window)
    , _window(window)
    , _state(std::make_shared<State>())
{
}

Updater::~Updater()
{
    // Before the application goes: a check still running posts no more.
    if (_state)
    {
        std::lock_guard<std::mutex> lock(_state->mailbox->mutex);
        _state->mailbox->open = false;
    }
}

void Updater::checkForUpdates(bool userAsked)
{
#ifdef PHOENIX_UPDATER
    if (_busy)
    {
        if (userAsked)
            emit statusMessage("Already checking for updates");
        return;
    }
    if (_state->ready)
    {
        offerRestart(QString::fromStdString(_state->ready->TargetFullRelease.Version));
        return;
    }

    _busy = true;
    if (userAsked)
        emit statusMessage("Checking for updates...");

    // Checking and downloading both wait on the network, so they run on a
    // thread of their own and report back to the GUI thread. The state is
    // shared so that it outlives the updater should Phoenix close first.
    QPointer<Updater> self(this);
    std::shared_ptr<State> state = _state;
    std::thread([self, state, userAsked]() {
        if (!state->manager)
        {
            try
            {
                // PHOENIX_UPDATE_SOURCE points at a folder or URL of
                // releases instead, for trying an update before it is out.
                const QByteArray source = qgetenv("PHOENIX_UPDATE_SOURCE");
                if (!source.isEmpty())
                    state->manager = std::make_unique<Velopack::UpdateManager>(source.toStdString());
                else
                    state->manager = std::make_unique<Velopack::UpdateManager>(
                        std::make_unique<Velopack::GithubSource>(RepositoryUrl));
            }
            catch (const std::exception& e)
            {
                const QString error = QString::fromUtf8(e.what());
                onGuiThread(state->mailbox, self, [userAsked, error](Updater* u) {
                    u->onChecked(userAsked, QString(), error, true);
                });
                return;
            }
        }

        QString version;
        QString error;
        try
        {
            std::optional<Velopack::UpdateInfo> update = state->manager->CheckForUpdates();
            if (update)
            {
                version = QString::fromStdString(update->TargetFullRelease.Version);
                onGuiThread(state->mailbox, self, [version](Updater* u) {
                    emit u->statusMessage(QString("Downloading Phoenix %1...").arg(version));
                });

                // Reported in tenths, so the status bar is not flooded.
                struct Progress { std::shared_ptr<Mailbox> mailbox; QPointer<Updater> updater; QString version; int last = -1; };
                Progress progress{ state->mailbox, self, version };
                state->manager->DownloadUpdates(*update, [](void* data, size_t percent) {
                    Progress* p = static_cast<Progress*>(data);
                    const int tenth = int(percent / 10);
                    if (tenth == p->last)
                        return;
                    p->last = tenth;
                    const QString message = QString("Downloading Phoenix %1... %2%")
                        .arg(p->version).arg(percent);
                    onGuiThread(p->mailbox, p->updater, [message](Updater* u) { emit u->statusMessage(message); });
                }, &progress);

                state->ready = update;
            }
        }
        catch (const std::exception& e)
        {
            error = QString::fromUtf8(e.what());
        }

        onGuiThread(state->mailbox, self, [userAsked, version, error](Updater* u) {
            u->onChecked(userAsked, version, error, false);
        });
    }).detach();
#else
    if (userAsked)
    {
        QMessageBox::information(_window, "Check for Updates",
            "This build of Phoenix was made without the updater.");
    }
#endif
}

void Updater::onChecked(bool userAsked, const QString& version, const QString& error, bool notInstalled)
{
    _busy = false;

    if (notInstalled)
    {
        if (userAsked)
        {
            QMessageBox::information(_window, "Check for Updates",
                "Phoenix can only update itself when it was installed with its installer.\n\n" + error);
        }
        return;
    }

    if (!error.isEmpty())
    {
        if (userAsked)
            QMessageBox::warning(_window, "Check for Updates", "Could not check for updates:\n\n" + error);
        else
            emit statusMessage("Could not check for updates");
        return;
    }

    if (version.isEmpty())
    {
        if (userAsked)
        {
            emit statusMessage(QString());
            QMessageBox::information(_window, "Check for Updates",
                QString("Phoenix %1 is the latest version.").arg(currentVersion()));
        }
        return;
    }

    emit statusMessage(QString("Phoenix %1 is ready to install").arg(version));
    offerRestart(version);
}

void Updater::offerRestart(const QString& version)
{
    // Not modal, so it can sit there while work carries on.
    QMessageBox* box = new QMessageBox(QMessageBox::Information, "Update Ready",
        QString("Phoenix %1 has been downloaded.\n\n"
                "Restart now to install it, or it will be installed when you quit.").arg(version),
        QMessageBox::NoButton, _window);
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* restart = box->addButton("Restart Now", QMessageBox::AcceptRole);
    box->addButton("Later", QMessageBox::RejectRole);
    box->setDefaultButton(restart);
    box->setWindowModality(Qt::NonModal);

    connect(restart, &QPushButton::clicked, this, [this]() {
        // Closing asks about unsaved changes; turning that down keeps
        // Phoenix open and the update waiting for the next quit.
        _restart = true;
        if (_window)
            _window->close();
        if (_window && _window->isVisible())
            _restart = false;
    });
    _offer = box;
    // A modal dialog up now (the start screen) would cover it and take
    // its clicks: it goes over that instead.
    if (QWidget* modal = QApplication::activeModalWidget(); modal && modal != box)
        placeOffer(modal);
    else
        box->show();
}

void Updater::placeOffer(QWidget* over)
{
    if (!_offer || !over)
        return;
    _offer->setParent(over, _offer->windowFlags());
    _offer->show();
    _offer->raise();
    _offer->activateWindow();
    // Back to the window before the dialog goes, which would take it along.
    if (QDialog* dialog = qobject_cast<QDialog*>(over))
    {
        connect(dialog, &QDialog::finished, this, [this]() {
            if (_offer && _window && _offer->parentWidget() != _window)
            {
                _offer->setParent(_window, _offer->windowFlags());
                _offer->show();
            }
        }, Qt::SingleShotConnection);
    }
}

void Updater::applyOnExit()
{
#ifdef PHOENIX_UPDATER
    if (_busy || !_state->ready || !_state->manager)
        return;
    try
    {
        // Shown when restarting, since the wait is the user's; silent when
        // quitting, since nobody is waiting for it.
        _state->manager->WaitExitThenApplyUpdates(*_state->ready, !_restart, _restart);
    }
    catch (const std::exception&)
    {
        // Left downloaded; the next check finds it again.
    }
#endif
}
