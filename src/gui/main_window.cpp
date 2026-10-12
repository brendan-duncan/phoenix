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
#include "xsheet_panel.h"
#include "layer_depth_panel.h"
#include "camera_tool.h"
#include "find_replace_dialog.h"
#include "toolbar_editor.h"
#include "toolbox.h"
#include "notes_panel.h"
#include "controllers_panel.h"
#include "function_curves_panel.h"
#include "../edit/palettes.h"
#include "../edit/shape_recognition.h"
#include <QGridLayout>
#include "spell_checker.h"
#include "../edit/text_style.h"
#include "../edit/document_settings.h"
#include "scene_panel.h"
#include "../edit/deformers.h"
#include "palettes_panel.h"
#include "../edit/pegs.h"
#include "../edit/color_art.h"
#include "../edit/layer_effects.h"
#include "../edit/lip_sync.h"
#include "../edit/edit_list.h"
#include "lip_sync_dialog.h"
#include "theme.h"
#include "theme_editor.h"
#include "../parser/fla_doctor.h"
#include "../edit/linked_assets.h"
#include "command_line.h"
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
#include "../edit/camera.h"
#include "../edit/shape_tween.h"
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
#include <QElapsedTimer>
#include <QImage>
#include <QPushButton>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QTreeWidget>
#include <QScrollArea>
#include <QPlainTextEdit>
#include <QProcess>
#include <QJsonObject>
#include <QGuiApplication>
#include <QScreen>
#include <QFrame>
#include <QJsonDocument>
#include <QListWidget>
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
#include <QTabBar>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QSignalBlocker>

#include "tool_icons.h"

namespace {

/// The version of the dock layout kept between runs and in workspaces: a
/// layout from before the timeline, tools and outliner docked is not used.
constexpr int kLayoutVersion = 2;
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
    _interactive = startup;
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
    createPanels();
    setupWindowMenu();
    connect(theme::notifier(), &theme::Notifier::changed, this, [this]() {
        QSettings settings;
        if (!settings.contains("onion/beforeColor"))
            _onion.beforeColor = theme::color("stage.onionBefore");
        if (!settings.contains("onion/afterColor"))
            _onion.afterColor = theme::color("stage.onionAfter");
        if (_phoenixView)
            _phoenixView->setOnionStyle(_onion.outline, _onion.keyframesOnly, _onion.opacity, _onion.beforeColor,
                _onion.afterColor);
        refreshStyleSwatches();
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    });
    // Every dock exists now: the layout as made is the default, and the
    // one kept from last time comes back.
    restoreLayout();

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
    _cameraTool = std::make_unique<CameraTool>(stack, [this]() -> fla::Layer* {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        fla::Timeline* scene = document ? document->currentScene() : nullptr;
        if (!scene)
            return nullptr;
        if (!fla::camera::cameraOf(*scene))
            addCamera();
        return fla::camera::cameraOf(*scene);
    });
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
        // The theme's colors, unless others were chosen from the onion menu.
        _onion.beforeColor = settings.contains("onion/beforeColor")
            ? QColor(settings.value("onion/beforeColor").toString()) : theme::color("stage.onionBefore");
        _onion.afterColor = settings.contains("onion/afterColor")
            ? QColor(settings.value("onion/afterColor").toString()) : theme::color("stage.onionAfter");
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
            openInTab(path);
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
    // The stage too: panels that refresh as the history goes ask it about
    // the document (the Motion Editor its motion paths).
    if (_phoenixView)
        _phoenixView->setDocument(nullptr);

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

    if (_findReplace)
        _findReplace->refresh();

    if (_flaDocument)
        _player->setCurrentFrame(0);
    syncFontSymbols();
    warnAboutFonts();
    // A document shown that has no tab yet (the first one, a recovered one)
    // gets one; a tab whose document failed to open goes.
    if (!_switchingTabs)
    {
        if (_flaDocument && (_activeTab < 0 || _activeTab >= static_cast<int>(_tabs.size())))
        {
            _tabs.push_back(std::make_unique<DocumentTab>());
            _activeTab = static_cast<int>(_tabs.size()) - 1;
        }
        else if (!_flaDocument && _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) && _tabs.size() > 1)
        {
            _tabs.erase(_tabs.begin() + _activeTab);
            _activeTab = -1;
            activateTab(static_cast<int>(_tabs.size()) - 1);
            return;
        }
        else if (!_flaDocument && _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()))
        {
            _tabs.erase(_tabs.begin() + _activeTab);
            _activeTab = -1;
        }
        refreshTabs();
    }
}

void MainWindow::warnAboutFonts()
{
    // Once per document, after it is up on the screen.
    if (!_interactive || !_flaDocument || _switchingTabs)
        return;
    QTimer::singleShot(0, this, [this, document = _flaDocument]() {
        if (_flaDocument != document)
            return;
        const QStringList missing = missingFonts();
        bool unmapped = false;
        const QHash<QString, QString> mapping = TextLayout::fontMapping();
        for (const QString& face : missing)
            unmapped = unmapped || !mapping.contains(face);
        if (unmapped)
            fontMappingDialog(true);
    });
}

void MainWindow::parkActiveDocument()
{
    if (!_flaDocument || _activeTab < 0 || _activeTab >= static_cast<int>(_tabs.size()))
        return;
    DocumentTab& tab = *_tabs[static_cast<size_t>(_activeTab)];
    if (_phoenixView)
        _phoenixView->exitToDepth(0);
    tab.path = _documentPath;
    tab.name = _documentName;
    tab.frame = _player ? _player->currentFrame() : 0;
    _editContext.commandStack().swapHistory(tab.history);
    tab.document = _flaDocument;
    // Off the window without deleting it: the panels and the history let
    // go of it as releaseDocument has them do.
    _flaDocument = nullptr;
    releaseDocument();
}

void MainWindow::activateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()) || index == _activeTab)
        return;
    _switchingTabs = true;
    parkActiveDocument();
    DocumentTab& tab = *_tabs[static_cast<size_t>(index)];
    fla::FLADocument* document = tab.document;
    tab.document = nullptr;
    _activeTab = index;
    adoptDocument(document, tab.path, tab.name);
    _editContext.commandStack().swapHistory(tab.history);
    if (_player)
        _player->setCurrentFrame(tab.frame);
    _switchingTabs = false;
    updateEditState();
    updateWindowTitle();
    refreshTabs();
}

void MainWindow::makeRoomForDocument()
{
    if (!_flaDocument)
        return;
    // A blank document nobody has touched is simply replaced.
    if (_documentPath.isEmpty() && !_editContext.isModified())
        return;
    _switchingTabs = true;
    parkActiveDocument();
    _tabs.push_back(std::make_unique<DocumentTab>());
    _activeTab = static_cast<int>(_tabs.size()) - 1;
    _switchingTabs = false;
}

void MainWindow::openInTab(const QString& path)
{
    if (path.isEmpty())
        return;
    // Open already: its tab.
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (size_t i = 0; i < _tabs.size(); ++i)
    {
        const QString tabPath = static_cast<int>(i) == _activeTab ? _documentPath : _tabs[i]->path;
        if (!tabPath.isEmpty() && QFileInfo(tabPath).absoluteFilePath() == wanted)
        {
            activateTab(static_cast<int>(i));
            return;
        }
    }
    makeRoomForDocument();
    loadFLAFile(path);
}

bool MainWindow::closeTab(int index)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()))
        return true;
    if (index != _activeTab)
        activateTab(index);
    if (!confirmDiscardChanges())
        return false;
    _switchingTabs = true;
    releaseDocument();
    _tabs.erase(_tabs.begin() + index);
    _activeTab = -1;
    _switchingTabs = false;
    if (!_tabs.empty())
        activateTab(std::min(index, static_cast<int>(_tabs.size()) - 1));
    else
    {
        adoptDocument(nullptr, QString(), QString());
        updateEditState();
    }
    refreshTabs();
    return true;
}

void MainWindow::refreshTabs()
{
    if (!_tabBar)
        return;
    _switchingTabs = true;
    while (_tabBar->count() > static_cast<int>(_tabs.size()))
        _tabBar->removeTab(_tabBar->count() - 1);
    while (_tabBar->count() < static_cast<int>(_tabs.size()))
        _tabBar->addTab(QString());
    for (size_t i = 0; i < _tabs.size(); ++i)
    {
        const bool active = static_cast<int>(i) == _activeTab;
        const DocumentTab& tab = *_tabs[i];
        const QString name = active ? _documentName : tab.name;
        const bool modified = active ? _editContext.isModified() : !tab.history.isClean();
        _tabBar->setTabText(static_cast<int>(i), (name.isEmpty() ? QString("Untitled") : name) + (modified ? "*" : ""));
        _tabBar->setTabToolTip(static_cast<int>(i), QDir::toNativeSeparators(active ? _documentPath : tab.path));
    }
    if (_activeTab >= 0)
        _tabBar->setCurrentIndex(_activeTab);
    // One document needs no tabs.
    _tabBar->setVisible(_tabs.size() > 1);
    _switchingTabs = false;
}

QString MainWindow::templatesFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("Templates");
}

void MainWindow::saveAsTemplate()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    bool ok = false;
    const QString suggested = QFileInfo(_documentName).completeBaseName();
    const QString name = QInputDialog::getText(this, "Save as Template", "Template name:", QLineEdit::Normal,
        suggested.isEmpty() ? QString("My Template") : suggested, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString safe = name;
    safe.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QDir().mkpath(templatesFolder());
    const QString path = QDir(templatesFolder()).filePath(safe + ".phx");
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, "Save as Template", QString("Replace the template %1?").arg(name)) != QMessageBox::Yes)
        return;
    phx::SaveOptions options;
    options.application = QString("Phoenix %1").arg(QCoreApplication::applicationVersion()).toStdString();
    std::string error;
    if (!phx::save(*_flaDocument->document, path.toStdString(), options, error))
        QMessageBox::warning(this, "Save as Template", QString::fromStdString(error));
    else
        statusBar()->showMessage(QString("Saved the template %1.").arg(name), 3000);
}

void MainWindow::newFromTemplate()
{
    QStringList names;
    const QFileInfoList files = QDir(templatesFolder()).entryInfoList({"*.phx", "*.fla"}, QDir::Files, QDir::Name);
    for (const QFileInfo& file : files)
        names << file.completeBaseName();
    if (names.isEmpty())
    {
        QMessageBox::information(this, "New from Template",
            "There are no templates yet. Open or make a document and choose File > Save as Template.");
        return;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, "New from Template", "Template:", names, 0, false, &ok);
    const int index = static_cast<int>(names.indexOf(chosen));
    if (!ok || index < 0)
        return;
    makeRoomForDocument();
    // A copy of the template, not the template itself: an untitled document
    // whose first Save asks where to put it.
    loadFLAFile(files[index].absoluteFilePath());
    // A template is not a recent file.
    _recentFiles.removeAll(files[index].absoluteFilePath());
    updateRecentFilesMenu();
    saveSettings();
    if (_flaDocument)
    {
        _documentPath.clear();
        _documentName = "Untitled";
        _editContext.markUnsaved();
        updateWindowTitle();
        updateEditState();
    }
}

void MainWindow::revertDocument()
{
    if (_documentPath.isEmpty() || !_flaDocument)
        return;
    if (_editContext.isModified() &&
        QMessageBox::question(this, "Revert",
            QString("Go back to the saved %1? The changes since it was saved will be lost.").arg(_documentName)) !=
            QMessageBox::Yes)
        return;
    const QString path = _documentPath;
    const int frame = _player ? _player->currentFrame() : 0;
    // In the same tab: the document reloads in place.
    _switchingTabs = true;
    loadFLAFile(path);
    _switchingTabs = false;
    if (_player && _flaDocument)
        _player->setCurrentFrame(frame);
    refreshTabs();
}

void MainWindow::createPanels()
{
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
        tabifyDockWidget(_timelineDock, _motionEditorDock);
        _motionEditorDock->hide();
        connect(_player, &Player::currentFrameChanged, _motionEditor, [this](int) { _motionEditor->refresh(); });
    }

    // Layer Depth, for the multiplane camera, closed until asked.
    if (!_layerDepthDock)
    {
        LayerDepthLinks depth;
        depth.commands = [this]() { return &_editContext.commandStack(); };
        depth.scene = [this]() -> fla::Timeline* {
            return _flaDocument && _flaDocument->document ? _flaDocument->document->currentScene() : nullptr;
        };
        depth.stageWidth = [this]() {
            return _flaDocument && _flaDocument->document ? static_cast<double>(_flaDocument->document->width) : 550.0;
        };
        depth.changed = [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        };
        _layerDepth = new LayerDepthPanel(depth);
        _layerDepthDock = new QDockWidget("Layer Depth", this);
        _layerDepthDock->setObjectName("layerDepthDock");
        _layerDepthDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _layerDepth->setParent(_layerDepthDock);
        _layerDepthDock->setWidget(_layerDepth);
        addDockWidget(Qt::BottomDockWidgetArea, _layerDepthDock);
        tabifyDockWidget(_timelineDock, _layerDepthDock);
        _layerDepthDock->hide();
    }

    // History, closed until asked.
    if (!_historyDock)
    {
        _historyDock = new QDockWidget("History", this);
        _historyDock->setObjectName("historyDock");
        _historyDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _history = new QListWidget(_historyDock);
        _history->setToolTip("Click a step to go back or forward to it");
        _historyDock->setWidget(_history);
        addDockWidget(Qt::RightDockWidgetArea, _historyDock);
        _historyDock->hide();
        connect(_history, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            // Row 0 is the document as opened; row n is after the n-th step.
            const size_t target = static_cast<size_t>(_history->row(item));
            fla::CommandStack& stack = _editContext.commandStack();
            while (stack.index() > target && stack.canUndo())
                undo();
            while (stack.index() < target && stack.canRedo())
                redo();
        });
    }

    // Review Notes, closed until asked.
    if (!_notesDock)
    {
        NotesLinks notes;
        notes.commands = [this]() { return &_editContext.commandStack(); };
        notes.timeline = [this]() -> fla::Timeline* {
            if (_phoenixView && _phoenixView->scopeTimeline())
                return _phoenixView->scopeTimeline();
            return _flaDocument && _flaDocument->document ? _flaDocument->document->currentScene() : nullptr;
        };
        notes.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        notes.setCurrentFrame = [this](int frame) {
            if (_player)
                _player->setCurrentFrame(frame);
        };
        notes.setCurrentLayer = [this](const fla::Layer* layer) {
            if (_timelineView)
                _timelineView->setCurrentLayer(layer);
        };
        notes.selection = [this]() { return &_selection; };
        notes.corner = [this]() {
            // The top left of the stage, or of the view when the stage runs
            // past it.
            if (!_phoenixView)
                return QPointF(20.0, 20.0);
            const QPointF inView = _phoenixView->widgetToStage(QPointF(40.0, 40.0));
            const QRectF stage = _phoenixView->stageInScope();
            return QPointF(std::max(inView.x(), stage.left() + 20.0), std::max(inView.y(), stage.top() + 20.0));
        };
        notes.changed = [this]() {
            if (_timelineView)
                _timelineView->refresh();
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        };
        _notes = new NotesPanel(notes);
        _notesDock = new QDockWidget("Review Notes", this);
        _notesDock->setObjectName("reviewNotesDock");
        _notesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _notes->setParent(_notesDock);
        _notesDock->setWidget(_notes);
        addDockWidget(Qt::RightDockWidgetArea, _notesDock);
        _notesDock->hide();
        connect(_phoenixView, &PhoenixView::scopeChanged, _notes, [this]() { _notes->refresh(); });
    }

    // Controllers, closed until asked.
    if (!_controllersDock)
    {
        ControllersLinks links;
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.document = [this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; };
        links.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        links.currentLayer = [this](fla::Timeline*& timeline) -> fla::Layer* {
            int index = 0;
            if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
                index >= static_cast<int>(timeline->layers.size()))
                return nullptr;
            return timeline->layers[static_cast<size_t>(index)];
        };
        links.changed = [this]() {
            if (_timelineView)
                _timelineView->refreshFrames();
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        };
        _controllers = new ControllersPanel(links);
        _controllersDock = new QDockWidget("Controllers", this);
        _controllersDock->setObjectName("controllersDock");
        _controllersDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _controllers->setParent(_controllersDock);
        _controllersDock->setWidget(_controllers);
        addDockWidget(Qt::RightDockWidgetArea, _controllersDock);
        _controllersDock->hide();
    }

    // Scenes, closed until asked.
    if (!_scenesDock)
    {
        SceneLinks links;
        links.document = [this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; };
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.showScene = [this](int index) { goToScene(index); };
        _scenes = new ScenePanel(links);
        _scenesDock = new QDockWidget("Scene", this);
        _scenesDock->setObjectName("sceneDock");
        _scenesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _scenes->setParent(_scenesDock);
        _scenesDock->setWidget(_scenes);
        addDockWidget(Qt::RightDockWidgetArea, _scenesDock);
        _scenesDock->hide();
    }

    // Function curves, closed until asked.
    if (!_functionCurvesDock)
    {
        FunctionCurvesLinks links;
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.currentLayer = [this](fla::Timeline*& timeline) -> fla::Layer* {
            int index = 0;
            if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
                index >= static_cast<int>(timeline->layers.size()))
                return nullptr;
            return timeline->layers[static_cast<size_t>(index)];
        };
        links.context = [this]() {
            fla::curves::Context context;
            if (_flaDocument && _flaDocument->document)
            {
                const fla::Document* document = _flaDocument->document;
                context.timeline = document->currentScene();
                context.frameRate = document->frameRate;
                context.width = document->width;
                context.height = document->height;
            }
            return context;
        };
        links.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        links.setCurrentFrame = [this](int frame) {
            if (_player)
                _player->setCurrentFrame(frame);
        };
        links.changed = [this]() {
            if (_timelineView)
                _timelineView->refreshFrames();
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
            if (_propertiesPanel)
                _propertiesPanel->refresh();
        };
        _functionCurves = new FunctionCurvesPanel(links);
        _functionCurvesDock = new QDockWidget("Function Curves", this);
        _functionCurvesDock->setObjectName("functionCurvesDock");
        _functionCurvesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _functionCurves->setParent(_functionCurvesDock);
        _functionCurvesDock->setWidget(_functionCurves);
        addDockWidget(Qt::BottomDockWidgetArea, _functionCurvesDock);
        _functionCurvesDock->hide();
        const auto refreshShown = [this]() {
            if (_functionCurvesDock->isVisible())
                _functionCurves->refresh();
        };
        connect(_player, &Player::currentFrameChanged, _functionCurves, refreshShown);
        connect(_timelineView, &TimelineView::currentLayerChanged, _functionCurves, refreshShown);
    }

    // Palettes, closed until asked.
    if (!_palettesDock)
    {
        PalettesLinks links;
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.document = [this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; };
        links.selection = [this]() { return selectedElements(); };
        links.fillColor = [this]() {
            return QColor(_drawingStyle.fillColor[0], _drawingStyle.fillColor[1], _drawingStyle.fillColor[2],
                _drawingStyle.fillColor[3]);
        };
        links.changed = [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
            if (_propertiesPanel)
                _propertiesPanel->refresh();
        };
        _palettes = new PalettesPanel(links);
        _palettesDock = new QDockWidget("Palettes", this);
        _palettesDock->setObjectName("palettesDock");
        _palettesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _palettes->setParent(_palettesDock);
        _palettesDock->setWidget(_palettes);
        addDockWidget(Qt::RightDockWidgetArea, _palettesDock);
        _palettesDock->hide();
    }

    // The X-sheet, Harmony's view of the exposures, closed until asked.
    if (!_xsheetDock)
    {
        XSheetLinks sheet;
        sheet.commands = [this]() { return &_editContext.commandStack(); };
        sheet.timeline = [this]() -> fla::Timeline* {
            if (_phoenixView && _phoenixView->scopeTimeline())
                return _phoenixView->scopeTimeline();
            return _flaDocument && _flaDocument->document ? _flaDocument->document->currentScene() : nullptr;
        };
        sheet.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        sheet.setCurrentFrame = [this](int frame) {
            if (_player)
                _player->setCurrentFrame(frame);
        };
        sheet.setCurrentLayer = [this](const fla::Layer* layer) {
            if (_timelineView)
                _timelineView->setCurrentLayer(layer);
        };
        sheet.changed = [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
            if (_timelineView)
                _timelineView->refreshFrames();
        };
        _xsheet = new XSheetPanel(sheet);
        _xsheetDock = new QDockWidget("X-Sheet", this);
        _xsheetDock->setObjectName("xsheetDock");
        _xsheetDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _xsheet->setParent(_xsheetDock);
        _xsheetDock->setWidget(_xsheet);
        addDockWidget(Qt::RightDockWidgetArea, _xsheetDock);
        _xsheetDock->hide();
        connect(_player, &Player::currentFrameChanged, _xsheet, [this](int) { _xsheet->refresh(); });
        connect(_phoenixView, &PhoenixView::scopeChanged, _xsheet, [this]() { _xsheet->refresh(); });
    }
    _timelineDock->raise();
}

