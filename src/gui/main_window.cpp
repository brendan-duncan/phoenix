#include "main_window.h"
#include "convert_to_symbol_dialog.h"
#include "lines_to_fills.h"
#include "fill_offset.h"
#include "new_document_dialog.h"
#include "start_dialog.h"
#include "autosave.h"
#include "timeline_audio.h"
#include "updater.h"
#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/radial_gradient.h"
#include "../data/linear_gradient.h"
#include "../data/bitmap_instance.h"
#include "draw_placement.h"
#include "library_panel.h"
#include "media_import.h"
#include "video_import.h"
#include "frame_picker_panel.h"
#include "color_panel.h"
#include "arrange_panels.h"
#include "motion_editor.h"
#include "motion_presets.h"
#include "swatches_panel.h"
#include "../edit/swatches.h"
#include "swap_symbol_dialog.h"
#include "../edit/layer_commands.h"
#include "../edit/library_media.h"
#include "../edit/frame_commands.h"
#include "../edit/frame_bake.h"
#include "../edit/combine_objects.h"
#include "../edit/timeline_edit.h"
#include "../edit/library_commands.h"
#include "../edit/property_command.h"
#include "phoenix_view.h"
#include "movie_export.h"
#include "image_export.h"
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
#include "../writer/xfl_writer.h"
#include "../parser/document_parser.h"
#include "text_layout.h"
#include "../data/solid_color.h"
#include "shape_outline.h"
#include "../data/static_text.h"
#include "../data/layer.h"
#include "../data/timeline.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"
#include "../phx/container.h"
#include "../phx/phx_document.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QPushButton>
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
#include <QLineEdit>

#include <memory>
#include <filesystem>
#include <QAction>
#include <QStatusBar>
#include <QTextEdit>
#include <QDialog>
#include <QTabWidget>
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
#include <QTemporaryDir>
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
#include <QSignalBlocker>

#include "tool_icons.h"

namespace {
fla::Frame* frameOfElement(const fla::Element* element);
}

MainWindow::MainWindow(const QString& initialFile, QWidget* parent, bool startup)
    : QMainWindow(parent)
    , _phoenixView(nullptr)
    , _documentView(nullptr)
    , _timelineView(nullptr)
    , _flaDocument(nullptr)
    , _player(new Player(this))
    , _updater(new Updater(this))
    , _recentFilesMenu(nullptr)
{
    setWindowTitle(QString("Phoenix %1").arg(QCoreApplication::applicationVersion()));

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
        // Selecting something on the stage makes its layer current.
        if (_timelineView && !_selection.isEmpty())
        {
            for (const fla::DOMElement* p = _selection.elements().front(); p; p = p->parent)
            {
                if (p->domType() == fla::DOMElement::DOMType::Layer)
                {
                    _timelineView->setCurrentLayer(static_cast<const fla::Layer*>(p));
                    break;
                }
            }
        }
        if (_transformPanel)
            _transformPanel->refresh();
        if (_infoPanel)
            _infoPanel->refresh();
        if (_motionEditor)
            _motionEditor->refresh();
        updateFramePicker();
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
    // Paste follows what other windows and applications put on the clipboard.
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() { updateSelectionState(); });

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
    _polygonTool = std::make_unique<LassoTool>(_selection, true);
    _gradientTransformTool = std::make_unique<GradientTransformTool>(stack);
    _addAnchorTool = std::make_unique<AnchorPointTool>(AnchorPointTool::Mode::Add, stack);
    _deleteAnchorTool = std::make_unique<AnchorPointTool>(AnchorPointTool::Mode::Delete, stack);
    _convertAnchorTool = std::make_unique<AnchorPointTool>(AnchorPointTool::Mode::Convert, stack);
    _magicWandTool = std::make_unique<MagicWandTool>(_drawingStyle);
    _rotationTool = std::make_unique<RotationTool>();
    _timeScrubTool = std::make_unique<TimeScrubTool>();
    _brushTool = std::make_unique<BrushTool>(BrushTool::Kind::Fluid, _selection, stack, _drawingStyle);
    _classicBrushTool = std::make_unique<BrushTool>(BrushTool::Kind::Classic, _selection, stack, _drawingStyle);
    _inkBrushTool = std::make_unique<BrushTool>(BrushTool::Kind::Ink, _selection, stack, _drawingStyle);
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

    // The file asked for; else the start screen, or, with that turned off,
    // the file worked on last. Opened once the window is up, so a message
    // about a file that cannot be opened has a window to sit on rather than
    // appearing before there is one.
    const bool startScreen = initialFile.isEmpty() && QSettings().value("showStartDialog", true).toBool();
    const QString startFile = !initialFile.isEmpty() ? initialFile
        : (_recentFiles.isEmpty() || startScreen ? QString() : _recentFiles.first());
    // Work a crash left behind comes first, unless a file was asked for.
    if (startup && !startFile.isEmpty())
    {
        QTimer::singleShot(0, this, [this, startFile, initialFile]() {
            if (!initialFile.isEmpty() || !offerRecovery())
                loadFLAFile(startFile);
        });
    }
    else if (startup)
    {
        QTimer::singleShot(0, this, [this, startScreen]() {
            if (!offerRecovery() && startScreen)
                showStartDialog();
        });
    }

    // The onion skin as it was left.
    {
        QSettings settings;
        _onion.before = settings.value("onion/before", 2).toInt();
        _onion.after = settings.value("onion/after", 2).toInt();
        _onion.outline = settings.value("onion/outline", false).toBool();
        _onion.keyframesOnly = settings.value("onion/keyframesOnly", false).toBool();
        _onion.opacity = settings.value("onion/opacity", 0.45).toDouble();
        _onion.beforeColor = QColor(settings.value("onion/beforeColor", "#0066ff").toString());
        _onion.afterColor = QColor(settings.value("onion/afterColor", "#00aa3c").toString());
        if (_phoenixView)
            _phoenixView->setOnionStyle(_onion.outline, _onion.keyframesOnly, _onion.opacity, _onion.beforeColor,
                _onion.afterColor);
    }

    // A copy of unsaved work every few minutes, for after a crash.
    if (startup)
    {
        AutoSave::Links links;
        links.document = [this]() { return _flaDocument ? _flaDocument->document : nullptr; };
        links.modified = [this]() { return _editContext.isModified(); };
        links.path = [this]() { return _documentPath; };
        links.name = [this]() { return _documentName; };
        links.busy = []() { return QApplication::mouseButtons() != Qt::NoButton; };
        _autoSave = new AutoSave(links, this);
    }

    // Look for an update once the start-up file has had its turn.
    connect(_updater, &Updater::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, message.isEmpty() ? 0 : 8000);
    });
    if (startup)
        QTimer::singleShot(3000, this, [this]() { _updater->checkForUpdates(false); });
}

namespace {

/// Whether a dropped or passed path is a document Phoenix opens.
bool isDocumentPath(const QString& path)
{
    const QString lower = path.toLower();
    return lower.endsWith(".phx") || lower.endsWith(".fla") || lower.endsWith(".xfl");
}

bool isPhxPath(const QString& path)
{
    return path.endsWith(".phx", Qt::CaseInsensitive);
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;
    const bool open = _flaDocument && _flaDocument->document;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (url.isLocalFile() && (isDocumentPath(path) ||
            (open && (MediaImport::isImage(path) || MediaImport::isSound(path)))))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList media;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (isDocumentPath(path))
        {
            event->acceptProposedAction();
            loadFLAFile(path);
            return;
        }
        if (MediaImport::isImage(path) || MediaImport::isSound(path))
            media << path;
    }
    if (media.isEmpty() || !_flaDocument || !_flaDocument->document)
        return;
    event->acceptProposedAction();

    // Files dropped on the stage land where they were dropped; anywhere else
    // in the window they go into the library, as Animate takes them.
    if (_phoenixView)
    {
        const QPoint local = _phoenixView->mapFrom(this, event->position().toPoint());
        if (_phoenixView->rect().contains(local))
        {
            const QPointF at = _phoenixView->mapToDocument(QPointF(local));
            importMedia(media, true, &at);
            return;
        }
    }
    importMedia(media, false);
}

void MainWindow::releaseDocument()
{
    // The document is going; so is the copy kept of it for recovery.
    if (_autoSave)
        _autoSave->discard();
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
    if (_timelineAudio)
        _timelineAudio->setDocument(nullptr);
    if (_libraryPanel)
        _libraryPanel->setDocument(nullptr);
    if (_colorPanel)
        _colorPanel->setDocument(nullptr);
    if (_swatchesPanel)
        _swatchesPanel->setDocument(nullptr);
    if (_framePicker)
        _framePicker->setInstance(nullptr, 0);

    // The selection and the undo history both point into the document, so both
    // have to let go before it is deleted. So do the copied frames' symbol
    // instances; a paste points them at the next document's symbols.
    _selection.clear();
    relinkFrameClipboard(nullptr);
    for (const std::unique_ptr<fla::Element>& element : _clipboard)
        fla::relinkSymbols(*element, nullptr);
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

    // Grid spacing and the snapping switches come from the document, so they
    // have to be picked up per file.
    if (_flaDocument && _flaDocument->document)
    {
        const fla::Document* document = _flaDocument->document;
        _snapper.setGridSpacing(document->gridSpacingX, document->gridSpacingY);
        const bool objects = document->objectsSnapTo;
        const bool guides = document->guidesSnapTo;
        if (_snapToObjectsAction)
            _snapToObjectsAction->setChecked(objects);
        if (_snapGuidesAction)
            _snapGuidesAction->setChecked(guides);
        _snapper.setObjectSnapEnabled(objects);
        _snapper.setGuideSnapEnabled(guides);
    }
    _phoenixView->setDocument(_flaDocument);
    _documentView->setDocument(_flaDocument);
    if (_timelineAudio)
        _timelineAudio->setDocument(_flaDocument);
    _timelineView->setDocument(_flaDocument);

    _documentName = displayName;
    _documentPath = filePath;
    updateWindowTitle();
    if (_propertiesPanel)
    {
        _propertiesPanel->refresh();
        _propertiesPanel->setTab(PropertiesPanel::Tab::Doc);
    }
    if (_libraryPanel)
        _libraryPanel->setDocument(_flaDocument);
    if (_colorPanel)
        _colorPanel->setDocument(_flaDocument);
    if (_swatchesPanel)
        _swatchesPanel->setDocument(_flaDocument);

    // Align, Transform and Info, closed until asked for (Ctrl+K, Ctrl+T,
    // Ctrl+I), in a group of their own as in Animate.
    ArrangeLinks arrange;
    arrange.view = _phoenixView;
    arrange.selection = &_selection;
    arrange.commands = [this]() { return &_editContext.commandStack(); };
    arrange.elements = [this]() { return selectedElements(); };
    arrange.stage = [this]() { return _phoenixView ? _phoenixView->stageInScope() : QRectF(); };
    arrange.changed = [this]() {
        _phoenixView->clearCaches();
        _phoenixView->update();
        if (_propertiesPanel)
            _propertiesPanel->refresh();
        if (_transformPanel)
            _transformPanel->refresh();
        if (_infoPanel)
            _infoPanel->refresh();
    };
    arrange.duplicate = [this]() { duplicateSelection(); };
    const auto arrangeDock = [this](const char* title, const char* name, QWidget* panel) {
        QDockWidget* dock = new QDockWidget(title, this);
        dock->setObjectName(name);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        panel->setParent(dock);
        dock->setWidget(panel);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        return dock;
    };
    // Made once; a document opened later uses the same ones.
    if (!_alignDock)
    {
        _alignPanel = new AlignPanel(arrange);
        _alignDock = arrangeDock("Align", "alignDock", _alignPanel);
        _transformPanel = new TransformPanel(arrange);
        _transformDock = arrangeDock("Transform", "transformDock", _transformPanel);
        _infoPanel = new InfoPanel(arrange);
        _infoDock = arrangeDock("Info", "infoDock", _infoPanel);
        tabifyDockWidget(_alignDock, _transformDock);
        tabifyDockWidget(_transformDock, _infoDock);
        for (QDockWidget* dock : {_alignDock, _transformDock, _infoDock})
            dock->hide();
        connect(_phoenixView, &PhoenixView::cursorMoved, _infoPanel, &InfoPanel::cursorMoved);
    }

    // The Motion Editor, below the stage as in Animate, closed until asked.
    MotionEditorLinks motionLinks;
    motionLinks.commands = [this]() { return &_editContext.commandStack(); };
    motionLinks.span = [this]() { return currentMotionSpan(); };
    motionLinks.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
    motionLinks.frameRate = [this]() {
        return _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
            ? _flaDocument->document->frameRate : 24.0;
    };
    motionLinks.changed = [this]() {
        if (_phoenixView)
        {
            _phoenixView->invalidateBounds();
            _phoenixView->update();
        }
    };
    if (!_motionEditorDock)
    {
        _motionEditor = new MotionEditorPanel(motionLinks);
        _motionEditorDock = new QDockWidget("Motion Editor", this);
        _motionEditorDock->setObjectName("motionEditorDock");
        _motionEditorDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _motionEditor->setParent(_motionEditorDock);
        _motionEditorDock->setWidget(_motionEditor);
        addDockWidget(Qt::BottomDockWidgetArea, _motionEditorDock);
        _motionEditorDock->hide();
        connect(_player, &Player::currentFrameChanged, _motionEditor, [this](int) { _motionEditor->refresh(); });
    }

    if (_flaDocument)
        _player->setCurrentFrame(0);
}

void MainWindow::newFile()
{
    if (!confirmDiscardChanges())
        return;

    // Animate's New Document dialog: a preset, or a size and frame rate.
    NewDocumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    createNewDocument(dialog.documentWidth(), dialog.documentHeight(), dialog.frameRate(), dialog.stageColor());
}

bool MainWindow::offerRecovery()
{
    const QList<AutoSave::Orphan> orphans = AutoSave::orphans();
    for (const AutoSave::Orphan& orphan : orphans)
    {
        const QString name = orphan.name.isEmpty() ? QString("Untitled") : orphan.name;
        QMessageBox box(QMessageBox::Question, "Recover Unsaved Work",
            QString("Phoenix closed unexpectedly while %1 had unsaved changes. A copy from %2 was kept.")
                .arg(name, QLocale().toString(orphan.saved, QLocale::ShortFormat)),
            QMessageBox::NoButton, this);
        QPushButton* recover = box.addButton("Recover", QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton("Discard", QMessageBox::DestructiveRole);
        box.addButton("Later", QMessageBox::RejectRole);
        box.setDefaultButton(recover);
        box.exec();
        if (box.clickedButton() == discard)
        {
            AutoSave::remove(orphan);
            continue;
        }
        if (box.clickedButton() != recover)
            continue;

        std::string error;
        std::string warning;
        bool readOnly = false;
        fla::FLADocument* document = phx::load(orphan.file.toStdString(), error, warning, readOnly);
        if (!document)
        {
            QMessageBox::warning(this, "Recover Unsaved Work",
                QString("The copy could not be read: %1").arg(QString::fromStdString(error)));
            continue;
        }
        // Back where it came from, and unsaved until it is saved.
        adoptDocument(document, orphan.originalPath, name);
        _editContext.markUnsaved();
        updateEditState();
        AutoSave::remove(orphan);
        statusBar()->showMessage(QString("Recovered %1. Save it to keep it.").arg(name), 8000);
        return true;
    }
    return false;
}

void MainWindow::showStartDialog()
{
    StartDialog dialog(_recentFiles, this);
    const int result = dialog.exec();
    QSettings().setValue("showStartDialog", dialog.showAtStartup());
    if (result != QDialog::Accepted)
        return;
    switch (dialog.choice())
    {
    case StartDialog::Choice::NewPreset:
    {
        const auto& presets = NewDocumentDialog::presets();
        if (dialog.preset() < 0 || dialog.preset() >= static_cast<int>(presets.size()) || !confirmDiscardChanges())
            return;
        const NewDocumentDialog::Preset& preset = presets[static_cast<size_t>(dialog.preset())];
        createNewDocument(preset.width, preset.height, preset.frameRate, Qt::white);
        break;
    }
    case StartDialog::Choice::NewCustom:
        newFile();
        break;
    case StartDialog::Choice::Browse:
        openFile();
        break;
    case StartDialog::Choice::OpenRecent:
        if (confirmDiscardChanges())
            loadFLAFile(dialog.path());
        break;
    case StartDialog::Choice::None:
        break;
    }
}

void MainWindow::createNewDocument(int width, int height, double frameRate, const QColor& stage)
{
    fla::FLADocument* document = fla::createEmptyDocument(width, height, frameRate);
    if (document && document->document)
    {
        document->document->backgroundColor[0] = static_cast<uint8_t>(stage.red());
        document->document->backgroundColor[1] = static_cast<uint8_t>(stage.green());
        document->document->backgroundColor[2] = static_cast<uint8_t>(stage.blue());
        document->document->backgroundColor[3] = 255;
    }

    // No path: the first Save has to ask where to put it, which saveFile()
    // already does by falling through to Save As.
    adoptDocument(document, QString(), "Untitled");
    // The new stage, fitted to the view.
    if (_phoenixView)
        QTimer::singleShot(0, this, [this]() { _phoenixView->zoomToFit(); });
}

void MainWindow::openDocumentPath(const QString& filePath)
{
    if (filePath.isEmpty() || !confirmDiscardChanges())
        return;
    loadFLAFile(filePath);
}

void MainWindow::loadFLAFile(const QString& filePath)
{
    releaseDocument();

    // A Phoenix document is known by its first bytes, whatever its name.
    if (phx::isPhx(filePath.toStdString()))
    {
        std::string error;
        std::string warning;
        bool readOnly = false;
        fla::FLADocument* document = phx::load(filePath.toStdString(), error, warning, readOnly);
        if (!document)
        {
            adoptDocument(nullptr, QString(), QString());
            QMessageBox::warning(this, "Cannot Open Document",
                QString("%1\n\n%2").arg(QFileInfo(filePath).fileName(), QString::fromStdString(error)));
            return;
        }
        adoptDocument(document, filePath, QFileInfo(filePath).fileName());
        addToRecentFiles(filePath);
        if (readOnly)
        {
            // Saving over it could lose what this Phoenix does not
            // understand: Save asks for a new name instead.
            _documentPath.clear();
            _documentName = QFileInfo(filePath).fileName() + " (read-only)";
            updateEditState();
        }
        if (!warning.empty())
        {
            QMessageBox::information(this, "Open Document",
                QString("%1\n\n%2").arg(QFileInfo(filePath).fileName(),
                    QString::fromStdString(warning).trimmed()));
        }
        return;
    }

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
        QMessageBox::warning(this, "Cannot Open Document",
                           QString("%1\n\n%2")
                           .arg(QFileInfo(filePath).fileName())
                           .arg(parser.errorString().empty() ? QString("The file could not be read.")
                               : QString::fromStdString(parser.errorString())));
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

    // The outliner's fold arrow sits at the left of the breadcrumb row; the
    // breadcrumb itself is rebuilt as scopes change, so it is kept apart.
    _outlinerButton = new QToolButton(this);
    _outlinerButton->setAutoRaise(true);
    _outlinerButton->setArrowType(Qt::LeftArrow);
    _outlinerButton->setFixedSize(18, 18);
    _outlinerButton->setToolTip("Hide the outliner");
    connect(_outlinerButton, &QToolButton::clicked, this, [this]() { setOutlinerVisible(!_outlinerVisible); });

    QWidget* breadcrumbRow = new QWidget(this);
    breadcrumbRow->setAutoFillBackground(true);
    QHBoxLayout* breadcrumbRowLayout = new QHBoxLayout(breadcrumbRow);
    breadcrumbRowLayout->setContentsMargins(2, 0, 0, 0);
    breadcrumbRowLayout->setSpacing(0);
    breadcrumbRowLayout->addWidget(_outlinerButton);
    breadcrumbRowLayout->addWidget(_breadcrumb, 1);

    QWidget* stageColumn = new QWidget(this);
    QVBoxLayout* stageLayout = new QVBoxLayout(stageColumn);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    stageLayout->addWidget(breadcrumbRow);
    stageLayout->addWidget(_phoenixView, 1);

    // Add widgets to view splitter
    _viewSplitter->addWidget(_documentView);
    _viewSplitter->addWidget(stageColumn);

    // Set view splitter properties. The outliner folds away; the stage
    // never does.
    _viewSplitter->setCollapsible(0, true);
    _viewSplitter->setCollapsible(1, false);
    _viewSplitter->setStretchFactor(0, 0);
    _viewSplitter->setStretchFactor(1, 1);
    connect(_viewSplitter, &QSplitter::splitterMoved, this, [this]() {
        const int width = _viewSplitter->sizes().value(0);
        _outlinerVisible = width > 0;
        if (width > 0)
            _outlinerWidth = width;
        syncOutlinerControls();
    });
    // Once the window has its size, show the outliner as it was left.
    QTimer::singleShot(0, this, [this]() { setOutlinerVisible(_outlinerVisible); });

    // Create the timeline view
    _timelineView = new TimelineView(_player, this);
    _timelineView->setMinimumHeight(150);

    // Sound follows the playhead and the transport.
    _timelineAudio = new TimelineAudio(_player, this);
    connect(_player, &Player::playingChanged, _timelineAudio, &TimelineAudio::onPlayingChanged);
    connect(_player, &Player::currentFrameChanged, _timelineAudio, &TimelineAudio::onFrameChanged);
    _timelineView->setSoundProvider([this](const fla::Frame* frame, size_t& in) {
        return _timelineAudio->soundOf(frame, &in);
    });

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

    // The onion skin button and the View menu item are one setting.
    connect(_timelineView, &TimelineView::onionSkinToggled, this, [this](bool on) {
        if (_onionSkinAction && _onionSkinAction->isChecked() != on)
            _onionSkinAction->setChecked(on);
        else if (_phoenixView)
            _phoenixView->setOnionSkin(on, _onion.before, _onion.after);
    });
    connect(_timelineView, &TimelineView::onionRangeChanged, this, [this](int before, int after) {
        _onion.before = before;
        _onion.after = after;
        applyOnion();
    });
    connect(_timelineView, &TimelineView::onionMenuRequested, this, &MainWindow::showOnionMenu);

    // Frame edits from the grid itself: dragging a keyframe, the menu.
    connect(_timelineView, &TimelineView::keyframeMoveRequested, this, &MainWindow::moveKeyframe);
    connect(_timelineView, &TimelineView::framePicked, this, &MainWindow::pickFrameContents);
    connect(_timelineView, &TimelineView::spanResizeRequested, this, &MainWindow::resizeSpan);
    connect(_timelineView, &TimelineView::framesMoveRequested, this, &MainWindow::moveFrames);
    connect(_timelineView, &TimelineView::framesContextMenuRequested, this, &MainWindow::showFramesMenu);

    // Adding, deleting and renaming layers go through the undo history.
    connect(_timelineView, &TimelineView::addLayerRequested, this, &MainWindow::addLayer);
    connect(_timelineView, &TimelineView::deleteLayerRequested, this, &MainWindow::deleteCurrentLayer);
    connect(_timelineView, &TimelineView::layerRenamed, this, &MainWindow::renameLayer);
    connect(_timelineView, &TimelineView::layerEditRequested, this, &MainWindow::editLayers);
}

fla::Timeline* MainWindow::editedTimeline() const
{
    if (_phoenixView && _phoenixView->scopeTimeline())
        return _phoenixView->scopeTimeline();
    if (_flaDocument && _flaDocument->document && !_flaDocument->document->timelines.empty())
        return _flaDocument->document->currentScene();
    return nullptr;
}

FrameSelection MainWindow::framesToEdit() const
{
    FrameSelection selection = _timelineView ? _timelineView->frameSelection() : FrameSelection();
    if (!selection.isEmpty())
        return selection;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (_timelineView && _timelineView->currentLayer(timeline, index) && timeline &&
        index >= 0 && index < static_cast<int>(timeline->layers.size()))
    {
        selection.layers.push_back(timeline->layers[index]);
        selection.first = selection.last = _player ? _player->currentFrame() : 0;
    }
    return selection;
}

void MainWindow::editFrames(const char* name, const std::function<void(fla::Layer&, int, int,
    std::vector<fla::Frame*>&)>& edit)
{
    const FrameSelection selection = framesToEdit();
    if (selection.isEmpty())
        return;

    std::vector<fla::Layer*> layers;
    for (const fla::Layer* layer : selection.layers)
        layers.push_back(const_cast<fla::Layer*>(layer));
    const int first = selection.first;
    const int last = selection.last;
    // Frames Ctrl-clicked in are edited one at a time too.
    std::vector<std::pair<fla::Layer*, int>> cells;
    std::vector<fla::Layer*> touched = layers;
    for (const auto& cell : selection.cells)
    {
        fla::Layer* layer = const_cast<fla::Layer*>(cell.first);
        cells.push_back({layer, cell.second});
        if (std::find(touched.begin(), touched.end(), layer) == touched.end())
            touched.push_back(layer);
    }

    // What is selected on the stage may be on a frame that goes.
    _selection.clear();
    auto* command = new fla::EditFramesCommand(touched, name,
        [layers, cells, first, last, edit](std::vector<fla::Frame*>& removed) {
            for (fla::Layer* layer : layers)
                edit(*layer, first, last, removed);
            for (const auto& cell : cells)
                edit(*cell.first, cell.second, cell.second, removed);
        });
    command->redo();
    if (!command->changedAnything())
    {
        command->undo();
        delete command;
        return;
    }
    command->undo();
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::insertFrame()
{
    // As many frames as are selected, at the start of the selection.
    editFrames("Insert Frame", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        fla::timeline::insertFrames(layer, first, last - first + 1);
    });
}

void MainWindow::insertKeyframe(bool blank)
{
    editFrames(blank ? "Insert Blank Keyframe" : "Insert Keyframe",
        [blank](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
            // One keyframe at each selected frame, as Animate makes them.
            for (int index = first; index <= last; ++index)
                fla::timeline::insertKeyframe(layer, index, blank);
        });
}

void MainWindow::removeFrames()
{
    editFrames("Remove Frames", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>& removed) {
        fla::timeline::removeFrames(layer, first, last - first + 1, removed);
    });
}

