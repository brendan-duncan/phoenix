#include "main_window.h"
#include "phoenix_view.h"
#include "timeline_view.h"
#include "../parser/fla_parser.h"
#include "../writer/fla_writer.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
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
#include <QCheckBox>
#include <QColorDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolButton>

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
            _phoenixView->update();
    });

    // Object snapping on by default, grid snapping off, matching Animate.
    _snapper.setObjectSnapEnabled(true);
    _phoenixView->setSnapper(&_snapper);

    _selectionTool = std::make_unique<SelectionTool>(_selection, _editContext.commandStack());
    _freeTransformTool = std::make_unique<FreeTransformTool>(
        _selection, _editContext.commandStack());

    fla::CommandStack& stack = _editContext.commandStack();
    _rectangleTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Rectangle, _selection, stack, _drawingStyle);
    _ovalTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Oval, _selection, stack, _drawingStyle);
    _lineTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::Line, _selection, stack, _drawingStyle);
    _polyStarTool = std::make_unique<PrimitiveTool>(
        PrimitiveTool::Kind::PolyStar, _selection, stack, _drawingStyle);
    _penTool = std::make_unique<PenTool>(_selection, stack, _drawingStyle);
    _subselectionTool = std::make_unique<SubselectionTool>(_selection, stack);
    _phoenixView->setSelection(&_selection);
    _phoenixView->setActiveTool(_selectionTool.get());
    updateSelectionState();

    // Auto-open last file if available
    if (!_recentFiles.isEmpty())
    {
        loadFLAFile(_recentFiles.first());
    }
}

void MainWindow::loadFLAFile(const QString& filePath)
{
    // Detach the old document before deleting it: the undo history holds
    // pointers into it and must not outlive it.
    // The selection and the undo history both point into the document, so both
    // have to let go before it is deleted.
    _selection.clear();
    _editContext.setDocument(nullptr);

    if (_flaDocument)
    {
        delete _flaDocument;
        _flaDocument = nullptr;
    }

    FLAParser parser;
    _flaDocument = parser.parse(filePath.toStdString());
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

    if (_flaDocument)
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
        
        _documentName = displayName;
        _documentPath = filePath;
        updateWindowTitle();
        addToRecentFiles(filePath);

        _player->setCurrentFrame(0);
    }
    else
    {
        _documentName.clear();
        _documentPath.clear();
        updateWindowTitle();
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

    // Add widgets to view splitter
    _viewSplitter->addWidget(_documentView);
    _viewSplitter->addWidget(_phoenixView);

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
}

void MainWindow::setupMenus()
{
    // Create menu bar
    QMenuBar* menuBar = this->menuBar();

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

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

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* viewDocumentAction = new QAction("View Document...", this);
    viewDocumentAction->setStatusTip("View the current document source");
    connect(viewDocumentAction, &QAction::triggered, this, &MainWindow::viewDocument);
    viewMenu->addAction(viewDocumentAction);

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
    showBoundsAction->setShortcut(QKeySequence(Qt::Key_B));
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

    // TODO: offer to save once XFL writing exists; until then the only honest
    // options are to discard the changes or to stay put.
    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
        "Unsaved Changes",
        QString("%1 has unsaved changes.\n\n"
                "Saving is not implemented yet, so continuing will discard them.").arg(_documentName),
        QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Cancel);

    return answer == QMessageBox::Discard;
}

void MainWindow::setupToolBar()
{
    QToolBar* toolBar = addToolBar("Tools");
    toolBar->setObjectName("toolsToolBar");
    toolBar->setMovable(false);

    // One action group so the tools behave like a radio button set, the way a
    // toolbox does.
    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    QAction* selectionAction = new QAction("Selection", this);
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

    QAction* subselectionAction = new QAction("Subselection", this);
    subselectionAction->setCheckable(true);
    subselectionAction->setShortcut(QKeySequence(Qt::Key_A));
    subselectionAction->setStatusTip(
        "Show a shape's anchors and drag them. Alt breaks a smooth point's tangent.");
    connect(subselectionAction, &QAction::triggered, this, [this]() {
        _phoenixView->setActiveTool(_subselectionTool.get());
    });
    toolGroup->addAction(subselectionAction);
    toolBar->addAction(subselectionAction);

    QAction* freeTransformAction = new QAction("Free Transform", this);
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

    toolBar->addSeparator();

    // The shape tools all behave the same way, so they are wired from a table
    // rather than four near-identical blocks.
    struct ShapeEntry
    {
        const char* label;
        Qt::Key shortcut;
        const char* tip;
        std::unique_ptr<PrimitiveTool>* tool;
    };

    const ShapeEntry shapes[] = {
        {"Rectangle", Qt::Key_R, "Drag to draw a rectangle. Shift for a square.",
            &_rectangleTool},
        {"Oval", Qt::Key_O, "Drag to draw an oval. Shift for a circle.",
            &_ovalTool},
        {"Line", Qt::Key_N, "Drag to draw a line. Shift for 45 degree steps.",
            &_lineTool},
        {"PolyStar", Qt::Key_Y, "Drag from the centre outwards. Sides and star mode "
            "are in the Properties panel.", &_polyStarTool},
    };

    for (const ShapeEntry& shape : shapes)
    {
        QAction* action = new QAction(shape.label, this);
        action->setCheckable(true);
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

    QAction* penAction = new QAction("Pen", this);
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

    // PolyStar settings, which only the polystar tool reads.
    QSpinBox* sides = new QSpinBox(panel);
    sides->setRange(3, 32);
    sides->setValue(_drawingStyle.sides);
    connect(sides, &QSpinBox::valueChanged, this, [this](int value) {
        _drawingStyle.sides = value;
    });
    layout->addRow("Sides", sides);

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

void MainWindow::updateSelectionState()
{
    const bool hasDocument = _flaDocument && _flaDocument->document;

    if (_selectAllAction)
        _selectAllAction->setEnabled(hasDocument);

    if (_deselectAllAction)
        _deselectAllAction->setEnabled(!_selection.isEmpty());

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