void MainWindow::setupWindowMenu()
{
    // Animate's Window menu: every panel, ticked when open. Choosing an
    // open panel that is in view closes it; one that is closed, or behind
    // another tab, comes to the front.
    QMenu* windowMenu = new QMenu("&Window", this);
    QAction* help = nullptr;
    for (QAction* action : menuBar()->actions())
        if (action->text() == "&Help")
            help = action;
    menuBar()->insertMenu(help, windowMenu);

    QMenu* workspaceMenu = windowMenu->addMenu("&Workspaces");
    connect(workspaceMenu, &QMenu::aboutToShow, this, [this, workspaceMenu]() { fillWorkspaceMenu(workspaceMenu); });
    windowMenu->addSeparator();

    struct Panel
    {
        const char* text;
        QDockWidget* dock;
        QKeySequence key;
        const char* tip;
    };
    QDockWidget* properties = findChild<QDockWidget*>("propertiesDock");
    QDockWidget* library = findChild<QDockWidget*>("libraryDock");
    const std::vector<std::vector<Panel>> groups = {
        {
            {"&Timeline", _timelineDock, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), "Layers and frames"},
            {"T&ools", _toolsDock, QKeySequence(Qt::CTRL | Qt::Key_F2), "The drawing and selection tools"},
            {"&Properties", properties, QKeySequence(Qt::CTRL | Qt::Key_F3), "What is selected, the tool, the frame or the document"},
            {"&Library", library, QKeySequence(Qt::CTRL | Qt::Key_L), "The document's symbols, bitmaps and sounds"},
            {"Ou&tliner", _outlinerDock, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_O), "The document as a tree"},
        },
        {
            {"&Color", _colorDock, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9), "Solid colors, gradients and bitmap fills for new art"},
            {"S&watches", _swatchesDock, QKeySequence(Qt::CTRL | Qt::Key_F9), "The document's saved colors"},
            {"&Align", _alignDock, QKeySequence(Qt::CTRL | Qt::Key_K), "Line objects up and space them out"},
            {"Tra&nsform", _transformDock, QKeySequence(Qt::CTRL | Qt::Key_T), "Scale, turn and skew by numbers"},
            {"&Info", _infoDock, QKeySequence(Qt::CTRL | Qt::Key_I), "The selection's size and place, and the color under the pointer"},
        },
        {
            {"&Frame Picker", _framePickerDock, QKeySequence(), "Pick the frame a graphic instance shows from pictures of its frames"},
            {"&Motion Editor", _motionEditorDock, QKeySequence(), "The property curves of a motion tween, with their keyframes and eases"},
            {"Layer &Depth", _layerDepthDock, QKeySequence(), "How far each layer sits from the multiplane camera"},
            {"&X-Sheet", _xsheetDock, QKeySequence(), "The exposures of each layer, frame by frame, typed in by drawing number"},
            {"&History", _historyDock, QKeySequence(Qt::CTRL | Qt::Key_F10), "Every step that can be undone; click one to go back or forward to it"},
            {"&Review Notes", _notesDock, QKeySequence(), "Notes on frames from whoever reviews the shot; they never export"},
            {"Co&ntrollers", _controllersDock, QKeySequence(), "Master controllers: one slider driving properties on several layers"},
            {"Sc&ene", _scenesDock, QKeySequence(Qt::SHIFT | Qt::Key_F2), "The document's scenes: switch, add, duplicate, delete, rename and reorder them"},
            {"F&unction Curves", _functionCurvesDock, QKeySequence(), "Any property of the current layer as a curve over time, its keyframes dragged up or down"},
            {"Pale&ttes", _palettesDock, QKeySequence(), "Linked palettes: named colors that recolor everything linked to them, swapped whole"},
        },
    };
    std::vector<std::pair<QAction*, QDockWidget*>> entries;
    for (const std::vector<Panel>& group : groups)
    {
        for (const Panel& panel : group)
        {
            if (!panel.dock)
                continue;
            QAction* action = windowMenu->addAction(panel.text);
            action->setCheckable(true);
            action->setShortcut(panel.key);
            action->setStatusTip(panel.tip);
            // The keys work with the menu closed too.
            addAction(action);
            QDockWidget* dock = panel.dock;
            connect(action, &QAction::triggered, this, [dock, action]() {
                const bool open = dock->toggleViewAction()->isChecked();
                if (open && !dock->visibleRegion().isEmpty())
                {
                    dock->close();
                }
                else
                {
                    dock->show();
                    dock->raise();
                    if (dock->isFloating())
                        dock->activateWindow();
                }
                action->setChecked(dock->toggleViewAction()->isChecked());
            });
            entries.emplace_back(action, dock);
        }
        windowMenu->addSeparator();
    }
    QAction* motionPresetsAction = windowMenu->addAction("Motion &Presets…");
    motionPresetsAction->setStatusTip("Saved motions to put on an object, Animate's among them");
    connect(motionPresetsAction, &QAction::triggered, this, &MainWindow::showMotionPresets);
    windowMenu->addSeparator();

    QAction* hidePanels = windowMenu->addAction("&Hide Panels");
    hidePanels->setShortcut(QKeySequence(Qt::Key_F4));
    hidePanels->setStatusTip("Hide every panel to see the stage alone; again brings them back");
    addAction(hidePanels);
    connect(hidePanels, &QAction::triggered, this, &MainWindow::togglePanels);
    QMenu* themeMenu = windowMenu->addMenu("T&heme");
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themeMenu]() { fillThemeMenu(themeMenu); });
    QAction* editTools = windowMenu->addAction("&Edit Toolbar…");
    editTools->setStatusTip("Arrange, group and hide the tools in the Tools panel");
    connect(editTools, &QAction::triggered, this, &MainWindow::editToolbar);

    connect(windowMenu, &QMenu::aboutToShow, this, [entries]() {
        for (const auto& [action, dock] : entries)
            action->setChecked(dock->toggleViewAction()->isChecked());
    });

    // What a panel shows is brought up to date when it comes into view.
    connect(_palettesDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _palettes->refresh();
    });
    connect(_controllersDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _controllers->refresh();
    });
    connect(_scenesDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _scenes->refresh();
    });
    connect(_functionCurvesDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _functionCurves->refresh();
    });
    connect(_notesDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _notes->refresh();
    });
    connect(_historyDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            refreshHistory();
    });
    connect(_layerDepthDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _layerDepth->refresh();
    });
    connect(_xsheetDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _xsheet->refresh();
    });
    connect(_motionEditorDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _motionEditor->refresh();
    });
}

void MainWindow::editToolbar()
{
    ToolbarEditor editor(_toolbox, this);
    if (editor.exec() == QDialog::Accepted)
        _toolbox->setLayout(editor.layout());
}

void MainWindow::fillThemeMenu(QMenu* menu)
{
    menu->clear();
    const QString current = theme::currentName();
    const QStringList names = theme::names();
    for (const QString& name : names)
    {
        if (name == names.value(theme::builtInNames().size()))
            menu->addSeparator();
        QAction* use = menu->addAction(name, this, [name]() { theme::use(name); });
        use->setCheckable(true);
        use->setChecked(name == current);
    }
    menu->addSeparator();
    menu->addAction("&Edit Theme\u2026", this, [this]() {
        ThemeEditor editor(this);
        editor.exec();
    });
}

void MainWindow::togglePanels()
{
    // Again after hiding brings back what was hidden.
    if (!_hiddenByF4.isEmpty())
    {
        for (QDockWidget* dock : _hiddenByF4)
            if (dock)
                dock->show();
        _hiddenByF4.clear();
        return;
    }
    for (QDockWidget* dock : findChildren<QDockWidget*>())
    {
        if (dock->toggleViewAction()->isChecked())
        {
            _hiddenByF4 << dock;
            dock->hide();
        }
    }
}

void MainWindow::newFile()
{
    // Animate's New Document dialog: a preset, or a size and frame rate.
    NewDocumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    makeRoomForDocument();
    createNewDocument(dialog.documentWidth(), dialog.documentHeight(), dialog.frameRate(), dialog.stageColor());
}

bool MainWindow::offerRecovery()
{
    QList<AutoSave::Orphan> orphans = AutoSave::orphans();
    if (orphans.isEmpty())
        return false;
    std::sort(orphans.begin(), orphans.end(),
        [](const AutoSave::Orphan& a, const AutoSave::Orphan& b) { return a.saved > b.saved; });

    // One dialog for every copy left behind, newest first: recover one,
    // discard any or all, or keep them for next time.
    QDialog dialog(this);
    dialog.setWindowTitle("Recover Unsaved Work");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* intro = new QLabel(&dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QListWidget* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const auto fill = [list, intro, &orphans]() {
        intro->setText(orphans.size() == 1
                ? QString("Phoenix closed unexpectedly while a document had unsaved changes. A copy was kept.")
                : QString("Phoenix closed unexpectedly while documents had unsaved changes. %1 copies were kept.")
                      .arg(orphans.size()));
        list->clear();
        for (const AutoSave::Orphan& orphan : orphans)
        {
            const QString name = orphan.name.isEmpty() ? QString("Untitled") : orphan.name;
            QListWidgetItem* item = new QListWidgetItem(
                QString("%1    %2").arg(name, QLocale().toString(orphan.saved, QLocale::ShortFormat)), list);
            item->setToolTip(orphan.originalPath.isEmpty() ? QString("Never saved")
                                                          : QDir::toNativeSeparators(orphan.originalPath));
        }
        if (list->count() > 0)
            list->setCurrentRow(0);
    };
    fill();
    layout->addWidget(list, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
    QPushButton* recover = buttons->addButton("Recover", QDialogButtonBox::AcceptRole);
    QPushButton* discard = buttons->addButton("Discard", QDialogButtonBox::DestructiveRole);
    QPushButton* discardAll = buttons->addButton("Discard All", QDialogButtonBox::DestructiveRole);
    buttons->addButton("Later", QDialogButtonBox::RejectRole);
    recover->setDefault(true);
    layout->addWidget(buttons);
    const auto update = [&]() {
        recover->setEnabled(list->selectedItems().size() == 1);
        discard->setEnabled(!list->selectedItems().isEmpty());
    };
    connect(list, &QListWidget::itemSelectionChanged, &dialog, update);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [recover]() { recover->click(); });
    connect(discard, &QPushButton::clicked, &dialog, [&]() {
        QList<AutoSave::Orphan> kept;
        for (int row = 0; row < orphans.size(); ++row)
        {
            if (list->item(row)->isSelected())
                AutoSave::remove(orphans[row]);
            else
                kept.push_back(orphans[row]);
        }
        orphans = kept;
        if (orphans.isEmpty())
            dialog.reject();
        fill();
        update();
    });
    connect(discardAll, &QPushButton::clicked, &dialog, [&]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(&dialog, "Discard All",
            QString("Discard all %1 copies? They cannot be recovered afterward.").arg(orphans.size()));
        if (answer != QMessageBox::Yes)
            return;
        for (const AutoSave::Orphan& orphan : orphans)
            AutoSave::remove(orphan);
        orphans.clear();
        dialog.reject();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0 || list->currentRow() >= orphans.size())
        return false;

    const AutoSave::Orphan orphan = orphans[list->currentRow()];
    const QString name = orphan.name.isEmpty() ? QString("Untitled") : orphan.name;
    std::string error;
    std::string warning;
    bool readOnly = false;
    fla::FLADocument* document = phx::load(orphan.file.toStdString(), error, warning, readOnly);
    if (!document)
    {
        QMessageBox::warning(this, "Recover Unsaved Work",
            QString("The copy could not be read: %1").arg(QString::fromStdString(error)));
        return false;
    }
    // Back where it came from, and unsaved until it is saved. The other
    // copies are offered again next time.
    adoptDocument(document, orphan.originalPath, name);
    _editContext.markUnsaved();
    updateEditState();
    AutoSave::remove(orphan);
    statusBar()->showMessage(QString("Recovered %1. Save it to keep it.").arg(name), 8000);
    return true;
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
        openInTab(dialog.path());
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
    openInTab(filePath);
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

    // As in Animate, every panel docks: the panels down the right take the
    // full height, the tools down the left, and the timeline sits under the
    // stage. A group of tabs drags as one by its title; a tab drags out on
    // its own.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
        QMainWindow::GroupedDragging);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);

    // The outliner: the document as a tree, closed until asked for.
    _documentView = new DocumentView(this);
    _documentView->setMinimumWidth(150);
    _outlinerDock = new QDockWidget("Outliner", this);
    _outlinerDock->setObjectName("outlinerDock");
    _outlinerDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _outlinerDock->setWidget(_documentView);

    // Create the phoenix view (right panel)
    _phoenixView = new PhoenixView(_player, this);

    // The stage sits under its breadcrumb, which says what is being edited.
    _breadcrumb = new QWidget(this);
    _breadcrumb->setAutoFillBackground(true);
    QHBoxLayout* breadcrumbLayout = new QHBoxLayout(_breadcrumb);
    breadcrumbLayout->setContentsMargins(6, 2, 6, 2);
    breadcrumbLayout->setSpacing(2);
    breadcrumbLayout->addStretch();

    // The breadcrumb is rebuilt as scopes change, so it has a row of its own.
    QWidget* breadcrumbRow = new QWidget(this);
    breadcrumbRow->setAutoFillBackground(true);
    QHBoxLayout* breadcrumbRowLayout = new QHBoxLayout(breadcrumbRow);
    breadcrumbRowLayout->setContentsMargins(2, 0, 0, 0);
    breadcrumbRowLayout->setSpacing(0);
    breadcrumbRowLayout->addWidget(_breadcrumb, 1);

    QWidget* stageColumn = new QWidget(this);
    QVBoxLayout* stageLayout = new QVBoxLayout(stageColumn);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    _tabBar = new QTabBar(this);
    _tabBar->setTabsClosable(true);
    _tabBar->setMovable(true);
    _tabBar->setDocumentMode(true);
    _tabBar->setExpanding(false);
    _tabBar->setDrawBase(false);
    _tabBar->hide();
    connect(_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!_switchingTabs && index >= 0)
            activateTab(index);
    });
    connect(_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) { closeTab(index); });
    connect(_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (from < 0 || to < 0 || from >= static_cast<int>(_tabs.size()) || to >= static_cast<int>(_tabs.size()))
            return;
        std::unique_ptr<DocumentTab> moved = std::move(_tabs[static_cast<size_t>(from)]);
        _tabs.erase(_tabs.begin() + from);
        _tabs.insert(_tabs.begin() + to, std::move(moved));
        _activeTab = _tabBar->currentIndex();
    });
    stageLayout->addWidget(_tabBar);
    stageLayout->addWidget(breadcrumbRow);
    stageLayout->addWidget(_phoenixView, 1);

    mainLayout->addWidget(stageColumn);

    // The timeline, under the stage.
    _timelineView = new TimelineView(_player, this);
    _timelineView->setMinimumHeight(150);
    _timelineDock = new QDockWidget("Timeline", this);
    _timelineDock->setObjectName("timelineDock");
    _timelineDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _timelineDock->setWidget(_timelineView);
    addDockWidget(Qt::BottomDockWidgetArea, _timelineDock);
    resizeDocks({_timelineDock}, {300}, Qt::Vertical);

    // Sound follows the playhead and the transport.
    _timelineAudio = new TimelineAudio(_player, this);
    connect(_player, &Player::playingChanged, _timelineAudio, &TimelineAudio::onPlayingChanged);
    connect(_player, &Player::currentFrameChanged, _timelineAudio, &TimelineAudio::onFrameChanged);
    _timelineView->setSoundProvider([this](const fla::Frame* frame, size_t& in) {
        return _timelineAudio->soundOf(frame, &in);
    });

    addDockWidget(Qt::LeftDockWidgetArea, _outlinerDock);
    _outlinerDock->hide();

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
        // Anchored, the markers dragged set the frames it stays on.
        if (_onion.anchored && _player)
        {
            _onion.anchorFirst = std::max(0, _player->currentFrame() - before);
            _onion.anchorLast = _player->currentFrame() + after;
        }
        applyOnion();
    });
    // Anchored, the range is the same frames wherever the playhead goes.
    connect(_player, &Player::currentFrameChanged, this, [this](int frame) {
        if (!_onion.anchored)
            return;
        _onion.before = std::max(0, frame - _onion.anchorFirst);
        _onion.after = std::max(0, _onion.anchorLast - frame);
        const bool on = _onionSkinAction && _onionSkinAction->isChecked();
        if (_phoenixView)
            _phoenixView->setOnionSkin(on, _onion.before, _onion.after);
        if (_timelineView)
            _timelineView->setOnionSkin(on, _onion.before, _onion.after);
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
    connect(_timelineView, &TimelineView::parentLayerRequested, this, &MainWindow::setParentLayer);
    connect(_timelineView, &TimelineView::effectSettingsRequested, this, &MainWindow::editEffect);
    connect(_timelineView, &TimelineView::layerClipboardRequested, this, &MainWindow::layerClipboard);
    // The frame view's thumbnails: a keyframe's art alone, cropped to it,
    // or the whole stage around it.
    _timelineView->setThumbnailProvider([this](const fla::Layer* layer, int frame, QSize size, bool inContext) {
        if (!_phoenixView || !_flaDocument || !_flaDocument->document)
            return QImage();
        const fla::Document* document = _flaDocument->document;
        const QSize stage(std::max(1, document->width), std::max(1, document->height));
        const QSize drawn = stage.scaled(QSize(size.height() * 8, size.height() * 2), Qt::KeepAspectRatio);
        if (!inContext)
            _phoenixView->setOnlyLayer(layer);
        QImage image = _phoenixView->renderFrame(frame, drawn.isEmpty() ? QSize(1, 1) : drawn, !inContext);
        _phoenixView->setOnlyLayer(nullptr);
        if (!inContext)
        {
            // Cropped to the art, with a little room.
            int left = image.width(), top = image.height(), right = -1, bottom = -1;
            const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < argb.height(); ++y)
            {
                const QRgb* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
                for (int x = 0; x < argb.width(); ++x)
                    if (qAlpha(line[x]) > 8)
                    {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            }
            if (right < 0)
                return QImage();
            image = argb.copy(QRect(QPoint(left, top), QPoint(right, bottom)).adjusted(-1, -1, 1, 1));
        }
        return image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    });
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

void MainWindow::editShapeHints(bool add)
{
    bool start = true;
    fla::Frame* tween = _phoenixView ? _phoenixView->shapeHintTween(start) : nullptr;
    if (!tween)
    {
        statusBar()->showMessage("Shape hints go on the first keyframe of a shape tween.", 4000);
        return;
    }
    std::vector<fla::ShapeHint> hints = fla::hintsOf(*tween);
    if (add)
    {
        if (hints.size() >= 26)
            return;
        // At the middle of each keyframe's shape, ready to be dragged into
        // place on both.
        const fla::Layer* layer = dynamic_cast<const fla::Layer*>(tween->parent);
        const fla::Frame* end = nullptr;
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            if (frame && frame->index == tween->index + std::max(1, tween->duration))
                end = frame;
        const fla::Shape* from = nullptr;
        const fla::Shape* to = nullptr;
        if (!end || !fla::tweenShapes(*tween, *end, from, to))
            return;
        const QPointF a = _phoenixView->elementBounds(from).center();
        const QPointF b = _phoenixView->elementBounds(to).center();
        fla::ShapeHint hint;
        hint.start = fla::Point(a.x(), a.y());
        hint.end = fla::Point(b.x(), b.y());
        hints.push_back(hint);
    }
    else
    {
        if (hints.empty())
            return;
        hints.clear();
    }
    auto attributes = tween->extraAttributes;
    fla::setHints(attributes, hints);
    _editContext.commandStack().push(fla::CommandPtr(
        new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
            tween->extraAttributes, tween->extraAttributes, attributes, add ? "Add Shape Hint" : "Remove All Hints")));
    _editContext.commandStack().breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::setParentLayer(fla::Timeline* timeline, fla::Layer* layer, fla::Layer* parent)
{
    if (!timeline || !layer || !_phoenixView || layer->isLocked())
        return;
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    const auto valueOf = [](const Attributes& attributes, const char* name) {
        for (const auto& attribute : attributes)
            if (attribute.first == name)
                return std::atoi(attribute.second.c_str());
        return -1;
    };
    const auto withValue = [](Attributes attributes, const char* name, int value) {
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                             [name](const auto& a) { return a.first == name; }),
            attributes.end());
        if (value >= 0)
            attributes.emplace_back(name, std::to_string(value));
        return attributes;
    };

    // The parent's rigging index, a new one when it has none.
    int rig = -1;
    Attributes parentAfter;
    if (parent)
    {
        rig = valueOf(parent->extraAttributes, "layerRiggingIndex");
        if (rig < 0)
        {
            for (const fla::Layer* other : timeline->layers)
                if (other)
                    rig = std::max(rig, valueOf(other->extraAttributes, "layerRiggingIndex"));
            ++rig;
        }
        parentAfter = withValue(parent->extraAttributes, "layerRiggingIndex", rig);
    }

    // Each keyframe's objects, where they show now and where they must be
    // stored to show there under the new parent.
    struct Move
    {
        fla::Element* element;
        fla::Transform before;
        fla::Transform after;
    };
    std::vector<Move> moves;
    std::vector<std::pair<fla::Frame*, Attributes>> frames;
    Attributes parentBefore = parent ? parent->extraAttributes : Attributes();
    if (parent)
        parent->extraAttributes = parentAfter;
    for (fla::Frame* frame : layer->frames)
    {
        if (!frame)
            continue;
        const QTransform old = _phoenixView->parentingAt(layer, frame, frame->index);
        const Attributes was = frame->extraAttributes;
        frame->extraAttributes = withValue(was, "parentLayerIndex", parent ? rig : -1);
        const QTransform now = _phoenixView->parentingAt(layer, frame, frame->index);
        frames.push_back({frame, frame->extraAttributes});
        frame->extraAttributes = was;
        bool invertible = false;
        const QTransform undo = now.inverted(&invertible);
        if (!invertible)
            continue;
        for (fla::Element* element : frame->elements)
        {
            if (!element)
                continue;
            const fla::Transform& t = element->transform;
            const QTransform world = QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty) * old;
            const QTransform stored = world * undo;
            moves.push_back({element, t,
                fla::Transform(stored.m11(), stored.m12(), stored.m21(), stored.m22(), stored.dx(), stored.dy())});
        }
    }
    if (parent)
        parent->extraAttributes = parentBefore;

    fla::CommandStack& stack = _editContext.commandStack();
    const char* name = parent ? "Parent Layer" : "Remove Parent";
    stack.beginMacro(name);
    if (parent && parentAfter != parent->extraAttributes)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
            parent->extraAttributes, parent->extraAttributes, parentAfter, name)));
    for (const auto& entry : frames)
        if (entry.second != entry.first->extraAttributes)
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
                entry.first->extraAttributes, entry.first->extraAttributes, entry.second, name)));
    for (const Move& move : moves)
        stack.push(fla::CommandPtr(new fla::SetElementTransformCommand(move.element, move.before, move.after, name)));
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_timelineView)
        _timelineView->refresh();
}

