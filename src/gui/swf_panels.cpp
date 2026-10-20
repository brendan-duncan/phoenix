#include "swf_panels.h"

#include "output_panel.h"
#include "script_runner.h"
#include "test_movie_window.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../jsfl/panel_script.h"
#include "../parser/fla_parser.h"

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSet>

#include <algorithm>

/// A panel: its movie's file, its dock, and once shown, the movie and the
/// context its MMExecute calls run in.
struct SwfPanels::Panel
{
    QString path;
    QPointer<QDockWidget> dock;
    std::unique_ptr<fla::FLADocument> movie;
    std::unique_ptr<jsfl::PanelScript> script;
    bool started = false;
};

SwfPanels::SwfPanels(QMainWindow& window, ScriptRunner& scripts)
    : QObject(&window)
    , _window(window)
    , _scripts(scripts)
{
}

SwfPanels::~SwfPanels() = default;

QString SwfPanels::folder()
{
    return QDir(ScriptRunner::configFolder()).filePath("WindowSWF");
}

void SwfPanels::refresh()
{
    QSet<QString> present;
    const QDir dir(folder());
    for (const QFileInfo& file : dir.entryInfoList({"*.swf"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
    {
        const QString path = file.absoluteFilePath();
        present.insert(path);
        const bool known = std::any_of(_panels.begin(), _panels.end(),
            [&path](const std::shared_ptr<Panel>& panel) { return panel->path == path; });
        if (known)
            continue;
        auto panel = std::make_shared<Panel>();
        panel->path = path;
        auto* dock = new QDockWidget(file.completeBaseName(), &_window);
        dock->setObjectName("swfPanel:" + file.fileName());
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _window.addDockWidget(Qt::RightDockWidgetArea, dock);
        // Floating at first, as a tool palette is; it docks like any panel.
        dock->setFloating(true);
        dock->hide();
        panel->dock = dock;
        // Held weakly: a panel whose movie was taken out goes before its dock.
        std::weak_ptr<Panel> held = panel;
        connect(dock, &QDockWidget::visibilityChanged, this, [this, held](bool shown) {
            if (const std::shared_ptr<Panel> live = held.lock(); live && shown)
                start(*live);
        });
        _panels.push_back(std::move(panel));
    }
    // Movies taken out of the folder since go, and their docks.
    for (auto it = _panels.begin(); it != _panels.end();)
    {
        if (present.contains((*it)->path))
        {
            ++it;
            continue;
        }
        if ((*it)->dock)
            (*it)->dock->deleteLater();
        it = _panels.erase(it);
    }
}

bool SwfPanels::fillMenu(QMenu* menu)
{
    refresh();
    for (const std::shared_ptr<Panel>& panel : _panels)
    {
        QDockWidget* dock = panel->dock;
        if (!dock)
            continue;
        QAction* action = menu->addAction(dock->windowTitle());
        action->setCheckable(true);
        action->setChecked(dock->isVisible());
        action->setStatusTip(QString("The SWF panel %1").arg(QFileInfo(panel->path).fileName()));
        const QString path = panel->path;
        connect(action, &QAction::triggered, this, [this, path, dock](bool on) {
            if (!on)
                dock->close();
            else
                open(path);
        });
    }
    return !_panels.empty();
}

bool SwfPanels::open(const QString& path)
{
    refresh();
    for (const std::shared_ptr<Panel>& panel : _panels)
    {
        if (panel->path != path || !panel->dock)
            continue;
        panel->dock->show();
        panel->dock->raise();
        if (panel->dock->isFloating())
            panel->dock->activateWindow();
        start(*panel);
        return true;
    }
    return false;
}

void SwfPanels::start(Panel& panel)
{
    if (panel.started || !panel.dock)
        return;
    panel.started = true;
    const QString name = panel.dock->windowTitle();
    FLAParser parser;
    panel.movie.reset(parser.parse(panel.path.toStdString()));
    if (!panel.movie || !panel.movie->document)
    {
        auto* label = new QLabel(QString("The SWF panel %1 could not be read.").arg(QFileInfo(panel.path).fileName()));
        label->setEnabled(false);
        label->setAlignment(Qt::AlignCenter);
        panel.dock->setWidget(label);
        return;
    }
    // What the panel traces, and the errors its scripts stop on, go to the
    // Output panel.
    OutputPanel* output = _scripts.output();
    auto* player = new TestMovieWindow(*panel.movie, name,
        [output](const QString& line) {
            if (output)
                output->append(line);
        },
        [output, name](const QString& message, const fla::DOMElement*, int) {
            if (output)
                output->appendError(name + ": " + message);
        },
        panel.dock, panel.path);
    player->setWindowFlags(Qt::Widget);
    // MMExecute: JSFL run as a command is, in a context the panel keeps, so
    // what one call defines the next can use. Its value comes back as text.
    std::weak_ptr<Panel> held;
    for (const std::shared_ptr<Panel>& each : _panels)
        if (each.get() == &panel)
            held = each;
    player->setPanel([this, held, name](const std::string& code) -> std::string {
        const std::shared_ptr<Panel> live = held.lock();
        if (!live)
            return std::string();
        if (!live->script)
            live->script = _scripts.panelScript(name);
        if (!live->script)
            return std::string();
        const jsfl::PanelScript::Result result = _scripts.evaluate(*live->script, QString::fromStdString(code), name);
        return result.ok ? result.value : std::string();
    });
    panel.dock->setWidget(player);
    // The movie at its own size to start with.
    const fla::Document& data = *panel.movie->document;
    if (data.width > 0 && data.height > 0)
        panel.dock->resize(static_cast<int>(data.width), static_cast<int>(data.height));
}
