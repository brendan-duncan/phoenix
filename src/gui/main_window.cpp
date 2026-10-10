#include "main_window.h"
#include "convert_to_symbol_dialog.h"
#include "lines_to_fills.h"
#include "fill_offset.h"
#include "../edit/layer_commands.h"
#include "../edit/property_command.h"
#include "phoenix_view.h"
#include "timeline_view.h"
#include "../data/frame.h"
#include "../data/group.h"
#include "../data/symbol_instance.h"
#include "../data/symbol_list.h"
#include "../edit/element_commands.h"
#include "../data/edge.h"
#include "../edit/path_tidy.h"
#include "../edit/element_clone.h"
#include "../edit/new_document.h"
#include "../edit/shape_merge.h"
#include "../parser/fla_parser.h"
#include "../writer/fla_writer.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"

#include <cstdint>
#include <algorithm>

#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
#include <QInputDialog>

#include <memory>
#include <filesystem>
#include <QAction>
#include <QStatusBar>
#include <QTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QKeySequence>
#include <QSettings>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QToolBar>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QHash>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolButton>
#include <QTimer>

#include "tool_icons.h"

namespace {
fla::Frame* frameOfElement(const fla::Element* element);
}

MainWindow::MainWindow(const QString& initialFile, QWidget* parent)
    : QMainWindow(parent)
    , _phoenixView(nullptr)
    , _documentView(nullptr)
    , _timelineView(nullptr)
    , _flaDocument(nullptr)
    , _player(new Player(this))
    , _recentFilesMenu(nullptr)
{
    setWindowTitle("Phoenix - FLA Viewer");

    QCoreApplication::setOrganizationName("Phoenix");
    QCoreApplication::setApplicationName("FLAViewer");

    // Set application icon
    QIcon phoenixIcon = createPhoenixIcon();
    setWindowIcon(phoenixIcon);
    QApplication::setWindowIcon(phoenixIcon);

    loadSettings();

    setupUI();
    setupMenus();
    setupToolBar();
    setupPropertiesPanel();

    // Keep the Edit menu and the title bar in step with the undo history.
    _editContext.commandStack().setChangedCallback([this]() { updateEditState(); });
    updateEditState();

    _selection.setChangedCallback([this]() {
        updateSelectionState();
        // The Properties panel follows the selection the way Animate's does:
        // an object shows its Object tab; nothing selected shows the tool, or
        // the document when the arrow is the tool.
        if (_propertiesPanel)
        {
            _propertiesPanel->refresh();
            if (!_selection.isEmpty())
                _propertiesPanel->setTab(PropertiesPanel::Tab::Object);
            else if (_phoenixView && _phoenixView->activeTool() == _selectionTool.get())
                _propertiesPanel->setTab(PropertiesPanel::Tab::Doc);
            else
                _propertiesPanel->setTab(PropertiesPanel::Tab::Tool);
        }
        // The tree follows the stage.
        if (_documentView && !_syncingSelection)
        {
            _syncingSelection = true;
            _documentView->selectElement(_selection.single());
            _syncingSelection = false;
        }
        // The transform box belongs to the old selection, so drop it.
        if (_freeTransformTool)
            _freeTransformTool->resetBox();
        if (_phoenixView)
        {
            // Letting go of a freshly drawn shape is what merges it into the
            // artwork underneath, so the view hears about this before it
            // repaints.
            _phoenixView->selectionChanged();
            _phoenixView->update();
        }
    });

    // Object snapping on by default, grid snapping off, matching Animate.
    _snapper.setObjectSnapEnabled(true);
    _phoenixView->setSnapper(&_snapper);

    _selectionTool = std::make_unique<SelectionTool>(_selection, _editContext.commandStack());
    _freeTransformTool = std::make_unique<FreeTransformTool>(
        _selection, _editContext.commandStack(), _drawingStyle);

    fla::CommandStack& stack = _editContext.commandStack();
    _rectangleTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Rectangle, _selection, stack, _drawingStyle);
    _ovalTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Oval, _selection, stack, _drawingStyle);
    _lineTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Line, _selection, stack, _drawingStyle);
    _polyStarTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::PolyStar, _selection, stack, _drawingStyle);
    _rectanglePrimitiveTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::RectanglePrimitive, _selection, stack, _drawingStyle);
    _ovalPrimitiveTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::OvalPrimitive, _selection, stack, _drawingStyle);
    _handTool = std::make_unique<HandTool>();
    _zoomTool = std::make_unique<ZoomTool>();
    _lassoTool = std::make_unique<LassoTool>(_selection);
    _brushTool = std::make_unique<BrushTool>(_selection, stack, _drawingStyle);
    _eraserTool = std::make_unique<EraserTool>(_selection, stack, _drawingStyle);
    _paintBucketTool = std::make_unique<PaintBucketTool>(stack, _drawingStyle);
    _inkBottleTool = std::make_unique<InkBottleTool>(stack, _drawingStyle);
    // Sampling hands over to the bucket or the bottle, as Animate does.
    _eyedropperTool = std::make_unique<EyedropperTool>(_drawingStyle, [this](bool stroke) {
        refreshStyleSwatches();
        QAction* next = stroke ? _inkBottleAction : _paintBucketAction;
        if (next)
            next->trigger();
    });
    _penTool = std::make_unique<PenTool>(_selection, stack, _drawingStyle);
    _pencilTool = std::make_unique<PencilTool>(_selection, stack, _drawingStyle);
    _textTool = std::make_unique<TextTool>(_selection, stack, _drawingStyle);
    _subselectionTool = std::make_unique<SubselectionTool>(_selection, stack);
    _phoenixView->setSelection(&_selection);
    _phoenixView->setCommandStack(&_editContext.commandStack());
    _phoenixView->setModifierTool(_selectionTool.get());
    _phoenixView->setActiveTool(_selectionTool.get());
    updateSelectionState();

    setAcceptDrops(true);

    // The file asked for, else the one worked on last.
    if (!initialFile.isEmpty())
        loadFLAFile(initialFile);
    else if (!_recentFiles.isEmpty())
        loadFLAFile(_recentFiles.first());
}

namespace {

/// Whether a dropped or passed path is a document Phoenix opens.
bool isDocumentPath(const QString& path)
{
    const QString lower = path.toLower();
    return lower.endsWith(".fla") || lower.endsWith(".xfl");
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isDocumentPath(url.toLocalFile()))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile() && isDocumentPath(url.toLocalFile()))
        {
            event->acceptProposedAction();
            loadFLAFile(url.toLocalFile());
            return;
        }
    }
}

void MainWindow::releaseDocument()
{
    // A drawing still waiting to merge points into the document too, and must
    // not outlive it. It is dropped rather than committed: the document is on
    // its way out either way.
    if (_phoenixView)
        _phoenixView->clearPendingMerge();

    // The panels point into the document too, and dropping the undo history
    // below makes them refresh, so they let go first.
    if (_timelineView)
        _timelineView->setDocument(nullptr);
    if (_documentView)
        _documentView->setDocument(nullptr);

    // The selection and the undo history both point into the document, so both
    // have to let go before it is deleted.
    _selection.clear();
    _editContext.setDocument(nullptr);

    if (_flaDocument)
    {
        delete _flaDocument;
        _flaDocument = nullptr;
    }
}

void MainWindow::adoptDocument(fla::FLADocument* document, const QString& filePath,
    const QString& displayName)
{
    releaseDocument();

    _flaDocument = document;
    _editContext.setDocument(_flaDocument);

    // Grid spacing comes from the document, so it has to be picked up per file.
    if (_flaDocument && _flaDocument->document)
    {
        _snapper.setGridSpacing(_flaDocument->document->gridSpacingX,
            _flaDocument->document->gridSpacingY);
    }
    _phoenixView->setDocument(_flaDocument);
    _documentView->setDocument(_flaDocument);
    _timelineView->setDocument(_flaDocument);

    _documentName = displayName;
    _documentPath = filePath;
    updateWindowTitle();
    if (_propertiesPanel)
    {
        _propertiesPanel->refresh();
        _propertiesPanel->setTab(PropertiesPanel::Tab::Doc);
    }

    if (_flaDocument)
        _player->setCurrentFrame(0);
}

void MainWindow::newFile()
{
    if (!confirmDiscardChanges())
        return;

    // No path: the first Save has to ask where to put it, which saveFile()
    // already does by falling through to Save As.
    adoptDocument(fla::createEmptyDocument(), QString(), "Untitled");
}

void MainWindow::loadFLAFile(const QString& filePath)
{
    releaseDocument();

    FLAParser parser;
    fla::FLADocument* document = parser.parse(filePath.toStdString());

    if (document)
    {
        QString displayName;
        QFileInfo fileInfo(filePath);
        if (filePath.endsWith(".xml"))
        {
            QDir parentDir = fileInfo.dir();
            displayName = parentDir.dirName() + "/" + fileInfo.fileName();
        }
        else
        {
            displayName = fileInfo.fileName();
        }

        adoptDocument(document, filePath, displayName);
        addToRecentFiles(filePath);
    }
    else
    {
        adoptDocument(nullptr, QString(), QString());
        QMessageBox::warning(this, "Failed to Load FLA",
                           QString("Failed to load FLA file:\n%1\n\nError: %2")
                           .arg(filePath)
                           .arg(parser.errorString()));
    }
}