void MainWindow::separateLineAndColor()
{
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
    {
        statusBar()->showMessage("Pick the line layer first.", 3000);
        return;
    }
    fla::Layer* line = timeline->layers[static_cast<size_t>(index)];
    if (!line || line->layerType == fla::Layer::Type::Folder || fla::colorart::isColorArt(*line))
        return;
    if (_phoenixView)
        _phoenixView->flushPendingMerge();
    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Color Art Layer");
    // The color layer under the line layer, with the fills.
    fla::Layer* color = fla::colorart::newColorLayer(*line);
    stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, index + 1, color, "Color Art Layer")));
    // The line layer keeps its strokes.
    for (fla::Frame* frame : line->frames)
    {
        if (!frame)
            continue;
        for (fla::Element* element : std::vector<fla::Element*>(frame->elements))
        {
            if (!element || element->elementType() != fla::Element::Type::Shape)
                continue;
            fla::Shape* shape = static_cast<fla::Shape*>(element);
            std::unique_ptr<fla::Shape> strokes(fla::colorart::strokesOnly(*shape));
            if (strokes->edges.empty())
                stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, shape, "Color Art Layer")));
            else
                stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *strokes, "Color Art Layer")));
        }
    }
    stack.endMacro();
    stack.breakMergeChain();
    if (_timelineView)
    {
        _timelineView->refresh();
        _timelineView->setCurrentLayer(color);
    }
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    statusBar()->showMessage(QString("The fills went to %1; the Paint Bucket there paints within the lines of %2.")
                                 .arg(QString::fromStdString(color->name), QString::fromStdString(line->name)), 5000);
}

void MainWindow::addEffectLayer(int kind)
{
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
    {
        statusBar()->showMessage("Pick the layer the effect goes over.", 3000);
        return;
    }
    fla::Layer* target = timeline->layers[static_cast<size_t>(index)];
    if (!target || target->layerType == fla::Layer::Type::Folder)
        return;
    const fla::effects::Effect effect = fla::effects::defaults(static_cast<fla::effects::Kind>(kind));
    fla::Layer* layer = fla::effects::newEffectLayer(*timeline, *target, effect);
    _editContext.commandStack().push(fla::CommandPtr(new fla::AddLayerCommand(timeline, index, layer,
        std::string("Add ") + fla::effects::kindName(effect.kind))));
    _editContext.commandStack().breakMergeChain();
    if (_timelineView)
    {
        _timelineView->refresh();
        _timelineView->setCurrentLayer(layer);
    }
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    statusBar()->showMessage(QString("Draw the %1's shape on %2; it is softened and kept within %3.")
                                 .arg(QString(fla::effects::kindName(effect.kind)).toLower(),
                                     QString::fromStdString(layer->name), QString::fromStdString(target->name)), 6000);
}

void MainWindow::editEffect(fla::Layer* layer)
{
    if (!layer || !fla::effects::isEffect(*layer))
        return;
    fla::effects::Effect effect = fla::effects::effectOf(*layer);
    QDialog dialog(this);
    dialog.setWindowTitle(QString("Effect: %1").arg(QString::fromStdString(layer->name)));
    QFormLayout* form = new QFormLayout(&dialog);
    QComboBox* kind = new QComboBox(&dialog);
    kind->addItems({"Tone (multiplied)", "Highlight (screened)", "Glow (added)"});
    kind->setCurrentIndex(static_cast<int>(effect.kind));
    form->addRow("Effect:", kind);
    QPushButton* color = new QPushButton(&dialog);
    QColor chosen(effect.color[0], effect.color[1], effect.color[2]);
    const auto showColor = [color, &chosen]() {
        QPixmap swatch(28, 14);
        swatch.fill(chosen);
        color->setIcon(QIcon(swatch));
        color->setText(chosen.name().toUpper());
    };
    showColor();
    connect(color, &QPushButton::clicked, &dialog, [&dialog, &chosen, showColor]() {
        const QColor picked = QColorDialog::getColor(chosen, &dialog, "Effect Color");
        if (picked.isValid())
        {
            chosen = picked;
            showColor();
        }
    });
    form->addRow("Color:", color);
    QDoubleSpinBox* blur = new QDoubleSpinBox(&dialog);
    blur->setRange(0.0, 200.0);
    blur->setSuffix(" px");
    blur->setValue(effect.blur);
    form->addRow("Softness:", blur);
    QSpinBox* opacity = new QSpinBox(&dialog);
    opacity->setRange(0, 100);
    opacity->setSuffix(" %");
    opacity->setValue(static_cast<int>(std::lround(effect.opacity * 100.0)));
    form->addRow("Strength:", opacity);
    QCheckBox* clip = new QCheckBox("Keep within the layer under it", &dialog);
    clip->setChecked(effect.clip);
    form->addRow("", clip);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    effect.kind = static_cast<fla::effects::Kind>(kind->currentIndex());
    effect.color[0] = static_cast<uint8_t>(chosen.red());
    effect.color[1] = static_cast<uint8_t>(chosen.green());
    effect.color[2] = static_cast<uint8_t>(chosen.blue());
    effect.blur = blur->value();
    effect.opacity = opacity->value() / 100.0;
    effect.clip = clip->isChecked();
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
        layer->extraAttributes, layer->extraAttributes, fla::effects::withEffect(*layer, effect), "Effect Settings")));
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::importAnimatic()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::Timeline* timeline = _phoenixView && _phoenixView->scopeTimeline() ? _phoenixView->scopeTimeline()
                                                                           : document ? document->currentScene() : nullptr;
    if (!document || !timeline)
        return;
    const QString path = QFileDialog::getOpenFileName(this, "Import Animatic", _lastDirectory,
        "Edit lists (*.edl *.xml);;All files (*)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const double fps = document->frameRate > 0.0 ? document->frameRate : 24.0;
    std::string error;
    fla::editlist::EditList list = fla::editlist::parse(file.readAll().toStdString(), fps, &error);
    std::vector<fla::editlist::Event> shots;
    std::vector<fla::editlist::Event> sounds;
    // A list counted at another rate is brought to the document's.
    const double scale = list.frameRate > 0.0 ? fps / list.frameRate : 1.0;
    for (fla::editlist::Event event : list.events)
    {
        event.start = static_cast<int>(std::lround(event.start * scale));
        event.end = std::max(event.start + 1, static_cast<int>(std::lround(event.end * scale)));
        event.sourceIn = static_cast<int>(std::lround(event.sourceIn * scale));
        if (event.audio)
            sounds.push_back(event);
        else if (event.track == 1)
            shots.push_back(event);
    }
    if (shots.empty() && sounds.empty())
    {
        QMessageBox::warning(this, "Import Animatic", QString("Nothing to import from %1.\n\n%2")
                                                         .arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return;
    }
    std::sort(shots.begin(), shots.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    _lastDirectory = QFileInfo(path).absolutePath();

    // A clip's file: as the list names it, or by its name beside the list.
    const QDir beside = QFileInfo(path).absoluteDir();
    const auto fileOf = [&beside](const fla::editlist::Event& event, const QStringList& extensions) {
        QStringList tries;
        if (!event.file.empty())
            tries << QString::fromStdString(event.file) << beside.filePath(QFileInfo(QString::fromStdString(event.file)).fileName());
        const QString name = QString::fromStdString(event.name);
        tries << beside.filePath(name);
        for (const QString& extension : extensions)
            tries << beside.filePath(QString::fromStdString(fla::editlist::shotName(event)) + "." + extension);
        for (const QString& candidate : tries)
            if (!candidate.isEmpty() && QFileInfo(candidate).isFile())
                return candidate;
        return QString();
    };

    // Builds a layer of keyframes at the events, blank between them.
    const auto spanned = [](const std::string& name, const std::vector<std::pair<int, int>>& spans,
                             const std::function<void(fla::Frame*, size_t)>& fill) {
        fla::Layer* layer = new fla::Layer(nullptr);
        layer->name = name;
        int at = 0;
        for (size_t i = 0; i < spans.size(); ++i)
        {
            if (spans[i].first > at)
            {
                fla::Frame* gap = new fla::Frame(layer);
                gap->index = at;
                gap->duration = spans[i].first - at;
                layer->frames.push_back(gap);
            }
            fla::Frame* frame = new fla::Frame(layer);
            frame->index = std::max(at, spans[i].first);
            frame->duration = std::max(1, spans[i].second - frame->index);
            fill(frame, i);
            layer->frames.push_back(frame);
            at = frame->index + frame->duration;
        }
        return layer;
    };

    fla::CommandStack& stack = _editContext.commandStack();
    QStringList missing;
    int boards = 0;
    int placed = 0;
    stack.beginMacro("Import Animatic");

    if (!shots.empty())
    {
        std::vector<std::pair<int, int>> spans;
        for (const auto& shot : shots)
            spans.emplace_back(shot.start, shot.end);
        // The shots, labeled, for Export Shots by label.
        fla::Layer* labels = spanned("Shots", spans, [&shots](fla::Frame* frame, size_t i) {
            frame->name = fla::editlist::shotName(shots[i]);
            frame->labelType = "name";
        });
        // The boards, fitted to the stage.
        fla::Layer* board = spanned("Board", spans, [&](fla::Frame* frame, size_t i) {
            const QString image = fileOf(shots[i], {"png", "jpg", "jpeg"});
            if (image.isEmpty() || !MediaImport::isImage(image))
            {
                missing << QString("board for %1").arg(QString::fromStdString(fla::editlist::shotName(shots[i])));
                return;
            }
            QString imageError;
            fla::Bitmap* bitmap = MediaImport::loadImage(*document, image, imageError);
            if (!bitmap)
            {
                missing << imageError;
                return;
            }
            stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, "Import Animatic")));
            fla::BitmapInstance* instance = new fla::BitmapInstance(frame);
            instance->libraryItemName = bitmap->name;
            const double fit = std::min(document->width / std::max(1, bitmap->frameRight / 20),
                document->height / std::max(1, bitmap->frameBottom / 20));
            instance->transform.m11 = instance->transform.m22 = fit;
            instance->transform.tx = (document->width - bitmap->frameRight / 20.0 * fit) / 2.0;
            instance->transform.ty = (document->height - bitmap->frameBottom / 20.0 * fit) / 2.0;
            frame->elements.push_back(instance);
            ++boards;
        });
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, 0, labels, "Import Animatic")));
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, static_cast<int>(timeline->layers.size()), board,
            "Import Animatic")));
    }

    // Each audio track's clips, where the cut has them, from where they
    // start in their files.
    std::map<int, std::vector<fla::editlist::Event>> tracks;
    for (const auto& sound : sounds)
        tracks[sound.track].push_back(sound);
    for (auto& [track, events] : tracks)
    {
        std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        std::vector<std::pair<int, int>> spans;
        for (const auto& event : events)
            spans.emplace_back(event.start, event.end);
        fla::Layer* audio = spanned("Audio " + std::to_string(track), spans, [&](fla::Frame* frame, size_t i) {
            const QString sound = fileOf(events[i], {"wav", "mp3", "aif", "aiff", "ogg", "flac"});
            fla::media::Sound data;
            QString soundError;
            if (sound.isEmpty() || !MediaImport::loadSound(sound, data, soundError))
            {
                missing << QString("sound %1").arg(QString::fromStdString(events[i].name));
                return;
            }
            data.name = QFileInfo(sound).fileName().toStdString();
            data.sourcePath = sound.toStdString();
            auto* add = new fla::AddSoundCommand(document, data, "Import Animatic");
            const std::string name = add->soundName();
            stack.push(fla::CommandPtr(add));
            frame->extraAttributes.emplace_back("soundName", name);
            frame->extraAttributes.emplace_back("soundSync", "stream");
            if (events[i].sourceIn > 0)
                frame->extraAttributes.emplace_back("inPoint44",
                    std::to_string(static_cast<long long>(std::lround(events[i].sourceIn / fps * 44100.0))));
            ++placed;
        });
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, static_cast<int>(timeline->layers.size()), audio,
            "Import Animatic")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    if (_timelineView)
        _timelineView->refresh();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    const auto count = [](size_t n, const char* one, const char* many) { return QString("%1 %2").arg(n).arg(n == 1 ? one : many); };
    QString report = QString("%1 labeled, %2 placed, %3 placed.")
                         .arg(count(shots.size(), "shot", "shots"), count(static_cast<size_t>(boards), "board", "boards"),
                             count(static_cast<size_t>(placed), "sound", "sounds"));
    if (list.frameRate > 0.0 && std::abs(list.frameRate - fps) > 0.01)
        report += QString("\n\nThe cut was at %1 fps and was brought to the document's %2.").arg(list.frameRate, 0, 'f', 3).arg(fps, 0, 'f', 3);
    if (!missing.isEmpty())
        report += "\n\nNot found:\n" + missing.mid(0, 12).join("\n") + (missing.size() > 12 ? "\n..." : "");
    report += "\n\nFile > Export > Export Shots, by frame labels, splits it into shot files.";
    QMessageBox::information(this, "Import Animatic", report);
}

void MainWindow::deformSelection(int what)
{
    std::vector<fla::SymbolInstance*> instances;
    for (fla::Element* element : selectedElements())
        if (auto* instance = dynamic_cast<fla::SymbolInstance*>(element))
        {
            fla::Frame* frame = frameOfElement(instance);
            auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
            if (!layer || !layer->isLocked())
                instances.push_back(instance);
        }
    if (instances.empty() || !_phoenixView)
    {
        QMessageBox::information(this, "Deformer", "Select one or more symbol instances.");
        return;
    }
    static const char* const names[] = {"Remove Deformer", "Curve Deformer", "Envelope Deformer", "Reset Pose"};
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(names[what]);
    for (fla::SymbolInstance* instance : instances)
    {
        std::optional<fla::deform::Deformer> deformer;
        if (what == 1 || what == 2)
        {
            // Around the art as drawn.
            QRectF bounds = _phoenixView->symbolBounds(instance->symbol);
            if (!bounds.isValid() || bounds.isEmpty())
                bounds = QRectF(-50.0, -50.0, 100.0, 100.0);
            deformer = what == 1 ? fla::deform::curveAround(bounds.left(), bounds.top(), bounds.right(), bounds.bottom())
                                 : fla::deform::envelopeAround(bounds.left(), bounds.top(), bounds.right(), bounds.bottom());
        }
        else if (what == 3)
        {
            deformer = fla::deform::of(*instance);
            if (!deformer)
                continue;
            deformer->posed = deformer->rest;
        }
        stack.push(fla::deform::setDeformer(*instance, deformer, names[what]));
    }
    stack.endMacro();
    stack.breakMergeChain();
    // The handles are dragged with the Selection tool.
    if ((what == 1 || what == 2) && _toolbox)
        if (QAction* selection = _toolbox->tool("Selection"))
            selection->trigger();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::lipSync()
{
    auto* mouth = dynamic_cast<fla::SymbolInstance*>(_selection.single());
    fla::Frame* frame = mouth ? frameOfElement(mouth) : nullptr;
    auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
    if (!mouth || !layer || !_flaDocument || !_flaDocument->document)
    {
        QMessageBox::information(this, "Lip Sync",
            "Select the mouth: a graphic symbol whose frames are the mouth shapes, on a layer of its own.");
        return;
    }
    if (layer->isLocked())
        return;
    // The mouth chart kept on the symbol from last time.
    std::map<std::string, int> saved;
    if (mouth->symbol)
        for (const auto& [key, value] : mouth->symbol->extraAttributes)
            if (key == "phoenixMouthChart")
                saved = fla::lipsync::chartFrom(value);
    const double fps = _flaDocument->document->frameRate > 0.0 ? _flaDocument->document->frameRate : 24.0;
    LipSyncDialog dialog(QString::fromStdString(mouth->libraryItemName), saved, _player ? _player->currentFrame() : 0, fps, this);
    if (dialog.exec() != QDialog::Accepted || dialog.cues().empty())
        return;
    const std::map<std::string, int> chart = dialog.chart();
    fla::CommandPtr keys = fla::lipsync::apply(*layer, *mouth, dialog.cues(), chart, dialog.startFrame(), fps);
    if (!keys)
        return;
    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Lip Sync");
    if (fla::Symbol* symbol = mouth->symbol)
    {
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        Attributes after = symbol->extraAttributes;
        after.erase(std::remove_if(after.begin(), after.end(), [](const auto& pair) { return pair.first == "phoenixMouthChart"; }),
            after.end());
        after.emplace_back("phoenixMouthChart", fla::lipsync::chartText(chart));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(symbol->extraAttributes, symbol->extraAttributes, after, "Lip Sync")));
    }
    stack.push(std::move(keys));
    stack.endMacro();
    stack.breakMergeChain();
    if (_timelineView)
        _timelineView->refreshFrames();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    statusBar()->showMessage(QString("Keyed %1 mouth shapes.").arg(dialog.cues().size()), 4000);
}