void MainWindow::clearFrames()
{
    editFrames("Clear Frames", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>& removed) {
        fla::timeline::clearFrames(layer, first, last, removed);
    });
}

void MainWindow::pickFrameContents(const fla::Layer* layer, int frame)
{
    // After the playhead has moved there, which lets go of the selection.
    QTimer::singleShot(0, this, [this, layer, frame]() {
        if (!layer || !layer->isVisible() || layer->isLocked() || !_phoenixView)
            return;
        fla::Frame* span = fla::timeline::spanAt(*const_cast<fla::Layer*>(layer), frame);
        if (!span)
            return;
        std::vector<fla::DOMElement*> picked;
        for (fla::Element* element : span->elements)
            if (element && element->visible && !element->isLocked)
                picked.push_back(element);
        if (!picked.empty())
            _selection.select(picked);
        _phoenixView->update();
    });
}

void MainWindow::clearKeyframe()
{
    editFrames("Clear Keyframe", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>& removed) {
        for (int index = last; index >= first; --index)
            fla::timeline::clearKeyframe(layer, index, removed);
    });
}

void MainWindow::setClassicTween(bool on)
{
    editFrames(on ? "Create Classic Tween" : "Remove Tween",
        [on](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
            // Every span the selection touches.
            for (int index = first; index <= last; ++index)
                fla::timeline::setTween(layer, index, on);
        });
}

void MainWindow::createShapeTween()
{
    editFrames("Create Shape Tween", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        for (int index = first; index <= last; ++index)
            fla::timeline::setShapeTween(layer, index);
    });
}

void MainWindow::createMotionTween()
{
    const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    PhoenixView* view = _phoenixView;
    editFrames("Create Motion Tween", [rate, view](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        // Where the transformation point sits in the object, which the
        // tween keeps.
        const auto pivotOf = [view](const fla::Element& element) {
            const QRectF bounds = view ? view->elementBounds(&element) : QRectF();
            if (!bounds.isValid() || bounds.width() <= 0.0 || bounds.height() <= 0.0)
                return std::make_pair(0.5, 0.5);
            const fla::Transform& t = element.transform;
            const fla::Point& p = element.transformationPoint;
            const double x = t.m11 * p.x + t.m21 * p.y + t.tx;
            const double y = t.m12 * p.x + t.m22 * p.y + t.ty;
            return std::make_pair((x - bounds.left()) / bounds.width(), (y - bounds.top()) / bounds.height());
        };
        for (int index = first; index <= last; ++index)
            fla::timeline::createMotionTween(layer, index, rate, pivotOf);
    });
}

fla::Frame* MainWindow::currentMotionSpan()
{
    // The selected object's tween, or the current layer's at the playhead.
    if (!_phoenixView)
        return nullptr;
    const std::vector<PhoenixView::MotionPath> paths = _phoenixView->motionPaths();
    if (!paths.empty())
        return paths.front().frame;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
        return nullptr;
    fla::Frame* frame = fla::timeline::spanAt(*timeline->layers[index], _player ? _player->currentFrame() : 0);
    return frame && fla::timeline::motionOf(*frame) ? frame : nullptr;
}

void MainWindow::copyMotion()
{
    fla::Frame* span = currentMotionSpan();
    const std::string* motion = span ? fla::timeline::motionOf(*span) : nullptr;
    if (!motion)
    {
        statusBar()->showMessage("Copy Motion copies a motion tween: select its object or put the playhead in it.", 4000);
        return;
    }
    _copiedMotion = *motion;
    statusBar()->showMessage("Motion copied.", 2000);
}

bool MainWindow::pasteMotion(const std::string& motion)
{
    if (motion.empty())
        return false;
    const double rate = _flaDocument && _flaDocument->document && _flaDocument->document->frameRate > 0.0
        ? _flaDocument->document->frameRate : 24.0;
    // Onto the selected object's frame when one is selected on the stage.
    const std::vector<fla::Element*> elements = selectedElements();
    fla::Frame* target = elements.size() == 1 && elements.front()
        ? dynamic_cast<fla::Frame*>(elements.front()->parent) : nullptr;
    fla::Layer* targetLayer = target ? dynamic_cast<fla::Layer*>(target->parent) : nullptr;
    if (targetLayer)
    {
        const int index = target->index;
        _selection.clear();
        auto* command = new fla::EditFramesCommand({targetLayer}, "Paste Motion",
            [targetLayer, index, motion, rate](std::vector<fla::Frame*>&) {
                fla::timeline::pasteMotion(*targetLayer, index, motion, rate);
            });
        command->redo();
        const bool changed = command->changedAnything();
        command->undo();
        if (!changed)
        {
            delete command;
            return false;
        }
        _editContext.commandStack().push(fla::CommandPtr(command));
        _editContext.commandStack().breakMergeChain();
        _phoenixView->clearCaches();
        _phoenixView->update();
        if (_timelineView)
            _timelineView->refreshFrames();
        return true;
    }
    const fla::Command* before = _editContext.commandStack().topCommand();
    editFrames("Paste Motion", [motion, rate](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        for (int index = first; index <= last; ++index)
            fla::timeline::pasteMotion(layer, index, motion, rate);
    });
    return _editContext.commandStack().topCommand() != before;
}

void MainWindow::showMotionPresets()
{
    MotionPresetsDialog dialog(
        [this]() {
            fla::Frame* span = currentMotionSpan();
            const std::string* motion = span ? fla::timeline::motionOf(*span) : nullptr;
            return motion ? *motion : std::string();
        },
        [this](const std::string& motion) { return pasteMotion(motion); }, this);
    dialog.exec();
}

void MainWindow::resizeSpan(const fla::Layer* constLayer, int keyframe, int duration)
{
    fla::Layer* layer = const_cast<fla::Layer*>(constLayer);
    if (!layer || layer->isLocked() || duration < 1)
        return;
    fla::Frame* span = nullptr;
    for (fla::Frame* frame : layer->frames)
    {
        if (frame && frame->index == keyframe)
            span = frame;
    }
    if (!span || span->duration == duration)
        return;
    const int before = span->duration;
    _selection.clear();
    auto* command = new fla::EditFramesCommand({layer}, duration > before ? "Extend Span" : "Shorten Span",
        [layer, keyframe, before, duration](std::vector<fla::Frame*>& removed) {
            if (duration > before)
                fla::timeline::insertFrames(*layer, keyframe + before - 1, duration - before);
            else
                fla::timeline::removeFrames(*layer, keyframe + duration, before - duration, removed);
            fla::timeline::fitMotionSpans(*layer);
        });
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::moveKeyframe(const fla::Layer* constLayer, int from, int to)
{
    if (!constLayer)
        return;
    fla::Layer* layer = const_cast<fla::Layer*>(constLayer);
    _selection.clear();
    auto* command = new fla::EditFramesCommand({layer}, "Move Keyframe",
        [layer, from, to](std::vector<fla::Frame*>&) { fla::timeline::moveKeyframe(*layer, from, to); });
    command->redo();
    if (!command->changedAnything())
    {
        command->undo();
        delete command;
        return;
    }
    command->undo();
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_player)
        _player->setCurrentFrame(to);
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::moveFrames(const std::vector<const fla::Layer*>& sources, const std::vector<const fla::Layer*>& targets, int first, int last, int to, bool copy)
{
    std::vector<fla::Layer*> from;
    std::vector<fla::Layer*> onto;
    std::vector<fla::Layer*> touched;
    for (size_t i = 0; i < sources.size() && i < targets.size(); ++i)
    {
        from.push_back(const_cast<fla::Layer*>(sources[i]));
        onto.push_back(const_cast<fla::Layer*>(targets[i]));
    }
    for (fla::Layer* layer : from)
        if (std::find(touched.begin(), touched.end(), layer) == touched.end())
            touched.push_back(layer);
    for (fla::Layer* layer : onto)
        if (std::find(touched.begin(), touched.end(), layer) == touched.end())
            touched.push_back(layer);
    if (from.empty())
        return;

    _selection.clear();
    auto* command = new fla::EditFramesCommand(touched, copy ? "Copy Frames" : "Move Frames",
        [from, onto, first, last, to, copy](std::vector<fla::Frame*>& removed) {
            fla::timeline::moveFrames(from, onto, first, last, to, copy, removed);
        });
    command->redo();
    if (!command->changedAnything())
    {
        command->undo();
        delete command;
        if (_timelineView)
            _timelineView->refreshFrames();
        return;
    }
    command->undo();
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_player)
        _player->setCurrentFrame(to);
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::copyFrames()
{
    const FrameSelection selection = framesToEdit();
    if (selection.isEmpty())
        return;
    _frameClipboard.clear();
    for (const fla::Layer* layer : selection.layers)
    {
        std::vector<std::unique_ptr<fla::Frame>> spans;
        for (fla::Frame* frame : fla::timeline::copyFrames(*layer, selection.first, selection.last))
            spans.emplace_back(frame);
        _frameClipboard.push_back(std::move(spans));
    }
    statusBar()->showMessage(QString("Copied %1 frame%2 on %3 layer%4")
        .arg(selection.last - selection.first + 1).arg(selection.last > selection.first ? "s" : "")
        .arg(selection.layers.size()).arg(selection.layers.size() == 1 ? "" : "s"), 3000);
}

void MainWindow::cutFrames()
{
    copyFrames();
    if (_frameClipboard.empty())
        return;
    editFrames("Cut Frames", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>& removed) {
        fla::timeline::removeFrames(layer, first, last - first + 1, removed);
    });
}

void MainWindow::relinkFrameClipboard(const fla::SymbolList* library)
{
    for (const auto& spans : _frameClipboard)
    {
        for (const auto& frame : spans)
        {
            for (fla::Element* element : frame->elements)
            {
                if (element)
                    fla::relinkSymbols(*element, library);
            }
        }
    }
}

void MainWindow::pasteFrames()
{
    pasteFramesAs(false);
}

void MainWindow::pasteFramesOverwrite()
{
    pasteFramesAs(true);
}

void MainWindow::convertToFrameByFrame()
{
    const double rate = _flaDocument && _flaDocument->document ? _flaDocument->document->frameRate : 24.0;
    editFrames("Convert to Frame-by-Frame", [rate](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        fla::frames::convertToFrameByFrame(layer, first, last, rate);
    });
}

void MainWindow::reverseFrames()
{
    editFrames("Reverse Frames", [](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>& removed) {
        fla::frames::reverseFrames(layer, first, last, removed);
    });
}