void MainWindow::setupUI()
{
    // Create the main layout
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QWidget* centralWidget = new QWidget(this);
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    // Create the main vertical splitter
    _mainSplitter = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(_mainSplitter);

    // Create the horizontal splitter for document and phoenix views
    _viewSplitter = new QSplitter(Qt::Horizontal, this);

    // Create the document view (left panel)
    _documentView = new DocumentView(this);
    _documentView->setMinimumWidth(150);

    // Create the phoenix view (right panel)
    _phoenixView = new PhoenixView(_player, this);

    // The stage sits under its breadcrumb, which says what is being edited.
    _breadcrumb = new QWidget(this);
    _breadcrumb->setAutoFillBackground(true);
    QHBoxLayout* breadcrumbLayout = new QHBoxLayout(_breadcrumb);
    breadcrumbLayout->setContentsMargins(6, 2, 6, 2);
    breadcrumbLayout->setSpacing(2);
    breadcrumbLayout->addStretch();

    QWidget* stageColumn = new QWidget(this);
    QVBoxLayout* stageLayout = new QVBoxLayout(stageColumn);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    stageLayout->addWidget(_breadcrumb);
    stageLayout->addWidget(_phoenixView, 1);

    // Add widgets to view splitter
    _viewSplitter->addWidget(_documentView);
    _viewSplitter->addWidget(stageColumn);

    // Set view splitter properties
    _viewSplitter->setCollapsible(0, false);
    _viewSplitter->setCollapsible(1, false);
    _viewSplitter->setStretchFactor(0, 0);
    _viewSplitter->setStretchFactor(1, 1);

    // Create the timeline view
    _timelineView = new TimelineView(_player, this);
    _timelineView->setMinimumHeight(150);

    // Add to main vertical splitter
    _mainSplitter->addWidget(_viewSplitter);
    _mainSplitter->addWidget(_timelineView);

    // Set main splitter properties
    _mainSplitter->setCollapsible(0, false);
    _mainSplitter->setCollapsible(1, true);
    _mainSplitter->setStretchFactor(0, 1);
    _mainSplitter->setStretchFactor(1, 0);

    connect(_documentView, &DocumentView::visibilityChanged,
            this, &MainWindow::onVisibilityChanged);

    // The tree and the stage share one selection: an element picked in the
    // tree becomes the stage selection; a finer node (an edge, a path) picks
    // its shape and is shown inside it for inspection.
    connect(_documentView, &DocumentView::elementSelected, this,
        [this](const fla::DOMElement* element) {
            _phoenixView->onElementSelected(element);
            const fla::DOMElement* owner = element;
            while (owner && !dynamic_cast<const fla::Element*>(owner))
                owner = owner->parent;
            const fla::Element* picked = owner ? dynamic_cast<const fla::Element*>(owner) : nullptr;
            if (_syncingSelection)
                return;
            _syncingSelection = true;
            if (picked && frameOfElement(picked))
                _selection.select(const_cast<fla::Element*>(picked));
            else if (!element)
                _selection.clear();
            _syncingSelection = false;
        });

    connect(_timelineView, &TimelineView::layerVisibilityChanged,
            this, &MainWindow::onVisibilityChanged);

    // Entering or leaving a symbol changes what the panels show.
    connect(_phoenixView, &PhoenixView::scopeChanged, this, [this]() {
        _timelineView->setTimeline(_phoenixView->scopeTimeline());
        rebuildBreadcrumb();
        updateSelectionState();
    });
    rebuildBreadcrumb();

    // Locking and the current layer change what the stage can pick and where
    // it draws; a repaint is all it needs, since both are read live.
    connect(_timelineView, &TimelineView::layerLockChanged,
            this, &MainWindow::onVisibilityChanged);
    connect(_timelineView, &TimelineView::currentLayerChanged,
            this, &MainWindow::onVisibilityChanged);
    connect(_timelineView, &TimelineView::layersChanged,
            this, &MainWindow::onVisibilityChanged);

    // Adding, deleting and renaming layers go through the undo history.
    connect(_timelineView, &TimelineView::addLayerRequested, this, &MainWindow::addLayer);
    connect(_timelineView, &TimelineView::deleteLayerRequested, this, &MainWindow::deleteCurrentLayer);
    connect(_timelineView, &TimelineView::layerRenamed, this, &MainWindow::renameLayer);
}

fla::Timeline* MainWindow::editedTimeline() const
{
    if (_phoenixView && _phoenixView->scopeTimeline())
        return _phoenixView->scopeTimeline();
    if (_flaDocument && _flaDocument->document && !_flaDocument->document->timelines.empty())
        return _flaDocument->document->timelines.front();
    return nullptr;
}

void MainWindow::addLayer(bool folder)
{
    fla::Timeline* timeline = editedTimeline();
    if (!timeline)
        return;

    // Above the current layer, as Animate puts it; at the top otherwise.
    int index = 0;
    fla::Timeline* currentTimeline = nullptr;
    int currentIndex = 0;
    if (_timelineView && _timelineView->currentLayer(currentTimeline, currentIndex) && currentTimeline == timeline)
        index = currentIndex;

    fla::Layer* layer = fla::newLayerFor(*timeline, folder);
    _editContext.commandStack().push(fla::CommandPtr(
        new fla::AddLayerCommand(timeline, index, layer, folder ? "New Folder" : "New Layer")));
    _editContext.commandStack().breakMergeChain();
    _timelineView->refresh();
    onVisibilityChanged();
}

void MainWindow::deleteCurrentLayer()
{
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline)
        return;

    // Animate keeps at least one layer that can hold frames.
    const std::vector<int> going = fla::RemoveLayerCommand::affectedIndices(*timeline, index);
    int remaining = 0;
    for (size_t i = 0; i < timeline->layers.size(); ++i)
    {
        const fla::Layer* layer = timeline->layers[i];
        if (layer && layer->layerType != fla::Layer::Type::Folder &&
            std::find(going.begin(), going.end(), static_cast<int>(i)) == going.end())
            ++remaining;
    }
    if (remaining == 0)
    {
        statusBar()->showMessage("A timeline keeps at least one layer.", 3000);
        return;
    }

    // Whatever was selected on the layer goes with it.
    _selection.clear();
    _editContext.commandStack().push(fla::CommandPtr(
        new fla::RemoveLayerCommand(timeline, index, "Delete Layer")));
    _editContext.commandStack().breakMergeChain();
    _timelineView->refresh();
    onVisibilityChanged();
}

void MainWindow::renameLayer(const fla::Timeline* timeline, int layerIndex, const QString& name)
{
    if (!timeline || layerIndex < 0 || layerIndex >= static_cast<int>(timeline->layers.size()))
        return;
    fla::Layer* layer = timeline->layers[layerIndex];
    if (!layer)
        return;
    const std::string after = name.toStdString();
    if (after == layer->name)
        return;

    // A typed name stops being automatic, so Animate will not renumber it.
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Rename Layer");
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(layer->name, layer->name, after, "Rename Layer")));
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(layer->autoNamed, layer->autoNamed, false, "Rename Layer")));
    stack.endMacro();
    stack.breakMergeChain();
    _timelineView->refresh();
}

