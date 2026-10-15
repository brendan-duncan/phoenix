#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QWidget;

/// Keeps an installed Phoenix up to date through Velopack. A check looks at
/// the GitHub releases for a newer version and, when there is one, downloads
/// it in the background and offers to restart into it; an update put off is
/// installed when Phoenix quits. A build that was not installed (one run
/// from the build folder) has nothing to update, and a build made without
/// PHOENIX_UPDATER never finds anything.
class Updater : public QObject
{
    Q_OBJECT

public:
    /// Velopack's start-up handling, which must run first thing in main():
    /// the installer and the updater start Phoenix with arguments of their
    /// own, which this answers and then exits.
    static void handleStartup();

    /// The version this build is, from PHOENIX_VERSION.
    static QString currentVersion();

    explicit Updater(QWidget* window);
    ~Updater() override;

    /// Looks for an update in the background. Asked for from the menu, it
    /// says what it found, even when that is nothing or an error; the
    /// check at start-up only speaks up when there is an update.
    void checkForUpdates(bool userAsked);

    /// Called as Phoenix closes: hands a downloaded update to the
    /// Velopack updater, which installs it once this process is gone, and
    /// starts Phoenix again when the restart was asked for.
    void applyOnExit();

    /// Puts the restart offer, when one is waiting, over \a over (a modal
    /// dialog such as the start screen), so it is neither hidden behind
    /// it nor blocked by it; it goes back to the window when \a over
    /// closes.
    void placeOffer(QWidget* over);

signals:
    /// A line for the status bar.
    void statusMessage(const QString& message);

private:
    struct State;

    void onChecked(bool userAsked, const QString& version, const QString& error, bool notInstalled);
    void offerRestart(const QString& version);

    QPointer<QWidget> _window;
    QPointer<QWidget> _offer;
    std::shared_ptr<State> _state;
    bool _busy = false;
    bool _restart = false;
};