void MainWindow::pasteFramesAs(bool overwrite)
{
    if (_frameClipboard.empty())
        return;
    const FrameSelection selection = framesToEdit();
    if (selection.isEmpty())
        return;

    // The first copied layer goes to the first selected one, the rest to the
    // layers below it in turn, folders passed over.
    std::vector<fla::Layer*> targets;
    fla::Timeline* timeline = editedTimeline();
    if (timeline)
    {
        bool started = false;
        for (fla::Layer* layer : timeline->layers)
        {
            if (layer == selection.layers.front())
                started = true;
            if (started && layer && layer->layerType != fla::Layer::Type::Folder)
                targets.push_back(layer);
            if (targets.size() == _frameClipboard.size())
                break;
        }
    }
    if (targets.empty())
        return;

    // The copies may come from another document: their instances are
    // pointed at this one's symbols before they are pasted.
    relinkFrameClipboard(_flaDocument && _flaDocument->document ? _flaDocument->document->symbolList : nullptr);

    std::vector<std::vector<const fla::Frame*>> clips;
    for (const auto& spans : _frameClipboard)
    {
        std::vector<const fla::Frame*> clip;
        for (const auto& frame : spans)
            clip.push_back(frame.get());
        clips.push_back(clip);
    }
    const int at = selection.first;

    _selection.clear();
    auto* command = new fla::EditFramesCommand(targets, overwrite ? "Paste and Overwrite Frames" : "Paste Frames",
        [targets, clips, at, overwrite](std::vector<fla::Frame*>& removed) {
            for (size_t i = 0; i < targets.size() && i < clips.size(); ++i)
            {
                if (overwrite)
                    fla::timeline::pasteFrames(*targets[i], at, clips[i], removed);
                else
                    fla::frames::insertFrames(*targets[i], at, clips[i], removed);
            }
        });
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::showFramesMenu(const QPoint& globalPos)
{
    if (_framesMenu)
        _framesMenu->popup(globalPos);
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

void MainWindow::editLayers(fla::Timeline* timeline, const fla::LayerSnapshot& after, const QString& name)
{
    if (!timeline)
        return;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.push(fla::CommandPtr(new fla::LayerEditCommand(timeline, fla::snapshotLayers(*timeline), after,
        name.toStdString())));
    stack.breakMergeChain();
    _timelineView->refresh();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
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

    QAction* newAction = new QAction("&New...", this);
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

    QMenu* importMenu = fileMenu->addMenu("&Import");

    QAction* importStageAction = new QAction("Import to &Stage...", this);
    importStageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    importStageAction->setStatusTip("Import images onto the current frame and sounds into the library");
    connect(importStageAction, &QAction::triggered, this, &MainWindow::importToStage);
    importMenu->addAction(importStageAction);
    QAction* importLibraryAction = new QAction("Import to &Library...", this);
    importLibraryAction->setStatusTip("Import images and sounds into the library");
    connect(importLibraryAction, &QAction::triggered, this, &MainWindow::importToLibrary);
    importMenu->addAction(importLibraryAction);
    QAction* importVideoAction = new QAction("Import &Video...", this);
    importVideoAction->setStatusTip("Bring in a video to animate over, on a guide layer");
    connect(importVideoAction, &QAction::triggered, this, &MainWindow::importVideo);
    importMenu->addAction(importVideoAction);

    QMenu* exportMenu = fileMenu->addMenu("Export");

    QAction* exportSvgAction = new QAction("Export to &SVG...", this);
    exportSvgAction->setStatusTip("Export the current frame to an SVG file");
    connect(exportSvgAction, &QAction::triggered, this, &MainWindow::exportSvg);
    exportMenu->addAction(exportSvgAction);
    QAction* exportImageAction = new QAction("Export &Image...", this);
    exportImageAction->setStatusTip("Export one frame as a PNG, JPEG or GIF image");
    connect(exportImageAction, &QAction::triggered, this, &MainWindow::exportImage);
    exportMenu->addAction(exportImageAction);
    QAction* exportMovieAction = new QAction("Export &Movie...", this);
    exportMovieAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S));
    exportMovieAction->setStatusTip("Export the frames as a PNG sequence or a video");
    connect(exportMovieAction, &QAction::triggered, this, &MainWindow::exportMovie);
    exportMenu->addAction(exportMovieAction);

    // How often unsaved work is copied aside for after a crash.
    QMenu* autoSaveMenu = fileMenu->addMenu("A&uto-Save");
    QActionGroup* autoSaveGroup = new QActionGroup(this);
    for (const int minutes : {0, 1, 2, 5, 10})
    {
        QAction* choice = autoSaveMenu->addAction(minutes == 0 ? QString("Off")
            : minutes == 1 ? QString("Every Minute") : QString("Every %1 Minutes").arg(minutes));
        choice->setCheckable(true);
        choice->setChecked(AutoSave::interval() == minutes);
        autoSaveGroup->addAction(choice);
        connect(choice, &QAction::triggered, this, [minutes]() { AutoSave::setInterval(minutes); });
    }

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
    // Insert: Animate's timeline commands, which also live on the frames'
    // right-click menu.
    // Edit > Edit Symbols / Edit Document, and Edit in Place.
    editMenu->addSeparator();
    QAction* editSymbolsAction = editMenu->addAction("Edit S&ymbols");
    editSymbolsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    editSymbolsAction->setStatusTip("Open the selected instance's symbol, or go back to the document");
    connect(editSymbolsAction, &QAction::triggered, this, &MainWindow::editSymbolsOrDocument);
    connect(editMenu, &QMenu::aboutToShow, this, [this, editSymbolsAction]() {
        editSymbolsAction->setText(_phoenixView && _phoenixView->scopeDepth() > 0 ? "Edit &Document" : "Edit S&ymbols");
    });
    QAction* editInPlaceAction = editMenu->addAction("Edit in &Place");
    connect(editInPlaceAction, &QAction::triggered, this, [this]() {
        if (auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single()))
            _phoenixView->enterSymbol(instance);
        else if (auto* group = dynamic_cast<fla::Group*>(_selection.single()))
            _phoenixView->enterGroup(group);
    });

    QMenu* insertMenu = menuBar->addMenu("&Insert");
    QAction* newSymbolAction = insertMenu->addAction("New &Symbol...");
    newSymbolAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F8));
    newSymbolAction->setStatusTip("Make an empty symbol in the library and open it to draw in");
    connect(newSymbolAction, &QAction::triggered, this, &MainWindow::newSymbol);
    insertMenu->addSeparator();

    QMenu* timelineMenu = insertMenu->addMenu("&Timeline");
    QAction* insertFrameAction = timelineMenu->addAction("&Frame");
    insertFrameAction->setShortcut(QKeySequence(Qt::Key_F5));
    insertFrameAction->setStatusTip("Lengthen the span at the playhead by a frame (or by the frames selected)");
    connect(insertFrameAction, &QAction::triggered, this, &MainWindow::insertFrame);
    QAction* insertKeyframeAction = timelineMenu->addAction("&Keyframe");
    insertKeyframeAction->setShortcut(QKeySequence(Qt::Key_F6));
    insertKeyframeAction->setStatusTip("Start a keyframe here with a copy of what the span shows");
    connect(insertKeyframeAction, &QAction::triggered, this, [this]() { insertKeyframe(false); });
    QAction* insertBlankAction = timelineMenu->addAction("&Blank Keyframe");
    insertBlankAction->setShortcut(QKeySequence(Qt::Key_F7));
    insertBlankAction->setStatusTip("Start an empty keyframe here");
    connect(insertBlankAction, &QAction::triggered, this, [this]() { insertKeyframe(true); });
    insertMenu->addSeparator();
    QAction* classicTweenAction = insertMenu->addAction("&Classic Tween");
    classicTweenAction->setStatusTip("Tween the span to the next keyframe");
    connect(classicTweenAction, &QAction::triggered, this, [this]() { setClassicTween(true); });
    QAction* motionTweenAction = insertMenu->addAction("&Motion Tween");
    motionTweenAction->setStatusTip("Move the span's symbol by a motion tween; changing it later in the span keys it");
    connect(motionTweenAction, &QAction::triggered, this, &MainWindow::createMotionTween);
    QAction* shapeTweenAction = insertMenu->addAction("&Shape Tween");
    shapeTweenAction->setStatusTip("Morph the span's shape into the next keyframe's");
    connect(shapeTweenAction, &QAction::triggered, this, &MainWindow::createShapeTween);

    QMenu* editTimelineMenu = new QMenu("Ti&meline", this);
    QAction* removeFramesAction = editTimelineMenu->addAction("&Remove Frames");
    removeFramesAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    connect(removeFramesAction, &QAction::triggered, this, &MainWindow::removeFrames);
    QAction* editMultipleAction = editTimelineMenu->addAction("&Edit Multiple Frames");
    editMultipleAction->setCheckable(true);
    editMultipleAction->setStatusTip("Edit every keyframe within the onion skin's range at once");
    connect(editMultipleAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setEditMultipleFrames(on);
    });
    QAction* clearFramesAction = editTimelineMenu->addAction("Clear &Frames");
    clearFramesAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Backspace));
    clearFramesAction->setStatusTip("Empty the selected frames, keeping what follows");
    connect(clearFramesAction, &QAction::triggered, this, &MainWindow::clearFrames);
    QAction* selectAllFramesAction = editTimelineMenu->addAction("Select &All Frames");
    selectAllFramesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));
    connect(selectAllFramesAction, &QAction::triggered, this, [this]() {
        if (_timelineView)
            _timelineView->selectAllFrames();
    });
    QAction* clearKeyframeAction = editTimelineMenu->addAction("&Clear Keyframe");
    clearKeyframeAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(clearKeyframeAction, &QAction::triggered, this, &MainWindow::clearKeyframe);
    QAction* removeTweenAction = editTimelineMenu->addAction("Remove &Tween");
    connect(removeTweenAction, &QAction::triggered, this, [this]() { setClassicTween(false); });
    editTimelineMenu->addSeparator();
    QAction* cutFramesAction = editTimelineMenu->addAction("Cu&t Frames");
    cutFramesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_X));
    connect(cutFramesAction, &QAction::triggered, this, &MainWindow::cutFrames);
    QAction* copyFramesAction = editTimelineMenu->addAction("C&opy Frames");
    copyFramesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    connect(copyFramesAction, &QAction::triggered, this, &MainWindow::copyFrames);
    QAction* pasteFramesAction = editTimelineMenu->addAction("&Paste Frames");
    pasteFramesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V));
    connect(pasteFramesAction, &QAction::triggered, this, &MainWindow::pasteFrames);
    QAction* pasteOverwriteAction = editTimelineMenu->addAction("Paste and &Overwrite Frames");
    connect(pasteOverwriteAction, &QAction::triggered, this, &MainWindow::pasteFramesOverwrite);
    editTimelineMenu->addSeparator();
    QAction* copyMotionAction = editTimelineMenu->addAction("Copy &Motion");
    copyMotionAction->setStatusTip("Copy the motion of the selected motion tween");
    connect(copyMotionAction, &QAction::triggered, this, &MainWindow::copyMotion);
    QAction* pasteMotionAction = editTimelineMenu->addAction("Paste Mo&tion");
    pasteMotionAction->setStatusTip("Move the selected object by the motion copied");
    connect(pasteMotionAction, &QAction::triggered, this, [this]() {
        if (_copiedMotion.empty())
            statusBar()->showMessage("Copy a motion first.", 3000);
        else if (!pasteMotion(_copiedMotion))
            statusBar()->showMessage("Paste Motion moves one symbol instance or text block alone on its frame.", 4000);
    });
    editTimelineMenu->addSeparator();
    QAction* reverseAction = editTimelineMenu->addAction("&Reverse Frames");
    reverseAction->setStatusTip("Play the selected frames backward");
    connect(reverseAction, &QAction::triggered, this, &MainWindow::reverseFrames);
    QAction* frameByFrameAction = editTimelineMenu->addAction("Convert to &Frame-by-Frame Animation");
    frameByFrameAction->setStatusTip("Make every frame of the selected tweens a keyframe of its own");
    connect(frameByFrameAction, &QAction::triggered, this, &MainWindow::convertToFrameByFrame);
    editMenu->addSeparator();
    editMenu->addMenu(editTimelineMenu);

    // The same commands on a right-click in the frames.
    _framesMenu = new QMenu(this);
    _framesMenu->addAction(motionTweenAction);
    _framesMenu->addAction(classicTweenAction);
    _framesMenu->addAction(shapeTweenAction);
    _framesMenu->addAction(removeTweenAction);
    _framesMenu->addAction(copyMotionAction);
    _framesMenu->addAction(pasteMotionAction);
    _framesMenu->addSeparator();
    _framesMenu->addAction(insertFrameAction);
    _framesMenu->addAction(removeFramesAction);
    _framesMenu->addSeparator();
    _framesMenu->addAction(insertKeyframeAction);
    _framesMenu->addAction(insertBlankAction);
    _framesMenu->addAction(clearKeyframeAction);
    _framesMenu->addSeparator();
    _framesMenu->addAction(cutFramesAction);
    _framesMenu->addAction(copyFramesAction);
    _framesMenu->addAction(pasteFramesAction);
    _framesMenu->addAction(pasteOverwriteAction);
    _framesMenu->addSeparator();
    _framesMenu->addAction(reverseAction);
    _framesMenu->addAction(frameByFrameAction);

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

    QMenu* symbolMenu = modifyMenu->addMenu("S&ymbol");
    _swapSymbolAction = new QAction("S&wap Symbol...", this);
    _swapSymbolAction->setStatusTip("Show another library symbol in the selected instances");
    connect(_swapSymbolAction, &QAction::triggered, this, &MainWindow::swapSymbol);
    symbolMenu->addAction(_swapSymbolAction);

    modifyMenu->addSeparator();

    // Modify > Transform, with Animate's keys (its Remove Transform key is
    // Redo's here).
    _transformMenu = modifyMenu->addMenu("&Transform");
    const auto addTransform = [this](const QString& text, const QKeySequence& key, std::function<void()> act) {
        QAction* action = new QAction(text, this);
        if (!key.isEmpty())
            action->setShortcut(key);
        connect(action, &QAction::triggered, this, [act]() { act(); });
        _transformMenu->addAction(action);
        _transformActions.push_back(action);
        return action;
    };
    addTransform("Scale and &Rotate...", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), [this]() { scaleAndRotate(); });
    _transformMenu->addSeparator();
    addTransform("Rotate 90\u00b0 &CW", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_9),
        [this]() { transformSelection("Rotate 90\u00b0 CW", QTransform().rotate(90.0)); });
    addTransform("Rotate 90\u00b0 CC&W", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7),
        [this]() { transformSelection("Rotate 90\u00b0 CCW", QTransform().rotate(-90.0)); });
    _transformMenu->addSeparator();
    addTransform("Flip &Vertical", QKeySequence(),
        [this]() { transformSelection("Flip Vertical", QTransform::fromScale(1.0, -1.0)); });
    addTransform("Flip &Horizontal", QKeySequence(),
        [this]() { transformSelection("Flip Horizontal", QTransform::fromScale(-1.0, 1.0)); });
    _transformMenu->addSeparator();
    addTransform("Remove &Transform", QKeySequence(), [this]() { removeTransform(); });

    QMenu* combineMenu = modifyMenu->addMenu("&Combine Objects");
    const char* combineNames[4] = {"&Union", "&Intersect", "&Punch", "&Crop"};
    const char* combineTips[4] = {"Make the selected shapes one drawing object",
        "Keep only where the selected shapes overlap, in the topmost one's colors",
        "Cut the topmost shape's area out of the ones under it", "Keep only what lies inside the topmost shape"};
    for (int i = 0; i < 4; ++i)
    {
        QAction* action = combineMenu->addAction(combineNames[i]);
        action->setStatusTip(combineTips[i]);
        connect(action, &QAction::triggered, this, [this, i]() { combineObjects(i); });
        _combineActions.push_back(action);
    }

    QMenu* modifyTimelineMenu = modifyMenu->addMenu("Ti&meline");
    _distributeAction = new QAction("&Distribute to Layers", this);
    _distributeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    _distributeAction->setStatusTip("Put each selected object on a layer of its own");
    connect(_distributeAction, &QAction::triggered, this, &MainWindow::distributeToLayers);
    modifyTimelineMenu->addAction(_distributeAction);
    QAction* distributeKeysAction = modifyTimelineMenu->addAction("Distribute to &Keyframes");
    distributeKeysAction->setStatusTip("Put each selected object on a keyframe of its own, one after another");
    connect(distributeKeysAction, &QAction::triggered, this, &MainWindow::distributeToKeyframes);

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
    QAction* lockAction = new QAction("&Lock", this);
    lockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));
    lockAction->setStatusTip("Keep the selected objects from being picked or moved");
    connect(lockAction, &QAction::triggered, this, &MainWindow::lockSelection);
    QAction* unlockAction = new QAction("&Unlock All", this);
    unlockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
    connect(unlockAction, &QAction::triggered, this, &MainWindow::unlockAll);
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
    arrangeMenu->addSeparator();
    arrangeMenu->addAction(lockAction);
    arrangeMenu->addAction(unlockAction);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* framePickerAction = new QAction("&Frame Picker", this);
    framePickerAction->setStatusTip("Pick the frame a graphic instance shows from pictures of its frames");
    connect(framePickerAction, &QAction::triggered, this, [this]() {
        if (!_framePickerDock)
            return;
        _framePickerDock->show();
        _framePickerDock->raise();
        updateFramePicker();
    });
    viewMenu->addAction(framePickerAction);
    QAction* colorAction = new QAction("&Color", this);
    colorAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9));
    colorAction->setStatusTip("Solid colors, gradients and bitmap fills for new art");
    connect(colorAction, &QAction::triggered, this, [this]() {
        if (_colorDock)
        {
            _colorDock->show();
            _colorDock->raise();
        }
    });
    viewMenu->addAction(colorAction);
    QAction* swatchesAction = new QAction("S&watches", this);
    swatchesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F9));
    swatchesAction->setStatusTip("The document's saved colors");
    connect(swatchesAction, &QAction::triggered, this, [this]() {
        if (_swatchesDock)
        {
            _swatchesDock->show();
            _swatchesDock->raise();
        }
    });
    viewMenu->addAction(swatchesAction);
    const auto showDock = [this](QDockWidget* const* dock) {
        return [this, dock]() {
            if (*dock)
            {
                (*dock)->show();
                (*dock)->raise();
            }
        };
    };
    QAction* alignAction = viewMenu->addAction("&Align");
    alignAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(alignAction, &QAction::triggered, this, showDock(&_alignDock));
    QAction* transformAction = viewMenu->addAction("&Transform");
    transformAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(transformAction, &QAction::triggered, this, showDock(&_transformDock));
    QAction* infoAction = viewMenu->addAction("&Info");
    infoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(infoAction, &QAction::triggered, this, showDock(&_infoDock));
    QAction* motionPresetsAction = viewMenu->addAction("Motion &Presets…");
    motionPresetsAction->setStatusTip("Saved motions to put on an object, Animate's among them");
    connect(motionPresetsAction, &QAction::triggered, this, &MainWindow::showMotionPresets);
    QAction* motionEditorAction = viewMenu->addAction("&Motion Editor");
    motionEditorAction->setStatusTip("The property curves of a motion tween, with their keyframes and eases");
    connect(motionEditorAction, &QAction::triggered, this, [this]() {
        if (!_motionEditorDock)
            return;
        _motionEditorDock->show();
        _motionEditorDock->raise();
        _motionEditor->refresh();
    });
    viewMenu->addSeparator();

    // Animate shows one scene at a time; these pick which.
    QMenu* sceneMenu = viewMenu->addMenu("&Scene");
    connect(sceneMenu, &QMenu::aboutToShow, this, [this, sceneMenu]() {
        sceneMenu->clear();
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        const int count = document ? static_cast<int>(document->timelines.size()) : 0;
        const int current = document ? document->currentSceneIndex() : -1;
        // The shortcuts belong to the window's own actions below; here they
        // are only shown.
        QAction* previous = sceneMenu->addAction("&Previous Scene\tShift+PgUp", this,
            [this, current]() { goToScene(current - 1); });
        previous->setEnabled(current > 0);
        QAction* next = sceneMenu->addAction("&Next Scene\tShift+PgDown", this,
            [this, current]() { goToScene(current + 1); });
        next->setEnabled(current >= 0 && current + 1 < count);
        if (count > 0)
            sceneMenu->addSeparator();
        for (int i = 0; i < count; ++i)
        {
            const std::string& name = document->timelines[static_cast<size_t>(i)]->name;
            QAction* scene = sceneMenu->addAction(
                name.empty() ? QString("Scene %1").arg(i + 1) : QString::fromStdString(name), this,
                [this, i]() { goToScene(i); });
            scene->setCheckable(true);
            scene->setChecked(i == current);
        }
    });
    // The shortcuts work with the menu closed too.
    QAction* previousScene = new QAction(this);
    previousScene->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_PageUp));
    connect(previousScene, &QAction::triggered, this, [this]() {
        if (_flaDocument && _flaDocument->document)
            goToScene(_flaDocument->document->currentSceneIndex() - 1);
    });
    addAction(previousScene);
    QAction* nextScene = new QAction(this);
    nextScene->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_PageDown));
    connect(nextScene, &QAction::triggered, this, [this]() {
        if (_flaDocument && _flaDocument->document)
            goToScene(_flaDocument->document->currentSceneIndex() + 1);
    });
    addAction(nextScene);

    _outlinerAction = new QAction("Ou&tliner", this);
    _outlinerAction->setCheckable(true);
    _outlinerAction->setChecked(_outlinerVisible);
    _outlinerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O));
    _outlinerAction->setStatusTip("Show or hide the document outliner at the left of the stage");
    connect(_outlinerAction, &QAction::toggled, this, [this](bool on) {
        if (on != _outlinerVisible)
            setOutlinerVisible(on);
    });
    viewMenu->addAction(_outlinerAction);

    _onionSkinAction = new QAction("&Onion Skin", this);
    _onionSkinAction->setCheckable(true);
    _onionSkinAction->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_O));
    _onionSkinAction->setStatusTip("Show the frames either side of the playhead faded under it");
    connect(_onionSkinAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setOnionSkin(on, _onion.before, _onion.after);
        if (_timelineView)
            _timelineView->setOnionSkin(on, _onion.before, _onion.after);
    });
    viewMenu->addAction(_onionSkinAction);
    QMenu* onionMenu = viewMenu->addMenu("Onion Skin &Settings");
    connect(onionMenu, &QMenu::aboutToShow, this, [this, onionMenu]() { fillOnionMenu(onionMenu); });

    // Sound plays with the timeline; Animate's Control > Mute Sounds.
    QAction* muteAction = new QAction("&Mute Sounds", this);
    muteAction->setCheckable(true);
    muteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M));
    muteAction->setStatusTip("Play the timeline without its sounds");
    connect(muteAction, &QAction::toggled, this, [this](bool on) {
        if (_timelineAudio)
            _timelineAudio->setMuted(on);
    });
    viewMenu->addAction(muteAction);

    QAction* rulersAction = new QAction("&Rulers", this);
    rulersAction->setCheckable(true);
    rulersAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_R));
    rulersAction->setStatusTip("Rulers along the stage, from which guides are dragged");
    connect(rulersAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setRulersVisible(on);
    });
    viewMenu->addAction(rulersAction);

    QMenu* guidesMenu = viewMenu->addMenu("&Guides");
    QAction* showGuidesAction = guidesMenu->addAction("&Show Guides");
    showGuidesAction->setCheckable(true);
    showGuidesAction->setChecked(true);
    showGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    connect(showGuidesAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setGuidesVisible(on);
    });
    QAction* snapGuidesAction = guidesMenu->addAction("S&nap to Guides");
    _snapGuidesAction = snapGuidesAction;
    snapGuidesAction->setCheckable(true);
    snapGuidesAction->setChecked(true);
    snapGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
    connect(snapGuidesAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setGuideSnapEnabled(on);
        // A setting of the document, as in Animate, saved with it.
        if (_flaDocument && _flaDocument->document)
            _flaDocument->document->guidesSnapTo = on;
    });
    QAction* clearGuidesAction = guidesMenu->addAction("&Clear Guides");
    connect(clearGuidesAction, &QAction::triggered, this, [this]() {
        if (_phoenixView)
            _phoenixView->clearGuides();
    });
    viewMenu->addSeparator();

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

    // The Rotation tool's turn, undone.
    QAction* straightenAction = new QAction("&Reset Rotation", this);
    straightenAction->setStatusTip("Set the stage square to the screen again");
    connect(straightenAction, &QAction::triggered, this, [this]() {
        if (_phoenixView)
            _phoenixView->setRotation(0.0);
    });
    viewMenu->addAction(straightenAction);

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
    _snapToObjectsAction = snapToObjectsAction;
    snapToObjectsAction->setCheckable(true);
    snapToObjectsAction->setChecked(true);
    snapToObjectsAction->setStatusTip("Line dragged objects up with other objects and the stage");
    connect(snapToObjectsAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setObjectSnapEnabled(on);
        if (_flaDocument && _flaDocument->document)
            _flaDocument->document->objectsSnapTo = on;
        saveSettings();
    });
    viewMenu->addAction(snapToObjectsAction);

    QAction* snapToPixelsAction = new QAction("Snap to &Pixels", this);
    snapToPixelsAction->setCheckable(true);
    snapToPixelsAction->setChecked(QSettings().value("snap/pixels", false).toBool());
    _snapper.setPixelSnapEnabled(snapToPixelsAction->isChecked());
    snapToPixelsAction->setStatusTip("Put what is drawn or dragged on whole pixels");
    connect(snapToPixelsAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setPixelSnapEnabled(on);
        QSettings().setValue("snap/pixels", on);
    });
    viewMenu->addAction(snapToPixelsAction);

    viewMenu->addSeparator();

    QAction* dimOutsideDocumentAction = new QAction("Dim Outside Document", this);
    dimOutsideDocumentAction->setCheckable(true);
    dimOutsideDocumentAction->setChecked(false);
    dimOutsideDocumentAction->setStatusTip("Dim the area outside the document rectangle");
    connect(dimOutsideDocumentAction, &QAction::toggled, _phoenixView, &PhoenixView::setDimOutsideDocument);
    viewMenu->addAction(dimOutsideDocumentAction);

    // Help menu
    // Control menu: playback, with Animate's keys. They act while the stage
    // or the timeline has the focus, so Enter in a field stays the field's.
    QMenu* controlMenu = menuBar->addMenu("&Control");
    const auto addControl = [this, controlMenu](const QString& text, const QKeySequence& key,
                                std::function<void()> act) {
        QAction* action = new QAction(text, this);
        action->setShortcut(key);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, [act]() { act(); });
        controlMenu->addAction(action);
        if (_phoenixView)
            _phoenixView->addAction(action);
        if (_timelineView)
            _timelineView->addAction(action);
        return action;
    };
    const auto timelines = [this]() { return _timelineView ? _timelineView->timelines() : nullptr; };
    // The keyframe after or before the playhead on the current layer, or on
    // any layer when there is no current one.
    const auto keyframeStep = [this](bool forward) {
        if (!_player || !_timelineView)
            return;
        fla::Timeline* timeline = nullptr;
        int index = 0;
        if (!_timelineView->currentLayer(timeline, index) || !timeline)
            timeline = _phoenixView ? _phoenixView->scopeTimeline() : nullptr;
        if (!timeline && _flaDocument && _flaDocument->document)
            timeline = _flaDocument->document->currentScene();
        if (!timeline)
            return;
        std::vector<const fla::Layer*> layers;
        if (index >= 0 && index < static_cast<int>(timeline->layers.size()) &&
            timeline->layers[index]->layerType != fla::Layer::Type::Folder)
            layers.push_back(timeline->layers[index]);
        else
            layers.assign(timeline->layers.begin(), timeline->layers.end());
        const int current = _player->currentFrame();
        int best = -1;
        for (const fla::Layer* layer : layers)
        {
            for (const fla::Frame* frame : layer->frames)
            {
                if (forward && frame->index > current && (best < 0 || frame->index < best))
                    best = frame->index;
                if (!forward && frame->index < current && frame->index > best)
                    best = frame->index;
            }
        }
        if (best >= 0)
            _player->setCurrentFrame(best);
    };
    addControl("&Play", QKeySequence(Qt::Key_Return), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->playStop();
    });
    // Shift with comma and period comes through as < and > on many
    // keyboards, so those keys do it too.
    QAction* rewind = addControl("&Rewind", QKeySequence(Qt::SHIFT | Qt::Key_Comma), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->goToFirst();
    });
    rewind->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_Comma), QKeySequence(Qt::SHIFT | Qt::Key_Less)});
    QAction* toEnd = addControl("Go To &End", QKeySequence(Qt::SHIFT | Qt::Key_Period), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->goToLast();
    });
    toEnd->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_Period), QKeySequence(Qt::SHIFT | Qt::Key_Greater)});
    controlMenu->addSeparator();
    addControl("Step &Forward One Frame", QKeySequence(Qt::Key_Period), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepForward();
    });
    addControl("Step &Backward One Frame", QKeySequence(Qt::Key_Comma), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->stepBack();
    });
    addControl("&Next Keyframe", QKeySequence(Qt::ALT | Qt::Key_Period), [keyframeStep]() { keyframeStep(true); });
    addControl("Pre&vious Keyframe", QKeySequence(Qt::ALT | Qt::Key_Comma), [keyframeStep]() { keyframeStep(false); });
    controlMenu->addSeparator();
    QAction* loopAction = addControl("&Loop Playback", QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_L), [timelines]() {
        if (TimelinesPanel* panel = timelines())
            panel->setLoopPlayback(!panel->loopPlayback());
    });
    loopAction->setStatusTip("Play the loop range over and over; drag its ends in the ruler");

    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* startAction = new QAction("&Start Screen...", this);
    startAction->setStatusTip("New from a preset, or open a recent file");
    connect(startAction, &QAction::triggered, this, &MainWindow::showStartDialog);
    helpMenu->addAction(startAction);

    QAction* checkForUpdatesAction = new QAction("Check for &Updates...", this);
    checkForUpdatesAction->setStatusTip("Look for a newer version of Phoenix");
    connect(checkForUpdatesAction, &QAction::triggered, this, [this]() { _updater->checkForUpdates(true); });
    helpMenu->addAction(checkForUpdatesAction);

    helpMenu->addSeparator();

    QAction* aboutAction = new QAction("&About Phoenix", this);
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    helpMenu->addAction(aboutAction);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open"), _lastDirectory,
        tr("Documents (*.phx *.fla DOMDocument.xml);;Phoenix Documents (*.phx);;FLA Files (*.fla);;"
           "XFL Documents (DOMDocument.xml);;All Files (*)"));

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

    // A group being edited is a group again in the file.
    if (_phoenixView)
        _phoenixView->exitGroups();

    // A drawing still floating is committed before anything is written, so that
    // saving with one selected and saving just after letting it go produce the
    // same file rather than two shapes in one case and one in the other.
    if (_phoenixView)
        _phoenixView->flushPendingMerge();

    if (!isPhxPath(filePath) && !confirmLossySave())
        return false;

    QString error;

    if (isPhxPath(filePath))
    {
        phx::SaveOptions options;
        options.application = QString("Phoenix %1").arg(QCoreApplication::applicationVersion()).toStdString();
        if (_phoenixView)
        {
            // A small picture of the first frame, for file browsers.
            const int width = 256;
            const fla::Document* document = _flaDocument->document;
            const int height = std::max(1, width * std::max(1, document->height) / std::max(1, document->width));
            const QImage picture = _phoenixView->renderFrame(0, QSize(width, height), false);
            QByteArray png;
            QBuffer buffer(&png);
            if (buffer.open(QIODevice::WriteOnly) && picture.save(&buffer, "PNG"))
                options.thumbnailPng.assign(png.begin(), png.end());
        }
        std::string phxError;
        if (!phx::save(*_flaDocument->document, filePath.toStdString(), options, phxError))
            error = QString::fromStdString(phxError);
    }
    else if (filePath.endsWith(".fla", Qt::CaseInsensitive))
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
    if (_autoSave)
        _autoSave->discard();
    _flaDocument->fromBinary = false;
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

    // Saving writes the current format, which Flash CS4 and earlier cannot
    // open, so an old binary document is not replaced without asking.
    if (_flaDocument && _flaDocument->fromBinary && !isPhxPath(_documentPath))
    {
        QMessageBox box(QMessageBox::Question, "Save",
            "This document was opened from the Flash CS4 (or earlier) format. Phoenix saves the current "
            "format, which Flash CS4 and earlier cannot open.",
            QMessageBox::NoButton, this);
        QPushButton* saveAs = box.addButton("Save As...", QMessageBox::AcceptRole);
        QPushButton* replace = box.addButton("Replace", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(saveAs);
        box.exec();
        if (box.clickedButton() == saveAs)
        {
            saveFileAs();
            return;
        }
        if (box.clickedButton() != replace)
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

    // A new document is a Phoenix document; one opened from FLA or XFL is
    // offered in its own format, with Phoenix's a choice away.
    const QString suggested = _documentPath.isEmpty()
        ? (_lastDirectory.isEmpty() ? QString("untitled.phx")
                                    : QDir(_lastDirectory).filePath("untitled.phx"))
        : _documentPath;

    const QString phxFilter = tr("Phoenix Document (*.phx)");
    const QString flaFilter = tr("FLA Files (*.fla)");
    const QString xflFilter = tr("XFL Folder (*)");
    QString selected = isPhxPath(suggested) ? phxFilter
        : suggested.endsWith(".fla", Qt::CaseInsensitive) ? flaFilter : xflFilter;
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Save As"), suggested, phxFilter + ";;" + flaFilter + ";;" + xflFilter, &selected);

    if (fileName.isEmpty())
        return;
    // The format chosen decides the extension when the name has none.
    if (selected == phxFilter && !isPhxPath(fileName))
        fileName += ".phx";
    else if (selected == flaFilter && !fileName.endsWith(".fla", Qt::CaseInsensitive))
        fileName += ".fla";

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

void MainWindow::goToScene(int index)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || index < 0 || index >= static_cast<int>(document->timelines.size()) ||
        index == document->currentSceneIndex())
    {
        return;
    }
    if (_phoenixView)
        _phoenixView->exitToDepth(0);
    _selection.clear();
    document->setCurrentScene(index);
    if (_player)
        _player->setCurrentFrame(0);
    if (_timelineView)
        _timelineView->setDocument(_flaDocument);
    if (_documentView)
        _documentView->setDocument(_flaDocument);
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    rebuildBreadcrumb();
    updateSelectionState();
}