void MainWindow::setupMenus()
{
    // Create menu bar
    QMenuBar* menuBar = this->menuBar();

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* newAction = new QAction("&New", this);
    newAction->setShortcut(QKeySequence::New);
    newAction->setStatusTip("Start a new, empty document");
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);
    fileMenu->addAction(newAction);

    QAction* openAction = new QAction("&Open...", this);
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip("Open an FLA file");
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addAction(openAction);

    // Add Open Recent submenu
    _recentFilesMenu = fileMenu->addMenu("Open &Recent");
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    QAction* saveAction = new QAction("&Save", this);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setStatusTip("Save the document");
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFile);
    fileMenu->addAction(saveAction);

    QAction* saveAsAction = new QAction("Save &As...", this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setStatusTip("Save the document to a new file or XFL folder");
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveFileAs);
    fileMenu->addAction(saveAsAction);

    fileMenu->addSeparator();

    QAction* exportSvgAction = new QAction("Export to &SVG...", this);
    exportSvgAction->setStatusTip("Export the current frame to an SVG file");
    connect(exportSvgAction, &QAction::triggered, this, &MainWindow::exportSvg);
    fileMenu->addAction(exportSvgAction);

    fileMenu->addSeparator();

    QAction* exitAction = new QAction("&Exit", this);
    exitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(exitAction, &QAction::triggered, this, &MainWindow::quit);
    fileMenu->addAction(exitAction);

    // Edit menu
    QMenu* editMenu = menuBar->addMenu("&Edit");

    _undoAction = new QAction("&Undo", this);
    _undoAction->setShortcut(QKeySequence::Undo);
    connect(_undoAction, &QAction::triggered, this, &MainWindow::undo);
    editMenu->addAction(_undoAction);

    _redoAction = new QAction("&Redo", this);
    // QKeySequence::Redo is Ctrl+Y on Windows; Ctrl+Shift+Z is what anyone
    // coming from Animate will reach for, so accept both.
    _redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(_redoAction, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addAction(_redoAction);

    editMenu->addSeparator();

    _selectAllAction = new QAction("Select &All", this);
    _selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(_selectAllAction, &QAction::triggered, this, &MainWindow::selectAll);
    editMenu->addAction(_selectAllAction);

    _deselectAllAction = new QAction("&Deselect All", this);
    _deselectAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(_deselectAllAction, &QAction::triggered, this, &MainWindow::deselectAll);
    editMenu->addAction(_deselectAllAction);

    editMenu->addSeparator();

    _cutAction = new QAction("Cu&t", this);
    _cutAction->setShortcut(QKeySequence::Cut);
    connect(_cutAction, &QAction::triggered, this, &MainWindow::cut);
    editMenu->addAction(_cutAction);

    _copyAction = new QAction("&Copy", this);
    _copyAction->setShortcut(QKeySequence::Copy);
    connect(_copyAction, &QAction::triggered, this, &MainWindow::copy);
    editMenu->addAction(_copyAction);

    _pasteAction = new QAction("&Paste in Center", this);
    _pasteAction->setShortcut(QKeySequence::Paste);
    connect(_pasteAction, &QAction::triggered, this, &MainWindow::paste);
    editMenu->addAction(_pasteAction);

    _pasteInPlaceAction = new QAction("Paste in P&lace", this);
    _pasteInPlaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(_pasteInPlaceAction, &QAction::triggered, this, &MainWindow::pasteInPlace);
    editMenu->addAction(_pasteInPlaceAction);

    _duplicateAction = new QAction("D&uplicate", this);
    _duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(_duplicateAction, &QAction::triggered, this, &MainWindow::duplicateSelection);
    editMenu->addAction(_duplicateAction);

    editMenu->addSeparator();

    // Delete and Backspace reach the stage directly, where the subselection
    // tool may want them for an anchor first, so the menu entry has no
    // shortcut of its own.
    QAction* clearAction = new QAction("Cl&ear", this);
    clearAction->setStatusTip("Delete the selection (Delete or Backspace on the stage)");
    connect(clearAction, &QAction::triggered, this, [this]() {
        if (_phoenixView)
            _phoenixView->deleteSelection();
    });
    editMenu->addAction(clearAction);

    // Modify menu, with Animate's keys.
    QMenu* modifyMenu = menuBar->addMenu("&Modify");

    _groupAction = new QAction("&Group", this);
    _groupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(_groupAction, &QAction::triggered, this, &MainWindow::groupSelection);
    modifyMenu->addAction(_groupAction);

    _ungroupAction = new QAction("&Ungroup", this);
    _ungroupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(_ungroupAction, &QAction::triggered, this, &MainWindow::ungroupSelection);
    modifyMenu->addAction(_ungroupAction);

    _breakApartAction = new QAction("&Break Apart", this);
    _breakApartAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    _breakApartAction->setStatusTip("Take a group or a symbol instance apart, or turn a drawing object into merge artwork");
    connect(_breakApartAction, &QAction::triggered, this, &MainWindow::breakApartSelection);
    modifyMenu->addAction(_breakApartAction);

    _convertToSymbolAction = new QAction("Convert to &Symbol...", this);
    _convertToSymbolAction->setShortcut(QKeySequence(Qt::Key_F8));
    _convertToSymbolAction->setStatusTip("Turn the selection into a library symbol and leave an instance of it");
    connect(_convertToSymbolAction, &QAction::triggered, this, &MainWindow::convertToSymbol);
    modifyMenu->addAction(_convertToSymbolAction);

    modifyMenu->addSeparator();

    // Modify > Shape, as Animate groups them.
    QMenu* shapeMenu = modifyMenu->addMenu("S&hape");

    _smoothAction = new QAction("&Smooth", this);
    _smoothAction->setStatusTip("Soften the selected shapes' curves");
    connect(_smoothAction, &QAction::triggered, this, &MainWindow::smoothSelection);
    shapeMenu->addAction(_smoothAction);

    _straightenAction = new QAction("S&traighten", this);
    _straightenAction->setStatusTip("Replace the selected shapes' curves with straight segments");
    connect(_straightenAction, &QAction::triggered, this, &MainWindow::straightenSelection);
    shapeMenu->addAction(_straightenAction);

    _optimizeAction = new QAction("&Optimize...", this);
    _optimizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_C));
    _optimizeAction->setStatusTip("Refit the selected shapes' curves with fewer anchors");
    connect(_optimizeAction, &QAction::triggered, this, &MainWindow::optimizeSelection);
    shapeMenu->addAction(_optimizeAction);

    shapeMenu->addSeparator();

    _linesToFillsAction = new QAction("Convert &Lines to Fills", this);
    _linesToFillsAction->setStatusTip("Turn the selected shapes' strokes into filled regions");
    connect(_linesToFillsAction, &QAction::triggered, this, &MainWindow::convertLinesToFillsSelection);
    shapeMenu->addAction(_linesToFillsAction);

    _expandFillAction = new QAction("E&xpand Fill...", this);
    _expandFillAction->setStatusTip("Grow or shrink the selected shapes' fills by a distance");
    connect(_expandFillAction, &QAction::triggered, this, &MainWindow::expandFillSelection);
    shapeMenu->addAction(_expandFillAction);

    _softenFillAction = new QAction("So&ften Fill Edges...", this);
    _softenFillAction->setStatusTip("Fade the selected shapes' fills out at their edges in steps");
    connect(_softenFillAction, &QAction::triggered, this, &MainWindow::softenFillEdgesSelection);
    shapeMenu->addAction(_softenFillAction);

    modifyMenu->addSeparator();

    QMenu* arrangeMenu = modifyMenu->addMenu("&Arrange");
    struct ArrangeEntry
    {
        const char* label;
        QKeySequence key;
        int step;
        bool toEnd;
    };
    const ArrangeEntry arrange[4] = {
        {"Bring to &Front", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up), 1, true},
        {"Bring F&orward", QKeySequence(Qt::CTRL | Qt::Key_Up), 1, false},
        {"Send &Backward", QKeySequence(Qt::CTRL | Qt::Key_Down), -1, false},
        {"Send to Bac&k", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down), -1, true},
    };
    for (int i = 0; i < 4; ++i)
    {
        QAction* action = new QAction(arrange[i].label, this);
        action->setShortcut(arrange[i].key);
        const int step = arrange[i].step;
        const bool toEnd = arrange[i].toEnd;
        connect(action, &QAction::triggered, this, [this, step, toEnd]() {
            arrangeSelection(step, toEnd);
        });
        arrangeMenu->addAction(action);
        _arrangeActions[i] = action;
    }

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* viewDocumentAction = new QAction("View Document...", this);
    viewDocumentAction->setStatusTip("View the current document source");
    connect(viewDocumentAction, &QAction::triggered, this, &MainWindow::viewDocument);
    viewMenu->addAction(viewDocumentAction);

    viewMenu->addSeparator();

    // Animate's View menu: Zoom In and Out step through the magnification
    // presets, and the presets sit in their own submenu with the same keys.
    QAction* zoomInAction = new QAction("Zoom &In", this);
    zoomInAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Equal),
        QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence::ZoomIn});
    connect(zoomInAction, &QAction::triggered, _phoenixView, &PhoenixView::zoomIn);
    viewMenu->addAction(zoomInAction);

    QAction* zoomOutAction = new QAction("Zoom &Out", this);
    zoomOutAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Minus), QKeySequence::ZoomOut});
    connect(zoomOutAction, &QAction::triggered, _phoenixView, &PhoenixView::zoomOut);
    viewMenu->addAction(zoomOutAction);

    QMenu* magnificationMenu = viewMenu->addMenu("&Magnification");

    QAction* fitAction = new QAction("&Fit in Window", this);
    fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(fitAction, &QAction::triggered, _phoenixView, &PhoenixView::zoomToFit);
    magnificationMenu->addAction(fitAction);

    QAction* showAllAction = new QAction("Show &All", this);
    showAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(showAllAction, &QAction::triggered, _phoenixView, &PhoenixView::zoomToShowAll);
    magnificationMenu->addAction(showAllAction);

    magnificationMenu->addSeparator();

    struct ZoomPreset
    {
        const char* label;
        double zoom;
        int key;
    };
    const ZoomPreset presets[] = {
        {"25%", 0.25, 0}, {"50%", 0.5, 0}, {"100%", 1.0, Qt::Key_1},
        {"200%", 2.0, 0}, {"400%", 4.0, Qt::Key_4}, {"800%", 8.0, Qt::Key_8},
    };
    for (const ZoomPreset& preset : presets)
    {
        QAction* action = new QAction(preset.label, this);
        if (preset.key != 0)
            action->setShortcut(QKeySequence(Qt::CTRL | preset.key));
        const double zoom = preset.zoom;
        connect(action, &QAction::triggered, this, [this, zoom]() {
            _phoenixView->setZoomLevel(zoom);
        });
        magnificationMenu->addAction(action);
    }

    viewMenu->addSeparator();

    _phoenixView->setHighQualityAntiAliasing(_highQualityAntiAliasing);

    QAction* highQualityAntialiasingAction = new QAction("High Quality Anti-Aliasing", this);
    highQualityAntialiasingAction->setCheckable(true);
    highQualityAntialiasingAction->setChecked(_highQualityAntiAliasing);
    highQualityAntialiasingAction->setStatusTip("Use higher quality anti-aliasing (may reduce gaps between shapes)");
    connect(highQualityAntialiasingAction, &QAction::toggled, this, &MainWindow::onHighQualityAntiAliasingToggled);
    viewMenu->addAction(highQualityAntialiasingAction);

    QAction* showBoundsAction = new QAction("Show Bounding Boxes", this);
    showBoundsAction->setCheckable(true);
    showBoundsAction->setChecked(false);
    // B belongs to the brush, as in Animate; the debug toggle moves aside.
    showBoundsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    showBoundsAction->setStatusTip("Show element bounding boxes used for culling (Green=rendered, Red=culled, Blue=visible area)");
    connect(showBoundsAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowBounds);
    viewMenu->addAction(showBoundsAction);

    viewMenu->addSeparator();

    QAction* showGridAction = new QAction("Show &Grid", this);
    showGridAction->setCheckable(true);
    showGridAction->setStatusTip("Show the document grid");
    connect(showGridAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowGrid);
    viewMenu->addAction(showGridAction);

    QAction* snapToGridAction = new QAction("Snap to Grid", this);
    snapToGridAction->setCheckable(true);
    snapToGridAction->setStatusTip("Snap dragged objects to the document grid");
    connect(snapToGridAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setGridEnabled(on);
        saveSettings();
    });
    viewMenu->addAction(snapToGridAction);

    QAction* snapToObjectsAction = new QAction("Snap to &Objects", this);
    snapToObjectsAction->setCheckable(true);
    snapToObjectsAction->setChecked(true);
    snapToObjectsAction->setStatusTip("Line dragged objects up with other objects and the stage");
    connect(snapToObjectsAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setObjectSnapEnabled(on);
        saveSettings();
    });
    viewMenu->addAction(snapToObjectsAction);

    viewMenu->addSeparator();

    QAction* dimOutsideDocumentAction = new QAction("Dim Outside Document", this);
    dimOutsideDocumentAction->setCheckable(true);
    dimOutsideDocumentAction->setChecked(false);
    dimOutsideDocumentAction->setStatusTip("Dim the area outside the document rectangle");
    connect(dimOutsideDocumentAction, &QAction::toggled, _phoenixView, &PhoenixView::setDimOutsideDocument);
    viewMenu->addAction(dimOutsideDocumentAction);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open FLA File"), _lastDirectory, tr("FLA Files (*.fla;DOMDocument.xml);;All Files (*)"));

    if (!fileName.isEmpty())
    {
        if (!confirmDiscardChanges())
            return;

        _lastDirectory = QFileInfo(fileName).absolutePath();
        saveSettings();
        loadFLAFile(fileName);
    }
}