void MainWindow::addPeg()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!document || !_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
    {
        statusBar()->showMessage("Pick the layer the peg is to move.", 3000);
        return;
    }
    fla::Layer* child = timeline->layers[static_cast<size_t>(index)];
    if (!child || child->layerType == fla::Layer::Type::Folder)
        return;
    // The pivot: the middle of the stage, which the peg can then be moved
    // from with the Free Transform tool's transformation point.
    const QRectF stage = _phoenixView ? _phoenixView->stageInScope() : QRectF(0, 0, document->width, document->height);
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Add Peg");
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);
    fla::Symbol* peg = fla::pegs::pegSymbol(*document->symbolList);
    if (!peg)
    {
        const std::string name = fla::library::nameIsFree(*document, "Peg") ? std::string("Peg")
                                                                             : fla::library::freeCopyName(*document, "Peg");
        peg = fla::pegs::newPegSymbol(name);
        stack.push(fla::CommandPtr(new fla::AddSymbolCommand(document, peg, "Add Peg")));
    }
    fla::Layer* pegLayer = fla::pegs::newPegLayer(*timeline, peg, stage.center().x(), stage.center().y());
    stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, index, pegLayer, "Add Peg")));
    setParentLayer(timeline, child, pegLayer);
    stack.endMacro();
    stack.breakMergeChain();
    if (_timelineView)
        _timelineView->refresh();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::addCamera()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::Timeline* scene = document ? document->currentScene() : nullptr;
    if (!scene)
        return;
    if (fla::camera::cameraOf(*scene))
    {
        statusBar()->showMessage("The scene has its camera already.", 3000);
        return;
    }
    int frames = 1;
    for (const fla::Layer* layer : scene->layers)
        if (layer)
            frames = std::max(frames, fla::timeline::layerEnd(*layer));
    fla::Layer* layer = fla::camera::newCameraLayer(*scene, frames, document->width, document->height);
    _editContext.commandStack().push(fla::CommandPtr(new fla::AddLayerCommand(scene, 0, layer, "Add Camera")));
    _editContext.commandStack().breakMergeChain();
    if (_timelineView)
        _timelineView->refresh();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
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
    const std::vector<int> picked = _timelineView ? _timelineView->selectedLayers(timeline) : std::vector<int>();
    if (timeline && !picked.empty())
        removeLayers(timeline, picked, picked.size() > 1 ? "Delete Layers" : "Delete Layer");
}

bool MainWindow::removeLayers(fla::Timeline* timeline, const std::vector<int>& indices, const char* name)
{
    // Animate keeps at least one layer that can hold frames.
    const std::vector<int> going = fla::layers::withContents(*timeline, indices);
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
        return false;
    }

    // Whatever was selected on the layers goes with them. Each layer whose
    // folder is not going too is removed with what it holds, from the
    // bottom up so the indices above stay put.
    _selection.clear();
    std::vector<int> roots;
    for (int index : going)
    {
        const fla::Layer* layer = timeline->layers[static_cast<size_t>(index)];
        const auto parent = std::find(timeline->layers.begin(), timeline->layers.end(), layer->parentLayer);
        const int parentIndex = parent == timeline->layers.end() ? -1 : static_cast<int>(parent - timeline->layers.begin());
        if (std::find(going.begin(), going.end(), parentIndex) == going.end())
            roots.push_back(index);
    }
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(name);
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        stack.push(fla::CommandPtr(new fla::RemoveLayerCommand(timeline, *it, name)));
    stack.endMacro();
    stack.breakMergeChain();
    _timelineView->refresh();
    onVisibilityChanged();
    return true;
}

void MainWindow::layerClipboard(int action)
{
    fla::Timeline* timeline = nullptr;
    std::vector<int> picked = _timelineView ? _timelineView->selectedLayers(timeline) : std::vector<int>();
    if (!timeline)
        timeline = editedTimeline();
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!timeline || !document)
        return;
    picked = fla::layers::withContents(*timeline, picked);

    // Copies of the layers go in above layer index, in its folder.
    const auto insert = [&](const std::vector<const fla::Layer*>& source, int index, const char* name, bool renamed) {
        std::vector<fla::Layer*> copies = fla::layers::cloneLayers(source, *timeline);
        fla::Layer* folder = nullptr;
        if (index >= 0 && index < static_cast<int>(timeline->layers.size()))
        {
            fla::Layer* at = timeline->layers[static_cast<size_t>(index)];
            if (at && at->parentLayer && at->parentLayer->layerType == fla::Layer::Type::Folder)
                folder = at->parentLayer;
        }
        fla::CommandStack& stack = _editContext.commandStack();
        stack.beginMacro(name);
        for (size_t i = 0; i < copies.size(); ++i)
        {
            const bool top = !copies[i]->parentLayer;
            if (top)
                copies[i]->parentLayer = folder;
            if (renamed && top)
            {
                copies[i]->name += " copy";
                copies[i]->autoNamed = false;
            }
            stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, std::max(0, index) + static_cast<int>(i), copies[i], name)));
        }
        stack.endMacro();
        stack.breakMergeChain();
        _timelineView->refresh();
        onVisibilityChanged();
    };

    std::vector<const fla::Layer*> source;
    for (int index : picked)
        source.push_back(timeline->layers[static_cast<size_t>(index)]);
    switch (action)
    {
    case 0:
    case 1:
    {
        if (source.empty())
            return;
        std::vector<fla::Layer*> copies = fla::layers::cloneLayers(source, *timeline);
        _copiedLayers.clear();
        for (fla::Layer* copy : copies)
            _copiedLayers.emplace_back(copy);
        _copiedLayersFrom = document;
        if (action == 0)
            removeLayers(timeline, picked, "Cut Layers");
        break;
    }
    case 2:
    {
        if (_copiedLayers.empty())
        {
            statusBar()->showMessage("Copy or cut layers first.", 3000);
            return;
        }
        if (_copiedLayersFrom != document)
        {
            statusBar()->showMessage("Layers paste into the document they were copied from.", 4000);
            return;
        }
        std::vector<const fla::Layer*> copied;
        for (const auto& layer : _copiedLayers)
            copied.push_back(layer.get());
        fla::Timeline* currentTimeline = nullptr;
        int current = 0;
        const int at = _timelineView->currentLayer(currentTimeline, current) && currentTimeline == timeline ? current : 0;
        insert(copied, at, "Paste Layers", false);
        break;
    }
    case 3:
        if (!source.empty())
            insert(source, picked.front(), "Duplicate Layers", true);
        break;
    default:
        break;
    }
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

    QAction* fromTemplateAction = new QAction("New from &Template...", this);
    fromTemplateAction->setStatusTip("Start a document from one saved as a template");
    connect(fromTemplateAction, &QAction::triggered, this, &MainWindow::newFromTemplate);
    fileMenu->insertAction(fileMenu->actions().value(1), fromTemplateAction);
    QAction* saveTemplateAction = fileMenu->addAction("Save as Te&mplate...");
    saveTemplateAction->setStatusTip("Keep this document as a template to start new ones from");
    connect(saveTemplateAction, &QAction::triggered, this, &MainWindow::saveAsTemplate);
    QAction* revertAction = fileMenu->addAction("Re&vert");
    revertAction->setStatusTip("Go back to the saved file, dropping the changes since");
    connect(revertAction, &QAction::triggered, this, &MainWindow::revertDocument);
    QAction* closeAction = fileMenu->addAction("&Close");
    closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    closeAction->setStatusTip("Close this document's tab");
    connect(closeAction, &QAction::triggered, this, [this]() {
        if (_activeTab >= 0)
            closeTab(_activeTab);
    });
    QAction* nextTabAction = new QAction(this);
    nextTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    connect(nextTabAction, &QAction::triggered, this, [this]() {
        if (_tabs.size() > 1)
            activateTab((_activeTab + 1) % static_cast<int>(_tabs.size()));
    });
    addAction(nextTabAction);

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
    QAction* importAnimaticAction = importMenu->addAction("Import &Animatic...");
    importAnimaticAction->setStatusTip("An animatic's cut from Storyboard Pro or an editor (EDL, Final Cut XML): its shots as labels, boards and audio");
    connect(importAnimaticAction, &QAction::triggered, this, &MainWindow::importAnimatic);
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
    QAction* exportShotsAction = new QAction("Export S&hots...", this);
    exportShotsAction->setStatusTip("Split the episode into shot files, by scene, frame label or camera cut");
    connect(exportShotsAction, &QAction::triggered, this, &MainWindow::exportShots);
    exportMenu->addAction(exportShotsAction);

    // The format Save As starts in for a new document: XFL folders suit a
    // studio keeping its files in version control.
    QMenu* formatMenu = fileMenu->addMenu("Default Save &Format");
    QActionGroup* formatGroup = new QActionGroup(this);
    const std::pair<const char*, const char*> formats[] = {
        {"phx", "Phoenix Document (.phx)"}, {"fla", "FLA File (.fla)"}, {"xfl", "XFL Folder (for version control)"}};
    const QString savedFormat = QSettings().value("save/defaultFormat", "phx").toString();
    for (const auto& [key, label] : formats)
    {
        QAction* choice = formatMenu->addAction(label);
        choice->setCheckable(true);
        choice->setChecked(savedFormat == key);
        formatGroup->addAction(choice);
        const QString value = key;
        connect(choice, &QAction::triggered, this, [value]() { QSettings().setValue("save/defaultFormat", value); });
    }

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
    QAction* checkAction = fileMenu->addAction("&Check Document...");
    checkAction->setStatusTip("Look for damage in the document and repair what can be repaired");
    connect(checkAction, &QAction::triggered, this, &MainWindow::checkDocument);
    QAction* compareAction = fileMenu->addAction("Compare &With...");
    compareAction->setStatusTip("What changed between this document and another version of it, part by part and frame by frame");
    connect(compareAction, &QAction::triggered, this, &MainWindow::compareWith);

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
    _redoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    connect(_redoAction, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addAction(_redoAction);

    _repeatAction = new QAction("Re&peat", this);
    _repeatAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y));
    _repeatAction->setStatusTip("Run the last Insert, Modify or Timeline command again on what is selected now");
    _repeatAction->setEnabled(false);
    connect(_repeatAction, &QAction::triggered, this, [this]() {
        if (_lastRepeatable && _lastRepeatable->isEnabled())
            _lastRepeatable->trigger();
    });
    editMenu->addAction(_repeatAction);

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

    editMenu->addSeparator();
    QAction* findAction = editMenu->addAction("&Find and Replace...");
    findAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    findAction->setStatusTip("Find text, a font, a color or a symbol anywhere in the document, and replace it");
    connect(findAction, &QAction::triggered, this, &MainWindow::showFindReplace);
    _fontMappingAction = editMenu->addAction("&Font Mapping...");
    _fontMappingAction->setStatusTip("Choose the installed fonts missing ones are shown in");
    connect(_fontMappingAction, &QAction::triggered, this, [this]() { fontMappingDialog(false); });
    QAction* spellingAction = editMenu->addAction("Check &Spelling...");
    spellingAction->setStatusTip("Check the spelling of every text field in the document");
    connect(spellingAction, &QAction::triggered, this, &MainWindow::checkSpelling);

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
    QAction* sceneAction = insertMenu->addAction("Sce&ne");
    sceneAction->setStatusTip("A new, empty scene after this one");
    connect(sceneAction, &QAction::triggered, this, [this]() {
        if (_scenes)
            _scenes->addScene(false);
    });
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
    timelineMenu->addSeparator();
    QAction* insertLayerAction = timelineMenu->addAction("&Layer");
    insertLayerAction->setStatusTip("A new layer above the current one");
    connect(insertLayerAction, &QAction::triggered, this, [this]() { addLayer(false); });
    QAction* insertFolderAction = timelineMenu->addAction("Layer F&older");
    insertFolderAction->setStatusTip("A new layer folder above the current layer");
    connect(insertFolderAction, &QAction::triggered, this, [this]() { addLayer(true); });
    insertMenu->addSeparator();
    QAction* classicTweenAction = insertMenu->addAction("&Classic Tween");
    classicTweenAction->setStatusTip("Tween the span to the next keyframe");
    connect(classicTweenAction, &QAction::triggered, this, [this]() { setClassicTween(true); });
    QAction* cameraAction = insertMenu->addAction("Ca&mera");
    cameraAction->setStatusTip("A multiplane camera for the scene: pan, zoom and turn it, with layers at depths");
    connect(cameraAction, &QAction::triggered, this, &MainWindow::addCamera);
    QAction* separateAction = insertMenu->addAction("Color &Art Layer");
    separateAction->setStatusTip("Move the current layer's fills to a color art layer under it; the bucket there paints within its lines");
    connect(separateAction, &QAction::triggered, this, &MainWindow::separateLineAndColor);
    QMenu* effectMenu = insertMenu->addMenu("&Effect Layer");
    effectMenu->setStatusTip("A tone, highlight or glow over the current layer: draw its shape on it");
    effectMenu->addAction("&Tone", this, [this]() { addEffectLayer(static_cast<int>(fla::effects::Kind::Tone)); });
    effectMenu->addAction("&Highlight", this, [this]() { addEffectLayer(static_cast<int>(fla::effects::Kind::Highlight)); });
    effectMenu->addAction("&Glow", this, [this]() { addEffectLayer(static_cast<int>(fla::effects::Kind::Glow)); });
    QAction* pegAction = insertMenu->addAction("&Peg");
    pegAction->setStatusTip("A peg above the current layer, which follows it: animate the peg to move, turn and scale the layer about its pivot");
    connect(pegAction, &QAction::triggered, this, &MainWindow::addPeg);
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
    editTimelineMenu->addAction("Cut La&yers", this, [this]() { layerClipboard(0); });
    editTimelineMenu->addAction("Cop&y Layers", this, [this]() { layerClipboard(1); });
    editTimelineMenu->addAction("Paste &Layers", this, [this]() { layerClipboard(2); });
    editTimelineMenu->addAction("&Duplicate Layers", this, [this]() { layerClipboard(3); });
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

    QAction* documentAction = modifyMenu->addAction("&Document...");
    documentAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    documentAction->setStatusTip("Stage size, Match Contents, scaling the content with the stage, ruler units");
    connect(documentAction, &QAction::triggered, this, &MainWindow::documentSettings);
    QAction* lipSyncAction = modifyMenu->addAction("&Lip Sync...");
    lipSyncAction->setStatusTip("Key the selected mouth graphic to a voice track, its mouth shapes from Rhubarb Lip Sync or a cue file");
    connect(lipSyncAction, &QAction::triggered, this, &MainWindow::lipSync);
    QMenu* deformMenu = modifyMenu->addMenu("Defo&rmer");
    QAction* curveDeformer = deformMenu->addAction("&Curve Deformer");
    curveDeformer->setStatusTip("Bend the selected symbols along a spine; drag its handles with the Selection tool");
    connect(curveDeformer, &QAction::triggered, this, [this]() { deformSelection(1); });
    QAction* envelopeDeformer = deformMenu->addAction("&Envelope Deformer");
    envelopeDeformer->setStatusTip("Pull the selected symbols by a grid of handles around them");
    connect(envelopeDeformer, &QAction::triggered, this, [this]() { deformSelection(2); });
    deformMenu->addSeparator();
    QAction* resetDeformer = deformMenu->addAction("&Reset Pose");
    resetDeformer->setStatusTip("Put the selected deformers' handles back where they were set");
    connect(resetDeformer, &QAction::triggered, this, [this]() { deformSelection(3); });
    QAction* removeDeformer = deformMenu->addAction("Re&move Deformer");
    connect(removeDeformer, &QAction::triggered, this, [this]() { deformSelection(0); });
    modifyMenu->addSeparator();

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
    QMenu* bitmapMenu = modifyMenu->addMenu("&Bitmap");
    QAction* swapBitmapAction = bitmapMenu->addAction("&Swap Bitmap...");
    swapBitmapAction->setStatusTip("Show another library bitmap in the selected bitmaps");
    connect(swapBitmapAction, &QAction::triggered, this, &MainWindow::swapBitmap);

    modifyMenu->addSeparator();

    // Modify > Transform, with Animate's keys.
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
    addTransform("Remove &Transform", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this]() { removeTransform(); });

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

    // Shape hints, on the first keyframe of a shape tween.
    QAction* addHintAction = shapeMenu->addAction("Add Shape &Hint");
    addHintAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    addHintAction->setStatusTip("A lettered hint to pin a point of a shape tween to where it should go");
    connect(addHintAction, &QAction::triggered, this, [this]() { editShapeHints(true); });
    QAction* removeHintsAction = shapeMenu->addAction("Remove All Hints");
    connect(removeHintsAction, &QAction::triggered, this, [this]() { editShapeHints(false); });
    shapeMenu->addSeparator();

    _smoothAction = new QAction("&Smooth", this);
    _smoothAction->setStatusTip("Soften the selected shapes' curves");
    connect(_smoothAction, &QAction::triggered, this, &MainWindow::smoothSelection);
    shapeMenu->addAction(_smoothAction);
    QAction* advancedSmoothAction = shapeMenu->addAction("Advanced S&mooth...");
    advancedSmoothAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_M));
    advancedSmoothAction->setStatusTip("Smooth the selected shapes' corners within angle limits, as strongly as you choose");
    connect(advancedSmoothAction, &QAction::triggered, this, &MainWindow::advancedSmooth);

    _straightenAction = new QAction("S&traighten", this);
    _straightenAction->setStatusTip("Replace the selected shapes' curves with straight segments");
    connect(_straightenAction, &QAction::triggered, this, &MainWindow::straightenSelection);
    shapeMenu->addAction(_straightenAction);
    QAction* advancedStraightenAction = shapeMenu->addAction("Advanced Straig&hten...");
    advancedStraightenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_N));
    advancedStraightenAction->setStatusTip("Straighten the selected shapes as strongly as you choose, and turn rough rectangles and ovals into true ones");
    connect(advancedStraightenAction, &QAction::triggered, this, &MainWindow::advancedStraighten);

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
    // What Repeat runs again: the last command chosen from these menus, by
    // menu or by key.
    const auto repeatable = [this](QMenu* menu) {
        const auto remember = [this](QAction* action) {
            if (!action || action->isCheckable() || action->menu() || action == _repeatAction)
                return;
            _lastRepeatable = action;
            _repeatAction->setText(QString("Re&peat %1").arg(action->text().remove('&').remove(QString::fromUtf8("\u2026")).remove("...")));
            _repeatAction->setEnabled(true);
        };
        connect(menu, &QMenu::triggered, this, remember);
        for (QAction* action : menu->actions())
            if (!action->menu() && !action->isSeparator())
                connect(action, &QAction::triggered, this, [remember, action]() { remember(action); });
        for (QMenu* sub : menu->findChildren<QMenu*>())
            for (QAction* action : sub->actions())
                if (!action->menu() && !action->isSeparator())
                    connect(action, &QAction::triggered, this, [remember, action]() { remember(action); });
    };
    repeatable(insertMenu);
    repeatable(modifyMenu);
    repeatable(editTimelineMenu);

    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* lightTableAction = viewMenu->addAction("&Light Table");
    lightTableAction->setCheckable(true);
    lightTableAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    lightTableAction->setStatusTip("Fade every layer but the one drawn on, as on a light table");
    connect(lightTableAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setLightTable(on);
    });
    QAction* cameraViewAction = viewMenu->addAction("&Camera View");
    cameraViewAction->setCheckable(true);
    cameraViewAction->setChecked(true);
    cameraViewAction->setStatusTip("See the scene through its camera, when it has one");
    connect(cameraViewAction, &QAction::toggled, this, [this](bool on) {
        if (_phoenixView)
            _phoenixView->setCameraView(on);
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
        _lastDirectory = QFileInfo(fileName).absolutePath();
        saveSettings();
        openInTab(fileName);
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

    // A new document is saved in the default format (File > Default Save
    // Format: a Phoenix document unless a studio asks for FLA or XFL
    // folders); one opened from a file is offered in its own format.
    const QString format = QSettings().value("save/defaultFormat", "phx").toString();
    const QString untitled = format == "fla" ? QString("untitled.fla") : format == "xfl" ? QString("untitled") : QString("untitled.phx");
    const QString suggested = _documentPath.isEmpty()
        ? (_lastDirectory.isEmpty() ? untitled : QDir(_lastDirectory).filePath(untitled))
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
    // -1 shows the current scene again, after the scenes themselves changed.
    const bool again = index < 0 && document && !document->timelines.empty();
    if (again)
        index = document->currentSceneIndex();
    if (!document || index < 0 || index >= static_cast<int>(document->timelines.size()) ||
        (index == document->currentSceneIndex() && !again))
    {
        return;
    }
    if (_phoenixView)
        _phoenixView->exitToDepth(0);
    _selection.clear();
    document->setCurrentScene(index);
    _shownScene = document->currentScene();
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
    if (_scenes && !again)
        _scenes->refresh();
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

void MainWindow::checkDocument()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    fla::doctor::Report report;
    // The file as saved, when it is: its parts, as they are on disk.
    if (!_documentPath.isEmpty() && QFileInfo(_documentPath).isFile() && !_editContext.isModified() &&
        !_documentPath.endsWith(".phx", Qt::CaseInsensitive))
        fla::doctor::checkContainer(_documentPath.toStdString(), report);
    fla::doctor::checkDocument(*_flaDocument->document, report, false);

    QDialog dialog(this);
    dialog.setWindowTitle("Check Document");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* summary = new QLabel(&dialog);
    layout->addWidget(summary);
    QTreeWidget* list = new QTreeWidget(&dialog);
    list->setHeaderLabels({"", "Where", "What"});
    list->setRootIsDecorated(false);
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    list->header()->resizeSection(1, 220);
    list->setMinimumSize(640, 300);
    layout->addWidget(list, 1);
    const auto fill = [list, summary](const fla::doctor::Report& shown) {
        list->clear();
        for (const fla::doctor::Issue& issue : shown.issues)
        {
            QTreeWidgetItem* item = new QTreeWidgetItem(list);
            const bool error = issue.severity == fla::doctor::Issue::Severity::Error;
            item->setText(0, error ? "Error" : issue.severity == fla::doctor::Issue::Severity::Warning ? "Warning" : "Note");
            item->setText(1, QString::fromStdString(issue.where));
            item->setText(2, QString::fromStdString(issue.what) + (issue.repaired ? "  (repaired)" : issue.repairable ? "  (can be repaired)" : ""));
            item->setToolTip(2, item->text(2));
        }
        summary->setText(shown.issues.empty() ? QString("No problems found.")
                                              : QString("%1 errors, %2 warnings.")
                                                    .arg(shown.count(fla::doctor::Issue::Severity::Error))
                                                    .arg(shown.count(fla::doctor::Issue::Severity::Warning)));
    };
    fill(report);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* repair = buttons->addButton("Repair", QDialogButtonBox::ActionRole);
    repair->setEnabled(std::any_of(report.issues.begin(), report.issues.end(),
        [](const fla::doctor::Issue& issue) { return issue.repairable && !issue.repaired; }));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(repair, &QPushButton::clicked, this, [this, &fill, repair]() {
        // The repair reshapes the document under the history, which is
        // let go; the document is unsaved until saved.
        if (_phoenixView)
            _phoenixView->exitToDepth(0);
        _selection.clear();
        fla::doctor::Report repaired;
        fla::doctor::checkDocument(*_flaDocument->document, repaired, true);
        _editContext.commandStack().clear();
        _editContext.commandStack().setDirty();
        fill(repaired);
        repair->setEnabled(false);
        if (_timelineView)
            _timelineView->setDocument(_flaDocument);
        if (_libraryPanel)
            _libraryPanel->refresh();
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        updateEditState();
    });
    dialog.exec();
}

void MainWindow::linkToMaster(const QString& name)
{
    if (!_flaDocument || !_flaDocument->document || !_flaDocument->document->symbolList)
        return;
    fla::Symbol* symbol = _flaDocument->document->symbolList->symbolMap.count(name.toStdString())
        ? _flaDocument->document->symbolList->symbolMap[name.toStdString()] : nullptr;
    if (!symbol)
        return;
    const QString start = _documentPath.isEmpty() ? _lastDirectory : QFileInfo(_documentPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, "Master File", start, "Documents (*.fla *.phx *.xfl);;All files (*)");
    if (path.isEmpty())
        return;
    std::string error;
    std::unique_ptr<fla::FLADocument> master = commands::load(QFileInfo(path).suffix() == "xfl" ? QFileInfo(path).absolutePath().toStdString() : path.toStdString(), error);
    if (!master || !master->document->symbolList)
    {
        QMessageBox::warning(this, "Link to Master File", QString("Could not read %1.\n\n%2").arg(path, QString::fromStdString(error)));
        return;
    }
    // Which of the master's symbols: the one of the same name, by default.
    QStringList names;
    for (const fla::Symbol* item : master->document->symbolList->symbols)
        if (item)
            names << QString::fromStdString(item->name);
    names.sort(Qt::CaseInsensitive);
    bool ok = false;
    const QString item = QInputDialog::getItem(this, "Link to Master File", "Master symbol:", names,
        std::max(0, static_cast<int>(names.indexOf(name))), false, &ok);
    if (!ok || item.isEmpty())
        return;
    // Kept relative to this file when it has been saved, so a show folder
    // can move as a whole.
    fla::links::Link link;
    link.master = _documentPath.isEmpty() ? QDir::fromNativeSeparators(path).toStdString()
                                          : QDir(QFileInfo(_documentPath).absolutePath()).relativeFilePath(path).toStdString();
    link.item = item.toStdString();
    fla::Symbol probe(nullptr);
    probe.extraAttributes = symbol->extraAttributes;
    fla::links::setLink(probe, link);
    _editContext.commandStack().push(fla::CommandPtr(
        new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
            symbol->extraAttributes, symbol->extraAttributes, probe.extraAttributes, "Link to Master")));
    if (_libraryPanel)
        _libraryPanel->refresh();
    if (QMessageBox::question(this, "Link to Master File", QString("Update %1 from the master now?").arg(name)) == QMessageBox::Yes)
        updateLinked({name});
}

