#include "script_runner.h"

#include "autosave.h"
#include "main_window.h"
#include "output_panel.h"
#include "script_api.h"
#include "xul_dialog.h"

#include "../data/document.h"
#include "../data/fla_document.h"
#include "../data/layer.h"
#include "../data/symbol.h"
#include "../data/timeline.h"
#include "../edit/new_document.h"
#include "../edit/selection.h"
#include "../edit/timeline_edit.h"
#include "../jsfl/engine.h"
#include "../jsfl/flfile.h"
#include "../parser/extension_package.h"
#include "../parser/fla_parser.h"
#include "../phx/container.h"
#include "../phx/phx_document.h"
#include "../python/python_engine.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>
#include <cstdint>

namespace {

QString settingsBase()
{
    // Beside settings kept apart (PHOENIX_SETTINGS_DIR), or in Phoenix's
    // own data folder.
    const QString apart = qEnvironmentVariable("PHOENIX_SETTINGS_DIR");
    return !apart.isEmpty() ? apart : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString requestsFolder()
{
    return QDir(settingsBase()).filePath("script-requests");
}

QString lockPath()
{
    return QDir(settingsBase()).filePath("scripts.lock");
}

/// No one to answer a dialog: the command line, or a window drawn offscreen.
bool noOneToAsk(const MainWindow& window)
{
    const QString platform = QGuiApplication::platformName();
    return !window.isVisible() || platform == "offscreen" || platform == "minimal";
}

std::string text(const QString& value)
{
    return value.toUtf8().toStdString();
}

QString qtext(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

/// The scripts the Commands folder lists.
QStringList scriptPatterns()
{
#ifdef PHOENIX_PYTHON
    return {"*.jsfl", "*.py"};
#else
    return {"*.jsfl"};
#endif
}

bool isPython(const QString& path)
{
    return path.endsWith(".py", Qt::CaseInsensitive);
}

} // namespace

/// The object model's view of a window: its tabs, history, selection,
/// stage and dialogs. MainWindow makes it a friend.
class JsflHost : public jsfl::Host
{
public:
    JsflHost(MainWindow& window, OutputPanel* output)
        : _window(window)
        , _output(output)
    {}

    /// Where trace output goes when there is no panel to show it.
    std::function<void(const std::string&)> print;
    /// What a file dialog answers on the command line.
    QString fileAnswer;

    // Output and dialogs.
    void trace(const std::string& message) override
    {
        if (print)
            print(message);
        else if (_output)
            _output->append(qtext(message));
        _text += message + "\n";
    }
    void clearOutput() override
    {
        _text.clear();
        if (_output && !print)
            _output->clear();
    }
    std::string outputText() override { return _output && !print ? text(_output->text()) + "\n" : _text; }
    void alert(const std::string& message) override
    {
        if (noOneToAsk(_window))
        {
            trace(message);
            return;
        }
        QMessageBox::information(&_window, "Phoenix", qtext(message));
    }
    bool confirm(const std::string& message) override
    {
        if (noOneToAsk(_window))
            return true;
        return QMessageBox::question(&_window, "Phoenix", qtext(message), QMessageBox::Ok | QMessageBox::Cancel) ==
               QMessageBox::Ok;
    }
    bool prompt(const std::string& message, const std::string& initial, std::string& answer) override
    {
        if (noOneToAsk(_window))
        {
            answer = initial;
            return true;
        }
        bool ok = false;
        const QString typed = QInputDialog::getText(&_window, "Phoenix", qtext(message), QLineEdit::Normal, qtext(initial), &ok);
        answer = text(typed);
        return ok;
    }
    std::string browseForFile(const std::string& mode, const std::string& title) override
    {
        if (noOneToAsk(_window))
            return mode == "open" || mode == "select" ? text(fileAnswer) : std::string();
        const QString caption = title.empty() ? QString("Choose a File") : qtext(title);
        const QString path = mode == "save" ? QFileDialog::getSaveFileName(&_window, caption)
                                            : QFileDialog::getOpenFileName(&_window, caption);
        return text(path);
    }
    std::string browseForFolder(const std::string& title) override
    {
        if (noOneToAsk(_window))
            return {};
        return text(QFileDialog::getExistingDirectory(&_window, title.empty() ? QString("Choose a Folder") : qtext(title)));
    }
    bool showDialog(jsfl::xul::Dialog& dialog) override
    {
        if (noOneToAsk(_window))
            return false;
        // The run's wait cursor and its Stop dialog stay away while the
        // person works in the dialog.
        ++dialogsOpen;
        QApplication::setOverrideCursor(Qt::ArrowCursor);
        {
            XulDialog window(dialog, &_window);
            window.exec();
        }
        QApplication::restoreOverrideCursor();
        --dialogsOpen;
        ++dialogsShown;
        return true;
    }
    /// Dialogs open now, and shown so far.
    int dialogsOpen = 0;
    int dialogsShown = 0;
    std::string configDirectory() override { return text(ScriptRunner::configFolder()); }
    std::string version() override { return text(QCoreApplication::applicationVersion()); }

    // Documents.
    std::vector<fla::FLADocument*> documents() override
    {
        std::vector<fla::FLADocument*> list;
        for (size_t i = 0; i < _window._tabs.size(); ++i)
        {
            fla::FLADocument* document = documentAt(static_cast<int>(i));
            if (document)
                list.push_back(document);
        }
        return list;
    }
    fla::FLADocument* activeDocument() override { return _window._flaDocument; }
    void activate(fla::FLADocument* document) override
    {
        const int index = tabOf(document);
        if (index >= 0 && document != _window._flaDocument)
            _window.activateTab(index);
    }
    fla::FLADocument* open(const std::string& path, std::string& error) override
    {
        const QString wanted = QFileInfo(qtext(path)).absoluteFilePath();
        for (size_t i = 0; i < _window._tabs.size(); ++i)
        {
            const QString tabPath = _window.tabPath(*_window._tabs[i]);
            if (!tabPath.isEmpty() && QFileInfo(tabPath).absoluteFilePath() == wanted)
            {
                _window.activateTab(static_cast<int>(i));
                return _window._flaDocument;
            }
        }
        if (!QFileInfo::exists(wanted))
        {
            error = "There is no document at " + path + ".";
            return nullptr;
        }
        // Read without the dialogs File > Open shows: what it would warn
        // about goes to the output.
        fla::FLADocument* document = nullptr;
        bool readOnly = false;
        std::string warning;
        if (phx::isPhx(text(wanted)))
            document = phx::load(text(wanted), error, warning, readOnly);
        else
        {
            FLAParser parser;
            document = parser.parse(text(wanted));
            error = parser.errorString();
            warning = parser.warningString();
        }
        if (!document || !document->document)
        {
            delete document;
            if (error.empty())
                error = "Could not read " + path + ".";
            return nullptr;
        }
        error.clear();
        if (!warning.empty())
            trace(QFileInfo(wanted).fileName().toStdString() + ": " + warning);
        _window.makeRoomForDocument();
        _window.adoptDocument(document, wanted, QFileInfo(wanted).fileName());
        if (readOnly)
        {
            _window._documentPath.clear();
            _window._documentName = QFileInfo(wanted).fileName() + " (read-only)";
        }
        if (_window._interactive)
            _window.addToRecentFiles(wanted);
        _window.updateEditState();
        return _window._flaDocument;
    }
    fla::FLADocument* create(std::string& error) override
    {
        _window.makeRoomForDocument();
        _window.createNewDocument(fla::kDefaultDocumentWidth, fla::kDefaultDocumentHeight, fla::kDefaultDocumentFrameRate, Qt::white);
        if (!_window._flaDocument)
            error = "A new document could not be made.";
        return _window._flaDocument;
    }
    bool save(fla::FLADocument* document, const std::string& path, bool copy, std::string& error) override
    {
        const int index = tabOf(document);
        if (index < 0 || !document->document)
        {
            error = "That document is not open.";
            return false;
        }
        MainWindow::DocumentTab& tab = *_window._tabs[static_cast<size_t>(index)];
        const bool shown = document == _window._flaDocument;
        const QString target = path.empty() ? (shown ? _window._documentPath : tab.path) : QFileInfo(qtext(path)).absoluteFilePath();
        if (target.isEmpty())
        {
            error = "The document has never been saved; give a file to save it to.";
            return false;
        }
        if (shown && _window._phoenixView)
        {
            _window._phoenixView->exitGroups();
            _window._phoenixView->flushPendingMerge();
        }
        if (tab.autoSave)
            tab.autoSave->waitForWrite();
        QString problem;
        if (!_window.writeDocumentFile(*document->document, target, problem))
        {
            error = text(target) + " could not be written: " + text(problem);
            return false;
        }
        if (copy)
            return true;
        if (shown)
        {
            _window._documentPath = target;
            _window._documentName = QFileInfo(target).fileName();
            _window._editContext.markSaved();
            _window.updateEditState();
        }
        else
        {
            tab.path = target;
            tab.name = QFileInfo(target).fileName();
            tab.history.setClean();
        }
        document->fromBinary = false;
        if (tab.autoSave)
            tab.autoSave->discard();
        if (_window._interactive)
            _window.addToRecentFiles(target);
        _window.refreshTabs();
        return true;
    }
    bool close(fla::FLADocument* document, bool askToSave) override
    {
        const int index = tabOf(document);
        if (index < 0)
            return false;
        _selections.erase(document);
        _edited.erase(document);
        return _window.closeTab(index, askToSave && !noOneToAsk(_window));
    }
    std::string path(fla::FLADocument* document) override
    {
        const int index = tabOf(document);
        return index < 0 ? std::string() : text(_window.tabPath(*_window._tabs[static_cast<size_t>(index)]));
    }
    std::string name(fla::FLADocument* document) override
    {
        const int index = tabOf(document);
        return index < 0 ? std::string() : text(_window.tabName(*_window._tabs[static_cast<size_t>(index)]));
    }
    bool modified(fla::FLADocument* document) override
    {
        const int index = tabOf(document);
        return index >= 0 && _window.tabModified(*_window._tabs[static_cast<size_t>(index)]);
    }
    bool readOnly(fla::FLADocument* document) override
    {
        const int index = tabOf(document);
        return index >= 0 && _window.tabName(*_window._tabs[static_cast<size_t>(index)]).endsWith(" (read-only)");
    }

    // Editing.
    fla::CommandStack& history(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument)
            return _window._editContext.commandStack();
        return _window._tabs[static_cast<size_t>(tabOf(document))]->history;
    }
    fla::Selection& selection(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument)
            return _window._selection;
        std::unique_ptr<fla::Selection>& own = _selections[document];
        if (!own)
            own = std::make_unique<fla::Selection>();
        return *own;
    }
    void changed(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument)
            _boundsStale = true;
    }
    void refresh() override
    {
        _window.refreshAfterScript();
        _window.refreshTabs();
    }

    // Where editing happens.
    fla::Timeline* editedTimeline(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument)
            return _window.editedTimeline();
        const auto found = _edited.find(document);
        if (found != _edited.end() && found->second)
            return found->second;
        return document && document->document ? document->document->currentScene() : nullptr;
    }
    void editTimeline(fla::FLADocument* document, fla::Timeline* timeline) override
    {
        if (document != _window._flaDocument)
        {
            _edited[document] = timeline;
            return;
        }
        fla::Document& data = *document->document;
        const auto scene = std::find(data.timelines.begin(), data.timelines.end(), timeline);
        if (scene != data.timelines.end())
        {
            if (_window._phoenixView)
                _window._phoenixView->exitToDepth(0);
            _window.goToScene(static_cast<int>(scene - data.timelines.begin()));
        }
        else if (auto* symbol = dynamic_cast<fla::Symbol*>(timeline ? timeline->parent : nullptr))
        {
            if (_window._phoenixView)
                _window._phoenixView->editSymbol(symbol);
        }
        _window.refreshAfterScript();
    }
    int currentFrame(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument && _window._player)
            return _window._player->currentFrame();
        return _frames[document];
    }
    void setCurrentFrame(fla::FLADocument* document, int frame) override
    {
        if (document == _window._flaDocument && _window._player)
            _window._player->setCurrentFrame(frame);
        else
            _frames[document] = frame;
    }
    int currentLayer(fla::FLADocument* document) override
    {
        if (document == _window._flaDocument && _window._timelineView)
        {
            fla::Timeline* timeline = nullptr;
            int index = 0;
            if (_window._timelineView->currentLayer(timeline, index) && timeline == editedTimeline(document))
                return index;
            return 0;
        }
        return _layers[document];
    }
    void setCurrentLayer(fla::FLADocument* document, int layer) override
    {
        fla::Timeline* timeline = editedTimeline(document);
        if (document == _window._flaDocument && _window._timelineView && timeline && layer >= 0 &&
            layer < static_cast<int>(timeline->layers.size()))
            _window._timelineView->setCurrentLayer(timeline->layers[static_cast<size_t>(layer)]);
        else
            _layers[document] = layer;
    }
    std::vector<jsfl::FrameRange> selectedFrames(fla::FLADocument* document) override
    {
        std::vector<jsfl::FrameRange> ranges;
        fla::Timeline* timeline = editedTimeline(document);
        if (document != _window._flaDocument || !_window._timelineView || !timeline)
            return ranges;
        const FrameSelection chosen = _window._timelineView->frameSelection();
        for (const fla::Layer* layer : chosen.layers)
        {
            const auto at = std::find(timeline->layers.begin(), timeline->layers.end(), layer);
            if (at != timeline->layers.end())
                ranges.push_back({static_cast<int>(at - timeline->layers.begin()), chosen.first, chosen.last});
        }
        for (const auto& cell : chosen.cells)
        {
            const auto at = std::find(timeline->layers.begin(), timeline->layers.end(), cell.first);
            if (at != timeline->layers.end())
                ranges.push_back({static_cast<int>(at - timeline->layers.begin()), cell.second, cell.second});
        }
        return ranges;
    }

    // Drawing.
    fla::DrawingStyle* drawingStyle() override { return &_window._drawingStyle; }
    void copyText(const std::string& value) override { QApplication::clipboard()->setText(qtext(value)); }
    jsfl::Rect bounds(fla::FLADocument*, const fla::Element* element) override
    {
        PhoenixView* view = _window._phoenixView;
        if (!view)
            return {};
        if (_boundsStale)
        {
            view->invalidateBounds();
            _boundsStale = false;
        }
        const QRectF box = view->elementBounds(element);
        if (!box.isValid())
            return {};
        return {box.left(), box.top(), box.right(), box.bottom()};
    }
    bool bitmapPixels(fla::FLADocument* document, const std::string& item, int& width, int& height,
        std::vector<uint8_t>& rgba) override
    {
        activate(document);
        PhoenixView* view = _window._phoenixView;
        if (document != _window._flaDocument || !view)
            return false;
        const QImage image = view->libraryBitmap(qtext(item)).toImage().convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull())
            return false;
        width = image.width();
        height = image.height();
        rgba.resize(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * width * 4, image.constScanLine(y), static_cast<size_t>(width) * 4);
        return true;
    }
    bool exportPNG(fla::FLADocument* document, const std::string& path, bool currentFrameOnly, std::string& error) override
    {
        activate(document);
        PhoenixView* view = _window._phoenixView;
        if (document != _window._flaDocument || !view || !document->document)
        {
            error = "Only a document that can be shown can be exported.";
            return false;
        }
        view->invalidateBounds();
        view->clearCaches();
        const fla::Document& data = *document->document;
        const QSize size(std::max(1, data.width), std::max(1, data.height));
        const QString target = qtext(path);
        if (currentFrameOnly)
        {
            const QImage image = view->renderFrame(currentFrame(document), size, false);
            if (image.isNull() || !image.save(target, "PNG"))
            {
                error = path + " could not be written.";
                return false;
            }
            return true;
        }
        // Every frame of the scene, numbered from 1: frame0001.png...
        int count = 1;
        if (const fla::Timeline* scene = data.currentScene())
        {
            for (const fla::Layer* layer : scene->layers)
            {
                if (layer)
                    count = std::max(count, fla::timeline::layerEnd(*layer));
            }
        }
        const QFileInfo info(target);
        const QString stem = info.dir().filePath(info.completeBaseName());
        for (int frame = 0; frame < count; ++frame)
        {
            const QImage image = view->renderFrame(frame, size, false);
            const QString file = QString("%1%2.png").arg(stem).arg(frame + 1, 4, 10, QChar('0'));
            if (image.isNull() || !image.save(file, "PNG"))
            {
                error = text(file) + " could not be written.";
                return false;
            }
        }
        return true;
    }
    bool exportSWF(fla::FLADocument* document, const std::string& path, std::string& error) override
    {
        activate(document);
        if (document != _window._flaDocument || !_window._phoenixView || !document->document)
        {
            error = "Only a document that can be shown can be exported.";
            return false;
        }
        const fla::Document& data = *document->document;
        QString problem;
        if (!_window.exportSwf(QFileInfo(qtext(path)).absoluteFilePath(), QSize(std::max(1, data.width), std::max(1, data.height)), problem))
        {
            error = problem.isEmpty() ? path + " could not be written." : text(problem);
            return false;
        }
        return true;
    }
    bool publish(fla::FLADocument* document, std::vector<std::string>& written, std::string& error) override
    {
        activate(document);
        if (document != _window._flaDocument || !_window._phoenixView || !document->document)
        {
            error = "Only a document that can be shown can be published.";
            return false;
        }
        QStringList files;
        QStringList problems;
        const bool ok = _window.publishFiles(files, problems);
        for (const QString& file : files)
            written.push_back(text(file));
        if (!ok)
            error = text(problems.join("\n"));
        return ok;
    }

    bool callOperation(const std::string& name, const std::string& argumentsJson, std::string& resultJson,
        std::string& error) override
    {
        if (!_api)
            _api = std::make_unique<ScriptApi>(_window);
        if (name.empty())
        {
            // The operations there are, for fl.phoenix.operations().
            QJsonArray list;
            for (const ScriptApi::Operation& operation : _api->operations())
                list.append(QJsonObject{{"name", operation.name}, {"title", operation.title},
                    {"description", operation.description}, {"arguments", operation.schema}, {"changes", operation.writes}});
            resultJson = QJsonDocument(list).toJson(QJsonDocument::Compact).toStdString();
            return true;
        }
        const QJsonDocument arguments = QJsonDocument::fromJson(QByteArray::fromStdString(argumentsJson));
        if (!arguments.isObject())
        {
            error = "An operation's arguments are an object.";
            return false;
        }
        const ScriptApi::Result result = _api->call(qtext(name), arguments.object());
        if (!result.ok())
        {
            error = text(result.error);
            return false;
        }
        resultJson = result.value.isArray() ? QJsonDocument(result.value.toArray()).toJson(QJsonDocument::Compact).toStdString()
            : result.value.isObject()       ? QJsonDocument(result.value.toObject()).toJson(QJsonDocument::Compact).toStdString()
                                            : std::string("null");
        return true;
    }

