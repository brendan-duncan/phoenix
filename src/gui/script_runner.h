#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class MainWindow;
class OutputPanel;
class QDockWidget;
class QFileSystemWatcher;
class QLockFile;
class QMenu;
class JsflHost;

namespace jsfl {
class Engine;
}

/// Runs JSFL scripts in a window: the Commands menu, the Output panel, and
/// scripts handed over from the command line (`Phoenix script.jsfl`) by
/// another Phoenix process. Each run is one undo step named after the
/// script; Stop (or Esc) in the dialog that comes up for a long run ends it.
class ScriptRunner : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRunner(MainWindow& window);
    ~ScriptRunner() override;

    /// The user's scripts folder (the JSFL configuration folder) and its
    /// Commands folder, made when missing. Kept beside the settings, so
    /// PHOENIX_SETTINGS_DIR moves them too.
    static QString configFolder();
    static QString commandsFolder();

    /// When another Phoenix is running and takes scripts, hands it
    /// \a script to run and returns true.
    static bool handToRunning(const QString& script);

    QDockWidget* outputDock() const { return _outputDock; }
    OutputPanel* output() const { return _output; }

    /// The Commands menu, filled each time it opens.
    void fillCommandsMenu(QMenu* menu);

    /// Runs the script at \a path. False when it failed (the Output panel
    /// says why).
    bool runFile(const QString& path);

    /// Phoenix run-script: opens \a documents, runs \a script with no
    /// window, prints what it traces on standard output and errors on
    /// standard error. The exit code: 0, or 1 when the script failed.
    int runHeadless(const QString& script, const QStringList& documents, double timeLimit);

    bool running() const;

private:
    void runCommand();
    void manageCommands();
    /// Takes the scripts other processes left in the requests folder.
    void takeRequests();

    MainWindow& _window;
    std::unique_ptr<JsflHost> _host;
    std::unique_ptr<jsfl::Engine> _engine;
    OutputPanel* _output = nullptr;
    QDockWidget* _outputDock = nullptr;
    std::unique_ptr<QLockFile> _lock;
    QFileSystemWatcher* _watcher = nullptr;
    QStringList _pending;
};