void MainWindow::updateLinked(const QStringList& requested)
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    fla::Document& target = *_flaDocument->document;
    QStringList names = requested;
    if (names.isEmpty())
        for (const std::string& name : fla::links::linkedSymbols(target))
            names << QString::fromStdString(name);
    if (names.isEmpty())
        return;
    if (QMessageBox::question(this, "Update from Master",
            QString("Replace %1 with the master's version? This cannot be undone.")
                .arg(names.size() == 1 ? names.front() : QString("%1 linked symbols").arg(names.size()))) != QMessageBox::Yes)
        return;
    if (_phoenixView)
        _phoenixView->exitToDepth(0);
    _selection.clear();
    std::map<std::string, std::unique_ptr<fla::FLADocument>> masters;
    QStringList report;

    for (const QString& name : names)
    {
        fla::Symbol* symbol = target.symbolList ? target.symbolList->symbolMap[name.toStdString()] : nullptr;
        fla::links::Link link;
        if (!symbol || !fla::links::linkOf(*symbol, link))
            continue;
        const std::string masterPath = fla::links::resolve(link.master, _documentPath.toStdString());
        std::string error;
        std::unique_ptr<fla::FLADocument>& master = masters[masterPath];
        if (!master)
            master = commands::load(masterPath, error);
        if (!master)
        {
            report << QString("%1: could not read %2").arg(name, QString::fromStdString(masterPath));
            continue;
        }
        const std::vector<std::string> changed = fla::links::update(target, name.toStdString(), *master->document, link.item, error);
        if (changed.empty())
            report << QString("%1: %2").arg(name, QString::fromStdString(error));
        else
        {

            report << QString("%1: updated, with %2 library items").arg(name).arg(changed.size());
        }
    }
    // The library changed under the history, which is let go.
    _editContext.commandStack().clear();
    _editContext.commandStack().setDirty();
    if (_libraryPanel)
        _libraryPanel->refresh();
    if (_timelineView)
        _timelineView->setDocument(_flaDocument);
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    updateEditState();
    QMessageBox::information(this, "Update from Master", report.join("\n"));
}

void MainWindow::exportShots()
{
    if (!_flaDocument || _documentPath.isEmpty() || !QFileInfo::exists(_documentPath))
    {
        QMessageBox::information(this, "Export Shots", "Save the episode first; the shots are cut from its file.");
        return;
    }
    if (_editContext.isModified() &&
        QMessageBox::question(this, "Export Shots",
            "The episode has unsaved changes; the file as last saved is split. Go on?") != QMessageBox::Yes)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle("Export Shots");
    QFormLayout* form = new QFormLayout(&dialog);
    QComboBox* by = new QComboBox(&dialog);
    by->addItem("Frame labels", "label");
    by->addItem("Scenes", "scene");
    by->addItem("Camera cuts", "camera");
    by->setToolTip("Labels: each named frame label starts a shot. Camera cuts: each camera keyframe it jumps to.");
    form->addRow("Cut at:", by);
    QComboBox* format = new QComboBox(&dialog);
    format->addItem("FLA files", "fla");
    format->addItem("Phoenix documents", "phx");
    format->addItem("XFL folders", "xfl");
    form->addRow("Save as:", format);
    QHBoxLayout* folderRow = new QHBoxLayout();
    QLineEdit* folder = new QLineEdit(QFileInfo(_documentPath).absoluteDir().filePath(QFileInfo(_documentPath).completeBaseName() + "_shots"), &dialog);
    QPushButton* browse = new QPushButton("Browse...", &dialog);
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    form->addRow("Folder:", folderRow);
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, folder]() {
        const QString chosen = QFileDialog::getExistingDirectory(&dialog, "Shots Folder", folder->text());
        if (!chosen.isEmpty())
            folder->setText(chosen);
    });
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(520, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    QProcess process;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    process.start(QCoreApplication::applicationFilePath(), {"shots", _documentPath, "--by", by->currentData().toString(),
        "--format", format->currentData().toString(), "--out", folder->text()});
    process.waitForFinished(-1);
    QApplication::restoreOverrideCursor();
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()) +
                           QString::fromLocal8Bit(process.readAllStandardError());
    if (process.exitCode() != 0)
        QMessageBox::warning(this, "Export Shots", output);
    else
        QMessageBox::information(this, "Export Shots", output);
}

void MainWindow::compareWith()
{
    if (!_flaDocument || _documentPath.isEmpty() || !QFileInfo::exists(_documentPath))
    {
        QMessageBox::information(this, "Compare With", "Save the document first; its file is what is compared.");
        return;
    }
    if (_editContext.isModified() &&
        QMessageBox::question(this, "Compare With",
            "The document has unsaved changes; the file as last saved is compared. Go on?") != QMessageBox::Yes)
        return;
    const QString other = QFileDialog::getOpenFileName(this, "Compare With", QFileInfo(_documentPath).absolutePath(),
        "Documents (*.fla *.phx);;All files (*)");
    if (other.isEmpty())
        return;

    // The diff command, run apart so neither document disturbs the open one.
    QTemporaryDir folder;
    QProcess process;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    process.start(QCoreApplication::applicationFilePath(), {"diff", other, _documentPath, "--out", folder.path()});
    process.waitForFinished(-1);
    QApplication::restoreOverrideCursor();
    const QString text = QString::fromLocal8Bit(process.readAllStandardOutput()) +
                         QString::fromLocal8Bit(process.readAllStandardError());

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Compare: %1 → %2").arg(QFileInfo(other).fileName(), QFileInfo(_documentPath).fileName()));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QPlainTextEdit* report = new QPlainTextEdit(text, &dialog);
    report->setReadOnly(true);
    report->setMaximumHeight(180);
    layout->addWidget(report);
    // Each frame that draws differently: before, after, and the changes in red.
    QScrollArea* scroll = new QScrollArea(&dialog);
    QWidget* strips = new QWidget(scroll);
    QVBoxLayout* stripLayout = new QVBoxLayout(strips);
    for (const QFileInfo& image : QDir(folder.path()).entryInfoList({"frame*.png"}, QDir::Files, QDir::Name))
    {
        QLabel* caption = new QLabel(QString("Frame %1: before, after, changes").arg(image.baseName().mid(5).toInt()), strips);
        QLabel* picture = new QLabel(strips);
        picture->setPixmap(QPixmap(image.absoluteFilePath()).scaledToWidth(900, Qt::SmoothTransformation));
        stripLayout->addWidget(caption);
        stripLayout->addWidget(picture);
    }
    stripLayout->addStretch(1);
    scroll->setWidget(strips);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll, 1);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(960, 760);
    dialog.exec();
}

int MainWindow::renderCommand(const QString& source, const QStringList& options)
{
    // Phoenix render <document> [--range 1-240] [--out frames/|movie.mp4|anim.gif]
    // [--size 1920x1080] [--transparent] [--no-camera] [--per-layer] [--no-audio]
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
    const int count = std::max(1, _phoenixView->frameCount());
    MovieExportSettings settings;
    settings.format = MovieExportSettings::Format::PngSequence;
    settings.size = QSize(static_cast<int>(std::lround(document->width)), static_cast<int>(std::lround(document->height)));
    settings.first = 0;
    settings.last = count - 1;
    const QString base = QFileInfo(source).completeBaseName();
    QString out = QFileInfo(source).absoluteDir().filePath(base + "_frames");
    for (int i = 0; i < options.size(); ++i)
    {
        const QString& name = options.at(i);
        const QString value = i + 1 < options.size() ? options.at(i + 1) : QString();
        if (name == "--range" && !value.isEmpty())
        {
            // 1-based and inclusive, as the timeline counts: "1-240", "12".
            const QStringList parts = value.split('-');
            settings.first = std::clamp(parts.value(0).toInt() - 1, 0, count - 1);
            settings.last = std::clamp(parts.size() > 1 ? parts.value(1).toInt() - 1 : settings.first, settings.first, count - 1);
            ++i;
        }
        else if (name == "--out" && !value.isEmpty())
        {
            out = value;
            ++i;
        }
        else if (name == "--size" && !value.isEmpty())
        {
            const QStringList parts = value.split('x');
            if (parts.size() == 2)
                settings.size = QSize(std::max(1, parts.at(0).toInt()), std::max(1, parts.at(1).toInt()));
            ++i;
        }
        else if (name == "--transparent")
            settings.transparent = true;
        else if (name == "--no-camera")
            settings.camera = false;
        else if (name == "--no-audio")
            settings.audio = false;
        else if (name == "--per-layer")
            settings.format = MovieExportSettings::Format::LayerSequences;
        else if (name == "--passes")
            settings.format = MovieExportSettings::Format::Passes;
        else if (name == "--exr")
            settings.format = MovieExportSettings::Format::Exr;
        else
        {
            std::fprintf(stderr, "Unknown option %s\n", qPrintable(name));
            return 1;
        }
    }
    // A folder gets a numbered PNG sequence; a file name says its format.
    const QString suffix = QFileInfo(out).suffix().toLower();
    if (settings.format == MovieExportSettings::Format::Passes || settings.format == MovieExportSettings::Format::Exr)
    {
        QDir().mkpath(out);
        out = QDir(out).filePath(base + ".png");
    }
    else if (suffix == "mp4" || suffix == "mov")
        settings.format = MovieExportSettings::Format::Video;
    else if (suffix == "gif")
        settings.format = MovieExportSettings::Format::Gif;
    else if (suffix != "png")
    {
        QDir().mkpath(out);
        out = QDir(out).filePath(base + ".png");
    }
    else
    {
        QDir().mkpath(QFileInfo(out).absolutePath());
    }
    settings.path = QFileInfo(out).absoluteFilePath();
    QString error;
    if (!MovieExport::run(settings, *_phoenixView, _timelineAudio, document->frameRate, nullptr, error))
    {
        std::fprintf(stderr, "%s\n", qPrintable(error.isEmpty() ? QString("Render stopped.") : error));
        return 1;
    }
    std::printf("Rendered frames %d-%d of %s to %s\n", settings.first + 1, settings.last + 1, qPrintable(source),
        qPrintable(settings.path));
    return 0;
}

std::vector<QImage> MainWindow::renderSamples(const QString& path, std::vector<int>& frames, QSize& size, int samples)
{
    std::vector<QImage> images;
    loadFLAFile(path);
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
        return images;
    const fla::Document* document = _flaDocument->document;
    if (size.isEmpty())
    {
        size = QSize(std::max(1, document->width), std::max(1, document->height));
        if (size.width() > 960)
            size = QSize(960, std::max(1, size.height() * 960 / size.width()));
    }
    if (frames.empty())
    {
        const int count = std::max(1, _phoenixView->frameCount());
        const int wanted = std::max(1, std::min(samples, count));
        for (int i = 0; i < wanted; ++i)
        {
            const int frame = wanted == 1 ? 0 : static_cast<int>(std::lround(i * (count - 1.0) / (wanted - 1.0)));
            if (frames.empty() || frames.back() != frame)
                frames.push_back(frame);
        }
    }
    for (int frame : frames)
        images.push_back(_phoenixView->renderFrame(frame, size, false).convertToFormat(QImage::Format_ARGB32));
    return images;
}