private:
    fla::FLADocument* documentAt(int index) const
    {
        if (index == _window._activeTab)
            return _window._flaDocument;
        return _window._tabs[static_cast<size_t>(index)]->document;
    }
    int tabOf(const fla::FLADocument* document) const
    {
        for (size_t i = 0; document && i < _window._tabs.size(); ++i)
        {
            if (documentAt(static_cast<int>(i)) == document)
                return static_cast<int>(i);
        }
        return -1;
    }

    MainWindow& _window;
    OutputPanel* _output;
    std::unique_ptr<ScriptApi> _api;
    std::string _text;
    bool _boundsStale = false;
    std::map<const fla::FLADocument*, std::unique_ptr<fla::Selection>> _selections;
    std::map<const fla::FLADocument*, fla::Timeline*> _edited;
    std::map<const fla::FLADocument*, int> _frames;
    std::map<const fla::FLADocument*, int> _layers;
};

ScriptRunner::ScriptRunner(MainWindow& window)
    : QObject(&window)
    , _window(window)
{
    _output = new OutputPanel(&window);
    _outputDock = new QDockWidget("Output", &window);
    _outputDock->setObjectName("outputDock");
    _outputDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _outputDock->setWidget(_output);
    window.addDockWidget(Qt::BottomDockWidgetArea, _outputDock);
    _outputDock->hide();
    connect(_output, &OutputPanel::written, this, [this]() {
        if (!_outputDock->isVisible())
            _outputDock->show();
        _outputDock->raise();
    });

    _host = std::make_unique<JsflHost>(window, _output);
    _engine = std::make_unique<jsfl::Engine>(*_host);
#ifdef PHOENIX_PYTHON
    // Python itself is only looked for when a Python script first runs.
    _python = std::make_unique<python::Engine>(*_host);
#endif

    // The first window that people work in takes scripts other Phoenix
    // processes hand over (Phoenix script.jsfl with Phoenix open).
    if (window._interactive && !window._secondary)
    {
        QDir().mkpath(settingsBase());
        auto lock = std::make_unique<QLockFile>(lockPath());
        lock->setStaleLockTime(0);
        if (lock->tryLock(0))
        {
            _lock = std::move(lock);
            QDir().mkpath(requestsFolder());
            _watcher = new QFileSystemWatcher({requestsFolder()}, this);
            connect(_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { takeRequests(); });
            QTimer::singleShot(0, this, [this]() { takeRequests(); });
        }
    }
}