int MainWindow::exportFromCommandLine(const QString& source, const QString& output, bool perLayer)
{
    if (!QFileInfo::exists(source))
    {
        std::fprintf(stderr, "No such file: %s\n", qPrintable(source));
        return 1;
    }
    loadFLAFile(source);
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        std::fprintf(stderr, "Could not open %s\n", qPrintable(source));
        return 1;
    }
    const fla::Document* document = _flaDocument->document;
    MovieExportSettings settings;
    settings.format = output.endsWith(".png", Qt::CaseInsensitive) ? MovieExportSettings::Format::PngSequence
        : output.endsWith(".gif", Qt::CaseInsensitive)               ? MovieExportSettings::Format::Gif
        : output.endsWith(".wav", Qt::CaseInsensitive)               ? MovieExportSettings::Format::Wav
                                                                     : MovieExportSettings::Format::Video;
    if (perLayer)
        settings.format = MovieExportSettings::Format::LayerSequences;
    settings.path = QFileInfo(output).absoluteFilePath();
    settings.size = QSize(static_cast<int>(std::lround(document->width)), static_cast<int>(std::lround(document->height)));
    settings.first = 0;
    settings.last = std::max(1, _phoenixView->frameCount()) - 1;
    QString error;
    if (!MovieExport::run(settings, *_phoenixView, _timelineAudio, document->frameRate, nullptr, error))
    {
        std::fprintf(stderr, "%s\n", qPrintable(error.isEmpty() ? QString("Export cancelled.") : error));
        return 1;
    }
    std::printf("Exported %d frames to %s\n", settings.last + 1, qPrintable(settings.path));
    return 0;
}

int MainWindow::exportImageFromCommandLine(const QString& source, const QString& output, const QStringList& options)
{
    loadFLAFile(source);
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        std::fprintf(stderr, "Could not open %s\n", qPrintable(source));
        return 1;
    }
    const fla::Document* document = _flaDocument->document;
    ImageExportSettings settings;
    settings.path = QFileInfo(output).absoluteFilePath();
    settings.format = ImageExportDialog::formatOf(output);
    settings.size = QSize(document->width, document->height);
    for (int i = 0; i + 1 < options.size(); i += 2)
    {
        const QString& name = options.at(i);
        const QString& value = options.at(i + 1);
        if (name == "--frame")
            settings.frame = std::max(0, value.toInt() - 1);
        else if (name == "--size")
        {
            const QStringList parts = value.split('x');
            if (parts.size() == 2)
                settings.size = QSize(std::max(1, parts.at(0).toInt()), std::max(1, parts.at(1).toInt()));
        }
        else if (name == "--matte")
        {
            if (value == "transparent")
                settings.matte = ImageExportSettings::Matte::Transparent;
            else if (QColor(value).isValid())
            {
                settings.matte = ImageExportSettings::Matte::Color;
                settings.matteColor = QColor(value);
            }
        }
        else if (name == "--quality")
            settings.quality = std::clamp(value.toInt(), 0, 100);
    }
    QString error;
    if (!ImageExport::run(settings, *_phoenixView, error))
    {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    std::printf("Exported frame %d to %s\n", settings.frame + 1, qPrintable(settings.path));
    return 0;
}