bool MainWindow::benchmarkRender(const QString& path, int maxFrames, double& openMs, std::vector<double>& coldMs,
    std::vector<double>& warmMs, QSize& size)
{
    QElapsedTimer timer;
    timer.start();
    loadFLAFile(path);
    openMs = timer.nsecsElapsed() / 1.0e6;
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
        return false;
    const fla::Document* document = _flaDocument->document;
    size = QSize(std::max(1, document->width), std::max(1, document->height));
    const int frames = std::max(1, std::min(maxFrames, _phoenixView->frameCount()));
    _phoenixView->clearCaches();
    for (std::vector<double>* times : {&coldMs, &warmMs})
        for (int frame = 0; frame < frames; ++frame)
        {
            timer.restart();
            const QImage image = _phoenixView->renderFrame(frame, size, false);
            times->push_back(timer.nsecsElapsed() / 1.0e6);
        }
    return true;
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

void MainWindow::showFindReplace()
{
    if (!_findReplace)
    {
        _findReplace = new FindReplaceDialog(
            [this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; }, this);
        connect(_findReplace, &FindReplaceDialog::replaceRequested, this, [this](const fla::find::Query& query) {
            fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            if (!document)
                return;
            int count = 0;
            fla::CommandPtr command = fla::find::replaceAll(*document, query, &count);
            if (!command)
            {
                statusBar()->showMessage(query.kind == fla::find::Kind::Symbol && query.find == query.replace
                                             ? QString("Pick another symbol to replace it with.")
                                             : QString("Nothing to replace."),
                    3000);
                return;
            }
            _editContext.commandStack().push(std::move(command));
            _editContext.commandStack().breakMergeChain();
            statusBar()->showMessage(QString("Replaced %1.").arg(count), 3000);
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
            if (_timelineView)
                _timelineView->refreshFrames();
            if (_propertiesPanel)
                _propertiesPanel->refresh();
        });
        connect(_findReplace, &FindReplaceDialog::goToRequested, this, [this](const fla::find::Match& match) {
            fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            if (!document || !_phoenixView)
                return;
            // On its scene, or inside its symbol, at its frame, selected.
            _phoenixView->exitToDepth(0);
            if (match.symbol)
                _phoenixView->editSymbol(match.symbol);
            else
                goToScene(match.scene);
            if (_player)
                _player->setCurrentFrame(match.frame);
            _selection.clear();
            _selection.select(match.topElement);
            _phoenixView->clearCaches();
            _phoenixView->update();
            updateSelectionState();
        });
    }
    _findReplace->refresh();
    _findReplace->show();
    _findReplace->raise();
    _findReplace->activateWindow();
}

void MainWindow::refreshHistory()
{
    if (!_history || !_historyDock || !_historyDock->isVisible())
        return;
    const fla::CommandStack& stack = _editContext.commandStack();
    _history->clear();
    _history->addItem(_documentName.isEmpty() ? QString("Opened") : QString("Opened %1").arg(_documentName));
    for (size_t i = 0; i < stack.count(); ++i)
        _history->addItem(QString::fromStdString(stack.nameAt(i)));
    // Steps undone are grey until they are done again or replaced.
    for (int row = 0; row < _history->count(); ++row)
    {
        QListWidgetItem* item = _history->item(row);
        if (static_cast<size_t>(row) > stack.index())
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    }
    _history->setCurrentRow(static_cast<int>(stack.index()));
    _history->scrollToItem(_history->currentItem());
}

void MainWindow::restoreLayout()
{
    // The docks exist now: the layout as made is the default, and the one
    // kept from last time comes back.
    if (_layoutRestored)
        return;
    _layoutRestored = true;
    _defaultLayout = saveState(kLayoutVersion);
    const QByteArray kept = QSettings().value("window/state").toByteArray();
    if (!kept.isEmpty())
        restoreState(kept, kLayoutVersion);
    keepOnScreen();
}

namespace {

/// The workspaces Phoenix comes with, as Animate's.
const QStringList kBuiltInWorkspaces = {"Essentials", "Animator", "Designer", "Classic"};

QString workspaceKey(const QString& name)
{
    return "workspaces/" + QString::fromUtf8(QUrl::toPercentEncoding(name));
}

} // namespace

QJsonObject MainWindow::currentWorkspace() const
{
    QJsonObject workspace;
    workspace["phoenixWorkspace"] = 1;
    workspace["layoutVersion"] = kLayoutVersion;
    workspace["docks"] = QString::fromLatin1(saveState(kLayoutVersion).toBase64());
    if (_toolbox)
        workspace["tools"] = _toolbox->layout().toJson();
    if (_timelineView)
        workspace["layersWidth"] = _timelineView->layersWidth();
    return workspace;
}

bool MainWindow::applyWorkspace(const QJsonObject& workspace)
{
    // A workspace from a newer Phoenix may name panels this one does not
    // have; Qt passes over those. One whose dock layout cannot be read
    // keeps the docks as they are and still takes the rest.
    bool docked = false;
    const QByteArray window = saveGeometry();
    if (workspace["layoutVersion"].toInt() == kLayoutVersion)
        docked = restoreState(QByteArray::fromBase64(workspace["docks"].toString().toLatin1()), kLayoutVersion);
    if (_toolbox && workspace["tools"].isObject())
        _toolbox->setLayout(Toolbox::Layout::fromJson(workspace["tools"].toObject()));
    if (_timelineView && workspace["layersWidth"].toInt() > 0)
        _timelineView->setLayersWidth(workspace["layersWidth"].toInt());
    _hiddenByF4.clear();
    restoreGeometry(window);
    keepOnScreen();
    return docked;
}

void MainWindow::applyBuiltInWorkspace(const QString& name)
{
    // Each starts from Phoenix's own layout, in the window as it is.
    const QByteArray window = saveGeometry();
    restoreState(_defaultLayout, kLayoutVersion);
    _hiddenByF4.clear();
    QDockWidget* properties = findChild<QDockWidget*>("propertiesDock");
    QDockWidget* library = findChild<QDockWidget*>("libraryDock");
    Toolbox::Layout tools = _toolbox->defaultLayout();
    if (name == "Animator")
    {
        // A tall timeline with the Motion Editor beside it, the Frame
        // Picker in the panels and the tools in two columns.
        tools.columns = 2;
        _motionEditorDock->show();
        _layerDepthDock->show();
        _framePickerDock->show();
        _timelineDock->raise();
        properties->raise();
        resizeDocks({_timelineDock}, {height() / 2}, Qt::Vertical);
    }
    else if (name == "Designer")
    {
        // Color and the arranging panels in a group under Properties, a
        // short timeline, the tools in two columns.
        tools.columns = 2;
        // Out of the group they start in: added to the right side, a dock
        // goes under what is there (a split onto a tabbed dock would only
        // tab it).
        for (QDockWidget* dock : {_colorDock, _swatchesDock, _alignDock, _transformDock, _infoDock})
            removeDockWidget(dock);
        addDockWidget(Qt::RightDockWidgetArea, _colorDock, Qt::Vertical);
        for (QDockWidget* dock : {_swatchesDock, _alignDock, _transformDock, _infoDock})
            tabifyDockWidget(_colorDock, dock);
        for (QDockWidget* dock : {_colorDock, _swatchesDock, _alignDock, _transformDock, _infoDock})
            dock->show();
        _colorDock->raise();
        properties->raise();
        resizeDocks({_timelineDock}, {170}, Qt::Vertical);
    }
    else if (name == "Classic")
    {
        // Flash's classic arrangement: the timeline over the stage, the
        // tools down the right beside Properties and the Library.
        addDockWidget(Qt::TopDockWidgetArea, _timelineDock);
        removeDockWidget(_toolsDock);
        addDockWidget(Qt::RightDockWidgetArea, _toolsDock, Qt::Horizontal);
        _toolsDock->show();
        library->raise();
        properties->raise();
        resizeDocks({_timelineDock}, {220}, Qt::Vertical);
    }
    _toolbox->setLayout(tools);
    resizeDocks({_toolsDock}, {_toolbox->widthFor(tools.columns)}, Qt::Horizontal);
    _timelineView->setLayersWidth(250);
    restoreGeometry(window);
}

void MainWindow::useWorkspace(const QString& name)
{
    const QByteArray saved = QSettings().value(workspaceKey(name)).toByteArray();
    const QJsonObject workspace = QJsonDocument::fromJson(saved).object();
    if (!workspace.isEmpty())
        applyWorkspace(workspace);
    else if (kBuiltInWorkspaces.contains(name))
        applyBuiltInWorkspace(name);
    else
        return;
    QSettings().setValue("workspace/current", name);
}

void MainWindow::saveWorkspaceAs(const QString& name)
{
    QSettings().setValue(workspaceKey(name), QJsonDocument(currentWorkspace()).toJson(QJsonDocument::Compact));
    QSettings().setValue("workspace/current", name);
    statusBar()->showMessage(QString("Saved the workspace %1.").arg(name), 3000);
}

QStringList MainWindow::savedWorkspaces() const
{
    // The built-in ones, then the user's own, a built-in one kept after
    // it was changed and saved under its own name counting once.
    QSettings settings;
    settings.beginGroup("workspaces");
    QStringList own;
    for (const QString& key : settings.childKeys())
    {
        const QString name = QUrl::fromPercentEncoding(key.toUtf8());
        if (!kBuiltInWorkspaces.contains(name))
            own << name;
    }
    settings.endGroup();
    own.sort(Qt::CaseInsensitive);
    return kBuiltInWorkspaces + own;
}

void MainWindow::keepOnScreen()
{
    // A panel left floating on a screen that is gone comes back to the
    // middle of the primary one.
    const QList<QScreen*> screens = QGuiApplication::screens();
    const auto visible = [&screens](const QRect& rect) {
        for (const QScreen* screen : screens)
            if (screen->availableGeometry().intersected(rect).width() >= 40 &&
                screen->availableGeometry().intersected(rect).height() >= 20)
                return true;
        return false;
    };
    const QRect primary = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry()
                                                            : QRect(0, 0, 1280, 720);
    for (QDockWidget* dock : findChildren<QDockWidget*>())
    {
        if (!dock->isFloating() || visible(dock->frameGeometry()))
            continue;
        QRect rect = dock->frameGeometry();
        rect.setSize(rect.size().boundedTo(primary.size()));
        rect.moveCenter(primary.center());
        dock->setGeometry(rect);
    }
    if (!visible(frameGeometry()))
    {
        QRect rect = geometry();
        rect.setSize(rect.size().boundedTo(primary.size()));
        rect.moveCenter(primary.center());
        setGeometry(rect);
    }
}

void MainWindow::fillWorkspaceMenu(QMenu* menu)
{
    menu->clear();
    const QString current = QSettings().value("workspace/current", "Essentials").toString();
    const QStringList names = savedWorkspaces();
    for (const QString& name : names)
    {
        if (name == names.value(kBuiltInWorkspaces.size()) && names.size() > kBuiltInWorkspaces.size())
            menu->addSeparator();
        QAction* use = menu->addAction(name, this, [this, name]() { useWorkspace(name); });
        use->setCheckable(true);
        use->setChecked(name == current);
    }
    menu->addSeparator();

    menu->addAction("&New Workspace...", this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Workspace", "Save the current layout as:",
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        if (savedWorkspaces().contains(name) &&
            QMessageBox::question(this, "New Workspace", QString("Replace the workspace %1?").arg(name)) != QMessageBox::Yes)
            return;
        saveWorkspaceAs(name);
    });
    menu->addAction(QString("&Save Workspace \"%1\"").arg(current), this, [this, current]() { saveWorkspaceAs(current); });
    menu->addAction(QString("&Reset \"%1\"").arg(current), this, [this, current]() {
        // Back to how it was saved; a built-in one never saved over goes
        // back to how Phoenix sets it up.
        useWorkspace(current);
    });

    QStringList own = savedWorkspaces().mid(kBuiltInWorkspaces.size());
    QMenu* rename = menu->addMenu("Re&name Workspace");
    QMenu* remove = menu->addMenu("&Delete Workspace");
    rename->setEnabled(!own.isEmpty());
    remove->setEnabled(!own.isEmpty());
    for (const QString& name : own)
    {
        rename->addAction(name, this, [this, name]() {
            bool ok = false;
            const QString to = QInputDialog::getText(this, "Rename Workspace", "New name:", QLineEdit::Normal, name, &ok).trimmed();
            if (!ok || to.isEmpty() || to == name)
                return;
            if (savedWorkspaces().contains(to))
            {
                QMessageBox::information(this, "Rename Workspace", QString("There is already a workspace called %1.").arg(to));
                return;
            }
            QSettings settings;
            settings.setValue(workspaceKey(to), settings.value(workspaceKey(name)));
            settings.remove(workspaceKey(name));
            if (settings.value("workspace/current").toString() == name)
                settings.setValue("workspace/current", to);
        });
        remove->addAction(name, this, [this, name]() {
            if (QMessageBox::question(this, "Delete Workspace", QString("Delete the workspace %1?").arg(name)) != QMessageBox::Yes)
                return;
            QSettings settings;
            settings.remove(workspaceKey(name));
            if (settings.value("workspace/current").toString() == name)
                settings.setValue("workspace/current", "Essentials");
        });
    }
    menu->addSeparator();

    menu->addAction("&Import Workspace...", this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, "Import Workspace", _lastDirectory,
            "Phoenix workspaces (*.phxworkspace);;All files (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        const QJsonObject workspace =
            file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        if (!workspace.contains("phoenixWorkspace"))
        {
            QMessageBox::warning(this, "Import Workspace", "That file is not a Phoenix workspace.");
            return;
        }
        QString name = workspace["name"].toString().trimmed();
        if (name.isEmpty())
            name = QFileInfo(path).completeBaseName();
        // Never over one already here.
        const QString base = name;
        for (int n = 2; savedWorkspaces().contains(name); ++n)
            name = QString("%1 %2").arg(base).arg(n);
        if (!applyWorkspace(workspace))
            statusBar()->showMessage("The panels' arrangement in that workspace could not be used; the rest was.", 5000);
        saveWorkspaceAs(name);
    });
    menu->addAction("&Export Workspace...", this, [this, current]() {
        const QString path = QFileDialog::getSaveFileName(this, "Export Workspace",
            QDir(_lastDirectory).filePath(current + ".phxworkspace"), "Phoenix workspaces (*.phxworkspace)");
        if (path.isEmpty())
            return;
        QJsonObject workspace = currentWorkspace();
        workspace["name"] = current;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(workspace).toJson()) < 0)
            QMessageBox::warning(this, "Export Workspace", QString("Could not write %1.").arg(path));
    });
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
    if (_xsheet)
        _xsheet->refresh();
    if (_layerDepth)
        _layerDepth->refresh();
    refreshHistory();
    if (_notes && _notesDock && _notesDock->isVisible())
        _notes->refresh();
    if (_controllers && _controllersDock && _controllersDock->isVisible() && !_editContext.commandStack().isInMacro())
        _controllers->refresh();
    if (_palettes && _palettesDock && _palettesDock->isVisible() && !_editContext.commandStack().isInMacro())
        _palettes->refresh();
    if (_functionCurves && _functionCurvesDock && _functionCurvesDock->isVisible() && !_editContext.commandStack().isInMacro())
        _functionCurves->refresh();
    if (_scenes && _scenesDock && _scenesDock->isVisible() && !_editContext.commandStack().isInMacro())
    {
        // An undo may have put back a scene, or taken the shown one away.
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (document && _shownScene != document->currentScene())
            goToScene(-1);
        _scenes->refresh();
    }
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
    if (!_switchingTabs)
        refreshTabs();
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
    // Animate's Tools panel: a dock down the left of the stage, the tools
    // in slots, a slot with hidden siblings opening them on a long press.
    // It docks to either side or floats, in one column or two, and its
    // slots can be rearranged (Edit Toolbar).
    _toolbox = new Toolbox(this);
    _toolsDock = new QDockWidget("Tools", this);
    _toolsDock->setObjectName("toolsDock");
    _toolsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    _toolsDock->setWidget(_toolbox);
    // A slim grip for a title, so the panel is no wider than its tools;
    // along the top or bottom the grip stands at its left end.
    _toolsDock->setTitleBarWidget(new DockGrip(_toolsDock));
    connect(_toolsDock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea area) {
        const bool across = area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea;
        _toolsDock->setFeatures(across ? _toolsDock->features() | QDockWidget::DockWidgetVerticalTitleBar
                                       : _toolsDock->features() & ~QDockWidget::DockWidgetVerticalTitleBar);
        _toolbox->setOrientation(across ? Qt::Horizontal : Qt::Vertical);
    });
    connect(_toolsDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating)
            _toolsDock->setFeatures(_toolsDock->features() & ~QDockWidget::DockWidgetVerticalTitleBar);
        _toolbox->setFloating(floating);
    });
    addDockWidget(Qt::LeftDockWidgetArea, _toolsDock);
    splitDockWidget(_toolsDock, _outlinerDock, Qt::Horizontal);
    Toolbox::Layout defaultTools;

    // One action group so the tools behave like a radio button set, the way a
    // toolbox does.
    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    // A tool's action: checkable, in the group, with its key, and known to
    // the window so the key works even when the action sits in a flyout.
    const auto makeTool = [this, toolGroup](ToolIcons::Tool icon, const char* name, QKeyCombination key,
                              const char* tip, std::function<Tool*()> tool) {
        QAction* action = new QAction(name, this);
        theme::themedIcon(action, [icon]() { return ToolIcons::icon(icon); });
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
        _toolbox->addTool(name, action);
        return action;
    };

    // A slot in the default layout: one tool, or a group whose first tool
    // shows and whose others open on a long press.
    const auto addSlot = [&defaultTools](const std::vector<QAction*>& actions) {
        Toolbox::Slot slot;
        for (QAction* action : actions)
            slot << action->text();
        defaultTools.entries.push_back(slot);
    };
    const auto addLine = [&defaultTools]() { defaultTools.entries.push_back(Toolbox::Slot{"|"}); };

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

    addLine();

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
        makeTool(ToolIcons::Tool::RectanglePrimitive, "Rectangle Primitive", Qt::SHIFT | Qt::Key_R,
            "Drag to draw an editable rectangle object with corner radii.",
            [this]() -> Tool* { return _rectanglePrimitiveTool.get(); }),
        makeTool(ToolIcons::Tool::OvalPrimitive, "Oval Primitive", Qt::SHIFT | Qt::Key_O,
            "Drag to draw an editable oval object with start and end angles.",
            [this]() -> Tool* { return _ovalPrimitiveTool.get(); }),
        makeTool(ToolIcons::Tool::PolyStar, "PolyStar", Qt::Key_unknown,
            "Drag from the center outwards. Sides and star mode are in the Properties panel.",
            [this]() -> Tool* { return _polyStarTool.get(); }),
    });
    addSlot({makeTool(ToolIcons::Tool::Pencil, "Pencil", Qt::SHIFT | Qt::Key_Y,
        "Draw freehand. The stroke is fitted on release; the mode is in the Properties panel.",
        [this]() -> Tool* { return _pencilTool.get(); })});
    // The Flow, Nib and Ink brushes share a slot; B is the Flow Brush's, as
    // it is the brush Animate gives B.
    addSlot({
        makeTool(ToolIcons::Tool::FluidBrush, "Flow Brush", Qt::SHIFT | Qt::Key_B,
            "Paint a fill whose width follows the pen's pressure and speed, with tapered ends "
            "and a smoothed line. Settings are in the Properties panel.",
            [this]() -> Tool* { return _brushTool.get(); }),
        makeTool(ToolIcons::Tool::Brush, "Nib Brush", Qt::Key_B,
            "Paint a fill with a shaped nib: round, oval, square or slanted. Use Pressure varies "
            "the width; Use Tilt turns the nib with the pen.",
            [this]() -> Tool* { return _classicBrushTool.get(); }),
        makeTool(ToolIcons::Tool::InkBrush, "Ink Brush", Qt::Key_unknown,
            "Paint steady inked lines: a stabilizer trails the pen, the pressure response is "
            "adjustable, and tilting the pen broadens the line.",
            [this]() -> Tool* { return _inkBrushTool.get(); }),
    });

    addLine();

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

    addLine();

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
        makeTool(ToolIcons::Tool::TimeScrub, "Time Scrub", Qt::Key_unknown,
            "Drag left or right across the stage to move through the frames.",
            [this]() -> Tool* { return _timeScrubTool.get(); }),
        makeTool(ToolIcons::Tool::Camera, "Camera", Qt::Key_C,
            "Move the scene's camera on this frame: drag to pan, Shift-drag to zoom, Alt-drag to turn. "
            "Adds the camera if the scene has none.",
            [this]() -> Tool* { return _cameraTool.get(); }),
    });
    addSlot({makeTool(ToolIcons::Tool::Zoom, "Zoom", Qt::Key_Z,
        "Click to zoom in, alt-click to zoom out, drag to zoom to an area.",
        [this]() -> Tool* { return _zoomTool.get(); })});

    // The tools are all known: the default layout, then the one kept.
    _toolbox->setDefaultLayout(defaultTools);
    _toolbox->setLayout(Toolbox::Layout::fromJson(
        QJsonDocument::fromJson(QSettings().value("toolbox/layout").toByteArray()).object()));
    connect(_toolbox, &Toolbox::layoutChanged, this, [this]() {
        QSettings().setValue("toolbox/layout", QJsonDocument(_toolbox->layout().toJson()).toJson(QJsonDocument::Compact));
    });
    connect(_toolbox, &Toolbox::editRequested, this, &MainWindow::editToolbar);

    // Under the tools: the stroke and fill colors, black and white, swap,
    // and Object Drawing.
    // They flow with the tools; a null entry is a line.
    QWidget* toolBar = _toolbox;
    QList<QWidget*> extras;
    const auto addExtra = [&extras](QWidget* widget) { extras << widget; };
    const auto addExtraLine = [&extras]() { extras << nullptr; };

    // The stroke and fill colors, under the tools as in Animate's toolbox.
    _toolboxStrokeSwatch = new QToolButton(toolBar);
    _toolboxStrokeSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.strokeColor));
    _toolboxStrokeSwatch->setIconSize(QSize(20, 20));
    _toolboxStrokeSwatch->setAutoRaise(true);
    _toolboxStrokeSwatch->setToolTip("Stroke color");
    connect(_toolboxStrokeSwatch, &QToolButton::clicked, this, [this]() { chooseStyleColor(true); });
    addExtra(_toolboxStrokeSwatch);

    _toolboxFillSwatch = new QToolButton(toolBar);
    _toolboxFillSwatch->setIcon(PropertiesPanel::swatchIcon(_drawingStyle.fillColor));
    _toolboxFillSwatch->setIconSize(QSize(20, 20));
    _toolboxFillSwatch->setAutoRaise(true);
    _toolboxFillSwatch->setToolTip("Fill color");
    connect(_toolboxFillSwatch, &QToolButton::clicked, this, [this]() { chooseStyleColor(false); });
    addExtra(_toolboxFillSwatch);

    // Black and White, and Swap Colors, under the swatches as in Animate's
    // toolbar.
    const auto smallButton = [toolBar](const QString& tip, const std::function<void(QPainter&)>& draw) {
        QToolButton* button = new QToolButton(toolBar);
        theme::themedIcon(button, [draw]() {
            QPixmap pixmap(16, 16);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing, true);
            draw(painter);
            painter.end();
            return QIcon(pixmap);
        });
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
    addExtra(blackWhite);
    QToolButton* swap = smallButton("Swap stroke and fill colors", [](QPainter& p) {
        p.setPen(QPen(theme::color("icon"), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(2.5, 2.5, 11, 11), 0, 90 * 16);
        p.setBrush(theme::color("icon"));
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
    addExtra(swap);

    addExtraLine();

    // Not a tool, so it stands outside the exclusive group: it changes what
    // every drawing tool does rather than replacing them.
    QAction* objectDrawingAction = new QAction("Object Drawing", this);
    theme::themedIcon(objectDrawingAction, []() { return ToolIcons::icon(ToolIcons::Tool::ObjectDrawing); });
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
    addAction(objectDrawingAction);
    QToolButton* objectDrawingButton = new QToolButton(toolBar);
    objectDrawingButton->setAutoRaise(true);
    objectDrawingButton->setIconSize(QSize(22, 22));
    objectDrawingButton->setDefaultAction(objectDrawingAction);
    addExtra(objectDrawingButton);
    _toolbox->setExtras(extras);
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
    links.editedText = [this](int& run) -> const fla::StaticText* {
        const fla::StaticText* text = _textTool && _textTool->isEditing() ? _textTool->editedText() : nullptr;
        if (!text)
            return nullptr;
        // The run the caret is in.
        int at = _textTool->caretIndex();
        run = 0;
        for (size_t i = 0; i < text->runs.size(); ++i)
        {
            const int length = QString::fromStdString(text->runs[i].text).length();
            run = static_cast<int>(i);
            if (at < length || (at == length && i + 1 == text->runs.size()))
                break;
            at -= length;
        }
        return text;
    };
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
    connect(_propertiesPanel, &PropertiesPanel::swapBitmapRequested, this, &MainWindow::swapBitmap);
    connect(_propertiesPanel, &PropertiesPanel::textStyleRequested, this, &MainWindow::applyTextStyle);
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
    connect(_swatchesPanel, &SwatchesPanel::colorsEditRequested, this,
        [this](const std::vector<fla::swatches::Color>& colors, const QString& name) {
            if (!_flaDocument || !_flaDocument->document)
                return;
            fla::Document* document = _flaDocument->document;
            const fla::swatches::State before = fla::swatches::stateOf(*document);
            _editContext.commandStack().push(fla::CommandPtr(new fla::SetSwatchesCommand(
                document, before, fla::swatches::withColors(before, colors), name.toStdString())));
            _editContext.commandStack().breakMergeChain();
        });
    connect(_swatchesPanel, &SwatchesPanel::palettesRequested, this, [this]() {
        if (_palettesDock)
        {
            _palettesDock->show();
            _palettesDock->raise();
        }
    });
    connect(_swatchesPanel, &SwatchesPanel::tagRequested, this, [this](int index) {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document)
            return;
        const std::vector<fla::swatches::Color> own = fla::swatches::of(*document);
        const std::vector<fla::swatches::Color> shown = own.empty() ? fla::swatches::defaults() : own;
        if (index < 0 || index >= static_cast<int>(shown.size()))
            return;
        fla::palettes::Palettes palettes = fla::palettes::load(*document);
        if (!palettes.activePalette())
        {
            palettes.list.push_back(fla::palettes::Palette{"Palette", {}});
            palettes.active = "Palette";
        }
        fla::palettes::Palette* active = palettes.activePalette();
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Convert to Tagged Swatch", "Name:", QLineEdit::Normal,
            QString("Swatch %1").arg(active->colors.size() + 1), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        for (const fla::palettes::Color& color : active->colors)
            if (color.name == name.toStdString())
            {
                QMessageBox::information(this, "Convert to Tagged Swatch", "The palette has a color of that name already.");
                return;
            }
        fla::palettes::Color color;
        color.name = name.toStdString();
        for (int i = 0; i < 4; ++i)
            color.rgba[i] = shown[static_cast<size_t>(index)][static_cast<size_t>(i)];
        active->colors.push_back(color);
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        fla::CommandStack& stack = _editContext.commandStack();
        stack.beginMacro("Tag Swatch");
        // A default palette color becomes one of the document's swatches.
        if (own.empty())
        {
            const fla::swatches::State before = fla::swatches::stateOf(*document);
            stack.push(fla::CommandPtr(new fla::SetSwatchesCommand(document, before, fla::swatches::withColors(before, shown), "Tag Swatch")));
        }
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
            document->extraAttributes, fla::palettes::saved(*document, palettes), "Tag Swatch")));
        if (fla::CommandPtr link = fla::palettes::linkMatching(*document, color))
            stack.push(std::move(link));
        stack.endMacro();
        stack.breakMergeChain();
        if (_palettes)
            _palettes->refresh();
        _swatchesPanel->refreshIfStale();
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
        // A font symbol: renamed in place, with the text set in it.
        std::vector<fla::fonts::FontItem> fonts = fla::fonts::itemsOf(*_flaDocument->document);
        for (fla::fonts::FontItem& font : fonts)
        {
            if (QString::fromStdString(font.name) != from)
                continue;
            const std::string newName = to.mid(to.lastIndexOf('/') + 1).toStdString();
            if (newName.empty() || !fla::library::nameIsFree(*_flaDocument->document, newName))
            {
                _libraryPanel->refresh();
                return;
            }
            const std::string oldName = font.name;
            font.name = newName;
            fla::CommandStack& stack = _editContext.commandStack();
            stack.beginMacro("Rename Font");
            setFontItems(fonts, "Rename Font");
            renameFontFaces(oldName + "*", newName + "*");
            stack.endMacro();
            afterLibraryEdit();
            return;
        }
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
    connect(_libraryPanel, &LibraryPanel::newFontRequested, this, [this]() { fontItemDialog(QString()); });
    connect(_libraryPanel, &LibraryPanel::fontPropertiesRequested, this, [this](const QString& name) { fontItemDialog(name); });
    connect(_libraryPanel, &LibraryPanel::useFontRequested, this, [this](const QString& name) {
        applyTextStyle("face", name + "*");
    });
    connect(_libraryPanel, &LibraryPanel::deleteItemsRequested, this,
        [this, symbolNamed, afterLibraryEdit](const QStringList& chosen) {
            if (!_flaDocument || !_flaDocument->document)
                return;
            fla::Document& document = *_flaDocument->document;
            // Font symbols go first; text set in one draws in its font's
            // name, as Animate leaves it.
            QStringList names = chosen;
            std::vector<fla::fonts::FontItem> fonts = fla::fonts::itemsOf(document);
            const size_t before = fonts.size();
            fonts.erase(std::remove_if(fonts.begin(), fonts.end(), [&names](const auto& font) {
                return names.contains(QString::fromStdString(font.name));
            }), fonts.end());
            if (fonts.size() != before)
            {
                for (const fla::fonts::FontItem& font : fla::fonts::itemsOf(document))
                    names.removeAll(QString::fromStdString(font.name));
                setFontItems(fonts, "Delete Font");
                if (names.isEmpty())
                {
                    afterLibraryEdit();
                    return;
                }
            }
            std::vector<fla::Symbol*> symbols;
            std::vector<fla::Bitmap*> bitmaps;
            QStringList kept;
            size_t instances = 0;
            const std::map<std::string, int> uses = fla::library::useCounts(document);
            for (const QString& name : names)
            {
                if (fla::Symbol* symbol = symbolNamed(name))
                {
                    symbols.push_back(symbol);
                    instances += fla::library::instancesOf(document, symbol).size();
                    continue;
                }
                for (fla::Resource* resource : document.resources)
                {
                    if (!resource || resource->resourceType() != fla::Resource::Type::Bitmap ||
                        static_cast<fla::Bitmap*>(resource)->name != name.toStdString())
                        continue;
                    // A bitmap in use stays: its fills have nothing to
                    // stand in for it.
                    if (uses.count(name.toStdString()))
                        kept << name;
                    else
                        bitmaps.push_back(static_cast<fla::Bitmap*>(resource));
                }
            }
            if (symbols.empty() && bitmaps.empty())
            {
                if (!kept.isEmpty())
                    QMessageBox::information(this, "Delete",
                        QString("%1 is in use, so it cannot be deleted.").arg(kept.join(", ")));
                return;
            }
            // Animate asks before taking instances with the symbols.
            if (instances > 0)
            {
                const QString what = symbols.size() == 1 ? QString::fromStdString(symbols.front()->name)
                                                         : QString("These %1 symbols").arg(symbols.size());
                if (QMessageBox::question(this, "Delete",
                        QString("%1 %2 used %3 time%4 in the document. Delete %5 along with %6?")
                            .arg(what)
                            .arg(symbols.size() == 1 ? "is" : "are")
                            .arg(instances)
                            .arg(instances == 1 ? "" : "s")
                            .arg(symbols.size() == 1 ? "it" : "them")
                            .arg(instances == 1 ? "the instance" : "every instance")) != QMessageBox::Yes)
                    return;
            }
            // Not while one is open for editing.
            if (_phoenixView)
            {
                _phoenixView->exitToDepth(0);
                if (_phoenixView->selection())
                    _phoenixView->selection()->clear();
            }
            fla::CommandStack& stack = _editContext.commandStack();
            const size_t count = symbols.size() + bitmaps.size();
            stack.beginMacro(count == 1 ? (symbols.empty() ? "Delete Bitmap" : "Delete Symbol") : "Delete Library Items");
            for (fla::Symbol* symbol : symbols)
                stack.push(fla::CommandPtr(new fla::DeleteSymbolCommand(&document, symbol)));
            for (fla::Bitmap* bitmap : bitmaps)
                stack.push(fla::CommandPtr(new fla::RemoveBitmapCommand(&document, bitmap)));
            stack.endMacro();
            if (!kept.isEmpty())
                statusBar()->showMessage(QString("Kept %1: it is in use.").arg(kept.join(", ")), 4000);
            afterLibraryEdit();
            if (_timelineView)
                _timelineView->refreshFrames();
        });
    connect(_libraryPanel, &LibraryPanel::linkToMasterRequested, this, &MainWindow::linkToMaster);
    connect(_libraryPanel, &LibraryPanel::updateLinkedRequested, this, &MainWindow::updateLinked);
    connect(_libraryPanel, &LibraryPanel::unlinkRequested, this, [this, symbolNamed, afterLibraryEdit](const QString& name) {
        fla::Symbol* symbol = symbolNamed(name);
        if (!symbol)
            return;
        auto after = symbol->extraAttributes;
        fla::Symbol probe(nullptr);
        probe.extraAttributes = after;
        fla::links::clearLink(probe);
        _editContext.commandStack().push(fla::CommandPtr(
            new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                symbol->extraAttributes, symbol->extraAttributes, probe.extraAttributes, "Unlink from Master")));
        afterLibraryEdit();
    });
    connect(_libraryPanel, &LibraryPanel::linkageRequested, this,
        [this, symbolNamed, afterLibraryEdit](const QString& name) {
            fla::Symbol* symbol = symbolNamed(name);
            if (!symbol)
                return;
            bool ok = false;
            const QString className = QInputDialog::getText(this, "Linkage",
                "Class (empty for no export):", QLineEdit::Normal,
                QString::fromStdString(fla::library::linkageOf(*symbol)), &ok).trimmed();
            if (!ok || className.toStdString() == fla::library::linkageOf(*symbol))
                return;
            auto after = fla::library::withLinkage(symbol->extraAttributes, className.toStdString());
            _editContext.commandStack().push(fla::CommandPtr(
                new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                    symbol->extraAttributes, symbol->extraAttributes, after, "Linkage")));
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
    if (tool == _cameraTool.get()) return IconTool::Camera;
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

void MainWindow::advancedSmooth()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Advanced Smooth");
    QFormLayout* form = new QFormLayout(&dialog);
    QSettings settings;
    QCheckBox* belowOn = new QCheckBox("Smooth angles below", &dialog);
    belowOn->setChecked(settings.value("advancedSmooth/belowOn", true).toBool());
    QSpinBox* below = new QSpinBox(&dialog);
    below->setRange(0, 180);
    below->setSuffix(QString::fromUtf8("\u00b0"));
    below->setValue(settings.value("advancedSmooth/below", 90).toInt());
    form->addRow(belowOn, below);
    QCheckBox* aboveOn = new QCheckBox("Smooth angles above", &dialog);
    aboveOn->setChecked(settings.value("advancedSmooth/aboveOn", true).toBool());
    QSpinBox* above = new QSpinBox(&dialog);
    above->setRange(0, 180);
    above->setSuffix(QString::fromUtf8("\u00b0"));
    above->setValue(settings.value("advancedSmooth/above", 90).toInt());
    form->addRow(aboveOn, above);
    QSpinBox* strength = new QSpinBox(&dialog);
    strength->setRange(0, 100);
    strength->setSuffix("%");
    strength->setValue(settings.value("advancedSmooth/strength", 50).toInt());
    form->addRow("Smoothing strength:", strength);
    form->addRow(new QLabel("A corner's angle is 180\u00b0 where the line runs straight on.", &dialog));
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue("advancedSmooth/belowOn", belowOn->isChecked());
    settings.setValue("advancedSmooth/below", below->value());
    settings.setValue("advancedSmooth/aboveOn", aboveOn->isChecked());
    settings.setValue("advancedSmooth/above", above->value());
    settings.setValue("advancedSmooth/strength", strength->value());
    const bool useBelow = belowOn->isChecked();
    const bool useAbove = aboveOn->isChecked();
    const double belowAngle = below->value();
    const double aboveAngle = above->value();
    const double amount = strength->value() / 100.0;
    tidySelectedShapes("Advanced Smooth", [=](fla::EditablePath& path) {
        fla::smoothPath(path, amount, [=](double angle) {
            return (useBelow && angle < belowAngle) || (useAbove && angle > aboveAngle);
        });
    });
}