ScriptRunner::~ScriptRunner() = default;

jsfl::Host& ScriptRunner::jsflHost()
{
    return *_host;
}

void ScriptRunner::dispatchEvent(const char* type)
{
    if (_engine)
        _engine->dispatch(type);
    if (_python)
        _python->dispatch(type);
}

QString ScriptRunner::configFolder()
{
    const QString folder = QDir(settingsBase()).filePath("Configuration");
    QDir().mkpath(QDir(folder).filePath("Commands"));
    QDir().mkpath(QDir(folder).filePath("Tools"));
    return folder;
}

QString ScriptRunner::commandsFolder()
{
    return QDir(configFolder()).filePath("Commands");
}

bool ScriptRunner::handToRunning(const QString& script)
{
    QLockFile lock(lockPath());
    lock.setStaleLockTime(0);
    if (lock.tryLock(0))
    {
        // No Phoenix holds it: this one runs the script itself.
        lock.unlock();
        return false;
    }
    if (lock.error() != QLockFile::LockFailedError)
        return false;
    QDir().mkpath(requestsFolder());
    const QString request = QDir(requestsFolder()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".request");
    QFile file(request + ".part");
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QFileInfo(script).absoluteFilePath().toUtf8());
    file.close();
    // Named once it is whole, so the running Phoenix never reads half of it.
    return QFile::rename(request + ".part", request);
}