bool MainWindow::confirmLossySave()
{
    if (!_flaDocument || !_flaDocument->document)
        return false;

    const std::vector<std::string> lost = fla::unsupportedContent(*_flaDocument->document);
    if (lost.empty())
        return true;

    QStringList items;
    for (const std::string& description : lost)
        items << "    " + QString::fromStdString(description);

    // Checked before writing rather than after, so declining leaves whatever is
    // already on disk untouched.
    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
        "Some Content Will Be Lost",
        QString("Phoenix cannot write this part of the document yet:\n\n%1\n\n"
                "Saving will drop it. Overwriting the original file would lose it "
                "for good.").arg(items.join("\n")),
        QMessageBox::Save | QMessageBox::Cancel,
        QMessageBox::Cancel);

    return answer == QMessageBox::Save;
}

bool MainWindow::saveToPath(const QString& filePath)
{
    if (!_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return false;
    }

    // A drawing still floating is committed before anything is written, so that
    // saving with one selected and saving just after letting it go produce the
    // same file rather than two shapes in one case and one in the other.
    if (_phoenixView)
        _phoenixView->flushPendingMerge();

    if (!confirmLossySave())
        return false;

    QString error;

    if (filePath.endsWith(".fla", Qt::CaseInsensitive))
    {
        // Written beside the original and moved into place once complete,
        // so a save that fails part way leaves the original untouched.
        const QString temporary = filePath + ".saving";
        fla::FLAWriter writer;
        if (!writer.write(*_flaDocument->document, temporary.toStdString()))
        {
            error = QString::fromStdString(writer.errorString());
            QFile::remove(temporary);
        }
        else
        {
            std::error_code moveError;
            std::filesystem::rename(std::filesystem::path(temporary.toStdWString()),
                std::filesystem::path(filePath.toStdWString()), moveError);
            if (moveError)
            {
                error = QString("could not replace the file: %1")
                    .arg(QString::fromStdString(moveError.message()));
                QFile::remove(temporary);
            }
        }
    }
    else
    {
        // Anything that is not a .fla is written as an uncompressed XFL folder,
        // which Animate opens just as happily and which stays readable on disk.
        fla::XFLFolderWriter writer;
        if (!writer.write(*_flaDocument->document, filePath.toStdString()))
            error = QString::fromStdString(writer.errorString());
    }

    if (!error.isEmpty())
    {
        QMessageBox::warning(this, "Save Failed",
            QString("Failed to save:\n%1\n\n%2").arg(filePath).arg(error));
        return false;
    }

    _documentPath = filePath;
    _documentName = QFileInfo(filePath).fileName();
    _editContext.markSaved();
    updateEditState();
    addToRecentFiles(filePath);
    statusBar()->showMessage(QString("Saved %1").arg(filePath), 5000);
    return true;
}

void MainWindow::saveFile()
{
    if (_documentPath.isEmpty())
    {
        saveFileAs();
        return;
    }

    saveToPath(_documentPath);
}

void MainWindow::saveFileAs()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }

    const QString suggested = _documentPath.isEmpty()
        ? (_lastDirectory.isEmpty() ? QString("untitled.fla")
                                    : QDir(_lastDirectory).filePath("untitled.fla"))
        : _documentPath;

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Save As"), suggested,
        tr("FLA Files (*.fla);;XFL Folder (*)"));

    if (fileName.isEmpty())
        return;

    if (saveToPath(fileName))
    {
        _lastDirectory = QFileInfo(fileName).absolutePath();
        saveSettings();
    }
}

void MainWindow::exportSvg()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }

    QString defaultName = QFileInfo(windowTitle()).baseName();
    QString suggested = _lastDirectory.isEmpty() ? QString("export.svg")
                                                 : QDir(_lastDirectory).filePath("export.svg");

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export to SVG"), suggested, tr("SVG Files (*.svg);;All Files (*)"));

    if (fileName.isEmpty())
        return;

    if (!fileName.endsWith(".svg", Qt::CaseInsensitive))
        fileName += ".svg";

    if (_phoenixView->exportToSvg(fileName))
    {
        _lastDirectory = QFileInfo(fileName).absolutePath();
        saveSettings();
        statusBar()->showMessage(QString("Exported SVG to %1").arg(fileName), 5000);
    }
    else
    {
        QMessageBox::warning(this, "Export Failed",
            QString("Failed to export SVG to:\n%1").arg(fileName));
    }
}

void MainWindow::quit()
{
    // close() goes through closeEvent(), so File > Exit gets the same
    // unsaved-changes prompt as closing the window.
    close();
}

void MainWindow::viewDocument()
{
    if (!_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }

    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Document Source");
    dialog->resize(600, 400);

    QTextEdit* textEdit = new QTextEdit(dialog);
    textEdit->setReadOnly(true);

    // For now, just show basic document info
    QString docText = QString::fromStdString(_flaDocument->document->source);

    textEdit->setPlainText(docText);

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->addWidget(textEdit);
    dialog->setLayout(layout);

    dialog->exec();
}