void MainWindow::exportImage()
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
        return;
    const fla::Document* document = _flaDocument->document;
    const QString base = _documentPath.isEmpty() ? QString("frame") : QFileInfo(_documentPath).completeBaseName();
    const QString folder = _lastDirectory.isEmpty() ? QFileInfo(_documentPath).absolutePath() : _lastDirectory;
    ImageExportDialog dialog(QSize(document->width, document->height), std::max(1, _phoenixView->frameCount()),
        _player ? _player->currentFrame() : 0, QDir(folder).filePath(base + ".png"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ImageExportSettings settings = dialog.settings();
    QString error;
    const bool written = ImageExport::run(settings, *_phoenixView, error);
    _phoenixView->update();
    if (!written)
    {
        QMessageBox::warning(this, "Export Image", QString("Could not write %1:\n\n%2").arg(settings.path, error));
        return;
    }
    _lastDirectory = QFileInfo(settings.path).absolutePath();
    saveSettings();
    statusBar()->showMessage(QString("Exported %1").arg(settings.path), 5000);
}

void MainWindow::exportMovie()
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }
    const fla::Document* document = _flaDocument->document;
    const QString base = _documentPath.isEmpty() ? QString("movie") : QFileInfo(_documentPath).completeBaseName();
    const QString folder = _lastDirectory.isEmpty() ? QFileInfo(_documentPath).absolutePath() : _lastDirectory;
    MovieExportDialog dialog(QSize(static_cast<int>(std::lround(document->width)),
                                 static_cast<int>(std::lround(document->height))),
        std::max(1, _phoenixView->frameCount()), QDir(folder).filePath(base + ".mp4"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const MovieExportSettings settings = dialog.settings();

    if (settings.format == MovieExportSettings::Format::Video &&
        findFfmpeg("Export Movie", "Phoenix encodes video with ffmpeg").isEmpty())
        return;

    QString error;
    const bool written = MovieExport::run(settings, *_phoenixView, _timelineAudio, document->frameRate, this, error);
    _phoenixView->update();
    if (written)
    {
        _lastDirectory = QFileInfo(settings.path).absolutePath();
        saveSettings();
        statusBar()->showMessage(QString("Exported %1").arg(QDir::toNativeSeparators(settings.path)), 5000);
    }
    else if (!error.isEmpty())
    {
        QMessageBox::warning(this, "Export Failed", error);
    }
}

void MainWindow::quit()
{
    // close() goes through closeEvent(), so File > Exit gets the same
    // unsaved-changes prompt as closing the window.
    close();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Phoenix",
        QString("<b>Phoenix</b> %1<br><br>An experimental Flash FLA editor.<br><br>"
                "<a href=\"https://github.com/brendan-duncan/phoenix\">github.com/brendan-duncan/phoenix</a>")
            .arg(Updater::currentVersion()));
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
    if (_autoSave)
        _autoSave->noteChanged();

    // Frames come and go with edits too, and the Properties panel shows
    // whatever an edit or an undo just changed. Layers do as well (an undone
    // New Layer, say), so the list is rebuilt when it no longer matches.
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_swatchesPanel)
        _swatchesPanel->refreshIfStale();
    if (_transformPanel)
        _transformPanel->refresh();
    if (_infoPanel)
        _infoPanel->refresh();
    if (_motionEditor)
        _motionEditor->refresh();
    if (_colorPanel)
        _colorPanel->refresh();
    updateFramePicker();
    if (_timelineView)
    {
        _timelineView->refreshIfStale();
        _timelineView->refreshFrames();
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
    // "Phoenix 0.2.0 - D:\fla\file.fla", with the full path of a saved
    // document and the name of one not saved yet, and a * for unsaved changes.
    const QString app = QString("Phoenix %1").arg(QCoreApplication::applicationVersion());
    if (_documentName.isEmpty())
    {
        setWindowTitle(app);
        return;
    }

    const QString shown = _documentPath.isEmpty() ? _documentName : QDir::toNativeSeparators(_documentPath);
    setWindowTitle(QString("%1 - %2%3")
        .arg(app)
        .arg(shown)
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
    const auto makeTool = [this, toolGroup](ToolIcons::Tool icon, const char* name, QKeyCombination key,
                              const char* tip, std::function<Tool*()> tool) {
        QAction* action = new QAction(ToolIcons::icon(icon), name, this);
        action->setCheckable(true);
        if (key.key() != Qt::Key_unknown)
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
        _toolActions[tool()] = action;
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
    // Animate's free transform slot: Free Transform and Gradient Transform.
    addSlot({
        makeTool(ToolIcons::Tool::FreeTransform, "Free Transform", Qt::Key_Q,
            "Scale, rotate and skew the selection by its handles.",
            [this]() -> Tool* { return _freeTransformTool.get(); }),
        makeTool(ToolIcons::Tool::GradientTransform, "Gradient Transform", Qt::Key_F,
            "Click a gradient or bitmap fill, then drag its handles to move, scale and turn it.",
            [this]() -> Tool* { return _gradientTransformTool.get(); }),
    });
    // Animate's lasso slot: the Lasso, the Polygon tool and the Magic Wand.
    addSlot({
        makeTool(ToolIcons::Tool::Lasso, "Lasso", Qt::Key_L,
            "Draw a loop round the objects to select. Shift adds to the selection.",
            [this]() -> Tool* { return _lassoTool.get(); }),
        makeTool(ToolIcons::Tool::PolygonLasso, "Polygon", Qt::Key_unknown,
            "Click the corners of a polygon round what to select; double-click, Enter or a click on the "
            "first corner closes it. Shift adds to the selection.",
            [this]() -> Tool* { return _polygonTool.get(); }),
        makeTool(ToolIcons::Tool::MagicWand, "Magic Wand", Qt::Key_unknown,
            "Click a broken-apart bitmap to select the area of like color. Threshold and smoothing are in "
            "the Properties panel.",
            [this]() -> Tool* { return _magicWandTool.get(); }),
    });

    toolBar->addSeparator();

    // P for the pen, as in Animate. B is already the bounding-box toggle, and a
    // clash leaves Qt firing neither.
    // Animate's pen slot: the Pen and the anchor point tools.
    addSlot({
        makeTool(ToolIcons::Tool::Pen, "Pen", Qt::Key_P,
            "Click for a corner point, drag for a smooth one. Alt-drag breaks the tangent, "
            "clicking the first point closes the path, Enter finishes it open.",
            [this]() -> Tool* { return _penTool.get(); }),
        makeTool(ToolIcons::Tool::AddAnchor, "Add Anchor Point", Qt::Key_Equal,
            "Click a path to add an anchor there, leaving the curve as it was.",
            [this]() -> Tool* { return _addAnchorTool.get(); }),
        makeTool(ToolIcons::Tool::DeleteAnchor, "Delete Anchor Point", Qt::Key_Minus,
            "Click an anchor to take it out; the curve is refitted through the gap.",
            [this]() -> Tool* { return _deleteAnchorTool.get(); }),
        makeTool(ToolIcons::Tool::ConvertAnchor, "Convert Anchor Point", Qt::SHIFT | Qt::Key_C,
            "Click a smooth anchor to make it a corner; drag from an anchor to pull out handles.",
            [this]() -> Tool* { return _convertAnchorTool.get(); }),
    });
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
    // Animate's two brushes and Phoenix's own share a slot; B is the Fluid
    // Brush's, as in Animate.
    addSlot({
        makeTool(ToolIcons::Tool::FluidBrush, "Fluid Brush", Qt::Key_B,
            "Paint a fill whose width follows the pen's pressure and speed, with tapered ends "
            "and a smoothed line. Settings are in the Properties panel.",
            [this]() -> Tool* { return _brushTool.get(); }),
        makeTool(ToolIcons::Tool::Brush, "Classic Brush", Qt::Key_unknown,
            "Paint a fill with one of Flash's brush shapes. Use Pressure varies the width; "
            "Use Tilt turns the tip with the pen.",
            [this]() -> Tool* { return _classicBrushTool.get(); }),
        makeTool(ToolIcons::Tool::InkBrush, "Ink Brush", Qt::Key_unknown,
            "Paint steady inked lines: a stabilizer trails the pen, the pressure response is "
            "adjustable, and tilting the pen broadens the line.",
            [this]() -> Tool* { return _inkBrushTool.get(); }),
    });

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

    // Animate's hand slot: the Hand, the Rotation tool and the Time Scrub
    // tool.
    addSlot({
        makeTool(ToolIcons::Tool::Hand, "Hand", Qt::Key_H,
            "Drag to pan the stage. Holding space does the same with any tool.",
            [this]() -> Tool* { return _handTool.get(); }),
        makeTool(ToolIcons::Tool::Rotation, "Rotation", Qt::SHIFT | Qt::Key_H,
            "Turn the view of the stage: click to set the pivot, drag to turn about it (shift for 15 degree "
            "steps), double-click to set it straight.",
            [this]() -> Tool* { return _rotationTool.get(); }),
        makeTool(ToolIcons::Tool::TimeScrub, "Time Scrub", Qt::Key_C,
            "Drag left or right across the stage to move through the frames.",
            [this]() -> Tool* { return _timeScrubTool.get(); }),
    });
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

    // Black and White, and Swap Colors, under the swatches as in Animate's
    // toolbar.
    const auto smallButton = [toolBar](const QString& tip, const std::function<void(QPainter&)>& draw) {
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        draw(painter);
        painter.end();
        QToolButton* button = new QToolButton(toolBar);
        button->setIcon(QIcon(pixmap));
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        button->setToolTip(tip);
        return button;
    };
    QToolButton* blackWhite = smallButton("Black and white", [](QPainter& p) {
        p.setPen(QColor(150, 150, 150));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(5.5, 5.5, 9, 9));
        p.setBrush(Qt::black);
        p.drawRect(QRectF(1.5, 1.5, 9, 9));
    });
    connect(blackWhite, &QToolButton::clicked, this, [this]() {
        const uint8_t black[4] = {0, 0, 0, 255};
        const uint8_t white[4] = {255, 255, 255, 255};
        std::copy(black, black + 4, _drawingStyle.strokeColor);
        std::copy(white, white + 4, _drawingStyle.fillColor);
        _drawingStyle.fillKind = fla::DrawingStyle::FillKind::Solid;
        _drawingStyle.hasFill = _drawingStyle.hasStroke = true;
        refreshStyleSwatches();
        if (_colorPanel)
            _colorPanel->refresh();
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    });
    toolBar->addWidget(blackWhite);
    QToolButton* swap = smallButton("Swap stroke and fill colors", [](QPainter& p) {
        p.setPen(QPen(QColor(210, 210, 210), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(2.5, 2.5, 11, 11), 0, 90 * 16);
        p.setBrush(QColor(210, 210, 210));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(8, 0.5), QPointF(8, 5.5), QPointF(5, 3)}));
        p.drawPolygon(QPolygonF({QPointF(15.5, 8), QPointF(10.5, 8), QPointF(13, 11)}));
    });
    connect(swap, &QToolButton::clicked, this, [this]() {
        uint8_t held[4];
        std::copy(_drawingStyle.strokeColor, _drawingStyle.strokeColor + 4, held);
        std::copy(_drawingStyle.fillColor, _drawingStyle.fillColor + 4, _drawingStyle.strokeColor);
        std::copy(held, held + 4, _drawingStyle.fillColor);
        std::swap(_drawingStyle.hasFill, _drawingStyle.hasStroke);
        refreshStyleSwatches();
        if (_colorPanel)
            _colorPanel->refresh();
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    });
    toolBar->addWidget(swap);

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

    // The Library sits in a tab beside Properties, as in Animate.
    QDockWidget* libraryDock = new QDockWidget("Library", this);
    libraryDock->setObjectName("libraryDock");
    libraryDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    _libraryPanel = new LibraryPanel(libraryDock);
    libraryDock->setWidget(_libraryPanel);
    addDockWidget(Qt::RightDockWidgetArea, libraryDock);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
    tabifyDockWidget(dock, libraryDock);

    // The Frame Picker waits, closed, until View > Frame Picker opens it.
    _framePickerDock = new QDockWidget("Frame Picker", this);
    _framePickerDock->setObjectName("framePickerDock");
    _framePickerDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _framePicker = new FramePickerPanel(_framePickerDock);
    _framePickerDock->setWidget(_framePicker);
    addDockWidget(Qt::RightDockWidgetArea, _framePickerDock);
    tabifyDockWidget(libraryDock, _framePickerDock);
    _framePickerDock->hide();
    _framePicker->setPreviewProvider([this](const QString& name, int frame, const QSize& size) {
        return _phoenixView ? _phoenixView->libraryPreview("symbol", name, size, frame) : QImage();
    });
    connect(_framePicker, &FramePickerPanel::frameChosen, this, &MainWindow::pickInstanceFrame);
    connect(_propertiesPanel, &PropertiesPanel::swapSymbolRequested, this, &MainWindow::swapSymbol);
    connect(_framePicker, &FramePickerPanel::loopChosen, this, [this](int loopType) {
        auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
        if (!instance || instance->loopType == static_cast<fla::LoopType>(loopType))
            return;
        _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<fla::LoopType>(
            instance->loopType, instance->loopType, static_cast<fla::LoopType>(loopType), "Looping")));
        if (_phoenixView)
            _phoenixView->update();
    });
    connect(_framePickerDock, &QDockWidget::visibilityChanged, this, [this](bool) { updateFramePicker(); });

    // Color and Swatches, in the same group, as Animate's workspace has them.
    _colorDock = new QDockWidget("Color", this);
    _colorDock->setObjectName("colorDock");
    _colorDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _colorPanel = new ColorPanel(&_drawingStyle, _colorDock);
    _colorDock->setWidget(_colorPanel);
    addDockWidget(Qt::RightDockWidgetArea, _colorDock);
    tabifyDockWidget(libraryDock, _colorDock);
    _colorPanel->setDocument(_flaDocument);
    connect(_colorPanel, &ColorPanel::styleChanged, this, [this]() { refreshStyleSwatches(); });
    connect(_colorPanel, &ColorPanel::applyToSelectionRequested, this, &MainWindow::applyFillToSelection);
    connect(_colorPanel, &ColorPanel::importBitmapRequested, this, &MainWindow::importToLibrary);
    // Properties' Edit for a gradient or bitmap fill: the Color panel takes it.
    connect(_propertiesPanel, &PropertiesPanel::editFillRequested, this, [this](const fla::FillStyle* fill) {
        if (!fill)
            return;
        fla::DrawingStyle& style = _drawingStyle;
        style.hasFill = true;
        style.gradientSpread = fill->spreadMethod;
        style.gradientLinearRGB = fill->interpolationMethod == "linearRGB";
        const auto stopsOf = [](const auto& entries) {
            std::vector<fla::DrawingStyle::Stop> stops;
            for (const auto& entry : entries)
            {
                fla::DrawingStyle::Stop stop;
                stop.ratio = entry.ratio;
                std::copy(entry.color, entry.color + 4, stop.color);
                stops.push_back(stop);
            }
            return stops;
        };
        switch (fill->type())
        {
        case fla::FillStyle::Type::LinearGradient:
            style.fillKind = fla::DrawingStyle::FillKind::Linear;
            style.gradient = stopsOf(static_cast<const fla::LinearGradient*>(fill)->entries);
            break;
        case fla::FillStyle::Type::RadialGradient:
            style.fillKind = fla::DrawingStyle::FillKind::Radial;
            style.gradient = stopsOf(static_cast<const fla::RadialGradient*>(fill)->entries);
            break;
        case fla::FillStyle::Type::BitmapFill:
            style.fillKind = fla::DrawingStyle::FillKind::Bitmap;
            style.fillBitmap = static_cast<const fla::BitmapFill*>(fill)->bitmapPath;
            break;
        case fla::FillStyle::Type::SolidColor:
            style.fillKind = fla::DrawingStyle::FillKind::Solid;
            break;
        }
        _colorPanel->refresh();
        _colorDock->show();
        _colorDock->raise();
        refreshStyleSwatches();
    });

    _swatchesDock = new QDockWidget("Swatches", this);
    _swatchesDock->setObjectName("swatchesDock");
    _swatchesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _swatchesPanel = new SwatchesPanel(_swatchesDock);
    _swatchesDock->setWidget(_swatchesPanel);
    addDockWidget(Qt::RightDockWidgetArea, _swatchesDock);
    tabifyDockWidget(_colorDock, _swatchesDock);
    _swatchesPanel->setDocument(_flaDocument);
    connect(_swatchesPanel, &SwatchesPanel::colorPicked, this, [this](const QColor& color, bool stroke) {
        uint8_t* target = stroke ? _drawingStyle.strokeColor : _drawingStyle.fillColor;
        target[0] = static_cast<uint8_t>(color.red());
        target[1] = static_cast<uint8_t>(color.green());
        target[2] = static_cast<uint8_t>(color.blue());
        target[3] = static_cast<uint8_t>(color.alpha());
        if (!stroke)
        {
            // A swatch is a solid color: new art takes it.
            _drawingStyle.hasFill = true;
            _drawingStyle.fillKind = fla::DrawingStyle::FillKind::Solid;
            if (_textTool)
                _textTool->applyColor(_drawingStyle.fillColor);
        }
        else
        {
            _drawingStyle.hasStroke = true;
        }
        refreshStyleSwatches();
        if (_colorPanel)
            _colorPanel->refresh();
    });
    connect(_swatchesPanel, &SwatchesPanel::addRequested, this, [this]() {
        if (!_flaDocument || !_flaDocument->document)
            return;
        fla::Document* document = _flaDocument->document;
        const fla::swatches::State before = fla::swatches::stateOf(*document);
        const fla::swatches::Color color = {_drawingStyle.fillColor[0], _drawingStyle.fillColor[1],
            _drawingStyle.fillColor[2], _drawingStyle.fillColor[3]};
        _editContext.commandStack().push(fla::CommandPtr(new fla::SetSwatchesCommand(
            document, before, fla::swatches::withAdded(before, color), "Add Swatch")));
    });
    connect(_swatchesPanel, &SwatchesPanel::removeRequested, this, [this](int index) {
        if (!_flaDocument || !_flaDocument->document || index < 0)
            return;
        fla::Document* document = _flaDocument->document;
        const fla::swatches::State before = fla::swatches::stateOf(*document);
        _editContext.commandStack().push(fla::CommandPtr(new fla::SetSwatchesCommand(
            document, before, fla::swatches::withRemoved(before, static_cast<size_t>(index)), "Delete Swatch")));
    });
    dock->raise();
    _libraryPanel->setDocument(_flaDocument);
    _libraryPanel->setPreviewProvider([this](const QString& kind, const QString& name, const QSize& size) {
        return _phoenixView ? _phoenixView->libraryPreview(kind, name, size) : QImage();
    });
    connect(_libraryPanel, &LibraryPanel::editSymbolRequested, this, [this](const QString& name) {
        if (!_phoenixView || !_flaDocument || !_flaDocument->document || !_flaDocument->document->symbolList)
            return;
        const auto& symbols = _flaDocument->document->symbolList->symbolMap;
        const auto found = symbols.find(name.toStdString());
        if (found != symbols.end())
            _phoenixView->editSymbol(found->second);
    });
    connect(_phoenixView, &PhoenixView::libraryItemDropped, this, &MainWindow::placeLibraryItem);
    connect(_phoenixView, &PhoenixView::stageMenuRequested, this, &MainWindow::showStageMenu);

    // The pen's eraser end picks the eraser, and turning the pen back picks
    // whatever tool was in use before, unless another was chosen meanwhile.
    connect(_phoenixView, &PhoenixView::penEndChanged, this, [this](bool eraser) {
        Tool* current = _phoenixView->activeTool();
        if (eraser)
        {
            if (current == _eraserTool.get())
                return;
            _toolBeforeEraserPen = current;
            chooseTool(_eraserTool.get());
        }
        else
        {
            if (_toolBeforeEraserPen && current == _eraserTool.get())
                chooseTool(_toolBeforeEraserPen);
            _toolBeforeEraserPen = nullptr;
        }
    });

    // The library's item menu, each an undo step.
    const auto symbolNamed = [this](const QString& name) -> fla::Symbol* {
        if (!_flaDocument || !_flaDocument->document || !_flaDocument->document->symbolList)
            return nullptr;
        const auto& symbols = _flaDocument->document->symbolList->symbolMap;
        const auto found = symbols.find(name.toStdString());
        return found == symbols.end() ? nullptr : found->second;
    };
    const auto afterLibraryEdit = [this]() {
        _editContext.commandStack().breakMergeChain();
        if (_libraryPanel)
            _libraryPanel->refreshIfStale();
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    };
    // Renaming and moving are both changes of path: a folder takes what is
    // in it along, and everything that names an item follows it.
    const auto changePath = [this, afterLibraryEdit](const QString& from, const QString& to, const char* what) {
        if (!_flaDocument || !_flaDocument->document)
            return;
        std::string problem;
        fla::library::Renames renames =
            fla::library::pathRenames(*_flaDocument->document, from.toStdString(), to.toStdString(), &problem);
        if (renames.empty())
        {
            if (!problem.empty())
                statusBar()->showMessage(QString::fromStdString(problem), 4000);
            _libraryPanel->refresh();
            return;
        }
        _editContext.commandStack().push(fla::CommandPtr(
            new fla::RenameLibraryCommand(_flaDocument->document, std::move(renames), what)));
        afterLibraryEdit();
    };
    connect(_libraryPanel, &LibraryPanel::renameRequested, this,
        [changePath](const QString& name, const QString& newName) { changePath(name, newName, "Rename"); });
    connect(_libraryPanel, &LibraryPanel::moveRequested, this,
        [changePath](const QString& name, const QString& folder) {
            const QString leaf = name.mid(name.lastIndexOf('/') + 1);
            const QString to = folder.isEmpty() ? leaf : folder + "/" + leaf;
            if (to != name)
                changePath(name, to, "Move to Folder");
        });
    connect(_libraryPanel, &LibraryPanel::duplicateSymbolRequested, this,
        [this, symbolNamed, afterLibraryEdit](const QString& name) {
            fla::Symbol* symbol = symbolNamed(name);
            if (!symbol)
                return;
            const std::string copyName = fla::library::freeCopyName(*_flaDocument->document, symbol->name);
            _editContext.commandStack().push(fla::CommandPtr(new fla::AddSymbolCommand(
                _flaDocument->document, fla::library::cloneSymbol(*symbol, copyName), "Duplicate Symbol")));
            afterLibraryEdit();
        });
    connect(_libraryPanel, &LibraryPanel::deleteSymbolRequested, this,
        [this, symbolNamed, afterLibraryEdit](const QString& name) {
            fla::Symbol* symbol = symbolNamed(name);
            if (!symbol)
                return;
            const size_t uses = fla::library::instancesOf(*_flaDocument->document, symbol).size();
            if (uses > 0)
            {
                QMessageBox::information(this, "Delete Symbol",
                    QString("%1 is used %2 time%3 in the document, so it cannot be deleted. Delete its instances first.")
                        .arg(name).arg(uses).arg(uses == 1 ? "" : "s"));
                return;
            }
            // Not while it is open for editing.
            if (_phoenixView)
                _phoenixView->exitToDepth(0);
            _editContext.commandStack().push(fla::CommandPtr(
                new fla::RemoveSymbolCommand(_flaDocument->document, symbol)));
            afterLibraryEdit();
        });
    connect(_libraryPanel, &LibraryPanel::newFolderRequested, this,
        [this, afterLibraryEdit](const QString& parentPath) {
            if (!_flaDocument || !_flaDocument->document)
                return;
            const QString prefix = parentPath.isEmpty() ? QString() : parentPath + "/";
            QString name;
            for (int n = 1;; ++n)
            {
                name = prefix + QString("Untitled Folder %1").arg(n);
                if (fla::library::nameIsFree(*_flaDocument->document, name.toStdString()))
                    break;
            }
            _editContext.commandStack().push(fla::CommandPtr(
                new fla::AddFolderCommand(_flaDocument->document, name.toStdString())));
            afterLibraryEdit();
        });

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
            _phoenixView->invalidateBounds();
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

void MainWindow::placeLibraryItem(const QString& kind, const QString& name, const QPointF& position)
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;
    if (kind == "sound")
    {
        attachSound(name);
        return;
    }
    fla::Frame* frame = _phoenixView->activeFrame();
    if (!frame)
    {
        statusBar()->showMessage("The current layer is locked or hidden.", 3000);
        return;
    }

    fla::Element* element = nullptr;
    const std::string itemName = name.toStdString();
    if (kind == "symbol")
    {
        fla::SymbolList* list = _flaDocument->document->symbolList;
        const auto found = list ? list->symbolMap.find(itemName) : decltype(list->symbolMap)::iterator();
        if (!list || found == list->symbolMap.end())
            return;
        fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = itemName;
        instance->symbol = found->second;
        instance->symbolType = found->second->symbolType;
        instance->loopType = fla::LoopType::Loop;
        element = instance;
    }
    else if (kind == "bitmap")
    {
        fla::BitmapInstance* bitmap = new fla::BitmapInstance(frame);
        bitmap->libraryItemName = itemName;
        element = bitmap;
    }
    if (!element)
        return;

    // Centered on the drop, as Animate places a dragged item.
    const QRectF bounds = _phoenixView->elementBounds(element);
    const QPointF center = bounds.isValid() ? bounds.center() : QPointF();
    element->transform.tx = position.x() - center.x();
    element->transform.ty = position.y() - center.y();
    _phoenixView->invalidateBounds();

    placeDrawnElement(*_phoenixView, _editContext.commandStack(), _selection, frame, element,
        "Add Instance", true);
    _phoenixView->clearCaches();
    _phoenixView->update();
}

namespace {

const char* const kImportFilter =
    "All importable files (*.png *.jpg *.jpeg *.gif *.bmp *.wav *.aif *.aiff *.mp3 *.flac *.ogg);;"
    "Images (*.png *.jpg *.jpeg *.gif *.bmp);;"
    "Sounds (*.wav *.aif *.aiff *.mp3 *.flac *.ogg);;"
    "All files (*)";

} // namespace

void MainWindow::importToStage()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    QSettings settings;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Import to Stage",
        settings.value("importFolder").toString(), kImportFilter);
    if (paths.isEmpty())
        return;
    settings.setValue("importFolder", QFileInfo(paths.front()).absolutePath());

    // One numbered image of a sequence offers the rest, as Animate asks.
    if (paths.size() == 1 && MediaImport::isImage(paths.front()))
    {
        const QStringList sequence = MediaImport::sequenceOf(paths.front());
        if (!sequence.isEmpty())
        {
            const QMessageBox::StandardButton answer = QMessageBox::question(this, "Import to Stage",
                QString("%1 appears to be one of a sequence of %2 images. Import them all?")
                    .arg(QFileInfo(paths.front()).fileName()).arg(sequence.size()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::Yes)
            {
                importSequence(sequence);
                return;
            }
        }
    }
    importMedia(paths, true);
}

void MainWindow::importToLibrary()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    QSettings settings;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Import to Library",
        settings.value("importFolder").toString(), kImportFilter);
    if (paths.isEmpty())
        return;
    settings.setValue("importFolder", QFileInfo(paths.front()).absolutePath());
    importMedia(paths, false);
}

QString MainWindow::findFfmpeg(const QString& title, const QString& why)
{
    const QString found = MovieExport::ffmpegPath();
    if (!found.isEmpty())
        return found;
    QMessageBox::information(this, title,
        why + ", which it could not find on the PATH. "
        "Choose ffmpeg's program file to use it; Phoenix will remember it.\n\n"
        "ffmpeg is free from ffmpeg.org.");
    const QString chosen = QFileDialog::getOpenFileName(this, "Where is ffmpeg?", QString(),
#ifdef Q_OS_WIN
        "ffmpeg (ffmpeg.exe);;Programs (*.exe)");
#else
        "ffmpeg (ffmpeg);;All Files (*)");