void ScriptRunner::takeRequests()
{
    const QDir folder(requestsFolder());
    for (const QFileInfo& info : folder.entryInfoList({"*.request"}, QDir::Files, QDir::Time | QDir::Reversed))
    {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString script = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        file.remove();
        if (!script.isEmpty())
            _pending << script;
    }
    if (_pending.isEmpty())
        return;
    // One at a time, when nothing modal is up.
    QTimer::singleShot(0, this, [this]() {
        if (running() || QApplication::activeModalWidget())
        {
            QTimer::singleShot(500, this, [this]() { takeRequests(); });
            return;
        }
        if (_pending.isEmpty())
            return;
        const QString script = _pending.takeFirst();
        _window.raise();
        _window.activateWindow();
        runFile(script);
        if (!_pending.isEmpty())
            QTimer::singleShot(0, this, [this]() { takeRequests(); });
    });
}

bool ScriptRunner::running() const
{
#ifdef PHOENIX_PYTHON
    if (_python->running())
        return true;
#endif
    return _engine->running();
}

bool ScriptRunner::isScript(const QString& path)
{
#ifdef PHOENIX_PYTHON
    if (isPython(path))
        return true;
#endif
    return path.endsWith(".jsfl", Qt::CaseInsensitive);
}

jsfl::RunResult ScriptRunner::run(const QString& path, const jsfl::RunOptions& options)
{
    const std::string file = QFileInfo(path).absoluteFilePath().toStdString();
    if (isPython(path))
    {
#ifdef PHOENIX_PYTHON
        return _python->runFile(file, options);
#else
        jsfl::RunResult result;
        result.ok = false;
        result.file = file;
        result.message = "This Phoenix was built without Python scripts.";
        return result;
#endif
    }
    return _engine->runFile(file, options);
}