void MainWindow::undo()
{
    _editContext.commandStack().undo();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::redo()
{
    _editContext.commandStack().redo();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::updateEditState()
{
    const fla::CommandStack& stack = _editContext.commandStack();

    // Frames come and go with edits too, and the Properties panel shows
    // whatever an edit or an undo just changed. Layers do as well (an undone
    // New Layer, say), so the list is rebuilt when it no longer matches.
    if (_timelineView)
    {
        _timelineView->refreshIfStale();
        _timelineView->update();
    }
    if (_propertiesPanel)
        _propertiesPanel->refresh();

    if (_undoAction)
    {
        _undoAction->setEnabled(stack.canUndo());
        // Name the specific edit, the way Animate does: "Undo Draw Rectangle".
        const QString name = QString::fromStdString(stack.undoName());
        _undoAction->setText(name.isEmpty() ? QString("&Undo") : QString("&Undo %1").arg(name));
    }

    if (_redoAction)
    {
        _redoAction->setEnabled(stack.canRedo());
        const QString name = QString::fromStdString(stack.redoName());
        _redoAction->setText(name.isEmpty() ? QString("&Redo") : QString("&Redo %1").arg(name));
    }

    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    if (_documentName.isEmpty())
    {
        setWindowTitle("Phoenix - FLA Viewer");
        return;
    }

    setWindowTitle(QString("Phoenix - %1%2")
        .arg(_documentName)
        .arg(_editContext.isModified() ? "*" : ""));
}

bool MainWindow::confirmDiscardChanges()
{
    if (!_editContext.isModified())
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
        "Unsaved Changes",
        QString("%1 has unsaved changes.").arg(_documentName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;

    if (answer == QMessageBox::Save)
    {
        saveFile();

        // Saving can still not happen: Save As can be canceled, a lossy save
        // can be declined, and a write can fail. The changes are then still
        // there, and carrying on would throw away what the user asked to keep.
        return !_editContext.isModified();
    }

    return true;
}

void MainWindow::setupToolBar()
{
    // Animate's toolbox: one column down the left of the stage, the tools in
    // groups, a slot with hidden siblings opening them on a long press.
    QToolBar* toolBar = new QToolBar("Tools", this);
    toolBar->setObjectName("toolsToolBar");
    toolBar->setMovable(false);
    toolBar->setOrientation(Qt::Vertical);
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->setIconSize(QSize(22, 22));
    addToolBar(Qt::LeftToolBarArea, toolBar);

    // One action group so the tools behave like a radio button set, the way a
    // toolbox does.
    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    // A tool's action: checkable, in the group, with its key, and known to
    // the window so the key works even when the action sits in a flyout.
    const auto makeTool = [this, toolGroup](ToolIcons::Tool icon, const char* name, Qt::Key key,
                              const char* tip, std::function<Tool*()> tool) {
        QAction* action = new QAction(ToolIcons::icon(icon), name, this);
        action->setCheckable(true);
        if (key != Qt::Key_unknown)
        {
            action->setShortcut(QKeySequence(key));
            action->setToolTip(QString("%1 (%2)").arg(name).arg(QKeySequence(key).toString()));
        }
        else
        {
            action->setToolTip(name);
        }
        action->setStatusTip(tip);
        connect(action, &QAction::triggered, this, [this, tool]() {
            _phoenixView->setActiveTool(tool());
        });
        toolGroup->addAction(action);
        addAction(action);
        return action;
    };

    // A slot in the column: one tool, or a group whose first tool shows and
    // whose others open on a long press, the chosen one taking the slot.
    const auto addSlot = [this, toolBar](const std::vector<QAction*>& actions) {
        if (actions.size() == 1)
        {
            toolBar->addAction(actions.front());
            return;
        }
        QToolButton* button = new QToolButton(toolBar);
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setDefaultAction(actions.front());
        QMenu* flyout = new QMenu(button);
        for (QAction* action : actions)
        {
            flyout->addAction(action);
            connect(action, &QAction::triggered, button, [button, action]() {
                button->setDefaultAction(action);
            });
        }
        button->setMenu(flyout);
        toolBar->addWidget(button);
    };

    QAction* selectionAction = makeTool(ToolIcons::Tool::Selection, "Selection", Qt::Key_V,
        "Select objects. Shift-click to add, drag to sweep a marquee.",
        [this]() -> Tool* { return _selectionTool.get(); });
    selectionAction->setChecked(true);
    addSlot({selectionAction});
    addSlot({makeTool(ToolIcons::Tool::Subselection, "Subselection", Qt::Key_A,
        "Click a path to show its anchors, then drag anchors and handles. Shift-click adds.",
        [this]() -> Tool* { return _subselectionTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::FreeTransform, "Free Transform", Qt::Key_Q,
        "Scale, rotate and skew the selection by its handles.",
        [this]() -> Tool* { return _freeTransformTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Lasso, "Lasso", Qt::Key_L,
        "Draw a loop round the objects to select. Shift adds to the selection.",
        [this]() -> Tool* { return _lassoTool.get(); })});

    toolBar->addSeparator();

    // P for the pen, as in Animate. B is already the bounding-box toggle, and a
    // clash leaves Qt firing neither.
    addSlot({makeTool(ToolIcons::Tool::Pen, "Pen", Qt::Key_P,
        "Click for a corner point, drag for a smooth one. Alt-drag breaks the tangent, "
        "clicking the first point closes the path, Enter finishes it open.",
        [this]() -> Tool* { return _penTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Text, "Text", Qt::Key_T,
        "Click to start a text box that grows as you type, or drag one out to a fixed width. "
        "Escape or a click elsewhere finishes it.",
        [this]() -> Tool* { return _textTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Line, "Line", Qt::Key_N,
        "Drag to draw a line. Shift for 45 degree steps.",
        [this]() -> Tool* { return _lineTool.get(); })});
    addSlot({
        makeTool(ToolIcons::Tool::Rectangle, "Rectangle", Qt::Key_R,
            "Drag to draw a rectangle. Shift for a square.",
            [this]() -> Tool* { return _rectangleTool.get(); }),
        makeTool(ToolIcons::Tool::Oval, "Oval", Qt::Key_O,
            "Drag to draw an oval. Shift for a circle.",
            [this]() -> Tool* { return _ovalTool.get(); }),
        makeTool(ToolIcons::Tool::RectanglePrimitive, "Rectangle Primitive", Qt::Key_unknown,
            "Drag to draw an editable rectangle object with corner radii.",
            [this]() -> Tool* { return _rectanglePrimitiveTool.get(); }),
        makeTool(ToolIcons::Tool::OvalPrimitive, "Oval Primitive", Qt::Key_unknown,
            "Drag to draw an editable oval object with start and end angles.",
            [this]() -> Tool* { return _ovalPrimitiveTool.get(); }),
        makeTool(ToolIcons::Tool::PolyStar, "PolyStar", Qt::Key_Y,
            "Drag from the center outwards. Sides and star mode are in the Properties panel.",
            [this]() -> Tool* { return _polyStarTool.get(); }),
    });
    addSlot({makeTool(ToolIcons::Tool::Pencil, "Pencil", Qt::Key_D,
        "Draw freehand. The stroke is fitted on release; the mode is in the Properties panel.",
        [this]() -> Tool* { return _pencilTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Brush, "Brush", Qt::Key_B,
        "Paint a fill along the drag. The size is in the Properties panel.",
        [this]() -> Tool* { return _brushTool.get(); })});

    toolBar->addSeparator();

    _paintBucketAction = makeTool(ToolIcons::Tool::PaintBucket, "Paint Bucket", Qt::Key_K,
        "Click inside an area to paint it with the current fill.",
        [this]() -> Tool* { return _paintBucketTool.get(); });
    _inkBottleAction = makeTool(ToolIcons::Tool::InkBottle, "Ink Bottle", Qt::Key_S,
        "Click a fill to stroke its outline, or a stroke to restyle it.",
        [this]() -> Tool* { return _inkBottleTool.get(); });
    addSlot({_paintBucketAction, _inkBottleAction});
    addSlot({makeTool(ToolIcons::Tool::Eyedropper, "Eyedropper", Qt::Key_I,
        "Click a fill or a stroke to make it the current one.",
        [this]() -> Tool* { return _eyedropperTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Eraser, "Eraser", Qt::Key_E,
        "Erase fills and strokes along the drag from merge-drawn artwork.",
        [this]() -> Tool* { return _eraserTool.get(); })});

    toolBar->addSeparator();

    addSlot({makeTool(ToolIcons::Tool::Hand, "Hand", Qt::Key_H,
        "Drag to pan the stage. Holding space does the same with any tool.",
        [this]() -> Tool* { return _handTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Zoom, "Zoom", Qt::Key_Z,
        "Click to zoom in, alt-click to zoom out, drag to zoom to an area.",
        [this]() -> Tool* { return _zoomTool.get(); })});

    toolBar->addSeparator();

    // The stroke and fill colors, under the tools as in Animate's toolbox.
    _toolboxStrokeSwatch = new QToolButton(toolBar);
    _toolboxStrokeSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.strokeColor));
    _toolboxStrokeSwatch->setIconSize(QSize(20, 20));
    _toolboxStrokeSwatch->setAutoRaise(true);
    _toolboxStrokeSwatch->setToolTip("Stroke color");
    connect(_toolboxStrokeSwatch, &QToolButton::clicked, this, [this]() { chooseStyleColor(true); });
    toolBar->addWidget(_toolboxStrokeSwatch);

    _toolboxFillSwatch = new QToolButton(toolBar);
    _toolboxFillSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.fillColor));
    _toolboxFillSwatch->setIconSize(QSize(20, 20));
    _toolboxFillSwatch->setAutoRaise(true);
    _toolboxFillSwatch->setToolTip("Fill color");
    connect(_toolboxFillSwatch, &QToolButton::clicked, this, [this]() { chooseStyleColor(false); });
    toolBar->addWidget(_toolboxFillSwatch);

    toolBar->addSeparator();

    // Not a tool, so it stands outside the exclusive group: it changes what
    // every drawing tool does rather than replacing them.
    QAction* objectDrawingAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::ObjectDrawing), "Object Drawing", this);
    objectDrawingAction->setCheckable(true);
    objectDrawingAction->setChecked(_drawingStyle.objectDrawing);
    objectDrawingAction->setShortcut(QKeySequence(Qt::Key_J));
    objectDrawingAction->setToolTip("Object Drawing (J)");
    objectDrawingAction->setStatusTip(
        "Keep each new shape its own object. Off, shapes merge with the artwork beneath: "
        "outlines cut each other and the newer fill replaces the older.");
    connect(objectDrawingAction, &QAction::toggled, this, [this](bool on) {
        _drawingStyle.objectDrawing = on;
    });
    toolBar->addAction(objectDrawingAction);
}

void MainWindow::chooseStyleColor(bool stroke)
{
    uint8_t* target = stroke ? _drawingStyle.strokeColor : _drawingStyle.fillColor;
    const QColor current(target[0], target[1], target[2], target[3]);
    const QColor chosen = QColorDialog::getColor(current, this, stroke ? "Stroke Color" : "Fill Color",
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    target[0] = static_cast<uint8_t>(chosen.red());
    target[1] = static_cast<uint8_t>(chosen.green());
    target[2] = static_cast<uint8_t>(chosen.blue());
    target[3] = static_cast<uint8_t>(chosen.alpha());
    refreshStyleSwatches();
    // Text being typed takes the fill color on its selection too.
    if (!stroke && _textTool)
        _textTool->applyColor(_drawingStyle.fillColor);
}

void MainWindow::setupPropertiesPanel()
{
    QDockWidget* dock = new QDockWidget("Properties", this);
    dock->setObjectName("propertiesDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    PropertiesPanel::Links links;
    links.style = &_drawingStyle;
    links.selection = &_selection;
    links.editContext = &_editContext;
    links.view = _phoenixView;
    links.player = _player;
    links.smoothAction = _smoothAction;
    links.straightenAction = _straightenAction;
    _propertiesPanel = new PropertiesPanel(links, dock);
    dock->setWidget(_propertiesPanel);
    dock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // The Tool tab writes into the drawing style itself; these are the side
    // effects the window has to carry out.
    connect(_propertiesPanel, &PropertiesPanel::toolSettingChanged, this, [this](const QString& key) {
        if (key == "fillColor" || key == "strokeColor")
        {
            refreshStyleSwatches();
            // Text being typed takes the color on its selection too.
            if (key == "fillColor" && _textTool)
                _textTool->applyColor(_drawingStyle.fillColor);
        }
        else if (key == "textFace")
        {
            // A selection in an open text box takes the font; otherwise it is
            // the font for the next text.
            const std::string face = _propertiesPanel->pendingTextFace();
            if (!_textTool || !_textTool->applyFace(face))
                _drawingStyle.textFace = face;
        }
        else if (key == "textSize")
        {
            const double size = _propertiesPanel->pendingTextSize();
            if (!_textTool || !_textTool->applySize(size))
                _drawingStyle.textSize = size;
        }
        else if (key == "textType")
        {
            const fla::StaticText::TextType type = static_cast<fla::StaticText::TextType>(_drawingStyle.textType);
            if (_textTool && _textTool->applyTextType(type))
                return;
            setSelectedTextType(type);
        }
        else if (key == "transformMode")
        {
            if (_freeTransformTool)
                _freeTransformTool->resetBox();
            if (_phoenixView)
                _phoenixView->update();
        }
        else if (key == "faucet")
        {
            if (_phoenixView && _eraserTool && _phoenixView->activeTool() == _eraserTool.get())
                _phoenixView->setCursor(_eraserTool->cursor());
        }
    });

    // The other tabs edit the document through the undo stack; the stage
    // and the timeline redraw from it.
    connect(_propertiesPanel, &PropertiesPanel::documentEdited, this, [this]() {
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        if (_timelineView)
            _timelineView->update();
    });

    // Picking a tool shows its options, as Animate does.
    connect(_phoenixView, &PhoenixView::activeToolChanged, this, [this](Tool* tool) {
        _propertiesPanel->setActiveTool(tool, iconForTool(tool));
        _propertiesPanel->setTab(PropertiesPanel::Tab::Tool);
    });

    // The frame shown and the current layer decide what the Frame tab shows.
    // The stage lets go of the selection when the playhead moves (it hears
    // the signal first), so with nothing left selected the frame is what
    // the panel shows, as Animate does when a frame is clicked.
    connect(_player, &Player::currentFrameChanged, this, [this](int) {
        _propertiesPanel->refresh();
        if (_selection.isEmpty())
            _propertiesPanel->setTab(PropertiesPanel::Tab::Frame);
    });
    connect(_timelineView, &TimelineView::currentLayerChanged, this, [this]() {
        _propertiesPanel->refresh();
    });
}

ToolIcons::Tool MainWindow::iconForTool(const Tool* tool) const
{
    using IconTool = ToolIcons::Tool;
    if (tool == _subselectionTool.get()) return IconTool::Subselection;
    if (tool == _freeTransformTool.get()) return IconTool::FreeTransform;
    if (tool == _rectangleTool.get()) return IconTool::Rectangle;
    if (tool == _ovalTool.get()) return IconTool::Oval;
    if (tool == _lineTool.get()) return IconTool::Line;
    if (tool == _polyStarTool.get()) return IconTool::PolyStar;
    if (tool == _rectanglePrimitiveTool.get()) return IconTool::RectanglePrimitive;
    if (tool == _ovalPrimitiveTool.get()) return IconTool::OvalPrimitive;
    if (tool == _penTool.get()) return IconTool::Pen;
    if (tool == _pencilTool.get()) return IconTool::Pencil;
    if (tool == _handTool.get()) return IconTool::Hand;
    if (tool == _zoomTool.get()) return IconTool::Zoom;
    if (tool == _paintBucketTool.get()) return IconTool::PaintBucket;
    if (tool == _inkBottleTool.get()) return IconTool::InkBottle;
    if (tool == _eyedropperTool.get()) return IconTool::Eyedropper;
    if (tool == _brushTool.get()) return IconTool::Brush;
    if (tool == _eraserTool.get()) return IconTool::Eraser;
    if (tool == _lassoTool.get()) return IconTool::Lasso;
    if (tool == _textTool.get()) return IconTool::Text;
    return IconTool::Selection;
}

void MainWindow::rebuildBreadcrumb()
{
    if (!_breadcrumb || !_phoenixView)
        return;

    QHBoxLayout* layout = static_cast<QHBoxLayout*>(_breadcrumb->layout());
    while (layout->count() > 0)
    {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    const int depth = _phoenixView->scopeDepth();
    for (int level = 0; level <= depth; ++level)
    {
        if (level > 0)
        {
            QLabel* arrow = new QLabel(QString::fromUtf8("\u203a"), _breadcrumb);
            layout->addWidget(arrow);
        }

        QToolButton* button = new QToolButton(_breadcrumb);
        button->setText(_phoenixView->scopeName(level));
        button->setAutoRaise(true);
        button->setEnabled(level < depth);
        connect(button, &QToolButton::clicked, this, [this, level]() {
            _phoenixView->exitToDepth(level);
        });
        layout->addWidget(button);
    }
    layout->addStretch();
}

void MainWindow::refreshStyleSwatches()
{
    if (_propertiesPanel)
        _propertiesPanel->refreshStyleSwatches();
    if (_toolboxStrokeSwatch)
        _toolboxStrokeSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.strokeColor));
    if (_toolboxFillSwatch)
        _toolboxFillSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.fillColor));
}

void MainWindow::selectAll()
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
        return;

    // A rect large enough to cover anything placed off-stage as well.
    const QRectF everything(-1.0e6, -1.0e6, 2.0e6, 2.0e6);

    std::vector<fla::Element*> elements = _phoenixView->elementsIn(everything);

    std::vector<fla::DOMElement*> picked;
    picked.reserve(elements.size());
    for (fla::Element* element : elements)
        picked.push_back(element);

    _selection.select(picked);
    _phoenixView->update();
}

void MainWindow::deselectAll()
{
    _selection.clear();
    if (_phoenixView)
        _phoenixView->update();
}

namespace {

fla::Frame* frameOfElement(const fla::Element* element)
{
    for (fla::DOMElement* p = element ? element->parent : nullptr; p; p = p->parent)
    {
        if (p->domType() == fla::DOMElement::DOMType::Frame)
            return static_cast<fla::Frame*>(p);
    }
    return nullptr;
}

} // namespace

void MainWindow::groupSelection()
{
    if (!_phoenixView)
        return;

    // Only things sitting directly in one frame can be grouped together.
    std::vector<fla::Element*> elements;
    fla::Frame* frame = nullptr;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || !element->parent ||
            element->parent->domType() != fla::DOMElement::DOMType::Frame)
        {
            continue;
        }
        fla::Frame* owner = static_cast<fla::Frame*>(element->parent);
        if (frame && owner != frame)
            continue;
        frame = owner;
        elements.push_back(element);
    }

    if (!frame || elements.empty())
        return;

    // A region lifted out of a shape is grouped as it is: flushing it would
    // merge it straight back into its source first.
    fla::CommandStack& stack = _editContext.commandStack();
    stack.push(fla::CommandPtr(new fla::GroupElementsCommand(frame, elements, "Group", &_selection)));
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::ungroupSelection()
{
    if (!_phoenixView)
        return;

    std::vector<fla::Group*> groups;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (element && element->elementType() == fla::Element::Type::Group && frameOfElement(element))
            groups.push_back(static_cast<fla::Group*>(element));
    }
    if (groups.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    if (groups.size() > 1)
        stack.beginMacro("Ungroup");
    for (fla::Group* group : groups)
    {
        stack.push(fla::CommandPtr(new fla::UngroupCommand(
            frameOfElement(group), group, "Ungroup", &_selection)));
    }
    if (groups.size() > 1)
        stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::breakApartSelection()
{
    if (!_phoenixView)
        return;

    // Groups come apart; drawing objects become merge artwork, which floats
    // selected until let go, like a fresh drawing; a symbol instance is
    // replaced by a copy of what it shows, whose merge artwork floats the
    // same way.
    std::vector<fla::Group*> groups;
    std::vector<fla::Shape*> drawingObjects;
    std::vector<fla::SymbolInstance*> instances;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || !frameOfElement(element))
            continue;
        if (element->elementType() == fla::Element::Type::Group)
            groups.push_back(static_cast<fla::Group*>(element));
        else if (element->elementType() == fla::Element::Type::Shape &&
            static_cast<fla::Shape*>(element)->isDrawingObject)
            drawingObjects.push_back(static_cast<fla::Shape*>(element));
        else if (element->elementType() == fla::Element::Type::SymbolInstance)
            instances.push_back(static_cast<fla::SymbolInstance*>(element));
    }
    if (groups.empty() && drawingObjects.empty() && instances.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Break Apart");
    for (fla::Group* group : groups)
    {
        stack.push(fla::CommandPtr(new fla::UngroupCommand(
            frameOfElement(group), group, "Break Apart", &_selection)));
    }
    for (fla::SymbolInstance* instance : instances)
    {
        fla::Frame* frame = frameOfElement(instance);
        fla::BreakApartInstanceCommand* command =
            new fla::BreakApartInstanceCommand(frame, instance, "Break Apart", &_selection);
        const std::vector<fla::Element*> pieces = command->pieces();
        stack.push(fla::CommandPtr(command));
        for (fla::Element* piece : pieces)
        {
            if (piece->elementType() == fla::Element::Type::Shape &&
                !static_cast<fla::Shape*>(piece)->isDrawingObject)
            {
                _phoenixView->setPendingMerge(static_cast<fla::Shape*>(piece), frame);
            }
        }
    }
    for (fla::Shape* shape : drawingObjects)
    {
        stack.push(fla::CommandPtr(new fla::SetDrawingObjectCommand(shape, false, "Break Apart")));
        _phoenixView->setPendingMerge(shape, frameOfElement(shape));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::tidySelectedShapes(const char* name,
    const std::function<void(fla::EditablePath&)>& tidy)
{
    if (!_phoenixView)
        return;

    const std::vector<fla::Shape*> shapes = selectedShapes();
    if (shapes.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    std::vector<fla::CommandPtr> commands;
    for (fla::Shape* shape : shapes)
    {
        // The command snapshots the shape as its "before", so the change is
        // made on a copy that becomes the "after".
        std::unique_ptr<fla::Shape> after(fla::cloneShape(*shape, nullptr));
        bool changed = false;
        for (fla::Edge* edge : after->edges)
        {
            if (!edge)
                continue;
            for (size_t pathIndex = 0; pathIndex < edge->paths.size(); ++pathIndex)
            {
                if (!edge->paths[pathIndex])
                    continue;
                fla::EditablePath path = fla::EditablePath::fromPath(*edge->paths[pathIndex]);
                if (path.anchors.size() < 2)
                    continue;
                tidy(path);
                if (path.anchors.size() < 2)
                    continue;
                path.applyTo(*edge, pathIndex);
                changed = true;
            }
        }
        if (!changed)
            continue;
        commands.push_back(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, name)));
    }

    if (commands.empty())
        return;

    const bool needsMacro = commands.size() > 1;
    if (needsMacro)
        stack.beginMacro(name);
    for (fla::CommandPtr& command : commands)
        stack.push(std::move(command));
    if (needsMacro)
        stack.endMacro();
    stack.breakMergeChain();

    for (fla::Shape* shape : shapes)
        _phoenixView->markShapeEdited(shape);
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::smoothSelection()
{
    // Animate's Smooth is a fixed, gentle amount each time it is applied.
    tidySelectedShapes("Smooth", [](fla::EditablePath& path) {
        fla::smoothPath(path, 0.5);
    });
}

void MainWindow::straightenSelection()
{
    const double tolerance = 2.0 / qMax(_phoenixView ? _phoenixView->zoom() : 1.0, 1.0e-6);
    tidySelectedShapes("Straighten", [tolerance](fla::EditablePath& path) {
        fla::straightenPath(path, tolerance);
    });
}

void MainWindow::optimizeSelection()
{
    bool ok = false;
    const int amount = QInputDialog::getInt(this, "Optimize Curves",
        "Optimization strength (0 keeps the shape, 100 simplifies most):", 50, 0, 100, 1, &ok);
    if (!ok)
        return;

    // The strength is a tolerance in screen pixels, like the pencil's smoothing.
    const double screenTolerance = 0.25 + 4.75 * (amount / 100.0);
    const double tolerance = screenTolerance / qMax(_phoenixView ? _phoenixView->zoom() : 1.0, 1.0e-6);
    tidySelectedShapes("Optimize", [tolerance](fla::EditablePath& path) {
        fla::optimizePath(path, tolerance);
    });
}

void MainWindow::reshapeSelectedShapes(const char* name,
    const std::function<bool(fla::Shape&, fla::Shape&)>& change)
{
    if (!_phoenixView)
        return;

    const std::vector<fla::Shape*> shapes = selectedShapes();
    if (shapes.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    std::vector<fla::CommandPtr> commands;
    for (fla::Shape* shape : shapes)
    {
        std::unique_ptr<fla::Shape> after(fla::cloneShape(*shape, nullptr));
        if (!change(*shape, *after))
            continue;
        commands.push_back(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, name)));
    }
    if (commands.empty())
        return;

    const bool needsMacro = commands.size() > 1;
    if (needsMacro)
        stack.beginMacro(name);
    for (fla::CommandPtr& command : commands)
        stack.push(std::move(command));
    if (needsMacro)
        stack.endMacro();
    stack.breakMergeChain();

    for (fla::Shape* shape : shapes)
        _phoenixView->markShapeEdited(shape);
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::convertLinesToFillsSelection()
{
    reshapeSelectedShapes("Convert Lines to Fills", [](fla::Shape&, fla::Shape& after) {
        return convertLinesToFills(after);
    });
}

namespace {

/// Animate's Expand Fill and Soften Fill Edges dialogs: a distance, for
/// softening a number of steps, and whether to go outward or inward.
bool askFillOffset(QWidget* parent, const QString& title, bool withSteps,
    double& distance, int& steps, bool& inset)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QFormLayout* form = new QFormLayout(&dialog);

    QDoubleSpinBox* distanceBox = new QDoubleSpinBox(&dialog);
    distanceBox->setRange(0.1, 1000.0);
    distanceBox->setDecimals(1);
    distanceBox->setSuffix(" px");
    distanceBox->setValue(distance);
    form->addRow("Distance:", distanceBox);

    QSpinBox* stepsBox = nullptr;
    if (withSteps)
    {
        stepsBox = new QSpinBox(&dialog);
        stepsBox->setRange(1, 50);
        stepsBox->setValue(steps);
        form->addRow("Number of steps:", stepsBox);
    }

    QWidget* direction = new QWidget(&dialog);
    QHBoxLayout* directionLayout = new QHBoxLayout(direction);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    QRadioButton* expand = new QRadioButton("Expand", direction);
    QRadioButton* insetButton = new QRadioButton("Inset", direction);
    (inset ? insetButton : expand)->setChecked(true);
    directionLayout->addWidget(expand);
    directionLayout->addWidget(insetButton);
    directionLayout->addStretch();
    form->addRow("Direction:", direction);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    // The distance is what is usually retyped, so it comes selected.
    distanceBox->setFocus();
    distanceBox->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    distance = distanceBox->value();
    if (stepsBox)
        steps = stepsBox->value();
    inset = insetButton->isChecked();
    return true;
}

} // namespace

void MainWindow::expandFillSelection()
{
    // The last settings are offered again, as Animate does.
    static double distance = 4.0;
    static bool inset = false;
    int steps = 1;
    if (!askFillOffset(this, "Expand Fill", false, distance, steps, inset))
        return;

    reshapeSelectedShapes("Expand Fill", [this](fla::Shape& shape, fla::Shape& after) {
        return fla::filloffset::expandFill(after, _phoenixView->fillRegions(shape), distance, inset);
    });
}

void MainWindow::softenFillEdgesSelection()
{
    static double distance = 8.0;
    static int steps = 4;
    static bool inset = false;
    if (!askFillOffset(this, "Soften Fill Edges", true, distance, steps, inset))
        return;

    reshapeSelectedShapes("Soften Fill Edges", [this](fla::Shape& shape, fla::Shape& after) {
        return fla::filloffset::softenFillEdges(after, _phoenixView->fillRegions(shape), distance, steps, inset);
    });
}

void MainWindow::convertToSymbol()
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;

    // Everything selected that sits directly in one frame.
    std::vector<fla::Element*> elements;
    fla::Frame* frame = nullptr;
    QRectF bounds;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || !element->parent ||
            element->parent->domType() != fla::DOMElement::DOMType::Frame)
        {
            continue;
        }
        fla::Frame* owner = static_cast<fla::Frame*>(element->parent);
        if (frame && owner != frame)
            continue;
        frame = owner;
        elements.push_back(element);

        const QRectF elementBounds = _phoenixView->elementBounds(element);
        if (elementBounds.isValid())
            bounds = bounds.isValid() ? bounds.united(elementBounds) : elementBounds;
    }
    if (!frame || elements.empty())
        return;

    // A region lifted out of a shape is converted as it is, like a selected
    // fill in Animate; flushing it would merge it back into its source first.

    // The first name not already in the library, as Animate numbers them,
    // offered in the dialog.
    fla::Document* document = _flaDocument->document;
    std::string symbolName;
    for (int n = 1; ; ++n)
    {
        symbolName = "Symbol " + std::to_string(n);
        if (!document->symbolList || document->symbolList->symbolMap.count(symbolName) == 0)
            break;
    }

    ConvertToSymbolDialog dialog(QString::fromStdString(symbolName), this);
    for (;;)
    {
        if (dialog.exec() != QDialog::Accepted)
            return;

        // The name has to be one the library does not have; Animate refuses
        // a clash rather than renaming behind the user's back.
        symbolName = dialog.symbolName().toStdString();
        if (symbolName.empty())
        {
            QMessageBox::warning(this, "Convert to Symbol", "Please enter a name for the symbol.");
            continue;
        }
        if (document->symbolList && document->symbolList->symbolMap.count(symbolName) != 0)
        {
            QMessageBox::warning(this, "Convert to Symbol",
                QString("A symbol named \"%1\" already exists in the library.")
                    .arg(QString::fromStdString(symbolName)));
            continue;
        }
        break;
    }

    // The registration point is the corner, edge or center of the selection
    // the dialog's grid picked.
    QPointF registration;
    if (bounds.isValid())
    {
        const QPointF fraction = dialog.registrationFraction();
        registration = QPointF(bounds.left() + bounds.width() * fraction.x(),
            bounds.top() + bounds.height() * fraction.y());
    }

    fla::CommandStack& stack = _editContext.commandStack();
    stack.push(fla::CommandPtr(new fla::ConvertToSymbolCommand(document, frame, elements,
        symbolName, registration.x(), registration.y(), "Convert to Symbol", &_selection,
        dialog.symbolType())));
    stack.breakMergeChain();

    // The library gained an item, which the document tree lists.
    _documentView->setDocument(_flaDocument);
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::arrangeSelection(int step, bool toEnd)
{
    if (!_phoenixView)
        return;

    // Each selected element in its own frame, ordered so that moving one does
    // not shuffle the next out of place: towards the front from the top down,
    // towards the back from the bottom up.
    struct Entry
    {
        fla::Frame* frame;
        fla::Element* element;
        int index;
    };
    std::vector<Entry> entries;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || !element->parent ||
            element->parent->domType() != fla::DOMElement::DOMType::Frame)
        {
            continue;
        }
        fla::Frame* frame = static_cast<fla::Frame*>(element->parent);
        const auto it = std::find(frame->elements.begin(), frame->elements.end(), element);
        if (it == frame->elements.end())
            continue;
        entries.push_back({frame, element, static_cast<int>(it - frame->elements.begin())});
    }
    if (entries.empty())
        return;

    std::sort(entries.begin(), entries.end(), [step](const Entry& a, const Entry& b) {
        return step > 0 ? a.index > b.index : a.index < b.index;
    });

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(step > 0 ? (toEnd ? "Bring to Front" : "Bring Forward")
                              : (toEnd ? "Send to Back" : "Send Backward"));
    for (const Entry& entry : entries)
    {
        const int count = static_cast<int>(entry.frame->elements.size());
        const int target = toEnd ? (step > 0 ? count - 1 : 0) : entry.index + step;
        if (target == entry.index || target < 0 || target >= count)
            continue;
        stack.push(fla::CommandPtr(new fla::ReorderElementCommand(
            entry.frame, entry.element, target, "Arrange")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

std::vector<fla::Shape*> MainWindow::selectedShapes() const
{
    std::vector<fla::Shape*> shapes;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (element && element->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<fla::Shape*>(element));
    }
    return shapes;
}

void MainWindow::copy()
{
    const std::vector<fla::Shape*> shapes = selectedShapes();
    if (shapes.empty())
        return;

    _clipboard.clear();
    for (const fla::Shape* shape : shapes)
        _clipboard.emplace_back(fla::cloneShape(*shape, nullptr));

    updateSelectionState();
}

void MainWindow::cut()
{
    if (selectedShapes().empty())
        return;

    copy();
    if (_phoenixView)
        _phoenixView->deleteSelection();
}

void MainWindow::placeCopies(const std::vector<const fla::Shape*>& sources, const char* name,
    bool centerInView)
{
    if (!_phoenixView || sources.empty())
        return;

    fla::Frame* frame = _phoenixView->activeFrame();
    if (!frame)
        return;

    // Anything still waiting to merge goes in first, so the copies land on
    // the artwork as it will be rather than on a drawing about to vanish.
    _phoenixView->flushPendingMerge();

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(name);

    std::vector<fla::Shape*> copies;
    for (const fla::Shape* source : sources)
    {
        fla::Shape* copy = fla::cloneShape(*source, frame);
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, copy, name, &_selection)));
        copies.push_back(copy);
    }

    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();

    if (centerInView)
    {
        QRectF bounds;
        for (fla::Shape* copy : copies)
        {
            const QRectF copyBounds = _phoenixView->elementBounds(copy);
            if (copyBounds.isValid())
                bounds = bounds.isValid() ? bounds.united(copyBounds) : copyBounds;
        }

        if (bounds.isValid())
        {
            const QPointF center = _phoenixView->mapToDocument(
                QPointF(_phoenixView->width() / 2.0, _phoenixView->height() / 2.0));
            const QPointF delta = center - bounds.center();
            for (fla::Shape* copy : copies)
            {
                copy->transform.tx += delta.x();
                copy->transform.ty += delta.y();
            }
            _phoenixView->invalidateBounds();
        }
    }

    // Pasted merge artwork floats, selected, until it is let go, just like a
    // fresh drawing; drawing objects keep to themselves.
    std::vector<fla::DOMElement*> picked;
    for (fla::Shape* copy : copies)
    {
        if (!copy->isDrawingObject)
            _phoenixView->setPendingMerge(copy, frame);
        picked.push_back(copy);
    }
    _selection.select(picked);
    _phoenixView->update();
}

