#include "main_window.h"
#include "convert_to_symbol_dialog.h"
#include "lines_to_fills.h"
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
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
#include <QInputDialog>

#include <memory>
#include <QAction>
#include <QStatusBar>
#include <QTextEdit>
#include <QDialog>
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

#include "tool_icons.h"

MainWindow::MainWindow(QWidget *parent)
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

    // Auto-open last file if available
    if (!_recentFiles.isEmpty())
    {
        loadFLAFile(_recentFiles.first());
    }
}

void MainWindow::releaseDocument()
{
    // A drawing still waiting to merge points into the document too, and must
    // not outlive it. It is dropped rather than committed: the document is on
    // its way out either way.
    if (_phoenixView)
        _phoenixView->clearPendingMerge();

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

    connect(_documentView, &DocumentView::elementSelected,
            _phoenixView, &PhoenixView::onElementSelected);

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
        fla::FLAWriter writer;
        if (!writer.write(*_flaDocument->document, filePath.toStdString()))
            error = QString::fromStdString(writer.errorString());
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

        // Saving can still not happen: Save As can be cancelled, a lossy save
        // can be declined, and a write can fail. The changes are then still
        // there, and carrying on would throw away what the user asked to keep.
        return !_editContext.isModified();
    }

    return true;
}

void MainWindow::setupToolBar()
{
    QToolBar* toolBar = addToolBar("Tools");
    toolBar->setObjectName("toolsToolBar");
    toolBar->setMovable(false);
    // Icons only: nine tools named in full make a very wide bar, and the name
    // stays available as a tooltip.
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->setIconSize(QSize(22, 22));

    // One action group so the tools behave like a radio button set, the way a
    // toolbox does.
    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    QAction* selectionAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::Selection), "Selection", this);
    selectionAction->setCheckable(true);
    selectionAction->setChecked(true);
    selectionAction->setShortcut(QKeySequence(Qt::Key_V));
    selectionAction->setStatusTip(
        "Select objects. Shift-click to add, drag to sweep a marquee.");
    connect(selectionAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_selectionTool.get());
    });
    toolGroup->addAction(selectionAction);
    toolBar->addAction(selectionAction);

    QAction* subselectionAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::Subselection), "Subselection", this);
    subselectionAction->setCheckable(true);
    subselectionAction->setShortcut(QKeySequence(Qt::Key_A));
    subselectionAction->setStatusTip(
        "Show a shape's anchors and drag them. Alt breaks a smooth point's tangent.");
    connect(subselectionAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_subselectionTool.get());
    });
    toolGroup->addAction(subselectionAction);
    toolBar->addAction(subselectionAction);

    QAction* freeTransformAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::FreeTransform), "Free Transform", this);
    freeTransformAction->setCheckable(true);
    freeTransformAction->setShortcut(QKeySequence(Qt::Key_Q));
    freeTransformAction->setStatusTip(
        "Move, scale, rotate and skew the selection. Shift constrains, Escape cancels.");
    connect(freeTransformAction, &QAction::triggered, this, [this]() {
        _freeTransformTool->resetBox();
        _phoenixView->setActiveTool(_freeTransformTool.get());
    });
    toolGroup->addAction(freeTransformAction);
    toolBar->addAction(freeTransformAction);

    QAction* lassoAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Lasso), "Lasso", this);
    lassoAction->setCheckable(true);
    lassoAction->setShortcut(QKeySequence(Qt::Key_L));
    lassoAction->setStatusTip("Draw a loop round the objects to select. Shift adds to the selection.");
    connect(lassoAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_lassoTool.get());
    });
    toolGroup->addAction(lassoAction);
    toolBar->addAction(lassoAction);

    toolBar->addSeparator();

    // The shape tools all behave the same way, so they are wired from a table
    // rather than four near-identical blocks.
    struct ShapeEntry
    {
        const char* label;
        ToolIcons::Tool icon;
        Qt::Key shortcut;
        const char* tip;
        std::unique_ptr<PrimitiveTool>* tool;
    };

    const ShapeEntry shapes[] = {
        {"Rectangle", ToolIcons::Tool::Rectangle, Qt::Key_R,
            "Drag to draw a rectangle. Shift for a square.", &_rectangleTool},
        {"Oval", ToolIcons::Tool::Oval, Qt::Key_O,
            "Drag to draw an oval. Shift for a circle.", &_ovalTool},
        {"Line", ToolIcons::Tool::Line, Qt::Key_N,
            "Drag to draw a line. Shift for 45 degree steps.", &_lineTool},
        {"PolyStar", ToolIcons::Tool::PolyStar, Qt::Key_Y,
            "Drag from the centre outwards. Sides and star mode are in the "
            "Properties panel.", &_polyStarTool},
        {"Rectangle Primitive", ToolIcons::Tool::RectanglePrimitive, Qt::Key_unknown,
            "Drag to draw an editable rectangle object with corner radii.",
            &_rectanglePrimitiveTool},
        {"Oval Primitive", ToolIcons::Tool::OvalPrimitive, Qt::Key_unknown,
            "Drag to draw an editable oval object with start and end angles.",
            &_ovalPrimitiveTool},
    };

    for (const ShapeEntry& shape : shapes)
    {
        QAction* action = new QAction(ToolIcons::icon(shape.icon), shape.label, this);
        action->setCheckable(true);
        if (shape.shortcut != Qt::Key_unknown)
            action->setShortcut(QKeySequence(shape.shortcut));
        action->setStatusTip(shape.tip);
        std::unique_ptr<PrimitiveTool>* tool = shape.tool;
        connect(action, &QAction::triggered, this, [this, tool]() {
            _phoenixView->setActiveTool(tool->get());
        });
        toolGroup->addAction(action);
        toolBar->addAction(action);
    }

    toolBar->addSeparator();

    QAction* penAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::Pen), "Pen", this);
    penAction->setCheckable(true);
    // P for the pen, as in Animate. B is already the bounding-box toggle, and a
    // clash leaves Qt firing neither.
    penAction->setShortcut(QKeySequence(Qt::Key_P));
    penAction->setStatusTip(
        "Click for a corner point, drag for a smooth one. Alt-drag breaks the tangent, "
        "clicking the first point closes the path, Enter finishes it open.");
    connect(penAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_penTool.get());
    });
    toolGroup->addAction(penAction);
    toolBar->addAction(penAction);

    QAction* pencilAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::Pencil), "Pencil", this);
    pencilAction->setCheckable(true);
    pencilAction->setShortcut(QKeySequence(Qt::Key_D));
    pencilAction->setStatusTip(
        "Draw freehand. The stroke is fitted on release; the mode is in the "
        "Properties panel.");
    connect(pencilAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_pencilTool.get());
    });
    toolGroup->addAction(pencilAction);
    toolBar->addAction(pencilAction);

    QAction* textAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Text), "Text", this);
    textAction->setCheckable(true);
    textAction->setShortcut(QKeySequence(Qt::Key_T));
    textAction->setStatusTip(
        "Click to start a text box that grows as you type, or drag one out to a fixed width. "
        "Escape or a click elsewhere finishes it.");
    connect(textAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_textTool.get());
    });
    toolGroup->addAction(textAction);
    toolBar->addAction(textAction);

    toolBar->addSeparator();

    QAction* handAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Hand), "Hand", this);
    handAction->setCheckable(true);
    handAction->setShortcut(QKeySequence(Qt::Key_H));
    handAction->setStatusTip("Drag to pan the stage. Holding space does the same with any tool.");
    connect(handAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_handTool.get());
    });
    toolGroup->addAction(handAction);
    toolBar->addAction(handAction);

    QAction* brushAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Brush), "Brush", this);
    brushAction->setCheckable(true);
    brushAction->setShortcut(QKeySequence(Qt::Key_B));
    brushAction->setStatusTip("Paint a fill along the drag. The size is in the Properties panel.");
    connect(brushAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_brushTool.get());
    });
    toolGroup->addAction(brushAction);
    toolBar->addAction(brushAction);

    QAction* eraserAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Eraser), "Eraser", this);
    eraserAction->setCheckable(true);
    eraserAction->setShortcut(QKeySequence(Qt::Key_E));
    eraserAction->setStatusTip("Erase fills and strokes along the drag from merge-drawn artwork.");
    connect(eraserAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_eraserTool.get());
    });
    toolGroup->addAction(eraserAction);
    toolBar->addAction(eraserAction);

    _paintBucketAction = new QAction(ToolIcons::icon(ToolIcons::Tool::PaintBucket), "Paint Bucket", this);
    _paintBucketAction->setCheckable(true);
    _paintBucketAction->setShortcut(QKeySequence(Qt::Key_K));
    _paintBucketAction->setStatusTip("Click inside an area to paint it with the current fill.");
    connect(_paintBucketAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_paintBucketTool.get());
    });
    toolGroup->addAction(_paintBucketAction);
    toolBar->addAction(_paintBucketAction);

    _inkBottleAction = new QAction(ToolIcons::icon(ToolIcons::Tool::InkBottle), "Ink Bottle", this);
    _inkBottleAction->setCheckable(true);
    _inkBottleAction->setShortcut(QKeySequence(Qt::Key_S));
    _inkBottleAction->setStatusTip("Click a fill to stroke its outline, or a stroke to restyle it.");
    connect(_inkBottleAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_inkBottleTool.get());
    });
    toolGroup->addAction(_inkBottleAction);
    toolBar->addAction(_inkBottleAction);

    QAction* eyedropperAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Eyedropper), "Eyedropper", this);
    eyedropperAction->setCheckable(true);
    eyedropperAction->setShortcut(QKeySequence(Qt::Key_I));
    eyedropperAction->setStatusTip("Click a fill or a stroke to make it the current one.");
    connect(eyedropperAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_eyedropperTool.get());
    });
    toolGroup->addAction(eyedropperAction);
    toolBar->addAction(eyedropperAction);

    toolBar->addSeparator();

    QAction* zoomAction = new QAction(ToolIcons::icon(ToolIcons::Tool::Zoom), "Zoom", this);
    zoomAction->setCheckable(true);
    zoomAction->setShortcut(QKeySequence(Qt::Key_Z));
    zoomAction->setStatusTip("Click to zoom in, alt-click to zoom out, drag to zoom to an area.");
    connect(zoomAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_zoomTool.get());
    });
    toolGroup->addAction(zoomAction);
    toolBar->addAction(zoomAction);

    toolBar->addSeparator();

    // Not a tool, so it stands outside the exclusive group: it changes what
    // every drawing tool does rather than replacing them.
    QAction* objectDrawingAction = new QAction(
        ToolIcons::icon(ToolIcons::Tool::ObjectDrawing), "Object Drawing", this);
    objectDrawingAction->setCheckable(true);
    objectDrawingAction->setChecked(_drawingStyle.objectDrawing);
    objectDrawingAction->setShortcut(QKeySequence(Qt::Key_J));
    objectDrawingAction->setStatusTip(
        "Keep each new drawing as its own object. With this off, drawings merge: "
        "outlines cut each other and the newer fill replaces the older.");
    connect(objectDrawingAction, &QAction::toggled, this, [this](bool on) {
        _drawingStyle.objectDrawing = on;
    });
    toolBar->addAction(objectDrawingAction);
}