void ScriptRunner::stop()
{
    _engine->stop();
#ifdef PHOENIX_PYTHON
    _python->stop();
#endif
}

bool ScriptRunner::runFile(const QString& path)
{
    if (running())
        return false;
    // What is being typed or drawn goes into the document first.
    _window.settleGestures();
    if (_window._phoenixView)
        _window._phoenixView->flushPendingMerge();

    jsfl::RunOptions options;
    options.undoName = QFileInfo(path).completeBaseName().toStdString();
    // A long run brings up a dialog whose Stop (or Esc) ends it.
    QElapsedTimer clock;
    clock.start();
    std::unique_ptr<QProgressDialog> progress;
    int dialogsShown = _host->dialogsShown;
    options.poll = [&]() {
        if (noOneToAsk(_window))
            return;
        // Time the person spends in a script's dialog is not the script
        // running long.
        if (_host->dialogsOpen > 0 || _host->dialogsShown != dialogsShown)
        {
            dialogsShown = _host->dialogsShown;
            clock.restart();
            if (_host->dialogsOpen > 0)
                return;
        }
        if (!progress && clock.elapsed() > 1000)
        {
            progress = std::make_unique<QProgressDialog>(QString("Running %1...").arg(QFileInfo(path).fileName()), "Stop", 0, 0, &_window);
            progress->setWindowTitle("Phoenix");
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->show();
        }
        if (progress)
        {
            QCoreApplication::processEvents();
            if (progress->wasCanceled())
                stop();
        }
    };
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const jsfl::RunResult result = run(path, options);
    QApplication::restoreOverrideCursor();
    progress.reset();
    if (!result.ok)
    {
        QString report = QString::fromStdString(result.report());
        if (!result.stack.empty() && !result.interrupted)
            report += "\n" + QString::fromStdString(result.stack).trimmed();
        _output->appendError(report);
    }
    return result.ok;
}