void MainWindow::paste()
{
    std::vector<const fla::Shape*> sources;
    for (const std::unique_ptr<fla::Shape>& shape : _clipboard)
        sources.push_back(shape.get());
    placeCopies(sources, "Paste", true);
}

void MainWindow::pasteInPlace()
{
    std::vector<const fla::Shape*> sources;
    for (const std::unique_ptr<fla::Shape>& shape : _clipboard)
        sources.push_back(shape.get());
    placeCopies(sources, "Paste in Place", false);
}

void MainWindow::duplicateSelection()
{
    // Copies are taken before anything is added, since adding changes the
    // selection the sources came from.
    std::vector<std::unique_ptr<fla::Shape>> snapshots;
    for (const fla::Shape* shape : selectedShapes())
        snapshots.emplace_back(fla::cloneShape(*shape, nullptr));

    std::vector<const fla::Shape*> sources;
    for (const std::unique_ptr<fla::Shape>& shape : snapshots)
        sources.push_back(shape.get());
    placeCopies(sources, "Duplicate", false);
}

void MainWindow::setSelectedTextType(fla::StaticText::TextType type)
{
    if (!_phoenixView)
        return;

    // Every selected text field, each as its own undo step inside one.
    std::vector<fla::StaticText*> texts;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (element && element->elementType() == fla::Element::Type::StaticText)
            texts.push_back(static_cast<fla::StaticText*>(element));
    }
    if (texts.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Text Type");
    for (fla::StaticText* text : texts)
    {
        if (text->textType == type)
            continue;
        std::unique_ptr<fla::StaticText> after(
            static_cast<fla::StaticText*>(fla::cloneElement(*text, nullptr)));
        after->textType = type;
        stack.push(fla::CommandPtr(new fla::SetTextContentsCommand(text, *text, *after, "Text Type")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::updateSelectionState()
{
    const bool hasDocument = _flaDocument && _flaDocument->document;

    if (_selectAllAction)
        _selectAllAction->setEnabled(hasDocument);

    if (_deselectAllAction)
        _deselectAllAction->setEnabled(!_selection.isEmpty());

    const bool hasShapes = !selectedShapes().empty();
    const bool hasElements = !_selection.isEmpty();
    if (_groupAction)
        _groupAction->setEnabled(_selection.count() >= 1);
    bool hasGroup = false;
    bool hasDrawingObject = false;
    bool hasInstance = false;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element)
            continue;
        if (element->elementType() == fla::Element::Type::Group)
            hasGroup = true;
        if (element->elementType() == fla::Element::Type::Shape &&
            static_cast<fla::Shape*>(element)->isDrawingObject)
            hasDrawingObject = true;
        if (element->elementType() == fla::Element::Type::SymbolInstance)
            hasInstance = true;
    }
    if (_ungroupAction)
        _ungroupAction->setEnabled(hasGroup);
    if (_breakApartAction)
        _breakApartAction->setEnabled(hasGroup || hasDrawingObject || hasInstance);
    for (QAction* action : _arrangeActions)
    {
        if (action)
            action->setEnabled(hasElements);
    }
    if (_convertToSymbolAction)
        _convertToSymbolAction->setEnabled(hasElements);
    for (QAction* action : {_smoothAction, _straightenAction, _optimizeAction, _linesToFillsAction,
             _expandFillAction, _softenFillAction})
    {
        if (action)
            action->setEnabled(hasShapes);
    }
    if (_cutAction)
        _cutAction->setEnabled(hasShapes);
    if (_copyAction)
        _copyAction->setEnabled(hasShapes);
    if (_duplicateAction)
        _duplicateAction->setEnabled(hasShapes);
    if (_pasteAction)
        _pasteAction->setEnabled(hasDocument && !_clipboard.empty());
    if (_pasteInPlaceAction)
        _pasteInPlaceAction->setEnabled(hasDocument && !_clipboard.empty());

    if (_selection.isEmpty())
    {
        statusBar()->clearMessage();
    }
    else
    {
        statusBar()->showMessage(_selection.count() == 1
            ? QString("1 object selected")
            : QString("%1 objects selected").arg(_selection.count()));
    }
}

void MainWindow::onVisibilityChanged()
{
    // Clear caches and trigger a repaint when visibility changes
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        if (!confirmDiscardChanges())
            return;

        QString filePath = action->data().toString();
        loadFLAFile(filePath);
    }
}

void MainWindow::updateRecentFilesMenu()
{
    _recentFilesMenu->clear();

    if (_recentFiles.isEmpty())
    {
        QAction* noRecentAction = _recentFilesMenu->addAction("No recent files");
        noRecentAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < _recentFiles.size() && i < MAX_RECENT_FILES; ++i)
    {
        QString filePath = _recentFiles[i];
        QFileInfo fileInfo(filePath);
        QString displayName;
        if (filePath.endsWith(".xml"))
        {
            QDir parentDir = fileInfo.dir();
            displayName = parentDir.dirName() + "/" + fileInfo.fileName();
        }
        else
        {
            displayName = fileInfo.fileName();
        }
        QAction* action = _recentFilesMenu->addAction(QString("&%1 %2").arg(i + 1).arg(displayName));
        action->setData(filePath);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentFile);
    }
}