void MainWindow::advancedStraighten()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Advanced Straighten");
    QFormLayout* form = new QFormLayout(&dialog);
    QSettings settings;
    QSpinBox* strength = new QSpinBox(&dialog);
    strength->setRange(0, 100);
    strength->setSuffix("%");
    strength->setValue(settings.value("advancedStraighten/strength", 50).toInt());
    form->addRow("Straightening strength:", strength);
    QCheckBox* recognize = new QCheckBox("Recognize shapes", &dialog);
    recognize->setToolTip("A closed path that reads as a rectangle or an oval becomes one");
    recognize->setChecked(settings.value("advancedStraighten/recognize", false).toBool());
    form->addRow("", recognize);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue("advancedStraighten/strength", strength->value());
    settings.setValue("advancedStraighten/recognize", recognize->isChecked());
    // The strength is a tolerance in screen pixels.
    const double screenTolerance = 0.25 + 7.75 * (strength->value() / 100.0);
    const double tolerance = screenTolerance / qMax(_phoenixView ? _phoenixView->zoom() : 1.0, 1.0e-6);
    const bool shapes = recognize->isChecked();
    tidySelectedShapes("Advanced Straighten", [tolerance, shapes](fla::EditablePath& path) {
        const std::vector<fla::Point> raw = fla::samplePath(path, 2.0);
        fla::straightenPath(path, tolerance);
        if (shapes && path.closed)
        {
            fla::StrokeTidyOptions options;
            options.connectLines = false;
            options.recognizeShapes = true;
            fla::tidyStraightenedStroke(path, raw, options);
        }
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

void MainWindow::documentSettings()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
        return;
    using fla::docsettings::Unit;
    QDialog dialog(this);
    dialog.setWindowTitle("Document Settings");
    QFormLayout* form = new QFormLayout(&dialog);
    QComboBox* units = new QComboBox(&dialog);
    const std::pair<Unit, const char*> unitNames[] = {{Unit::Pixels, "Pixels"}, {Unit::Inches, "Inches"},
        {Unit::Points, "Points"}, {Unit::Centimeters, "Centimeters"}, {Unit::Millimeters, "Millimeters"}};
    for (const auto& [unit, name] : unitNames)
        units->addItem(name, static_cast<int>(unit));
    units->setCurrentIndex(units->findData(static_cast<int>(fla::docsettings::unitOf(*document))));
    form->addRow("Units:", units);
    QDoubleSpinBox* width = new QDoubleSpinBox(&dialog);
    QDoubleSpinBox* height = new QDoubleSpinBox(&dialog);
    // The size in pixels, shown in the units chosen.
    double pixelWidth = document->width;
    double pixelHeight = document->height;
    const auto showSize = [&]() {
        const Unit unit = static_cast<Unit>(units->currentData().toInt());
        const double per = fla::docsettings::pixelsPer(unit);
        for (QDoubleSpinBox* box : {width, height})
        {
            const QSignalBlocker blocker(box);
            box->setDecimals(unit == Unit::Pixels || unit == Unit::Points ? 0 : 2);
            box->setRange(1.0 / per, 8192.0 / per);
            box->setSuffix(QString(" ") + fla::docsettings::label(unit));
        }
        const QSignalBlocker blockWidth(width);
        const QSignalBlocker blockHeight(height);
        width->setValue(pixelWidth / per);
        height->setValue(pixelHeight / per);
    };
    showSize();
    connect(units, &QComboBox::currentIndexChanged, &dialog, [&]() { showSize(); });
    const auto perUnit = [&]() { return fla::docsettings::pixelsPer(static_cast<Unit>(units->currentData().toInt())); };
    connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { pixelWidth = value * perUnit(); });
    connect(height, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { pixelHeight = value * perUnit(); });
    QHBoxLayout* size = new QHBoxLayout();
    size->addWidget(width);
    size->addWidget(new QLabel("x", &dialog));
    size->addWidget(height);
    form->addRow("Stage size:", size);

    // Match Contents: the stage around everything on it, the art moved to
    // its corner.
    double moveX = 0.0;
    double moveY = 0.0;
    QPushButton* match = new QPushButton("Match Contents", &dialog);
    match->setToolTip("Size the stage to what is on it, in every frame of every scene");
    match->setAutoDefault(false);
    connect(match, &QPushButton::clicked, &dialog, [&]() {
        QRectF bounds;
        for (fla::Element* element : fla::docsettings::sceneElements(*document))
        {
            const QRectF box = _phoenixView->elementBounds(element);
            if (box.isValid() && !box.isEmpty())
                bounds = bounds.isValid() ? bounds.united(box) : box;
        }
        if (!bounds.isValid())
            return;
        pixelWidth = std::max(1.0, std::ceil(bounds.width()));
        pixelHeight = std::max(1.0, std::ceil(bounds.height()));
        moveX = -bounds.left();
        moveY = -bounds.top();
        showSize();
    });
    form->addRow("", match);
    QCheckBox* scaleContent = new QCheckBox("Scale content with stage", &dialog);
    scaleContent->setToolTip("Everything on the stage grows or shrinks with it");
    form->addRow("", scaleContent);
    form->addRow("", new QLabel("Stage color and frame rate are in the Properties panel's Doc tab.", &dialog));
    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    box->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(box);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int newWidth = static_cast<int>(std::lround(pixelWidth));
    const int newHeight = static_cast<int>(std::lround(pixelHeight));
    const Unit unit = static_cast<Unit>(units->currentData().toInt());
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Document Settings");
    if (newWidth != document->width || newHeight != document->height || moveX != 0.0 || moveY != 0.0)
        stack.push(fla::docsettings::resize(*document, newWidth, newHeight, scaleContent->isChecked(), moveX, moveY));
    if (unit != fla::docsettings::unitOf(*document))
    {
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(document->extraAttributes,
            document->extraAttributes, fla::docsettings::withUnit(*document, unit), "Document Settings")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::applyTextStyle(const QString& keyName, const QString& valueText)
{
    const std::string key = keyName.toStdString();
    const std::string value = valueText.toStdString();
    // The text being typed: its selection, its paragraphs or its field.
    if (_textTool && _textTool->isEditing() && _textTool->applyStyle(key, value))
    {
        if (_propertiesPanel)
            QTimer::singleShot(0, _propertiesPanel, [this]() { _propertiesPanel->refresh(); });
        return;
    }
    // Text fields picked with the Selection tool, restyled whole.
    std::vector<fla::StaticText*> texts;
    for (fla::DOMElement* selected : _selection.elements())
        if (auto* text = dynamic_cast<fla::StaticText*>(selected))
        {
            fla::Frame* frame = frameOfElement(text);
            auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
            if (!layer || !layer->isLocked())
                texts.push_back(text);
        }
    if (!texts.empty())
    {
        _editContext.commandStack().push(fla::textstyle::restyle(texts, key, value, "Text Settings", [](fla::StaticText& text) {
            const QRectF bounds = TextLayout::localBounds(text);
            if (text.autoExpand)
                text.width = bounds.width();
            text.height = bounds.height();
            text.localBounds = fla::Rect(fla::Point(bounds.left(), bounds.top()), fla::Point(bounds.right(), bounds.bottom()));
            text.bounds = text.localBounds;
        }));
        _editContext.commandStack().breakMergeChain();
        if (_phoenixView)
        {
            _phoenixView->invalidateBounds();
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        if (_propertiesPanel)
            QTimer::singleShot(0, _propertiesPanel, [this]() { _propertiesPanel->refresh(); });
        return;
    }
    // Nothing to restyle: the settings new text is set in.
    if (key == "face")
        _drawingStyle.textFace = value;
    else if (key == "size")
        _drawingStyle.textSize = std::max(1.0, valueText.toDouble());
    else if (key == "textType")
        _drawingStyle.textType = value == "dynamic" ? 1 : value == "input" ? 2 : 0;
}

QStringList MainWindow::missingFonts() const
{
    QStringList missing;
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return missing;
    std::vector<const fla::Timeline*> timelines(document->timelines.begin(), document->timelines.end());
    if (document->symbolList)
        for (const fla::Symbol* symbol : document->symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<void(const fla::Element*)> visit = [&](const fla::Element* element) {
        if (const auto* text = dynamic_cast<const fla::StaticText*>(element))
            for (const fla::TextRun& run : text->runs)
            {
                const QString face = QString::fromStdString(run.face);
                // A font symbol is the library's, not the system's.
                if (!face.isEmpty() && !face.endsWith('*') && !missing.contains(face) && !TextLayout::isInstalled(face))
                    missing << face;
            }
        if (const auto* group = dynamic_cast<const fla::Group*>(element))
            for (const fla::Element* member : group->members)
                visit(member);
    };
    for (const fla::Timeline* timeline : timelines)
        for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                    visit(element);
    missing.sort(Qt::CaseInsensitive);
    return missing;
}

void MainWindow::fontMappingDialog(bool onlyMissing)
{
    QHash<QString, QString> mapping = TextLayout::fontMapping();
    QStringList faces = missingFonts();
    if (!onlyMissing)
        for (auto it = mapping.begin(); it != mapping.end(); ++it)
            if (!faces.contains(it.key()))
                faces << it.key();
    if (faces.isEmpty())
    {
        if (!onlyMissing)
            QMessageBox::information(this, "Font Mapping", "This document's fonts are all installed, and no fonts are mapped.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(onlyMissing ? "Missing Fonts" : "Font Mapping");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* intro = new QLabel(onlyMissing
            ? "This document uses fonts that are not installed. Choose a font to show each one in; the document keeps the fonts it names."
            : "Each missing font is shown in the font chosen for it.", &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QFormLayout* form = new QFormLayout();
    std::vector<std::pair<QString, QFontComboBox*>> choices;
    for (const QString& face : faces)
    {
        QFontComboBox* box = new QFontComboBox(&dialog);
        box->setCurrentFont(QFont(mapping.value(face, TextLayout::familyOf(face))));
        form->addRow(face + ":", box);
        choices.emplace_back(face, box);
    }
    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (const auto& [face, box] : choices)
        mapping.insert(face, box->currentFont().family());
    TextLayout::setFontMapping(mapping);
    if (_phoenixView)
    {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

std::vector<fla::StaticText*> MainWindow::allTexts() const
{
    std::vector<fla::StaticText*> texts;
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return texts;
    std::vector<fla::Timeline*> timelines(document->timelines.begin(), document->timelines.end());
    if (document->symbolList)
        for (fla::Symbol* symbol : document->symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<void(fla::Element*)> visit = [&](fla::Element* element) {
        if (auto* text = dynamic_cast<fla::StaticText*>(element))
            texts.push_back(text);
        if (auto* group = dynamic_cast<fla::Group*>(element))
            for (fla::Element* member : group->members)
                visit(member);
    };
    for (fla::Timeline* timeline : timelines)
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                    visit(element);
    return texts;
}

void MainWindow::checkSpelling()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    SpellChecker checker;
    if (!checker.available())
    {
        QMessageBox::information(this, "Check Spelling", "No spell checker is available on this computer.");
        return;
    }
    if (_textTool && _textTool->isEditing() && _phoenixView)
        _textTool->deactivate(*_phoenixView);
    const std::vector<fla::StaticText*> texts = allTexts();

    // Replaces characters of a field's text, inside one run, undoably; the
    // box is measured again.
    const auto replace = [this](fla::StaticText* text, int start, int length, const QString& with) {
        std::vector<fla::TextRun> runs = text->runs;
        int at = 0;
        for (fla::TextRun& run : runs)
        {
            QString characters = QString::fromStdString(run.text);
            if (start >= at && start + length <= at + characters.length())
            {
                characters.replace(start - at, length, with);
                run.text = characters.toStdString();
                fla::StaticText measured(nullptr);
                measured.runs = runs;
                measured.left = text->left;
                measured.top = text->top;
                measured.width = text->width;
                measured.height = text->height;
                measured.autoExpand = text->autoExpand;
                measured.extraAttributes = text->extraAttributes;
                const QRectF bounds = TextLayout::localBounds(measured);
                const fla::Rect box(fla::Point(bounds.left(), bounds.top()), fla::Point(bounds.right(), bounds.bottom()));
                fla::CommandStack& stack = _editContext.commandStack();
                stack.beginMacro("Check Spelling");
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::TextRun>>(text->runs, text->runs, runs, "Check Spelling")));
                if (text->autoExpand)
                    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(text->width, text->width, bounds.width(), "Check Spelling")));
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(text->height, text->height, bounds.height(), "Check Spelling")));
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Rect>(text->localBounds, text->localBounds, box, "Check Spelling")));
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Rect>(text->bounds, text->bounds, box, "Check Spelling")));
                stack.endMacro();
                return true;
            }
            at += characters.length();
        }
        return false;
    };

    QDialog dialog(this);
    dialog.setWindowTitle("Check Spelling");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* context = new QLabel(&dialog);
    context->setWordWrap(true);
    context->setTextFormat(Qt::RichText);
    context->setMinimumWidth(360);
    layout->addWidget(context);
    QHBoxLayout* changeRow = new QHBoxLayout();
    changeRow->addWidget(new QLabel("Change to:", &dialog));
    QLineEdit* changeTo = new QLineEdit(&dialog);
    changeRow->addWidget(changeTo, 1);
    layout->addLayout(changeRow);
    QListWidget* suggestions = new QListWidget(&dialog);
    suggestions->setMaximumHeight(140);
    layout->addWidget(suggestions);
    connect(suggestions, &QListWidget::currentTextChanged, changeTo, &QLineEdit::setText);
    QGridLayout* buttons = new QGridLayout();
    QPushButton* change = new QPushButton("Change", &dialog);
    QPushButton* changeAll = new QPushButton("Change All", &dialog);
    QPushButton* ignore = new QPushButton("Ignore", &dialog);
    QPushButton* ignoreAll = new QPushButton("Ignore All", &dialog);
    QPushButton* addWord = new QPushButton("Add to Dictionary", &dialog);
    QPushButton* close = new QPushButton("Close", &dialog);
    buttons->addWidget(change, 0, 0);
    buttons->addWidget(changeAll, 0, 1);
    buttons->addWidget(ignore, 1, 0);
    buttons->addWidget(ignoreAll, 1, 1);
    buttons->addWidget(addWord, 2, 0);
    buttons->addWidget(close, 2, 1);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);

    // Where the search stands: the field and how far into it.
    size_t field = 0;
    int position = 0;
    int start = 0;
    int length = 0;
    QString word;
    int changes = 0;
    QStringList ignored;
    std::function<void()> next = [&]() {
        while (field < texts.size())
        {
            const QString joined = TextLayout::joinedText(*texts[field]);
            for (const SpellChecker::Misspelling& miss : checker.check(joined))
            {
                const QString candidate = joined.mid(miss.start, miss.length);
                if (miss.start < position || ignored.contains(candidate))
                    continue;
                start = miss.start;
                length = miss.length;
                word = candidate;
                const QString before = joined.mid(std::max(0, start - 30), std::min(start, 30)).toHtmlEscaped();
                const QString after = joined.mid(start + length, 30).toHtmlEscaped();
                context->setText(QString("Not in the dictionary: <b>%1</b><br><br>%2<u><b>%1</b></u>%3")
                                     .arg(word.toHtmlEscaped(), before, after));
                suggestions->clear();
                const QStringList offered = checker.suggestions(word);
                suggestions->addItems(offered);
                changeTo->setText(offered.isEmpty() ? word : offered.front());
                return;
            }
            ++field;
            position = 0;
        }
        dialog.accept();
    };
    connect(change, &QPushButton::clicked, &dialog, [&]() {
        if (field < texts.size() && replace(texts[field], start, length, changeTo->text()))
            ++changes;
        position = start + static_cast<int>(changeTo->text().length());
        next();
    });
    connect(changeAll, &QPushButton::clicked, &dialog, [&]() {
        // The word everywhere, as a whole word.
        const QString from = word;
        const QString with = changeTo->text();
        const QRegularExpression whole("\\b" + QRegularExpression::escape(from) + "\\b");
        for (fla::StaticText* text : texts)
        {
            int offset = 0;
            for (;;)
            {
                const QRegularExpressionMatch match = whole.match(TextLayout::joinedText(*text), offset);
                if (!match.hasMatch())
                    break;
                if (replace(text, static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), with))
                    ++changes;
                offset = static_cast<int>(match.capturedStart() + with.length());
            }
        }
        position = start + static_cast<int>(with.length());
        next();
    });
    connect(ignore, &QPushButton::clicked, &dialog, [&]() {
        position = start + length;
        next();
    });
    connect(ignoreAll, &QPushButton::clicked, &dialog, [&]() {
        ignored << word;
        checker.ignore(word);
        next();
    });
    connect(addWord, &QPushButton::clicked, &dialog, [&]() {
        checker.add(word);
        ignored << word;
        next();
    });
    next();
    const bool finished = field >= texts.size();
    if (!finished)
        dialog.exec();
    _editContext.commandStack().breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (field >= texts.size())
        QMessageBox::information(this, "Check Spelling",
            changes > 0 ? QString("Spelling checked; %1 change%2 made.").arg(changes).arg(changes == 1 ? "" : "s")
                        : QString("Spelling checked; nothing to change."));
}