int ScriptRunner::runHeadless(const QString& script, const QStringList& documents, double timeLimit)
{
    _host->print = [](const std::string& line) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    };
    for (const QString& document : documents)
    {
        std::string error;
        if (!_host->open(QFileInfo(document).absoluteFilePath().toStdString(), error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    }
    if (!documents.isEmpty())
        _host->fileAnswer = QFileInfo(documents.front()).absoluteFilePath();
    jsfl::RunOptions options;
    options.undoName = QFileInfo(script).completeBaseName().toStdString();
    options.timeLimit = timeLimit;
    const jsfl::RunResult result = run(script, options);
    if (!result.ok)
    {
        std::fprintf(stderr, "%s\n", result.report().c_str());
        if (!result.stack.empty() && !result.interrupted)
            std::fprintf(stderr, "%s\n", result.stack.c_str());
        return 1;
    }
    return 0;
}

int ScriptRunner::runBatch(const QString& script, const QStringList& documents, bool save, const QString& out, double timeLimit)
{
    _host->print = [](const std::string& line) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    };
    int failed = 0;
    for (const QString& file : documents)
    {
        std::printf("== %s\n", qPrintable(file));
        std::fflush(stdout);
        const QString path = QFileInfo(file).absoluteFilePath();
        std::string error;
        fla::FLADocument* document = _host->open(path.toStdString(), error);
        if (!document)
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            ++failed;
            continue;
        }
        _host->fileAnswer = path;
        jsfl::RunOptions options;
        options.undoName = QFileInfo(script).completeBaseName().toStdString();
        options.timeLimit = timeLimit;
        const jsfl::RunResult result = run(script, options);
        bool ok = result.ok;
        if (!ok)
        {
            std::fprintf(stderr, "%s\n", result.report().c_str());
            if (!result.stack.empty() && !result.interrupted)
                std::fprintf(stderr, "%s\n", result.stack.c_str());
        }
        // The script may have closed the document itself.
        const std::vector<fla::FLADocument*> open = _host->documents();
        const bool stillOpen = std::find(open.begin(), open.end(), document) != open.end();
        if (ok && stillOpen && (save || !out.isEmpty()))
        {
            const QString target = !out.isEmpty() ? QDir(out).filePath(QFileInfo(path).fileName()) : path;
            if ((!out.isEmpty() || _host->modified(document)) && !_host->save(document, target.toStdString(), !out.isEmpty(), error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                ok = false;
            }
        }
        if (stillOpen)
            _host->close(document, false);
        failed += ok ? 0 : 1;
    }
    return failed;
}

void ScriptRunner::fillCommandsMenu(QMenu* menu, const QList<QAction*>& first)
{
    connect(menu, &QMenu::aboutToShow, this, [this, menu, first]() {
        menu->clear();
        if (!first.isEmpty())
        {
            menu->addActions(first);
            menu->addSeparator();
        }
        QAction* runAction = menu->addAction("&Run Command...");
        runAction->setStatusTip("Run a script (JSFL or Python) on the open document, as one step to undo");
        connect(runAction, &QAction::triggered, this, &ScriptRunner::runCommand);
        menu->addSeparator();
        // The user's own commands: the scripts in the Commands folder.
        QDir folder(commandsFolder());
        const QFileInfoList scripts = folder.entryInfoList(scriptPatterns(), QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& script : scripts)
        {
            QAction* action = menu->addAction(script.completeBaseName());
            action->setStatusTip(QString("Run %1").arg(script.fileName()));
            const QString path = script.absoluteFilePath();
            connect(action, &QAction::triggered, this, [this, path]() { runFile(path); });
        }
        if (!scripts.isEmpty())
            menu->addSeparator();
        QAction* manage = menu->addAction("&Manage Saved Commands...");
        manage->setStatusTip("Rename or delete the commands in your Commands folder");
        connect(manage, &QAction::triggered, this, &ScriptRunner::manageCommands);
        QAction* extensionsAction = menu->addAction("Manage E&xtensions...");
        extensionsAction->setStatusTip("Install extension packages (ZXP, or a folder) whose commands and tools join Phoenix, or remove them");
        connect(extensionsAction, &QAction::triggered, this, &ScriptRunner::manageExtensions);
        QAction* openFolder = menu->addAction("&Open Commands Folder");
        openFolder->setStatusTip("Show the folder whose scripts this menu lists; put .jsfl or .py files there to add commands");
        connect(openFolder, &QAction::triggered, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(commandsFolder())); });
    });
    // Filled once now too, so its keys and the shortcut list see it.
    emit menu->aboutToShow();
}