#endif
    if (!chosen.isEmpty())
        MovieExport::setFfmpegPath(chosen);
    return chosen;
}

void MainWindow::importVideo()
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;
    fla::Timeline* timeline = editedTimeline();
    if (!timeline)
        return;
    QSettings settings;
    const QString video = QFileDialog::getOpenFileName(this, "Import Video",
        settings.value("importFolder").toString(), VideoImport::fileFilter());
    if (video.isEmpty())
        return;
    settings.setValue("importFolder", QFileInfo(video).absolutePath());
    const QString ffmpeg = findFfmpeg("Import Video", "Phoenix reads video with ffmpeg");
    if (ffmpeg.isEmpty())
        return;

    // The choices Animate's wizard offers that matter for a reference.
    QDialog options(this);
    options.setWindowTitle("Import Video");
    QLabel* about = new QLabel(QString("%1 goes on a guide layer of its own, a still for each frame at %2 fps, "
        "to animate over. Guide layers are left out of what is published and exported.")
        .arg(QFileInfo(video).fileName()).arg(_flaDocument->document->frameRate, 0, 'f', 2), &options);
    about->setWordWrap(true);
    QCheckBox* fit = new QCheckBox("Shrink to the stage width", &options);
    fit->setChecked(true);
    QCheckBox* sound = new QCheckBox("Import its sound, as a stream", &options);
    sound->setChecked(true);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
    connect(buttons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
    QVBoxLayout* optionsLayout = new QVBoxLayout(&options);
    optionsLayout->addWidget(about);
    optionsLayout->addWidget(fit);
    optionsLayout->addWidget(sound);
    optionsLayout->addWidget(buttons);
    options.setMinimumWidth(420);
    if (options.exec() != QDialog::Accepted)
        return;

    fla::Document* document = _flaDocument->document;
    QTemporaryDir folder;
    if (!folder.isValid())
    {
        QMessageBox::warning(this, "Import Video", "There is no room for the video's frames in the temporary folder.");
        return;
    }
    VideoImport::Result extracted;
    QString error;
    const double rate = document->frameRate > 0.0 ? document->frameRate : 24.0;
    if (!VideoImport::extract(ffmpeg, video, folder.path(), rate, fit->isChecked() ? document->width : 0,
            sound->isChecked(), this, extracted, error))
    {
        if (!error.isEmpty())
            QMessageBox::warning(this, "Import Video", error);
        return;
    }

    // The stills go in a library folder of their own.
    const QString baseName = QFileInfo(video).completeBaseName();
    const std::string folderName = fla::media::freeName(*document, baseName.toStdString() + " frames");
    QStringList problems;
    std::vector<fla::Bitmap*> bitmaps;
    for (const QString& still : extracted.frames)
    {
        QString stillError;
        fla::Bitmap* bitmap = MediaImport::loadImage(*document, still, stillError);
        if (!bitmap)
        {
            problems << stillError;
            continue;
        }
        bitmap->name = folderName + "/" + bitmap->name;
        bitmap->href = bitmap->name;
        bitmap->sourceExternalFilepath = video.toStdString();
        bitmaps.push_back(bitmap);
    }
    if (bitmaps.empty())
    {
        QMessageBox::warning(this, "Import Video", problems.join("\n"));
        return;
    }

    // A locked guide layer at the bottom, a keyframe per still.
    fla::Layer* layer = fla::newLayerFor(*timeline, false);
    for (fla::Frame* frame : layer->frames)
        delete frame;
    layer->frames.clear();
    layer->name = baseName.toStdString();
    layer->autoNamed = false;
    layer->layerType = fla::Layer::Type::Guide;
    layer->locked = true;
    for (size_t i = 0; i < bitmaps.size(); ++i)
    {
        fla::Frame* frame = new fla::Frame(layer);
        frame->index = static_cast<int>(i);
        frame->duration = 1;
        frame->keyMode = "9728";
        fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
        instance->libraryItemName = bitmaps[i]->name;
        frame->elements.push_back(instance);
        layer->frames.push_back(frame);
    }
    layer->firstFrame = 0;
    layer->lastFrame = static_cast<int>(bitmaps.size());

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Import Video");
    stack.push(fla::CommandPtr(new fla::AddFolderCommand(document, folderName)));
    for (fla::Bitmap* bitmap : bitmaps)
        stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, "Import Video")));
    if (!extracted.audio.isEmpty())
    {
        fla::media::Sound soundData;
        QString soundError;
        if (MediaImport::loadSound(extracted.audio, soundData, soundError))
        {
            soundData.name = baseName.toStdString() + ".wav";
            soundData.sourcePath = video.toStdString();
            auto* addSound = new fla::AddSoundCommand(document, soundData, "Import Video");
            const std::string soundName = addSound->soundName();
            stack.push(fla::CommandPtr(addSound));
            fla::Frame* first = layer->frames.front();
            first->extraAttributes.emplace_back("soundName", soundName);
            first->extraAttributes.emplace_back("soundSync", "stream");
        }
        else
        {
            problems << soundError;
        }
    }
    stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, static_cast<int>(timeline->layers.size()), layer,
        "Import Video")));
    stack.endMacro();
    stack.breakMergeChain();

    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_timelineView)
    {
        _timelineView->refresh();
        _timelineView->refreshFrames();
    }
    onVisibilityChanged();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    statusBar()->showMessage(QString("Imported %1 frames of %2.").arg(bitmaps.size()).arg(QFileInfo(video).fileName()), 5000);
    if (!problems.isEmpty())
        QMessageBox::warning(this, "Import Video", problems.join("\n"));
}

void MainWindow::importMedia(const QStringList& paths, bool toStage, const QPointF* at)
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;
    fla::Document* document = _flaDocument->document;
    _phoenixView->flushPendingMerge();
    fla::Frame* frame = toStage ? _phoenixView->activeFrame() : nullptr;
    if (toStage && !frame && std::any_of(paths.begin(), paths.end(), MediaImport::isImage))
        statusBar()->showMessage("The current layer is locked or hidden; the images went into the library.", 4000);

    fla::CommandStack& stack = _editContext.commandStack();
    const char* name = toStage ? "Import to Stage" : "Import to Library";
    QStringList problems;
    std::vector<fla::DOMElement*> placed;
    QString soundOnStage;
    stack.beginMacro(name);
    for (const QString& path : paths)
    {
        QString error;
        if (MediaImport::isImage(path))
        {
            fla::Bitmap* bitmap = MediaImport::loadImage(*document, path, error);
            if (!bitmap)
            {
                problems << error;
                continue;
            }
            const std::string itemName = bitmap->name;
            const double width = bitmap->frameRight / 20.0;
            const double height = bitmap->frameBottom / 20.0;
            stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, name)));
            if (!frame)
                continue;
            // At the top-left of the stage, or centered where it was dropped.
            fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
            instance->libraryItemName = itemName;
            instance->transform.tx = at ? at->x() - width / 2.0 : 0.0;
            instance->transform.ty = at ? at->y() - height / 2.0 : 0.0;
            stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, name, &_selection)));
            placed.push_back(instance);
        }
        else if (MediaImport::isSound(path))
        {
            fla::media::Sound sound;
            if (!MediaImport::loadSound(path, sound, error))
            {
                problems << error;
                continue;
            }
            auto* command = new fla::AddSoundCommand(document, sound, name);
            const QString soundName = QString::fromStdString(command->soundName());
            stack.push(fla::CommandPtr(command));
            if (at)
                soundOnStage = soundName;
        }
        else
        {
            problems << QString("%1 is not a file Phoenix imports.").arg(QFileInfo(path).fileName());
        }
    }
    stack.endMacro();
    stack.breakMergeChain();

    if (!placed.empty())
        _selection.select(placed);
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (!soundOnStage.isEmpty())
        attachSound(soundOnStage);
    if (!problems.isEmpty())
        QMessageBox::warning(this, name, problems.join("\n"));
}

void MainWindow::importSequence(const QStringList& paths)
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document || !_timelineView)
        return;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView->currentLayer(timeline, index) || !timeline ||
        index < 0 || index >= static_cast<int>(timeline->layers.size()))
        return;
    fla::Layer* layer = timeline->layers[index];
    if (layer->isLocked())
    {
        statusBar()->showMessage("The current layer is locked.", 3000);
        return;
    }
    fla::Document* document = _flaDocument->document;
    _phoenixView->flushPendingMerge();
    _selection.clear();

    // Read them all first, so a file that cannot be read leaves no gap.
    QStringList problems;
    std::vector<fla::Bitmap*> bitmaps;
    for (const QString& path : paths)
    {
        QString error;
        fla::Bitmap* bitmap = MediaImport::loadImage(*document, path, error);
        if (bitmap)
            bitmaps.push_back(bitmap);
        else
            problems << error;
    }
    if (bitmaps.empty())
    {
        QMessageBox::warning(this, "Import to Stage", problems.join("\n"));
        return;
    }

    const char* name = "Import to Stage";
    fla::CommandStack& stack = _editContext.commandStack();
    const int start = _player ? _player->currentFrame() : 0;
    const int count = static_cast<int>(bitmaps.size());
    auto frames = std::make_shared<std::vector<fla::Frame*>>();
    stack.beginMacro(name);
    stack.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, name,
        [layer, start, count, frames](std::vector<fla::Frame*>&) {
            for (int i = 0; i < count; ++i)
                frames->push_back(fla::timeline::insertKeyframe(*layer, start + i, true));
        })));
    for (int i = 0; i < count; ++i)
    {
        fla::Bitmap* bitmap = bitmaps[static_cast<size_t>(i)];
        const std::string itemName = bitmap->name;
        stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, name)));
        fla::Frame* frame = i < static_cast<int>(frames->size()) ? (*frames)[static_cast<size_t>(i)] : nullptr;
        if (!frame)
            continue;
        fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
        instance->libraryItemName = itemName;
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, name, &_selection)));
    }
    stack.endMacro();
    stack.breakMergeChain();

    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_timelineView)
        _timelineView->refreshFrames();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (!problems.isEmpty())
        QMessageBox::warning(this, name, problems.join("\n"));
}

void MainWindow::attachSound(const QString& name)
{
    if (!_flaDocument || !_flaDocument->document || !_timelineView)
        return;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView->currentLayer(timeline, index) || !timeline ||
        index < 0 || index >= static_cast<int>(timeline->layers.size()))
        return;
    fla::Layer* layer = timeline->layers[index];
    if (layer->isLocked())
    {
        statusBar()->showMessage("The current layer is locked.", 3000);
        return;
    }
    const int at = _player ? _player->currentFrame() : 0;
    const std::string soundName = name.toStdString();
    _selection.clear();
    auto* command = new fla::EditFramesCommand({layer}, "Add Sound",
        [layer, at, soundName](std::vector<fla::Frame*>&) {
            // The keyframe the playhead is in, or a new one past the end.
            fla::Frame* frame = fla::timeline::spanAt(*layer, at);
            if (!frame)
                frame = fla::timeline::insertKeyframe(*layer, at, true);
            if (!frame)
                return;
            // A sound replaces whatever the keyframe played, with Animate's
            // defaults: an event sound, played once.
            static const char* const settings[] = {"soundName", "soundSync", "inPoint44", "outPoint44",
                "soundLoop", "soundLoopMode", "soundZoomLevel", "soundEffect"};
            auto& attributes = frame->extraAttributes;
            attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                [](const std::pair<std::string, std::string>& attribute) {
                    return std::find_if(std::begin(settings), std::end(settings),
                        [&attribute](const char* setting) { return attribute.first == setting; }) != std::end(settings);
                }), attributes.end());
            attributes.emplace_back("soundName", soundName);
        });
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (_timelineView)
        _timelineView->refreshFrames();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::chooseTool(Tool* tool)
{
    const auto found = _toolActions.find(tool);
    if (found != _toolActions.end() && found->second)
        found->second->trigger();
    else if (_phoenixView && tool)
        _phoenixView->setActiveTool(tool);
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
    if (tool == _brushTool.get()) return IconTool::FluidBrush;
    if (tool == _classicBrushTool.get()) return IconTool::Brush;
    if (tool == _inkBrushTool.get()) return IconTool::InkBrush;
    if (tool == _eraserTool.get()) return IconTool::Eraser;
    if (tool == _lassoTool.get()) return IconTool::Lasso;
    if (tool == _polygonTool.get()) return IconTool::PolygonLasso;
    if (tool == _gradientTransformTool.get()) return IconTool::GradientTransform;
    if (tool == _addAnchorTool.get()) return IconTool::AddAnchor;
    if (tool == _deleteAnchorTool.get()) return IconTool::DeleteAnchor;
    if (tool == _convertAnchorTool.get()) return IconTool::ConvertAnchor;
    if (tool == _magicWandTool.get()) return IconTool::MagicWand;
    if (tool == _rotationTool.get()) return IconTool::Rotation;
    if (tool == _timeScrubTool.get()) return IconTool::TimeScrub;
    if (tool == _textTool.get()) return IconTool::Text;
    return IconTool::Selection;
}

void MainWindow::setOutlinerVisible(bool visible)
{
    if (!_viewSplitter)
        return;
    QList<int> sizes = _viewSplitter->sizes();
    if (sizes.size() < 2)
        return;
    const int total = sizes[0] + sizes[1];
    if (!visible && sizes[0] > 0)
        _outlinerWidth = sizes[0];
    const int width = visible ? std::min(std::max(_outlinerWidth, 150), std::max(total - 200, 150)) : 0;
    _viewSplitter->setSizes({width, total - width});
    _outlinerVisible = visible;
    syncOutlinerControls();
}

void MainWindow::syncOutlinerControls()
{
    if (_outlinerButton)
    {
        _outlinerButton->setArrowType(_outlinerVisible ? Qt::LeftArrow : Qt::RightArrow);
        _outlinerButton->setToolTip(_outlinerVisible ? "Hide the outliner" : "Show the outliner");
    }
    if (_outlinerAction && _outlinerAction->isChecked() != _outlinerVisible)
    {
        const QSignalBlocker blocker(_outlinerAction);
        _outlinerAction->setChecked(_outlinerVisible);
    }
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

    // Back out one level, as Animate's edit bar arrow does.
    QToolButton* back = new QToolButton(_breadcrumb);
    back->setText(QString::fromUtf8("\u2190"));
    back->setAutoRaise(true);
    back->setToolTip("Back");
    back->setEnabled(depth > 0);
    connect(back, &QToolButton::clicked, this, [this]() { _phoenixView->exitScope(); });
    layout->addWidget(back);

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

    // Edit Symbols: any symbol of the library, by name.
    QToolButton* symbols = new QToolButton(_breadcrumb);
    symbols->setText("Edit Symbols");
    symbols->setAutoRaise(true);
    symbols->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(symbols);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        std::vector<const fla::Symbol*> list;
        if (document && document->symbolList)
            for (const fla::Symbol* symbol : document->symbolList->symbols)
                if (symbol && !symbol->timelines.empty())
                    list.push_back(symbol);
        std::sort(list.begin(), list.end(),
            [](const fla::Symbol* a, const fla::Symbol* b) { return a->name < b->name; });
        if (list.empty())
            menu->addAction("No symbols")->setEnabled(false);
        for (const fla::Symbol* symbol : list)
            menu->addAction(QString::fromStdString(symbol->name), this,
                [this, symbol]() { _phoenixView->editSymbol(symbol); });
    });
    symbols->setMenu(menu);
    layout->addWidget(symbols);
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
    std::vector<fla::StaticText*> texts;
    std::vector<fla::BitmapInstance*> bitmaps;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element || !frameOfElement(element))
            continue;
        if (element->elementType() == fla::Element::Type::StaticText)
            texts.push_back(static_cast<fla::StaticText*>(element));
        else if (element->elementType() == fla::Element::Type::BitmapInstance)
            bitmaps.push_back(static_cast<fla::BitmapInstance*>(element));
        else if (element->elementType() == fla::Element::Type::Group)
            groups.push_back(static_cast<fla::Group*>(element));
        else if (element->elementType() == fla::Element::Type::Shape &&
            static_cast<fla::Shape*>(element)->isDrawingObject)
            drawingObjects.push_back(static_cast<fla::Shape*>(element));
        else if (element->elementType() == fla::Element::Type::SymbolInstance)
            instances.push_back(static_cast<fla::SymbolInstance*>(element));
    }
    if (groups.empty() && drawingObjects.empty() && instances.empty() && texts.empty() && bitmaps.empty())
        return;

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Break Apart");
    std::vector<fla::DOMElement*> results;
    for (fla::StaticText* text : texts)
    {
        fla::Frame* frame = frameOfElement(text);
        const int index = static_cast<int>(
            std::find(frame->elements.begin(), frame->elements.end(), text) - frame->elements.begin());
        const QString joined = TextLayout::joinedText(*text);
        int letters = 0;
        for (const QChar c : joined)
            letters += c.isSpace() ? 0 : 1;
        if (letters > 1)
        {
            // Into letters: a text field for each, standing where it stood.
            int at = index + 1;
            int offset = 0;
            for (size_t r = 0; r < text->runs.size(); ++r)
            {
                const QString runText = QString::fromStdString(text->runs[r].text);
                for (int k = 0; k < runText.size(); ++k, ++offset)
                {
                    if (runText[k].isSpace())
                        continue;
                    auto* letter = static_cast<fla::StaticText*>(fla::cloneElement(*text, frame));
                    if (!letter)
                        continue;
                    fla::TextRun run = text->runs[r];
                    run.text = QString(runText[k]).toStdString();
                    run.alignment = fla::TextRun::Alignment::Left;
                    letter->runs = {run};
                    letter->autoExpand = true;
                    letter->width = 0.0;
                    const QRectF was = TextLayout::caretRect(*text, offset);
                    const QRectF now = TextLayout::caretRect(*letter, 0);
                    letter->left += was.left() - now.left();
                    letter->top += was.top() - now.top();
                    stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, letter, "Break Apart", &_selection, at++)));
                    results.push_back(letter);
                }
            }
            stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, text, "Break Apart", &_selection)));
            continue;
        }
        // A letter into shapes: its glyph's outline, filled with its color.
        const TextLayout::Layout layout = TextLayout::layout(*text);
        QPainterPath glyphs;
        int offset = 0;
        for (const fla::TextRun& run : text->runs)
        {
            const QString runText = QString::fromStdString(run.text);
            for (int k = 0; k < runText.size(); ++k, ++offset)
            {
                if (runText[k].isSpace())
                    continue;
                const QRectF caret = TextLayout::caretRect(*text, offset);
                double ascent = caret.height() * 0.8;
                for (const TextLayout::Line& line : layout.lines)
                    if (offset >= line.start && offset <= line.start + line.length)
                        ascent = line.ascent;
                glyphs.addText(QPointF(caret.left(), caret.top() + ascent), TextLayout::fontFor(run), QString(runText[k]));
            }
        }
        const fla::Transform& t = text->transform;
        glyphs = QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty).map(glyphs);
        auto* fill = new fla::SolidColor(nullptr);
        if (!text->runs.empty())
            std::copy(text->runs.front().fillColor, text->runs.front().fillColor + 4, fill->color);
        fla::Shape* shape = fla::outline::shapeFromOutline(glyphs, fill);
        if (!shape)
        {
            delete fill;
            continue;
        }
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Break Apart", &_selection, index + 1)));
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, text, "Break Apart", &_selection)));
        _phoenixView->setPendingMerge(shape, frame);
        results.push_back(shape);
    }
    for (fla::BitmapInstance* bitmap : bitmaps)
    {
        // Into a shape filled with the bitmap, clipped, where the bitmap was.
        fla::Frame* frame = frameOfElement(bitmap);
        const QSize size = _phoenixView->libraryBitmap(QString::fromStdString(bitmap->libraryItemName)).size();
        if (size.isEmpty())
            continue;
        const fla::Transform& t = bitmap->transform;
        const QTransform place(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
        const QPolygonF corners = place.map(QPolygonF(QRectF(0.0, 0.0, size.width(), size.height())));
        auto* fill = new fla::BitmapFill(nullptr);
        fill->bitmapPath = bitmap->libraryItemName;
        fill->bitmapIsClipped = true;
        fill->transform = fla::Transform(t.m11 * 20.0, t.m12 * 20.0, t.m21 * 20.0, t.m22 * 20.0, t.tx, t.ty);
        QPolygonF ring = corners;
        if (ring.size() == 5)
            ring.removeLast();
        fla::Shape* shape = fla::outline::shapeFromLoops({ring}, fill);
        if (!shape)
        {
            delete fill;
            continue;
        }
        const int index = static_cast<int>(
            std::find(frame->elements.begin(), frame->elements.end(), bitmap) - frame->elements.begin());
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Break Apart", &_selection, index + 1)));
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, bitmap, "Break Apart", &_selection)));
        _phoenixView->setPendingMerge(shape, frame);
        results.push_back(shape);
    }
    for (fla::Group* group : groups)
    {
        stack.push(fla::CommandPtr(new fla::UngroupCommand(
            frameOfElement(group), group, "Break Apart", &_selection)));
    }
    for (fla::SymbolInstance* instance : instances)
    {
        fla::Frame* frame = frameOfElement(instance);
        // What the stage shows of it is what is taken apart.
        const int shown = frame && _phoenixView ? _phoenixView->symbolFrameShown(*instance, *frame) : -1;
        fla::BreakApartInstanceCommand* command =
            new fla::BreakApartInstanceCommand(frame, instance, "Break Apart", &_selection, shown);
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
    if (!results.empty())
        _selection.select(results);
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

void MainWindow::swapSymbol()
{
    if (!_flaDocument || !_flaDocument->document || !_flaDocument->document->symbolList)
        return;
    std::vector<fla::SymbolInstance*> instances;
    for (fla::DOMElement* selected : _selection.elements())
    {
        if (auto* instance = dynamic_cast<fla::SymbolInstance*>(selected))
            instances.push_back(instance);
    }
    if (instances.empty())
        return;
    fla::Document* document = _flaDocument->document;

    QStringList names;
    for (const fla::Symbol* symbol : document->symbolList->symbols)
    {
        if (symbol)
            names << QString::fromStdString(symbol->name);
    }
    names.sort(Qt::CaseInsensitive);

    // A duplicate made in the dialog stays, whether or not it is chosen, as
    // in Animate; it is its own undo step.
    SwapSymbolDialog dialog(names, QString::fromStdString(instances.front()->libraryItemName),
        [this](const QString& name, const QSize& size) {
            return _phoenixView ? _phoenixView->libraryPreview("symbol", name, size) : QImage();
        },
        [this, document](const QString& name) {
            const auto found = document->symbolList->symbolMap.find(name.toStdString());
            if (found == document->symbolList->symbolMap.end() || !found->second)
                return QString();
            bool ok = false;
            const std::string suggested = fla::library::freeCopyName(*document, found->second->name);
            const QString copyName = QInputDialog::getText(this, "Duplicate Symbol", "Symbol name:",
                QLineEdit::Normal, QString::fromStdString(suggested), &ok).trimmed();
            if (!ok || copyName.isEmpty())
                return QString();
            if (!fla::library::nameIsFree(*document, copyName.toStdString()))
            {
                QMessageBox::information(this, "Duplicate Symbol",
                    QString("The library already has an item named %1.").arg(copyName));
                return QString();
            }
            _editContext.commandStack().push(fla::CommandPtr(new fla::AddSymbolCommand(
                document, fla::library::cloneSymbol(*found->second, copyName.toStdString()), "Duplicate Symbol")));
            if (_libraryPanel)
                _libraryPanel->refreshIfStale();
            return copyName;
        },
        this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::string chosen = dialog.chosen().toStdString();
    const auto found = document->symbolList->symbolMap.find(chosen);
    if (found == document->symbolList->symbolMap.end() || !found->second)
        return;
    fla::Symbol* symbol = found->second;

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Swap Symbol");
    for (fla::SymbolInstance* instance : instances)
    {
        if (instance->symbol == symbol)
            continue;
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
            instance->libraryItemName, instance->libraryItemName, chosen, "Swap Symbol")));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Symbol*>(
            instance->symbol, instance->symbol, symbol, "Swap Symbol")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_propertiesPanel)
        _propertiesPanel->refresh();
    updateFramePicker();
}

