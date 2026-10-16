#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class MainWindow;
class OutputPanel;
class QAction;
class QDockWidget;
class QFileSystemWatcher;
class QLockFile;
class QMenu;
class JsflHost;

namespace fla {
class FLADocument;
}

namespace jsfl {
class Engine;
class Host;
struct RunOptions;
struct RunResult;
} // namespace jsfl

namespace python {
class Engine;
}

/// Runs JSFL and Python scripts in a window: the Commands menu, the Output
/// panel, and scripts handed over from the command line (`Phoenix
/// script.jsfl`) by another Phoenix process. Each run is one undo step
/// named after the script; Stop (or Esc) in the dialog that comes up for a
/// long run ends it.
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

    /// Where installed extensions keep their files, beside the settings.
    static QString extensionsFolder();
    /// Installs the extension package (a ZXP or a folder) at \a path,
    /// telling the person in a box over \a parent, or on the console when
    /// there is none.
    static bool installExtension(const QString& path, QWidget* parent);

    /// When another Phoenix is running and takes scripts, hands it
    /// \a script to run and returns true.
    static bool handToRunning(const QString& script);

    QDockWidget* outputDock() const { return _outputDock; }
    /// The host scripts reach the window through, for JSFL tools.
    jsfl::Host& jsflHost();
    /// Tells the JSFL scripts listening (fl.addEventListener) that \a type
    /// happened.
    void dispatchEvent(const char* type);
    /// Lets go of what scripts kept for \a document, which is closing.
    void forgetDocument(const fla::FLADocument* document);
    OutputPanel* output() const { return _output; }

    /// The Commands menu, filled each time it opens, with \a first at its
    /// top when given.
    void fillCommandsMenu(QMenu* menu, const QList<QAction*>& first = {});

    /// Runs the script (.jsfl or .py) at \a path. False when it failed
    /// (the Output panel says why).
    bool runFile(const QString& path);

    /// Whether \a path is a script Phoenix runs: JSFL, and Python when
    /// built with it.
    static bool isScript(const QString& path);

    /// Phoenix run-script: opens \a documents, runs \a script with no
    /// window, prints what it traces on standard output and errors on
    /// standard error. The exit code: 0, or 1 when the script failed.
    int runHeadless(const QString& script, const QStringList& documents, double timeLimit);

    /// Phoenix batch --jsfl: opens each of \a documents alone and runs
    /// \a script on it with no window, printing "== file" before each as
    /// batch does, what it traces on standard output and errors on standard
    /// error. With \a save, what the script changed is written back to the
    /// file; with \a out, a copy goes into that folder instead. Returns how
    /// many documents failed.
    int runBatch(const QString& script, const QStringList& documents, bool save, const QString& out, double timeLimit);

    bool running() const;

private:
    void runCommand();
    void manageCommands();
    void manageExtensions();
    /// Takes the scripts other processes left in the requests folder.
    void takeRequests();
    /// Runs \a path with the engine for its language.
    jsfl::RunResult run(const QString& path, const jsfl::RunOptions& options);
    void stop();

    MainWindow& _window;
    std::unique_ptr<JsflHost> _host;
    std::unique_ptr<jsfl::Engine> _engine;
    /// Python scripts' engine; null when Phoenix is built without them.
    std::unique_ptr<python::Engine> _python;
    OutputPanel* _output = nullptr;
    QDockWidget* _outputDock = nullptr;
    std::unique_ptr<QLockFile> _lock;
    QFileSystemWatcher* _watcher = nullptr;
    QStringList _pending;
};