void ScriptRunner::runCommand()
{
    QSettings settings;
    const QString start = settings.value("scripts/lastFolder", commandsFolder()).toString();
#ifdef PHOENIX_PYTHON
    const QString filter = "Scripts (*.jsfl *.py);;JSFL scripts (*.jsfl);;Python scripts (*.py);;All files (*)";
#else
    const QString filter = "JSFL scripts (*.jsfl);;All files (*)";
#endif
    const QString path = QFileDialog::getOpenFileName(&_window, "Run Command", start, filter);
    if (path.isEmpty())
        return;
    settings.setValue("scripts/lastFolder", QFileInfo(path).absolutePath());
    runFile(path);
}

void ScriptRunner::manageCommands()
{
    QDialog dialog(&_window);
    dialog.setWindowTitle("Manage Saved Commands");
    QListWidget* list = new QListWidget(&dialog);
    const auto fill = [list]() {
        list->clear();
        const QFileInfoList scripts = QDir(commandsFolder()).entryInfoList(scriptPatterns(), QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& script : scripts)
        {
            QListWidgetItem* item = new QListWidgetItem(script.completeBaseName(), list);
            item->setData(Qt::UserRole, script.absoluteFilePath());
        }
    };
    fill();
    QPushButton* rename = new QPushButton("&Rename...", &dialog);
    QPushButton* remove = new QPushButton("&Delete", &dialog);
    connect(rename, &QPushButton::clicked, &dialog, [&dialog, list, fill]() {
        QListWidgetItem* item = list->currentItem();
        if (!item)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(&dialog, "Rename Command", "Name:", QLineEdit::Normal, item->text(), &ok).trimmed();
        if (!ok || name.isEmpty() || name == item->text())
            return;
        if (name.contains('/') || name.contains('\\'))
        {
            QMessageBox::warning(&dialog, "Rename Command", "A command's name cannot hold a slash.");
            return;
        }
        const QString from = item->data(Qt::UserRole).toString();
        const QString to = QDir(commandsFolder()).filePath(name + "." + QFileInfo(from).suffix());
        if (QFileInfo::exists(to) || !QFile::rename(from, to))
            QMessageBox::warning(&dialog, "Rename Command", QString("%1 could not be renamed to %2.").arg(item->text(), name));
        fill();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&dialog, list, fill]() {
        QListWidgetItem* item = list->currentItem();
        if (!item)
            return;
        if (QMessageBox::question(&dialog, "Delete Command", QString("Delete the command %1?").arg(item->text())) != QMessageBox::Yes)
            return;
        QFile::remove(item->data(Qt::UserRole).toString());
        fill();
    });
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* side = new QVBoxLayout();
    side->addWidget(rename);
    side->addWidget(remove);
    side->addStretch();
    QHBoxLayout* body = new QHBoxLayout();
    body->addWidget(list, 1);
    body->addLayout(side);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(body);
    layout->addWidget(buttons);
    dialog.resize(420, 320);
    dialog.exec();
}

QString ScriptRunner::extensionsFolder()
{
    const QString folder = QDir(settingsBase()).filePath("Extensions");
    QDir().mkpath(folder);
    return folder;
}