namespace {

QTransform toQ(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

fla::Transform fromQ(const QTransform& q)
{
    return fla::Transform(q.m11(), q.m12(), q.m21(), q.m22(), q.dx(), q.dy());
}

} // namespace

void MainWindow::transformSelection(const char* name, const QTransform& change)
{
    if (!_phoenixView)
        return;
    _phoenixView->flushPendingMerge();
    const std::vector<fla::Element*> elements = selectedElements();
    QRectF bounds;
    for (fla::Element* element : elements)
    {
        const QRectF each = _phoenixView->elementBounds(element);
        if (each.isValid())
            bounds = bounds.isValid() ? bounds.united(each) : each;
    }
    if (elements.empty() || !bounds.isValid())
        return;

    // About the middle of the selection, as Animate turns and flips it.
    const QPointF c = bounds.center();
    const QTransform about = QTransform::fromTranslate(-c.x(), -c.y()) * change * QTransform::fromTranslate(c.x(), c.y());
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(name);
    for (fla::Element* element : elements)
    {
        const fla::Transform before = element->transform;
        stack.push(_phoenixView->transformCommand(element, before, fromQ(toQ(before) * about), name));
        if (element->elementType() == fla::Element::Type::Shape)
            _phoenixView->markShapeEdited(static_cast<fla::Shape*>(element));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::scaleAndRotate()
{
    if (selectedElements().empty())
        return;
    static double scale = 100.0;
    static double rotation = 0.0;
    QDialog dialog(this);
    dialog.setWindowTitle("Scale and Rotate");
    QDoubleSpinBox* scaleBox = new QDoubleSpinBox(&dialog);
    scaleBox->setRange(1.0, 10000.0);
    scaleBox->setSuffix(" %");
    scaleBox->setValue(scale);
    QDoubleSpinBox* rotateBox = new QDoubleSpinBox(&dialog);
    rotateBox->setRange(-360.0, 360.0);
    rotateBox->setSuffix(QString::fromUtf8(" °"));
    rotateBox->setValue(rotation);
    QFormLayout* form = new QFormLayout();
    form->addRow("Scale:", scaleBox);
    form->addRow("Rotate:", rotateBox);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    scale = scaleBox->value();
    rotation = rotateBox->value();
    QTransform change;
    change.rotate(rotation);
    change.scale(scale / 100.0, scale / 100.0);
    transformSelection("Scale and Rotate", change);
}

void MainWindow::removeTransform()
{
    if (!_phoenixView)
        return;
    _phoenixView->flushPendingMerge();
    const std::vector<fla::Element*> elements = selectedElements();
    if (elements.empty())
        return;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Remove Transform");
    for (fla::Element* element : elements)
    {
        // Unscaled and unturned, with the middle of the object where it was.
        const fla::Transform before = element->transform;
        const QRectF bounds = _phoenixView->elementBounds(element);
        bool invertible = false;
        const QTransform inverse = toQ(before).inverted(&invertible);
        if (!bounds.isValid() || !invertible)
            continue;
        const QPointF localCenter = inverse.map(bounds.center());
        const fla::Transform after(1.0, 0.0, 0.0, 1.0, bounds.center().x() - localCenter.x(),
            bounds.center().y() - localCenter.y());
        stack.push(_phoenixView->transformCommand(element, before, after, "Remove Transform"));
        if (element->elementType() == fla::Element::Type::Shape)
            _phoenixView->markShapeEdited(static_cast<fla::Shape*>(element));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::newSymbol()
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
        return;
    fla::Document* document = _flaDocument->document;
    std::string suggested;
    for (int n = 1;; ++n)
    {
        suggested = "Symbol " + std::to_string(n);
        if (fla::library::nameIsFree(*document, suggested))
            break;
    }
    ConvertToSymbolDialog dialog(QString::fromStdString(suggested), this);
    dialog.setWindowTitle("Create New Symbol");
    dialog.hideRegistration();
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::string name = dialog.symbolName().trimmed().toStdString();
    if (name.empty() || !fla::library::nameIsFree(*document, name))
    {
        QMessageBox::information(this, "Create New Symbol",
            QString("The library already has an item named %1.").arg(QString::fromStdString(name)));
        return;
    }
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);

    // One layer with one empty keyframe, as Animate makes it.
    auto* symbol = new fla::Symbol(document->symbolList);
    symbol->name = name;
    symbol->href = name + ".xml";
    symbol->symbolType = dialog.symbolType();
    symbol->itemId = fla::media::newItemId();
    auto* timeline = new fla::Timeline(symbol);
    timeline->name = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
    fla::Layer* layer = fla::newLayerFor(*timeline, false);
    timeline->layers.push_back(layer);
    symbol->timelines.push_back(timeline);

    _editContext.commandStack().push(fla::CommandPtr(new fla::AddSymbolCommand(document, symbol, "New Symbol")));
    _editContext.commandStack().breakMergeChain();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    _phoenixView->editSymbol(symbol);
}

void MainWindow::editSymbolsOrDocument()
{
    if (!_phoenixView)
        return;
    if (_phoenixView->scopeDepth() > 0)
    {
        _phoenixView->exitToDepth(0);
        return;
    }
    auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
    if (instance && instance->symbol)
        _phoenixView->editSymbol(instance->symbol);
}

void MainWindow::lockSelection()
{
    const std::vector<fla::Element*> elements = selectedElements();
    if (elements.empty())
        return;
    _phoenixView->flushPendingMerge();
    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Lock");
    for (fla::Element* element : elements)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(element->isLocked, element->isLocked, true, "Lock")));
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->update();
}

void MainWindow::unlockAll()
{
    // Every locked object on the frames shown of the timeline being edited.
    fla::Timeline* timeline = editedTimeline();
    if (!timeline || !_player)
        return;
    std::vector<fla::Element*> locked;
    const int at = _player->currentFrame();
    for (fla::Layer* layer : timeline->layers)
    {
        fla::Frame* frame = layer ? fla::timeline::spanAt(*layer, at) : nullptr;
        if (!frame)
            continue;
        for (fla::Element* element : frame->elements)
        {
            if (element && element->isLocked)
                locked.push_back(element);
        }
    }
    if (locked.empty())
        return;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Unlock All");
    for (fla::Element* element : locked)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(element->isLocked, true, false, "Unlock All")));
    stack.endMacro();
    stack.breakMergeChain();
    _selection.select(std::vector<fla::DOMElement*>(locked.begin(), locked.end()));
    _phoenixView->update();
}

void MainWindow::applyOnion()
{
    const bool on = _onionSkinAction && _onionSkinAction->isChecked();
    if (_phoenixView)
    {
        _phoenixView->setOnionStyle(_onion.outline, _onion.keyframesOnly, _onion.opacity, _onion.beforeColor,
            _onion.afterColor);
        _phoenixView->setOnionSkin(on, _onion.before, _onion.after);
    }
    if (_timelineView)
        _timelineView->setOnionSkin(on, _onion.before, _onion.after);
    QSettings settings;
    settings.setValue("onion/before", _onion.before);
    settings.setValue("onion/after", _onion.after);
    settings.setValue("onion/outline", _onion.outline);
    settings.setValue("onion/keyframesOnly", _onion.keyframesOnly);
    settings.setValue("onion/opacity", _onion.opacity);
    settings.setValue("onion/beforeColor", _onion.beforeColor.name());
    settings.setValue("onion/afterColor", _onion.afterColor.name());
}

void MainWindow::showOnionMenu(const QPoint& globalPos)
{
    QMenu menu(this);
    fillOnionMenu(&menu);
    menu.exec(globalPos);
}

void MainWindow::fillOnionMenu(QMenu* menu)
{
    menu->clear();
    // The range, as Animate's presets give it, or as dragged.
    for (const int span : {1, 2, 5})
    {
        QAction* action = menu->addAction(QString("Onion %1").arg(span));
        action->setCheckable(true);
        action->setChecked(_onion.before == span && _onion.after == span);
        connect(action, &QAction::triggered, this, [this, span]() {
            _onion.before = _onion.after = span;
            applyOnion();
        });
    }
    QAction* all = menu->addAction("Onion All");
    connect(all, &QAction::triggered, this, [this]() {
        const int frames = _phoenixView ? _phoenixView->frameCount() : 99;
        _onion.before = _onion.after = std::clamp(frames, 1, 99);
        applyOnion();
    });
    menu->addSeparator();
    QAction* outline = menu->addAction("Outline");
    outline->setCheckable(true);
    outline->setChecked(_onion.outline);
    outline->setStatusTip("Show the other frames as outlines");
    connect(outline, &QAction::toggled, this, [this](bool on) {
        _onion.outline = on;
        applyOnion();
    });
    QAction* keyframes = menu->addAction("Keyframes Only");
    keyframes->setCheckable(true);
    keyframes->setChecked(_onion.keyframesOnly);
    keyframes->setStatusTip("Show the keyframes either side rather than every frame");
    connect(keyframes, &QAction::toggled, this, [this](bool on) {
        _onion.keyframesOnly = on;
        applyOnion();
    });
    QMenu* opacity = menu->addMenu("Opacity");
    for (const int percent : {25, 45, 65, 85})
    {
        QAction* action = opacity->addAction(QString("%1%").arg(percent));
        action->setCheckable(true);
        action->setChecked(std::lround(_onion.opacity * 100.0) == percent);
        connect(action, &QAction::triggered, this, [this, percent]() {
            _onion.opacity = percent / 100.0;
            applyOnion();
        });
    }
    menu->addSeparator();
    menu->addAction("Color Before...", this, [this]() {
        const QColor chosen = QColorDialog::getColor(_onion.beforeColor, this, "Onion Skin: Frames Before");
        if (chosen.isValid())
        {
            _onion.beforeColor = chosen;
            applyOnion();
        }
    });
    menu->addAction("Color After...", this, [this]() {
        const QColor chosen = QColorDialog::getColor(_onion.afterColor, this, "Onion Skin: Frames After");
        if (chosen.isValid())
        {
            _onion.afterColor = chosen;
            applyOnion();
        }
    });
}

void MainWindow::combineObjects(int operation)
{
    if (!_phoenixView)
        return;
    _phoenixView->flushPendingMerge();
    // The selected shapes of one frame, bottom first.
    std::vector<fla::Shape*> shapes;
    fla::Frame* frame = nullptr;
    for (fla::Element* element : selectedElements())
    {
        if (element->elementType() != fla::Element::Type::Shape)
            continue;
        auto* owner = dynamic_cast<fla::Frame*>(element->parent);
        if (!owner || (frame && owner != frame))
            continue;
        frame = owner;
        shapes.push_back(static_cast<fla::Shape*>(element));
    }
    if (shapes.size() < 2 || !frame)
    {
        statusBar()->showMessage("Select two or more shapes on one frame to combine.", 4000);
        return;
    }
    const auto op = static_cast<fla::combine::Operation>(operation);
    const char* names[4] = {"Union", "Intersect", "Punch", "Crop"};
    const std::string name = names[std::clamp(operation, 0, 3)];
    fla::combine::Result result;
    if (!fla::combine::apply(op, shapes, result))
    {
        statusBar()->showMessage(op == fla::combine::Operation::Intersect ? "The shapes do not overlap."
                                                                          : "Nothing to combine.", 4000);
        return;
    }

    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(name);
    fla::Element* placed = nullptr;
    if (result.replacement)
    {
        // The new object goes where the lowest of them was.
        const auto at = std::find(frame->elements.begin(), frame->elements.end(), shapes.front());
        const int index = at == frame->elements.end() ? -1 : static_cast<int>(at - frame->elements.begin());
        result.replacement->parent = frame;
        placed = result.replacement;
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, result.replacement, name, &_selection, index)));
    }
    for (const auto& rewrite : result.rewritten)
    {
        stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(rewrite.first, *rewrite.second, name)));
        delete rewrite.second;
    }
    for (fla::Shape* shape : result.removed)
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, shape, name, &_selection)));
    stack.endMacro();
    stack.breakMergeChain();
    if (placed)
        _selection.select(placed);
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::distributeToKeyframes()
{
    if (!_phoenixView)
        return;
    _phoenixView->flushPendingMerge();
    // The selected objects of one frame, bottom first; the first stays and
    // each next one gets the keyframe after.
    std::vector<fla::Element*> elements = selectedElements();
    fla::Frame* frame = elements.empty() ? nullptr : dynamic_cast<fla::Frame*>(elements.front()->parent);
    fla::Layer* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
    if (!layer || elements.size() < 2 || layer->isLocked())
        return;
    for (fla::Element* element : elements)
        if (element->parent != frame)
            return;
    const int at = frame->index;
    const int count = static_cast<int>(elements.size());
    const fla::SymbolList* library = _flaDocument && _flaDocument->document ? _flaDocument->document->symbolList : nullptr;

    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Distribute to Keyframes");
    // Room for them: frames after this one, each a blank keyframe.
    stack.push(fla::CommandPtr(new fla::EditFramesCommand({layer}, "Distribute to Keyframes",
        [layer, at, count](std::vector<fla::Frame*>&) {
            fla::timeline::insertFrames(*layer, at, count - 1);
            for (int k = 1; k < count; ++k)
                fla::timeline::insertKeyframe(*layer, at + k, true);
        })));
    for (int k = 1; k < count; ++k)
    {
        fla::Frame* target = nullptr;
        for (fla::Frame* candidate : layer->frames)
            if (candidate && candidate->index == at + k)
                target = candidate;
        fla::Element* element = elements[static_cast<size_t>(k)];
        fla::Element* copy = target ? fla::cloneElement(*element, target) : nullptr;
        if (!copy)
            continue;
        fla::relinkSymbols(*copy, library);
        stack.push(fla::CommandPtr(new fla::AddElementCommand(target, copy, "Distribute to Keyframes", &_selection)));
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, element, "Distribute to Keyframes", &_selection)));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_timelineView)
        _timelineView->refreshFrames();
}

void MainWindow::distributeToLayers()
{
    if (!_phoenixView || !_timelineView)
        return;
    _phoenixView->flushPendingMerge();
    fla::Timeline* timeline = nullptr;
    int current = 0;
    if (!_timelineView->currentLayer(timeline, current) || !timeline)
        return;
    std::vector<fla::Element*> elements = selectedElements();
    // Only objects lying on a frame of the current timeline move.
    elements.erase(std::remove_if(elements.begin(), elements.end(), [timeline](fla::Element* element) {
        auto* frame = dynamic_cast<fla::Frame*>(element->parent);
        auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
        return !layer || std::find(timeline->layers.begin(), timeline->layers.end(), layer) == timeline->layers.end();
    }), elements.end());
    if (elements.empty())
        return;

    _selection.clear();
    const fla::SymbolList* library = _flaDocument && _flaDocument->document ? _flaDocument->document->symbolList : nullptr;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Distribute to Layers");
    // The topmost object gets the layer just under the current one, the next
    // the one under that, as Animate stacks them.
    int at = current + 1;
    for (auto it = elements.rbegin(); it != elements.rend(); ++it)
    {
        fla::Element* element = *it;
        auto* frame = static_cast<fla::Frame*>(element->parent);
        fla::Layer* layer = fla::newLayerFor(*timeline, false);
        for (fla::Frame* empty : layer->frames)
            delete empty;
        layer->frames.clear();
        // Named for the symbol, as Animate names them, else numbered.
        if (element->elementType() == fla::Element::Type::SymbolInstance)
        {
            const auto* instance = static_cast<const fla::SymbolInstance*>(element);
            const std::string& name = instance->name.empty() ? instance->libraryItemName : instance->name;
            layer->name = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
            layer->autoNamed = false;
        }
        if (frame->index > 0)
        {
            fla::Frame* before = new fla::Frame(layer);
            before->index = 0;
            before->duration = frame->index;
            before->keyMode = "9728";
            layer->frames.push_back(before);
        }
        fla::Frame* holder = new fla::Frame(layer);
        holder->index = frame->index;
        holder->duration = frame->duration;
        holder->keyMode = "9728";
        if (fla::Element* copy = fla::cloneElement(*element, holder))
        {
            fla::relinkSymbols(*copy, library);
            holder->elements.push_back(copy);
        }
        layer->frames.push_back(holder);
        layer->firstFrame = 0;
        layer->lastFrame = holder->index + holder->duration;
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, element, "Distribute to Layers", &_selection)));
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, at++, layer, "Distribute to Layers")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _timelineView->refresh();
    _timelineView->refreshFrames();
    onVisibilityChanged();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::showStageMenu(const QPoint& globalPos, const QPointF& documentPos)
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;
    // A right-click on something not selected selects it, as a click would.
    const HitResult hit = _phoenixView->hitTest(documentPos, _phoenixView->pickTolerance());
    if (hit.element && !_selection.contains(hit.element))
        _selection.select(hit.element);
    else if (!hit.element && !_selection.isEmpty())
        _selection.clear();
    updateSelectionState();

    QMenu menu(this);
    const bool selected = !_selection.isEmpty();
    for (QAction* action : {_cutAction, _copyAction, _pasteAction, _pasteInPlaceAction})
    {
        if (action)
            menu.addAction(action);
    }
    menu.addSeparator();
    if (selected)
    {
        QMenu* arrange = menu.addMenu("Arrange");
        for (QAction* action : _arrangeActions)
        {
            if (action)
                arrange->addAction(action);
        }
        if (_transformMenu)
            menu.addMenu(_transformMenu);
        if (_distributeAction)
            menu.addAction(_distributeAction);
        menu.addSeparator();
        if (_convertToSymbolAction)
            menu.addAction(_convertToSymbolAction);
        auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
        if (instance && instance->symbol)
        {
            menu.addAction("Edit Symbol", this, [this, instance]() { _phoenixView->editSymbol(instance->symbol); });
            menu.addAction("Edit in Place", this, [this, instance]() { _phoenixView->enterSymbol(instance); });
            if (_swapSymbolAction)
                menu.addAction(_swapSymbolAction);
        }
        if (auto* group = dynamic_cast<fla::Group*>(_selection.single()))
            menu.addAction("Edit in Place", this, [this, group]() { _phoenixView->enterGroup(group); });
        for (QAction* action : {_breakApartAction, _groupAction, _ungroupAction})
        {
            if (action && action->isEnabled())
                menu.addAction(action);
        }
    }
    else if (_selectAllAction)
    {
        menu.addAction(_selectAllAction);
    }
    menu.exec(globalPos);
}