namespace {

/// Renders a colour swatch for the fill and stroke buttons.
QIcon swatchIcon(const uint8_t rgba[4])
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    // A checker behind the swatch so a transparent colour reads as transparent
    // rather than as black.
    painter.fillRect(0, 0, 20, 20, QColor(120, 120, 120));
    painter.fillRect(0, 0, 10, 10, QColor(160, 160, 160));
    painter.fillRect(10, 10, 10, 10, QColor(160, 160, 160));
    painter.fillRect(1, 1, 18, 18, QColor(rgba[0], rgba[1], rgba[2], rgba[3]));
    painter.setPen(QColor(30, 30, 30));
    painter.drawRect(0, 0, 19, 19);

    return QIcon(pixmap);
}

void applyColor(const QColor& color, uint8_t rgba[4])
{
    rgba[0] = static_cast<uint8_t>(color.red());
    rgba[1] = static_cast<uint8_t>(color.green());
    rgba[2] = static_cast<uint8_t>(color.blue());
    rgba[3] = static_cast<uint8_t>(color.alpha());
}

} // namespace

void MainWindow::setupPropertiesPanel()
{
    QDockWidget* dock = new QDockWidget("Properties", this);
    dock->setObjectName("propertiesDock");
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    QWidget* panel = new QWidget(dock);
    QFormLayout* layout = new QFormLayout(panel);

    // Fill.
    _fillColorButton = new QToolButton(panel);
    _fillColorButton->setIcon(swatchIcon(_drawingStyle.fillColor));
    _fillColorButton->setToolTip("Fill colour for new shapes");
    connect(_fillColorButton, &QToolButton::clicked, this, [this]() {
        const QColor current(_drawingStyle.fillColor[0], _drawingStyle.fillColor[1],
            _drawingStyle.fillColor[2], _drawingStyle.fillColor[3]);
        const QColor chosen = QColorDialog::getColor(current, this, "Fill Colour",
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        applyColor(chosen, _drawingStyle.fillColor);
        _fillColorButton->setIcon(swatchIcon(_drawingStyle.fillColor));
        // Text being typed takes the colour on its selection too.
        if (_textTool)
            _textTool->applyColor(_drawingStyle.fillColor);
    });

    QCheckBox* fillEnabled = new QCheckBox("Fill", panel);
    fillEnabled->setChecked(_drawingStyle.hasFill);
    connect(fillEnabled, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.hasFill = on;
    });
    layout->addRow(fillEnabled, _fillColorButton);

    // Stroke.
    _strokeColorButton = new QToolButton(panel);
    _strokeColorButton->setIcon(swatchIcon(_drawingStyle.strokeColor));
    _strokeColorButton->setToolTip("Stroke colour for new shapes");
    connect(_strokeColorButton, &QToolButton::clicked, this, [this]() {
        const QColor current(_drawingStyle.strokeColor[0], _drawingStyle.strokeColor[1],
            _drawingStyle.strokeColor[2], _drawingStyle.strokeColor[3]);
        const QColor chosen = QColorDialog::getColor(current, this, "Stroke Colour",
            QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid())
            return;
        applyColor(chosen, _drawingStyle.strokeColor);
        _strokeColorButton->setIcon(swatchIcon(_drawingStyle.strokeColor));
    });

    QCheckBox* strokeEnabled = new QCheckBox("Stroke", panel);
    strokeEnabled->setChecked(_drawingStyle.hasStroke);
    connect(strokeEnabled, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.hasStroke = on;
    });
    layout->addRow(strokeEnabled, _strokeColorButton);

    QDoubleSpinBox* weight = new QDoubleSpinBox(panel);
    weight->setRange(0.1, 200.0);
    weight->setSingleStep(0.5);
    weight->setValue(_drawingStyle.strokeWeight);
    connect(weight, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        _drawingStyle.strokeWeight = value;
    });
    layout->addRow("Weight", weight);

    _cornerRadiusBox = new QDoubleSpinBox(panel);
    _cornerRadiusBox->setRange(-500.0, 500.0);
    _cornerRadiusBox->setSingleStep(1.0);
    _cornerRadiusBox->setValue(_drawingStyle.cornerRadius);
    _cornerRadiusBox->setToolTip("Corner radius for rectangles; Up and Down change it while dragging");
    connect(_cornerRadiusBox, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        _drawingStyle.cornerRadius = value;
    });
    layout->addRow("Corner radius", _cornerRadiusBox);

    QDoubleSpinBox* brushSize = new QDoubleSpinBox(panel);
    brushSize->setRange(1.0, 200.0);
    brushSize->setSingleStep(1.0);
    brushSize->setValue(_drawingStyle.brushSize);
    connect(brushSize, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        _drawingStyle.brushSize = value;
    });
    layout->addRow("Brush size", brushSize);

    QDoubleSpinBox* eraserSize = new QDoubleSpinBox(panel);
    eraserSize->setRange(1.0, 200.0);
    eraserSize->setSingleStep(1.0);
    eraserSize->setValue(_drawingStyle.eraserSize);
    connect(eraserSize, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        _drawingStyle.eraserSize = value;
    });
    layout->addRow("Eraser size", eraserSize);

    QComboBox* transformMode = new QComboBox(panel);
    transformMode->addItem("Free Transform");
    transformMode->addItem("Distort");
    transformMode->addItem("Envelope");
    transformMode->setToolTip("The free transform tool's mode; Distort and Envelope work on shapes");
    connect(transformMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.transformMode = static_cast<fla::DrawingStyle::TransformMode>(index);
        if (_freeTransformTool)
            _freeTransformTool->resetBox();
        if (_phoenixView)
            _phoenixView->update();
    });
    layout->addRow("Transform", transformMode);

    QCheckBox* usePressure = new QCheckBox("Use pressure", panel);
    usePressure->setChecked(_drawingStyle.brushPressure);
    usePressure->setToolTip("Let a pen's pressure vary the brush width");
    connect(usePressure, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.brushPressure = on;
    });
    layout->addRow(QString(), usePressure);

    QComboBox* brushMode = new QComboBox(panel);
    brushMode->addItem("Paint Normal");
    brushMode->addItem("Paint Fills");
    brushMode->addItem("Paint Behind");
    brushMode->addItem("Paint Selection");
    brushMode->addItem("Paint Inside");
    brushMode->setToolTip("Where a brush stroke lands, as in Animate");
    connect(brushMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.brushMode = static_cast<fla::DrawingStyle::BrushMode>(index);
    });
    layout->addRow("Brush mode", brushMode);

    QComboBox* eraserMode = new QComboBox(panel);
    eraserMode->addItem("Erase Normal");
    eraserMode->addItem("Erase Fills");
    eraserMode->addItem("Erase Lines");
    eraserMode->addItem("Erase Selected Fills");
    eraserMode->addItem("Erase Inside");
    eraserMode->setToolTip("What a pass of the eraser takes away, as in Animate");
    connect(eraserMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.eraserMode = static_cast<fla::DrawingStyle::EraserMode>(index);
    });
    layout->addRow("Eraser mode", eraserMode);

    QCheckBox* faucet = new QCheckBox("Faucet", panel);
    faucet->setChecked(_drawingStyle.faucet);
    faucet->setToolTip("Click a fill or a stroke with the eraser to remove it whole");
    connect(faucet, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.faucet = on;
        if (_phoenixView && _phoenixView->activeTool() == _eraserTool.get())
            _phoenixView->setCursor(_eraserTool->cursor());
    });
    layout->addRow(QString(), faucet);

    QComboBox* gapSize = new QComboBox(panel);
    gapSize->addItem("Don't Close Gaps");
    gapSize->addItem("Close Small Gaps");
    gapSize->addItem("Close Medium Gaps");
    gapSize->addItem("Close Large Gaps");
    gapSize->setCurrentIndex(static_cast<int>(_drawingStyle.gapSize));
    gapSize->setToolTip("How far apart an outline's ends may be for the paint bucket to fill it");
    connect(gapSize, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.gapSize = static_cast<fla::DrawingStyle::GapSize>(index);
    });
    layout->addRow("Gap size", gapSize);

    // Text settings, which only the text tool reads. The face is kept the
    // way Animate writes it, the family without its spaces.
    QFontComboBox* textFont = new QFontComboBox(panel);
    textFont->setCurrentFont(QFont("Arial"));
    connect(textFont, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
        // Animate names a font by its PostScript name, so the common ones get
        // the suffix their foundry gave them; the rest just lose their spaces.
        static const QHash<QString, QString> postScriptNames = {
            {"Arial", "ArialMT"},
            {"Times New Roman", "TimesNewRomanPSMT"},
            {"Courier New", "CourierNewPSMT"},
        };
        const QString family = font.family();
        const std::string face = postScriptNames.value(family, QString(family).remove(' ')).toStdString();
        // A selection in an open text box takes the font; otherwise it is the
        // font for the next text.
        if (!_textTool || !_textTool->applyFace(face))
            _drawingStyle.textFace = face;
    });
    layout->addRow("Font", textFont);

    QDoubleSpinBox* textSize = new QDoubleSpinBox(panel);
    textSize->setRange(1.0, 500.0);
    textSize->setSingleStep(1.0);
    textSize->setDecimals(1);
    textSize->setValue(_drawingStyle.textSize);
    textSize->setToolTip("Size of new text, in pixels");
    connect(textSize, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!_textTool || !_textTool->applySize(value))
            _drawingStyle.textSize = value;
    });
    layout->addRow("Size", textSize);

    QComboBox* textType = new QComboBox(panel);
    textType->addItem("Static Text");
    textType->addItem("Dynamic Text");
    textType->addItem("Input Text");
    textType->setToolTip("What kind of field the text is: artwork, scriptable, or typed into");
    connect(textType, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.textType = index;
        const fla::StaticText::TextType type = static_cast<fla::StaticText::TextType>(index);
        if (_textTool && _textTool->applyTextType(type))
            return;
        setSelectedTextType(type);
    });
    layout->addRow("Text type", textType);

    // PolyStar settings, which only the polystar tool reads.
    QSpinBox* sides = new QSpinBox(panel);
    sides->setRange(3, 32);
    sides->setValue(_drawingStyle.sides);
    connect(sides, &QSpinBox::valueChanged, this, [this](int value) {
        _drawingStyle.sides = value;
    });
    layout->addRow("Sides", sides);

    QComboBox* pencilMode = new QComboBox(panel);
    pencilMode->addItem("Smooth");
    pencilMode->addItem("Straighten");
    pencilMode->addItem("Ink");
    pencilMode->setToolTip("How the pencil tidies a freehand stroke");
    connect(pencilMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        _drawingStyle.pencilMode = static_cast<fla::DrawingStyle::PencilMode>(index);
    });
    layout->addRow("Pencil", pencilMode);

    QSpinBox* smoothing = new QSpinBox(panel);
    smoothing->setRange(0, 100);
    smoothing->setSingleStep(5);
    smoothing->setValue(_drawingStyle.pencilSmoothing);
    smoothing->setToolTip("How much the pencil smooths a stroke, 0 to 100, as in Animate");
    connect(smoothing, &QSpinBox::valueChanged, this, [this](int value) {
        _drawingStyle.pencilSmoothing = value;
    });
    layout->addRow("Smoothing", smoothing);

    QCheckBox* connectLines = new QCheckBox("Connect lines", panel);
    connectLines->setChecked(_drawingStyle.connectLines);
    connectLines->setToolTip("In Straighten mode, close a pencil stroke whose ends nearly meet");
    connect(connectLines, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.connectLines = on;
    });
    layout->addRow(QString(), connectLines);

    QCheckBox* recognizeShapes = new QCheckBox("Recognize shapes", panel);
    recognizeShapes->setChecked(_drawingStyle.recognizeShapes);
    recognizeShapes->setToolTip("In Straighten mode, turn a roughly drawn rectangle or oval into the real thing");
    connect(recognizeShapes, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.recognizeShapes = on;
    });
    layout->addRow(QString(), recognizeShapes);

    QCheckBox* star = new QCheckBox("Star", panel);
    star->setChecked(_drawingStyle.star);
    star->setToolTip("Draw a star rather than a regular polygon");
    connect(star, &QCheckBox::toggled, this, [this](bool on) {
        _drawingStyle.star = on;
    });
    layout->addRow(QString(), star);

    panel->setLayout(layout);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
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
    if (_fillColorButton)
        _fillColorButton->setIcon(swatchIcon(_drawingStyle.fillColor));
    if (_strokeColorButton)
        _strokeColorButton->setIcon(swatchIcon(_drawingStyle.strokeColor));
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

void MainWindow::convertLinesToFillsSelection()
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
        if (!convertLinesToFills(*after))
            continue;
        commands.push_back(fla::CommandPtr(
            new fla::SetShapeContentsCommand(shape, *after, "Convert Lines to Fills")));
    }
    if (commands.empty())
        return;

    const bool needsMacro = commands.size() > 1;
    if (needsMacro)
        stack.beginMacro("Convert Lines to Fills");
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

    // The registration point is the corner, edge or centre of the selection
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
    bool centreInView)
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

    if (centreInView)
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
            const QPointF centre = _phoenixView->mapToDocument(
                QPointF(_phoenixView->width() / 2.0, _phoenixView->height() / 2.0));
            const QPointF delta = centre - bounds.center();
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
    for (QAction* action : {_smoothAction, _straightenAction, _optimizeAction, _linesToFillsAction})
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