bool ScriptRunner::installExtension(const QString& path, QWidget* parent)
{
    extensions::Installed installed;
    std::vector<std::string> notes;
    std::string error;
    if (!extensions::install(QDir::toNativeSeparators(path).toStdString(), configFolder().toStdString(),
            extensionsFolder().toStdString(), installed, notes, error))
    {
        if (parent)
            QMessageBox::warning(parent, "Install Extension", QString::fromStdString(error));
        else
            std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    QString message = QString("%1 %2 is installed.")
                          .arg(QString::fromStdString(installed.package.name), QString::fromStdString(installed.package.version))
                          .simplified();
    bool tools = false;
    for (const std::string& placed : installed.placed)
        tools = tools || QString::fromStdString(placed).startsWith("Tools/", Qt::CaseInsensitive);
    if (tools)
        notes.push_back("Its tools join the toolbox when Phoenix starts again.");
    for (const std::string& note : notes)
        message += "\n\n" + QString::fromStdString(note);
    if (parent)
        QMessageBox::information(parent, "Install Extension", message);
    else
        std::printf("%s\n", message.toStdString().c_str());
    return true;
}

void ScriptRunner::manageExtensions()
{
    QDialog dialog(&_window);
    dialog.setWindowTitle("Manage Extensions");
    QListWidget* list = new QListWidget(&dialog);
    QLabel* details = new QLabel(&dialog);
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    details->setMinimumHeight(110);
    auto installed = std::make_shared<std::vector<extensions::Installed>>();
    const auto fill = [list, installed]() {
        list->clear();
        *installed = extensions::list(extensionsFolder().toStdString());
        for (const extensions::Installed& each : *installed)
        {
            const extensions::Package& p = each.package;
            QString text = QString::fromStdString(p.name);
            if (!p.version.empty())
                text += "  " + QString::fromStdString(p.version);
            new QListWidgetItem(text, list);
        }
        if (installed->empty())
            new QListWidgetItem("No extensions installed", list);
        list->setCurrentRow(0);
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, [details, installed](int row) {
        if (row < 0 || row >= static_cast<int>(installed->size()))
        {
            details->setText("Install an extension from a ZXP package, or from a folder holding one "
                             "unpacked. Its commands join the Commands menu and its tools the toolbox.");
            return;
        }
        const extensions::Installed& each = (*installed)[static_cast<size_t>(row)];
        const extensions::Package& p = each.package;
        QStringList lines;
        if (!p.author.empty())
            lines << "By " + QString::fromStdString(p.author);
        if (!p.description.empty())
            lines << QString::fromStdString(p.description).simplified();
        int commands = 0, tools = 0, other = 0;
        for (const std::string& placed : each.placed)
        {
            const QString path = QString::fromStdString(placed);
            if (path.startsWith("Commands/", Qt::CaseInsensitive))
                ++commands;
            else if (path.startsWith("Tools/", Qt::CaseInsensitive))
                ++tools;
            else
                ++other;
        }
        QStringList adds;
        if (commands)
            adds << QString("%1 command%2").arg(commands).arg(commands == 1 ? "" : "s");
        if (tools)
            adds << QString("%1 tool%2").arg(tools).arg(tools == 1 ? "" : "s");
        if (other)
            adds << QString("%1 other file%2").arg(other).arg(other == 1 ? "" : "s");
        for (const extensions::Package::Panel& panel : p.panels)
            adds << QString("the panel %1 (HTML panels do not show in Phoenix yet)").arg(QString::fromStdString(panel.menu));
        if (!adds.isEmpty())
            lines << "Adds " + adds.join(", ") + ".";
        details->setText(lines.join("\n\n"));
    });
    QPushButton* installPackage = new QPushButton("Install &Package...", &dialog);
    QPushButton* installFolder = new QPushButton("Install from &Folder...", &dialog);
    QPushButton* remove = new QPushButton("&Remove", &dialog);
    QPushButton* openFolder = new QPushButton("&Open Folder", &dialog);
    installPackage->setToolTip("Install a ZXP extension package");
    installFolder->setToolTip("Install an extension unpacked into a folder: one with an .mxi file or a CSXS folder");
    connect(installPackage, &QPushButton::clicked, &dialog, [this, &dialog, fill]() {
        QSettings settings;
        const QString path = QFileDialog::getOpenFileName(&dialog, "Install Extension",
            settings.value("extensions/lastFolder").toString(), "Extension packages (*.zxp *.zip);;All files (*)");
        if (path.isEmpty())
            return;
        settings.setValue("extensions/lastFolder", QFileInfo(path).absolutePath());
        installExtension(path, &dialog);
        fill();
    });
    connect(installFolder, &QPushButton::clicked, &dialog, [this, &dialog, fill]() {
        QSettings settings;
        const QString path = QFileDialog::getExistingDirectory(&dialog, "Install Extension from Folder",
            settings.value("extensions/lastFolder").toString());
        if (path.isEmpty())
            return;
        settings.setValue("extensions/lastFolder", path);
        installExtension(path, &dialog);
        fill();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&dialog, list, installed, fill]() {
        const int row = list->currentRow();
        if (row < 0 || row >= static_cast<int>(installed->size()))
            return;
        const extensions::Installed each = (*installed)[static_cast<size_t>(row)];
        if (QMessageBox::question(&dialog, "Remove Extension",
                QString("Remove %1 and the commands and tools it added?").arg(QString::fromStdString(each.package.name))) !=
            QMessageBox::Yes)
            return;
        std::string error;
        if (!extensions::remove(each, configFolder().toStdString(), error))
            QMessageBox::warning(&dialog, "Remove Extension", QString::fromStdString(error));
        fill();
    });
    connect(openFolder, &QPushButton::clicked, &dialog,
        []() { QDesktopServices::openUrl(QUrl::fromLocalFile(extensionsFolder())); });
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* side = new QVBoxLayout();
    side->addWidget(installPackage);
    side->addWidget(installFolder);
    side->addWidget(remove);
    side->addStretch();
    side->addWidget(openFolder);
    QVBoxLayout* left = new QVBoxLayout();
    left->addWidget(list, 1);
    left->addWidget(details);
    QHBoxLayout* body = new QHBoxLayout();
    body->addLayout(left, 1);
    body->addLayout(side);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(body);
    layout->addWidget(buttons);
    fill();
    dialog.resize(560, 420);
    dialog.exec();
}