void MainWindow::applyFillToSelection()
{
    if (!_phoenixView || !_drawingStyle.hasFill)
        return;
    std::vector<fla::Shape*> shapes;
    for (fla::Element* element : selectedElements())
    {
        if (element && element->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<fla::Shape*>(element));
    }
    if (shapes.empty())
    {
        statusBar()->showMessage("Select shapes to fill.", 3000);
        return;
    }
    _phoenixView->flushPendingMerge();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Apply Fill");
    for (fla::Shape* shape : shapes)
    {
        std::unique_ptr<fla::Element> copy(fla::cloneElement(*shape, shape->parent));
        fla::Shape* after = dynamic_cast<fla::Shape*>(copy.get());
        if (!after || after->fillsMap.empty())
            continue;
        // Every fill of the shape becomes a new one, each fitted to the
        // art it fills.
        for (auto& entry : after->fillsMap)
        {
            fla::FillStyle* fill = _drawingStyle.createFill(after);
            if (!fill)
                continue;
            std::replace(after->fills.begin(), after->fills.end(), entry.second, fill);
            delete entry.second;
            entry.second = fill;
        }
        fla::DrawingStyle::fitFills(*after);
        stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, "Apply Fill")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::updateFramePicker()
{
    if (!_framePicker || !_framePickerDock || !_framePickerDock->isVisible())
        return;
    auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
    int frames = 0;
    if (instance && instance->symbol && !instance->symbol->timelines.empty())
    {
        for (const fla::Layer* layer : instance->symbol->timelines.front()->layers)
        {
            if (layer)
                frames = std::max(frames, fla::timeline::layerEnd(*layer));
        }
    }
    _framePicker->setInstance(instance, frames);
}

void MainWindow::pickInstanceFrame(int frame, bool createKeyframe)
{
    auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
    if (!instance || !_phoenixView)
        return;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Pick Frame");
    if (createKeyframe)
    {
        // The instance's copy on a new keyframe at the playhead takes the
        // frame, leaving the one before as it was.
        fla::Frame* holder = dynamic_cast<fla::Frame*>(instance->parent);
        fla::Layer* layer = holder ? dynamic_cast<fla::Layer*>(holder->parent) : nullptr;
        const int at = _player ? _player->currentFrame() : 0;
        if (layer && holder && holder->index != at && !layer->isLocked())
        {
            const auto position = std::find(holder->elements.begin(), holder->elements.end(), instance);
            const size_t slot = static_cast<size_t>(position - holder->elements.begin());
            auto* insert = new fla::InsertKeyframeCommand(layer, at, "Pick Frame");
            stack.push(fla::CommandPtr(insert));
            fla::Frame* made = insert->frame();
            if (made && made != holder && slot < made->elements.size())
            {
                if (auto* copy = dynamic_cast<fla::SymbolInstance*>(made->elements[slot]))
                    instance = copy;
            }
        }
    }
    if (instance->firstFrame != frame)
    {
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(
            instance->firstFrame, instance->firstFrame, frame, "Pick Frame")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    if (!_selection.contains(instance))
        _selection.select(instance);
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_timelineView)
        _timelineView->refreshFrames();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::arrangeSelection(int step, bool toEnd)
{
    if (!_phoenixView)
        return;

    // Each selected element in its own frame. The selected items move as a
    // block and keep their order among themselves: a step towards the front
    // goes from the top down, so each moves into room the one above left, and
    // a selected item never steps past another; all the way to the front goes
    // from the bottom up, each landing above the one before. Towards the back
    // is the mirror of both.
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

    const bool topDown = (step > 0) != toEnd;
    std::sort(entries.begin(), entries.end(), [topDown](const Entry& a, const Entry& b) {
        return topDown ? a.index > b.index : a.index < b.index;
    });
    const auto selected = [&entries](const fla::Element* element) {
        for (const Entry& entry : entries)
        {
            if (entry.element == element)
                return true;
        }
        return false;
    };

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(step > 0 ? (toEnd ? "Bring to Front" : "Bring Forward")
                              : (toEnd ? "Send to Back" : "Send Backward"));
    for (const Entry& entry : entries)
    {
        // Where it is now, after the moves before it.
        std::vector<fla::Element*>& elements = entry.frame->elements;
        const int index = static_cast<int>(std::find(elements.begin(), elements.end(), entry.element) - elements.begin());
        const int count = static_cast<int>(elements.size());
        const int target = toEnd ? (step > 0 ? count - 1 : 0) : index + step;
        if (target == index || target < 0 || target >= count || (!toEnd && selected(elements[target])))
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

std::vector<fla::Element*> MainWindow::selectedElements() const
{
    // In the order they stack, bottom first, so copies stack the same way.
    std::vector<std::pair<int, fla::Element*>> ordered;
    for (fla::DOMElement* selected : _selection.elements())
    {
        fla::Element* element = dynamic_cast<fla::Element*>(selected);
        if (!element)
            continue;
        int index = 0;
        if (element->parent && element->parent->domType() == fla::DOMElement::DOMType::Frame)
        {
            const auto& elements = static_cast<fla::Frame*>(element->parent)->elements;
            index = static_cast<int>(std::find(elements.begin(), elements.end(), element) - elements.begin());
        }
        ordered.emplace_back(index, element);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<fla::Element*> elements;
    for (const auto& entry : ordered)
        elements.push_back(entry.second);
    return elements;
}

void MainWindow::copy()
{
    const std::vector<fla::Element*> elements = selectedElements();
    if (elements.empty())
        return;

    _clipboard.clear();
    for (const fla::Element* element : elements)
    {
        if (fla::Element* copy = fla::cloneElement(*element, nullptr))
            _clipboard.emplace_back(copy);
    }
    putOnSystemClipboard();

    updateSelectionState();
}

void MainWindow::cut()
{
    if (selectedElements().empty())
        return;

    copy();
    if (_phoenixView)
        _phoenixView->deleteSelection();
}

bool MainWindow::canPaste() const
{
    if (!_clipboard.empty())
        return true;
    const QMimeData* data = QApplication::clipboard()->mimeData();
    return data && (data->hasFormat(kElementsMime) || data->hasImage() || (data->hasText() && !data->text().isEmpty()));
}

void MainWindow::putOnSystemClipboard()
{
    // Three forms: the elements as XFL, which another Phoenix window pastes
    // whole; a picture of them for other applications; and a lone text
    // field's characters as plain text.
    QMimeData* data = new QMimeData();
    {
        fla::Document holder(nullptr);
        fla::Timeline* timeline = new fla::Timeline(&holder);
        fla::Layer* layer = new fla::Layer(timeline);
        fla::Frame* frame = new fla::Frame(layer);
        holder.timelines.push_back(timeline);
        timeline->layers.push_back(layer);
        layer->frames.push_back(frame);
        for (const std::unique_ptr<fla::Element>& element : _clipboard)
        {
            if (fla::Element* copy = fla::cloneElement(*element, frame))
                frame->elements.push_back(copy);
        }
        fla::XFLWriter writer;
        data->setData(kElementsMime, QByteArray::fromStdString(writer.writeDocument(holder)));
    }
    if (_phoenixView)
    {
        std::vector<const fla::Element*> shown;
        for (fla::DOMElement* selected : _selection.elements())
        {
            if (const fla::Element* element = dynamic_cast<const fla::Element*>(selected))
                shown.push_back(element);
        }
        const QImage picture = _phoenixView->renderElements(shown, 2.0);
        if (!picture.isNull())
            data->setImageData(picture);
    }
    if (_clipboard.size() == 1 && _clipboard.front()->elementType() == fla::Element::Type::StaticText)
    {
        const QString text = TextLayout::joinedText(static_cast<const fla::StaticText&>(*_clipboard.front()));
        data->setText(QString(text).replace(QChar('\r'), QChar('\n')));
    }
    QApplication::clipboard()->setMimeData(data);
}

bool MainWindow::pasteFromSystemClipboard(const char* name, bool centerInView)
{
    // What this window copied is pasted from its own copies; what another
    // window or application put there is read from the clipboard.
    const QClipboard* clipboard = QApplication::clipboard();
    if (clipboard->ownsClipboard())
        return false;
    const QMimeData* data = clipboard->mimeData();
    if (!data)
        return false;

    std::vector<std::unique_ptr<fla::Element>> elements;
    if (data->hasFormat(kElementsMime))
    {
        const QByteArray xml = data->data(kElementsMime);
        DocumentParser parser(nullptr);
        std::unique_ptr<fla::Document> holder(parser.parse(xml.toStdString(), nullptr));
        if (holder && !holder->timelines.empty() && !holder->timelines.front()->layers.empty() &&
            !holder->timelines.front()->layers.front()->frames.empty())
        {
            fla::Frame* frame = holder->timelines.front()->layers.front()->frames.front();
            for (fla::Element* element : frame->elements)
            {
                element->parent = nullptr;
                elements.emplace_back(element);
            }
            frame->elements.clear();
        }
    }
    else if (data->hasImage() && _flaDocument && _flaDocument->document && _phoenixView)
    {
        // A picture from another application goes into the library as a
        // bitmap, and an instance of it onto the stage.
        const QImage image = qvariant_cast<QImage>(data->imageData());
        fla::Frame* frame = _phoenixView->activeFrame();
        if (image.isNull() || !frame)
            return false;
        fla::Document* document = _flaDocument->document;
        fla::Bitmap* bitmap = MediaImport::bitmapFromImage(*document, image, "Bitmap 1");
        if (!bitmap)
            return false;
        _phoenixView->flushPendingMerge();
        fla::CommandStack& stack = _editContext.commandStack();
        stack.beginMacro(name);
        fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
        instance->libraryItemName = bitmap->name;
        const QPointF center = centerInView
            ? _phoenixView->mapToDocument(QPointF(_phoenixView->width() / 2.0, _phoenixView->height() / 2.0))
            : QPointF(image.width() / 2.0, image.height() / 2.0);
        instance->transform.tx = center.x() - image.width() / 2.0;
        instance->transform.ty = center.y() - image.height() / 2.0;
        stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, name)));
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, name, &_selection)));
        stack.endMacro();
        stack.breakMergeChain();
        _selection.select(std::vector<fla::DOMElement*>{instance});
        if (_libraryPanel)
            _libraryPanel->refreshIfStale();
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
        return true;
    }
    else if (data->hasText() && !data->text().isEmpty())
    {
        // Text from another application becomes a text field that grows to
        // fit it.
        fla::StaticText* text = new fla::StaticText(nullptr);
        text->autoExpand = true;
        fla::TextRun run;
        run.text = QString(data->text()).replace(QChar('\n'), QChar('\r')).toStdString();
        run.face = "ArialMT";
        run.size = 20.0;
        text->runs.push_back(run);
        elements.emplace_back(text);
    }
    if (elements.empty())
        return false;

    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : elements)
        sources.push_back(element.get());
    placeCopies(sources, name, centerInView);
    return true;
}

void MainWindow::placeCopies(const std::vector<const fla::Element*>& sources, const char* name,
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

    // A copy's instances point at this document's symbols, whichever
    // document the copy came from.
    const fla::SymbolList* library = _flaDocument && _flaDocument->document
        ? _flaDocument->document->symbolList : nullptr;
    std::vector<fla::Element*> copies;
    for (const fla::Element* source : sources)
    {
        fla::Element* copy = fla::cloneElement(*source, frame);
        if (!copy)
            continue;
        fla::relinkSymbols(*copy, library);
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, copy, name, &_selection)));
        copies.push_back(copy);
    }

    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();

    if (centerInView)
    {
        QRectF bounds;
        for (fla::Element* copy : copies)
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
            for (fla::Element* copy : copies)
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
    for (fla::Element* copy : copies)
    {
        if (copy->elementType() == fla::Element::Type::Shape &&
            !static_cast<fla::Shape*>(copy)->isDrawingObject)
        {
            _phoenixView->setPendingMerge(static_cast<fla::Shape*>(copy), frame);
        }
        picked.push_back(copy);
    }
    _selection.select(picked);
    _phoenixView->update();
}

void MainWindow::paste()
{
    if (pasteFromSystemClipboard("Paste", true))
        return;
    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : _clipboard)
        sources.push_back(element.get());
    placeCopies(sources, "Paste", true);
}

void MainWindow::pasteInPlace()
{
    if (pasteFromSystemClipboard("Paste in Place", false))
        return;
    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : _clipboard)
        sources.push_back(element.get());
    placeCopies(sources, "Paste in Place", false);
}

void MainWindow::duplicateSelection()
{
    // Copies are taken before anything is added, since adding changes the
    // selection the sources came from.
    std::vector<std::unique_ptr<fla::Element>> snapshots;
    for (const fla::Element* element : selectedElements())
    {
        if (fla::Element* copy = fla::cloneElement(*element, nullptr))
            snapshots.emplace_back(copy);
    }

    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : snapshots)
        sources.push_back(element.get());
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
    const bool hasCopyable = !selectedElements().empty();
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
        if (element->elementType() == fla::Element::Type::SymbolInstance ||
            element->elementType() == fla::Element::Type::StaticText ||
            element->elementType() == fla::Element::Type::BitmapInstance)
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
    if (_swapSymbolAction)
        _swapSymbolAction->setEnabled(hasInstance);
    for (QAction* action : _transformActions)
        action->setEnabled(hasElements);
    if (_distributeAction)
        _distributeAction->setEnabled(hasElements);
    int selectedShapes = 0;
    for (fla::DOMElement* selected : _selection.elements())
    {
        if (auto* element = dynamic_cast<fla::Element*>(selected))
            selectedShapes += element->elementType() == fla::Element::Type::Shape ? 1 : 0;
    }
    for (QAction* action : _combineActions)
        action->setEnabled(selectedShapes >= 2);
    for (QAction* action : {_smoothAction, _straightenAction, _optimizeAction, _linesToFillsAction,
             _expandFillAction, _softenFillAction})
    {
        if (action)
            action->setEnabled(hasShapes);
    }
    if (_cutAction)
        _cutAction->setEnabled(hasCopyable);
    if (_copyAction)
        _copyAction->setEnabled(hasCopyable);
    if (_duplicateAction)
        _duplicateAction->setEnabled(hasCopyable);
    if (_pasteAction)
        _pasteAction->setEnabled(hasDocument && canPaste());
    if (_pasteInPlaceAction)
        _pasteInPlaceAction->setEnabled(hasDocument && canPaste());

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

namespace {

/// A brush's settings under \a group, so each brush comes back as it was
/// left. What the brush is (its tilt effect) is not a setting and stays.
void saveBrush(QSettings& settings, const QString& group, const fla::BrushSettings& brush)
{
    settings.beginGroup(group);
    settings.setValue("size", brush.size);
    settings.setValue("tip", static_cast<int>(brush.tip));
    settings.setValue("roundness", brush.roundness);
    settings.setValue("angle", brush.angle);
    settings.setValue("usePressure", brush.usePressure);
    settings.setValue("minSize", brush.minSize);
    settings.setValue("pressureCurve", brush.pressureCurve);
    settings.setValue("useTilt", brush.useTilt);
    settings.setValue("velocity", static_cast<int>(brush.velocity));
    settings.setValue("taperStart", brush.taperStart);
    settings.setValue("taperEnd", brush.taperEnd);
    settings.setValue("smoothing", brush.smoothing);
    settings.setValue("precision", brush.precision);
    settings.setValue("stabilizer", brush.stabilizer);
    settings.endGroup();
}

void loadBrush(QSettings& settings, const QString& group, fla::BrushSettings& brush)
{
    settings.beginGroup(group);
    brush.size = std::max(1.0, settings.value("size", brush.size).toDouble());
    brush.tip = settings.value("tip", static_cast<int>(brush.tip)).toInt() == 1
        ? fla::BrushSettings::Tip::Rectangle : fla::BrushSettings::Tip::Ellipse;
    brush.roundness = std::max(1.0, std::min(100.0, settings.value("roundness", brush.roundness).toDouble()));
    brush.angle = settings.value("angle", brush.angle).toDouble();
    brush.usePressure = settings.value("usePressure", brush.usePressure).toBool();
    brush.minSize = std::max(1.0, std::min(100.0, settings.value("minSize", brush.minSize).toDouble()));
    brush.pressureCurve = std::max(0.1, settings.value("pressureCurve", brush.pressureCurve).toDouble());
    brush.useTilt = settings.value("useTilt", brush.useTilt).toBool();
    const int velocity = settings.value("velocity", static_cast<int>(brush.velocity)).toInt();
    brush.velocity = velocity >= 0 && velocity <= 2 ? static_cast<fla::BrushSettings::Velocity>(velocity)
                                                    : fla::BrushSettings::Velocity::None;
    brush.taperStart = std::max(0.0, std::min(50.0, settings.value("taperStart", brush.taperStart).toDouble()));
    brush.taperEnd = std::max(0.0, std::min(50.0, settings.value("taperEnd", brush.taperEnd).toDouble()));
    brush.smoothing = std::max(0, std::min(100, settings.value("smoothing", brush.smoothing).toInt()));
    brush.precision = std::max(0, std::min(100, settings.value("precision", brush.precision).toInt()));
    brush.stabilizer = std::max(0.0, std::min(60.0, settings.value("stabilizer", brush.stabilizer).toDouble()));
    settings.endGroup();
}

} // namespace

void MainWindow::loadSettings()
{
    QSettings settings;
    loadBrush(settings, "brushes/fluid", _drawingStyle.fluidBrush);
    loadBrush(settings, "brushes/classic", _drawingStyle.classicBrush);
    loadBrush(settings, "brushes/ink", _drawingStyle.inkBrush);
    const int brushMode = settings.value("brushes/mode", 0).toInt();
    _drawingStyle.brushMode = brushMode >= 0 && brushMode <= 4
        ? static_cast<fla::DrawingStyle::BrushMode>(brushMode) : fla::DrawingStyle::BrushMode::Normal;
    _recentFiles = settings.value("recentFiles").toStringList();
    _lastDirectory = settings.value("lastDirectory").toString();
    _highQualityAntiAliasing = settings.value("view/highQualityAntiAliasing", true).toBool();
    _outlinerVisible = settings.value("view/outlinerVisible", true).toBool();
    _outlinerWidth = settings.value("view/outlinerWidth", 260).toInt();

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
    settings.setValue("view/outlinerVisible", _outlinerVisible);
    settings.setValue("view/outlinerWidth", _outlinerWidth);
    saveBrush(settings, "brushes/fluid", _drawingStyle.fluidBrush);
    saveBrush(settings, "brushes/classic", _drawingStyle.classicBrush);
    saveBrush(settings, "brushes/ink", _drawingStyle.inkBrush);
    settings.setValue("brushes/mode", static_cast<int>(_drawingStyle.brushMode));
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
    _updater->applyOnExit();
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