void MainWindow::syncFontSymbols()
{
    QHash<QString, QString> fonts;
    if (_flaDocument && _flaDocument->document)
        for (const fla::fonts::FontItem& item : fla::fonts::itemsOf(*_flaDocument->document))
            fonts.insert(QString::fromStdString(item.name), QString::fromStdString(item.font));
    TextLayout::setFontSymbols(fonts);
}

void MainWindow::setFontItems(const std::vector<fla::fonts::FontItem>& items, const char* what)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    using Sections = std::vector<fla::Document::RawSection>;
    _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<Sections>(
        document->extraSections, document->extraSections, fla::fonts::withItems(*document, items), what)));
    _editContext.commandStack().breakMergeChain();
    syncFontSymbols();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    if (_phoenixView)
    {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::fontItemDialog(const QString& name)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    std::vector<fla::fonts::FontItem> items = fla::fonts::itemsOf(*document);
    int editing = -1;
    for (size_t i = 0; i < items.size(); ++i)
        if (QString::fromStdString(items[i].name) == name)
            editing = static_cast<int>(i);
    fla::fonts::FontItem item;
    if (editing >= 0)
        item = items[static_cast<size_t>(editing)];
    else
    {
        // "Font 1", "Font 2"... and the font new text is set in.
        for (int n = 1;; ++n)
        {
            const std::string candidate = "Font " + std::to_string(n);
            if (std::none_of(items.begin(), items.end(), [&](const auto& i) { return i.name == candidate; }) &&
                fla::library::nameIsFree(*document, candidate))
            {
                item.name = candidate;
                break;
            }
        }
        item.font = _drawingStyle.textFace;
        item.ranges = {1, 2, 3, 4};
    }

    QDialog dialog(this);
    dialog.setWindowTitle(editing >= 0 ? "Font Properties" : "New Font");
    QFormLayout* form = new QFormLayout(&dialog);
    QLineEdit* nameEdit = new QLineEdit(QString::fromStdString(item.name), &dialog);
    form->addRow("Name:", nameEdit);
    QFontComboBox* family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(TextLayout::familyOf(QString::fromStdString(item.font))));
    form->addRow("Font:", family);
    const QString font = QString::fromStdString(item.font);
    QCheckBox* bold = new QCheckBox("Bold", &dialog);
    bold->setChecked(font.contains("Bold", Qt::CaseInsensitive));
    QCheckBox* italic = new QCheckBox("Italic", &dialog);
    italic->setChecked(font.contains("Italic", Qt::CaseInsensitive) || font.contains("Oblique", Qt::CaseInsensitive));
    QHBoxLayout* style = new QHBoxLayout();
    style->addWidget(bold);
    style->addWidget(italic);
    style->addStretch();
    form->addRow("Style:", style);
    // The characters carried with the movie, so text set in the symbol shows
    // where the font is not installed.
    const std::pair<int, const char*> ranges[] = {{1, "Uppercase (A-Z)"}, {2, "Lowercase (a-z)"}, {3, "Numerals (0-9)"},
        {4, "Punctuation"}, {5, "Basic Latin"}};
    std::vector<std::pair<int, QCheckBox*>> rangeBoxes;
    QVBoxLayout* embed = new QVBoxLayout();
    for (const auto& [id, label] : ranges)
    {
        QCheckBox* box = new QCheckBox(label, &dialog);
        box->setChecked(std::find(item.ranges.begin(), item.ranges.end(), id) != item.ranges.end());
        embed->addWidget(box);
        rangeBoxes.emplace_back(id, box);
    }
    form->addRow("Embed:", embed);
    QLineEdit* characters = new QLineEdit(QString::fromStdString(item.characters), &dialog);
    characters->setPlaceholderText("Other characters to embed");
    form->addRow("Also:", characters);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const std::string newName = nameEdit->text().trimmed().toStdString();
    const bool nameTaken = std::any_of(items.begin(), items.end(), [&](const auto& other) {
        return other.name == newName && &other != (editing >= 0 ? &items[static_cast<size_t>(editing)] : nullptr);
    }) || (newName != item.name && !fla::library::nameIsFree(*document, newName));
    if (newName.empty() || nameTaken)
    {
        QMessageBox::information(this, dialog.windowTitle(), "Choose a name no other library item has.");
        return;
    }
    // The font as text names it: the family run together, then its style.
    QString face = family->currentFont().family().remove(' ');
    if (bold->isChecked() || italic->isChecked())
        face += QString("-") + (bold->isChecked() ? "Bold" : "") + (italic->isChecked() ? "Italic" : "");
    item.name = newName;
    item.font = face.toStdString();
    item.ranges.clear();
    for (const auto& [id, box] : rangeBoxes)
        if (box->isChecked())
            item.ranges.push_back(id);
    item.characters = characters->text().toStdString();
    if (item.itemId.empty())
        item.itemId = fla::media::newItemId();
    const std::string oldName = editing >= 0 ? items[static_cast<size_t>(editing)].name : std::string();
    if (editing >= 0)
        items[static_cast<size_t>(editing)] = item;
    else
        items.push_back(item);

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(editing >= 0 ? "Font Properties" : "New Font");
    setFontItems(items, editing >= 0 ? "Font Properties" : "New Font");
    // Renamed: the text set in it follows.
    if (!oldName.empty() && oldName != newName)
        renameFontFaces(oldName + "*", newName + "*");
    stack.endMacro();
    stack.breakMergeChain();
}

void MainWindow::renameFontFaces(const std::string& from, const std::string& to)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    std::vector<fla::Timeline*> timelines(document->timelines.begin(), document->timelines.end());
    if (document->symbolList)
        for (fla::Symbol* symbol : document->symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<void(fla::Element*)> visit = [&](fla::Element* element) {
        if (auto* text = dynamic_cast<fla::StaticText*>(element))
        {
            std::vector<fla::TextRun> runs = text->runs;
            bool changed = false;
            for (fla::TextRun& run : runs)
                if (run.face == from)
                {
                    run.face = to;
                    changed = true;
                }
            if (changed)
                _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<fla::TextRun>>(
                    text->runs, text->runs, runs, "Rename Font")));
        }
        if (auto* group = dynamic_cast<fla::Group*>(element))
            for (fla::Element* member : group->members)
                visit(member);
    };
    for (fla::Timeline* timeline : timelines)
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                    visit(element);
}

void MainWindow::swapBitmap()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    std::vector<fla::BitmapInstance*> bitmaps;
    for (fla::DOMElement* selected : _selection.elements())
        if (auto* bitmap = dynamic_cast<fla::BitmapInstance*>(selected))
            bitmaps.push_back(bitmap);
    if (bitmaps.empty())
    {
        statusBar()->showMessage("Select a bitmap on the stage first.", 3000);
        return;
    }
    QStringList names;
    for (const fla::Resource* resource : _flaDocument->document->resources)
        if (const auto* bitmap = dynamic_cast<const fla::Bitmap*>(resource))
            names << QString::fromStdString(bitmap->name);
    names.sort(Qt::CaseInsensitive);
    SwapSymbolDialog dialog(names, QString::fromStdString(bitmaps.front()->libraryItemName),
        [this](const QString& name, const QSize& size) {
            return _phoenixView ? _phoenixView->libraryPreview("bitmap", name, size) : QImage();
        },
        nullptr, this, "Swap Bitmap");
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::string chosen = dialog.chosen().toStdString();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Swap Bitmap");
    for (fla::BitmapInstance* bitmap : bitmaps)
        if (bitmap->libraryItemName != chosen)
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
                bitmap->libraryItemName, bitmap->libraryItemName, chosen, "Swap Bitmap")));
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
    // Kept only when they are not the theme's.
    if (_onion.beforeColor == theme::color("stage.onionBefore"))
        settings.remove("onion/beforeColor");
    else
        settings.setValue("onion/beforeColor", _onion.beforeColor.name());
    if (_onion.afterColor == theme::color("stage.onionAfter"))
        settings.remove("onion/afterColor");
    else
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
    QAction* anchor = menu->addAction("Anchor Onion");
    anchor->setCheckable(true);
    anchor->setChecked(_onion.anchored);
    anchor->setStatusTip("Keep the onion skin on the frames it covers now as the playhead moves");
    connect(anchor, &QAction::toggled, this, [this](bool on) {
        _onion.anchored = on;
        const int current = _player ? _player->currentFrame() : 0;
        _onion.anchorFirst = std::max(0, current - _onion.before);
        _onion.anchorLast = current + _onion.after;
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
        openInTab(action->data().toString());
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
    loadBrush(settings, "brushes/eraser", _drawingStyle.eraserBrush);
    _drawingStyle.eraserSyncWithBrush = settings.value("brushes/eraserSync", false).toBool();
    const int brushMode = settings.value("brushes/mode", 0).toInt();
    _drawingStyle.brushMode = brushMode >= 0 && brushMode <= 4
        ? static_cast<fla::DrawingStyle::BrushMode>(brushMode) : fla::DrawingStyle::BrushMode::Normal;
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
    saveBrush(settings, "brushes/fluid", _drawingStyle.fluidBrush);
    saveBrush(settings, "brushes/classic", _drawingStyle.classicBrush);
    saveBrush(settings, "brushes/ink", _drawingStyle.inkBrush);
    saveBrush(settings, "brushes/eraser", _drawingStyle.eraserBrush);
    settings.setValue("brushes/eraserSync", _drawingStyle.eraserSyncWithBrush);
    settings.setValue("brushes/mode", static_cast<int>(_drawingStyle.brushMode));
    settings.setValue("window/geometry", saveGeometry());
    if (_layoutRestored)
        settings.setValue("window/state", saveState(kLayoutVersion));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Every tab with unsaved changes is shown in turn and asked about.
    for (size_t i = 0; i < _tabs.size(); ++i)
    {
        const bool modified = static_cast<int>(i) == _activeTab ? _editContext.isModified() : !_tabs[i]->history.isClean();
        if (!modified)
            continue;
        activateTab(static_cast<int>(i));
        if (!confirmDiscardChanges())
        {
            event->ignore();
            return;
        }
    }
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