void MainWindow::addToRecentFiles(const QString& filePath)
{
    if (_recentFiles.length() > 0 && _recentFiles[0] == filePath)
        return;

    _recentFiles.removeAll(filePath);
    _recentFiles.prepend(filePath);

    while (_recentFiles.size() > MAX_RECENT_FILES)
    {
        _recentFiles.removeLast();
    }

    updateRecentFilesMenu();
    saveSettings();
}

void MainWindow::loadSettings()
{
    QSettings settings;
    _recentFiles = settings.value("recentFiles").toStringList();
    _lastDirectory = settings.value("lastDirectory").toString();
    _highQualityAntiAliasing = settings.value("view/highQualityAntiAliasing", true).toBool();

    QByteArray geometry = settings.value("window/geometry").toByteArray();
    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("recentFiles", _recentFiles);
    settings.setValue("lastDirectory", _lastDirectory);
    settings.setValue("view/highQualityAntiAliasing", _highQualityAntiAliasing);
    settings.setValue("window/geometry", saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscardChanges())
    {
        event->ignore();
        return;
    }

    saveSettings();
    event->accept();
}

void MainWindow::onHighQualityAntiAliasingToggled(bool checked)
{
    _highQualityAntiAliasing = checked;
    _phoenixView->setHighQualityAntiAliasing(checked);
    saveSettings();
}

QIcon MainWindow::createPhoenixIcon()
{
    // Create multiple sizes for better quality at different scales
    QIcon icon;
    QList<int> sizes = {16, 32, 48, 64, 128, 256};

    for (int size : sizes)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        // Scale factor for drawing
        double scale = size / 256.0;
        painter.scale(scale, scale);

        // Define colors - orange to red gradient for phoenix
        QLinearGradient bodyGradient(128, 80, 128, 200);
        bodyGradient.setColorAt(0, QColor(255, 165, 0));  // Orange
        bodyGradient.setColorAt(1, QColor(220, 20, 60));  // Crimson

        QLinearGradient wingGradient(50, 100, 200, 150);
        wingGradient.setColorAt(0, QColor(255, 69, 0));   // Red-Orange
        wingGradient.setColorAt(0.5, QColor(255, 140, 0)); // Dark Orange
        wingGradient.setColorAt(1, QColor(255, 215, 0));  // Gold

        // Draw stylized phoenix bird

        // Left wing (flame-like)
        QPainterPath leftWing;
        leftWing.moveTo(128, 140);
        leftWing.cubicTo(80, 120, 40, 100, 30, 80);
        leftWing.cubicTo(25, 70, 35, 60, 50, 70);
        leftWing.cubicTo(60, 75, 70, 80, 80, 90);
        leftWing.cubicTo(90, 100, 100, 110, 110, 120);
        leftWing.lineTo(128, 140);
        painter.fillPath(leftWing, QBrush(wingGradient));

        // Right wing (flame-like)
        QPainterPath rightWing;
        rightWing.moveTo(128, 140);
        rightWing.cubicTo(176, 120, 216, 100, 226, 80);
        rightWing.cubicTo(231, 70, 221, 60, 206, 70);
        rightWing.cubicTo(196, 75, 186, 80, 176, 90);
        rightWing.cubicTo(166, 100, 156, 110, 146, 120);
        rightWing.lineTo(128, 140);
        painter.fillPath(rightWing, QBrush(wingGradient));

        // Body (bird body shape)
        QPainterPath body;
        body.moveTo(128, 100);
        body.cubicTo(140, 100, 150, 110, 150, 130);
        body.cubicTo(150, 150, 145, 170, 135, 180);
        body.cubicTo(130, 185, 128, 190, 128, 195);
        body.cubicTo(128, 190, 126, 185, 121, 180);
        body.cubicTo(111, 170, 106, 150, 106, 130);
        body.cubicTo(106, 110, 116, 100, 128, 100);
        painter.fillPath(body, QBrush(bodyGradient));

        // Head
        QRadialGradient headGradient(128, 90, 15);
        headGradient.setColorAt(0, QColor(255, 200, 0));  // Bright yellow
        headGradient.setColorAt(1, QColor(255, 140, 0));  // Orange
        painter.setBrush(QBrush(headGradient));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(128, 90), 15, 15);

        // Eye
        painter.setBrush(QBrush(QColor(50, 50, 50)));
        painter.drawEllipse(QPointF(135, 88), 3, 3);

        // Beak
        QPainterPath beak;
        beak.moveTo(143, 90);
        beak.lineTo(153, 92);
        beak.lineTo(143, 94);
        beak.closeSubpath();
        painter.setBrush(QBrush(QColor(255, 215, 0)));
        painter.drawPath(beak);

        // Tail feathers (flame-like)
        QPainterPath tail;
        tail.moveTo(128, 195);
        tail.cubicTo(120, 210, 115, 230, 118, 250);
        tail.cubicTo(120, 240, 124, 220, 128, 210);
        tail.cubicTo(132, 220, 136, 240, 138, 250);
        tail.cubicTo(141, 230, 136, 210, 128, 195);
        QLinearGradient tailGradient(128, 195, 128, 250);
        tailGradient.setColorAt(0, QColor(255, 140, 0));
        tailGradient.setColorAt(1, QColor(255, 69, 0));
        painter.fillPath(tail, QBrush(tailGradient));

        // Flame accents on wings
        painter.setPen(QPen(QColor(255, 215, 0, 150), 2));
        painter.drawLine(50, 75, 80, 95);
        painter.drawLine(206, 75, 176, 95);

        painter.end();
        icon.addPixmap(pixmap);
    }

    return icon;
}
