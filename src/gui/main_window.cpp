#include "main_window.h"
#include "convert_to_symbol_dialog.h"
#include "lines_to_fills.h"
#include "fill_offset.h"
#include "new_document_dialog.h"
#include "start_dialog.h"
#include "stage_controls.h"
#include "safe_zones_dialog.h"
#include "../edit/canvas_document.h"
#include "../edit/motion_blur.h"
#include "../edit/safe_zones.h"
#include "tutorial_lesson.h"
#include "tutorial_runner.h"
#include "../edit/phoenix_features.h"
#include "mcp_server.h"
#include "script_runner.h"
#include "app_identity.h"
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
#include "color_well.h"
#include "arrange_panels.h"
#include "behavior_library.h"
#include "behaviors_panel.h"
#include "../edit/behaviors.h"
#include "../edit/controllers.h"
#include "../jsfl/behavior_frames.h"
#include "../edit/shape_operators.h"
#include "../edit/text_animators.h"
#include "../edit/adjustments.h"
#include "../edit/html5_export.h"
#include "../edit/other_apps.h"
#include "../edit/frame_bake.h"
#include "motion_editor.h"
#include "motion_presets.h"
#include "xsheet_panel.h"
#include "layer_depth_panel.h"
#include "camera_tool.h"
#include "bone_tool.h"
#include "envelope_dialog.h"
#include "../edit/frame_sound.h"
#include "../edit/trace_bitmap.h"
#include "movie_explorer.h"
#include "../edit/library_transfer.h"
#include "../edit/document_import.h"
#include "../edit/document_session.h"
#include "preferences.h"
#include "../edit/ik_pose.h"
#include "find_replace_dialog.h"
#include "toolbar_editor.h"
#include "toolbox.h"
#include "notes_panel.h"
#include "controllers_panel.h"
#include "function_curves_panel.h"
#include "node_graph_panel.h"
#include "../edit/palettes.h"
#include "../edit/shape_recognition.h"
#include <QButtonGroup>
#include <QProgressDialog>
#include <QCryptographicHash>
#include <QGridLayout>
#include "spell_checker.h"
#include "../edit/text_style.h"
#include "../edit/vector_import.h"
#include "../edit/psd_import.h"
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
#include "claude_code.h"
#include "actions_panel.h"
#include "cep_panel.h"
#include "claude_code_panel.h"
#include "ik_poser.h"
#include "script_window.h"
#include "theme_editor.h"
#include "output_panel.h"
#include "test_movie_window.h"
#include "trace_bitmap_dialog.h"
#include "../parser/fla_doctor.h"
#include "../edit/linked_assets.h"
#include "command_line.h"
#include "swatches_panel.h"
#include "../edit/swatches.h"
#include "swap_symbol_dialog.h"
#include "../edit/layer_commands.h"
#include "../edit/library_media.h"
#include "../edit/node_graph.h"
#include "../data/bitmap_decoder.h"
#include "../edit/frame_commands.h"
#include "../edit/frame_bake.h"
#include "../edit/combine_objects.h"
#include "../edit/timeline_edit.h"
#include "../edit/library_commands.h"
#include "../edit/property_command.h"
#include "phoenix_view.h"
#include "player.h"
#include "movie_export.h"
#include "script_tool.h"
#include "../jsfl/tool_script.h"
#include "sprite_sheet_dialog.h"
#include "brush_library_dialog.h"
#include "brush_library_store.h"
#include "brush_properties_dialog.h"
#include "document_brushes_dialog.h"
#include "publish_dialog.h"
#include "html5_publish.h"
#include "render_queue.h"
#include "paint_recorder.h"
#include "../util/swf_sound.h"
#include "../util/swf_writer.h"
#include "../edit/publish_settings.h"
#include "../edit/swf_event_sounds.h"
#include "../edit/swf_actionscript3.h"
#include "../edit/swf_movie.h"
#include "../play/script/class_path.h"
#include "movie_hooks.h"
#include "swf_export.h"
#include "../edit/brush_presets.h"
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
#include "../edit/paint_brushes.h"
#include "../edit/camera.h"
#include "../edit/shape_tween.h"
#include "../edit/new_document.h"
#include "../edit/shape_merge.h"
#include "../edit/shape_transform.h"
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
#include "../data/symbol.h"
#include "../writer/xfl_content.h"
#include "../writer/xfl_folder_writer.h"
#include "../phx/container.h"
#include "../phx/phx_document.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
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
#include <QDesktopServices>
#include <QFileSystemWatcher>
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
#include <QSpinBox>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QToolBar>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QComboBox>
#include <QColorDialog>
#include <QDockWidget>
#include "hot_text.h"
#include <QFontComboBox>
#include <QHash>
#include <QFormLayout>
#include <QToolButton>
#include <QTabBar>
#include <QRegularExpression>
#include <QUuid>
#include <QStandardPaths>
#include <QTimer>
#include <QSignalBlocker>
#include <QCursor>
#include <QDrag>
#include <QMouseEvent>
#include <cstdlib>
#include <cstring>

#include "tool_icons.h"

namespace {

/// The version of the dock layout kept between runs and in workspaces: a
/// layout from before the timeline, tools and outliner docked is not used.
constexpr int kLayoutVersion = 2;
fla::Frame* frameOfElement(const fla::Element* element);
std::string publishXml(const fla::Document& document);
}

namespace {
bool s_skipStartScreen = false;
/// The next window made is one opened to take a tab (openSecondaryWindow).
bool s_openingSecondary = false;
/// Every window people work in, in the order they opened.
QList<MainWindow*> s_windows;
// Every window, shown or not (exports from the command line too), for the
// sound behaviors hear.
QList<MainWindow*> s_hearing;
/// What a tab dragged between windows carries: its index in its window.
constexpr const char* kTabMime = "application/x-phoenix-document-tab";
}

bool MainWindow::s_quitting = false;
const fla::Document* MainWindow::_libraryClipboardSource = nullptr;
QStringList MainWindow::_libraryClipboard;
const fla::Document* MainWindow::_externalDragSource = nullptr;
QString MainWindow::_externalDragName;
// Never freed: they outlive every window, and the elements in them are let
// go with the process rather than after the libraries they use.
std::vector<std::unique_ptr<fla::Element>>& MainWindow::_clipboard =
    *new std::vector<std::unique_ptr<fla::Element>>();
const fla::Document* MainWindow::_clipboardFrom = nullptr;
std::vector<std::vector<std::unique_ptr<fla::Frame>>>& MainWindow::_frameClipboard =
    *new std::vector<std::vector<std::unique_ptr<fla::Frame>>>();
const fla::Document* MainWindow::_frameClipboardFrom = nullptr;

/// The document tabs over the stage. A tab dragged well off the bar comes
/// away: dropped on another window's tabs it moves there, anywhere else it
/// opens in a window of its own. The bar takes tabs dropped from another
/// window's.
class DocumentTabBar : public QTabBar
{
public:
    explicit DocumentTabBar(MainWindow* window)
        : QTabBar(window)
        , _window(window)
    {
        setAcceptDrops(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        _pressed = event->button() == Qt::LeftButton && tabAt(event->position().toPoint()) >= 0;
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (_pressed && (event->buttons() & Qt::LeftButton))
        {
            const QPoint at = event->position().toPoint();
            const int margin = std::max(30, height());
            if (at.y() < -margin || at.y() > height() + margin)
            {
                _pressed = false;
                // The bar's own drag of the tab ends where it is first.
                QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                    Qt::LeftButton, Qt::NoButton, event->modifiers());
                QTabBar::mouseReleaseEvent(&release);
                const int index = currentIndex();
                QTimer::singleShot(0, _window, [window = _window, index]() { window->tabDraggedOut(index); });
                return;
            }
        }
        QTabBar::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        _pressed = false;
        QTabBar::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(kTabMime))
            event->acceptProposedAction();
        else
            QTabBar::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasFormat(kTabMime))
            event->acceptProposedAction();
        else
            QTabBar::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(kTabMime))
        {
            QTabBar::dropEvent(event);
            return;
        }
        event->acceptProposedAction();
        // Back onto its own bar: it stays.
        if (event->source() == this)
            return;
        MainWindow* source = nullptr;
        for (MainWindow* window : MainWindow::windows())
            if (window->_tabBar == event->source())
                source = window;
        const int index = event->mimeData()->data(kTabMime).toInt();
        int at = tabAt(event->position().toPoint());
        if (at < 0)
            at = count();
        // Once the drag is over in the window it came from.
        QTimer::singleShot(0, _window, [source, target = _window, index, at]() {
            if (source && MainWindow::windows().contains(source))
                source->moveTabToWindow(index, target, at);
        });
    }

private:
    MainWindow* _window;
    bool _pressed = false;
};

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
    _secondary = startup && s_openingSecondary;
    s_openingSecondary = false;
    if (startup)
        s_windows.push_back(this);
    setWindowTitle(QString("Phoenix %1").arg(QCoreApplication::applicationVersion()));

    app_identity::apply();

    // Set application icon
    QIcon phoenixIcon = createPhoenixIcon();
    setWindowIcon(phoenixIcon);
    QApplication::setWindowIcon(phoenixIcon);

    loadSettings();

    setupUI();
    _scripts = new ScriptRunner(*this);
    setupMenus();
    setupToolBar();
    setupPropertiesPanel();
    createPanels();
    // The installed extensions' HTML panels, docked before the layout comes
    // back so one left open opens again.
    _cepPanels = new CepPanels(*this, *_scripts);
    _cepPanels->refresh();
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

    // Every command exists too: its key as made is the default, and the
    // set chosen goes on.
    {
        std::vector<QAction*> tools;
        for (const auto& entry : _toolActionList)
            tools.push_back(entry.second);
        std::sort(tools.begin(), tools.end(), [](QAction* a, QAction* b) { return a->text() < b->text(); });
        _shortcuts = Shortcuts::collect(menuBar(), tools);
        Shortcuts::apply(_shortcuts);
    }

    // Keep the Edit menu and the title bar in step with the undo history.
    _editContext.commandStack().setChangedCallback([this]() { updateEditState(); });
    _editContext.commandStack().setLimit(static_cast<size_t>(Preferences::undoLevels()));
    updateEditState();

    _selection.setChangedCallback([this]() {
        updateSelectionState();
        if (_behaviors && _behaviorsDock && _behaviorsDock->isVisible())
            _behaviors->refresh();
        scriptEvent("selectionChanged");
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
        // The Properties panel follows the selection:
        // an object picked with a selecting tool shows its Object tab, while
        // a drawing tool keeps its own settings in view even though what it
        // just drew is selected; nothing selected shows the tool, or the
        // document when the arrow is the tool.
        if (_propertiesPanel)
        {
            _propertiesPanel->refresh();
            const Tool* tool = _phoenixView ? _phoenixView->activeTool() : nullptr;
            const bool selecting = !tool || tool == _selectionTool.get() || tool == _subselectionTool.get() ||
                                   tool == _freeTransformTool.get() || tool == _lassoTool.get() ||
                                   tool == _polygonTool.get() || tool == _magicWandTool.get() ||
                                   tool == _gradientTransformTool.get() || tool == _rotate3DTool.get() ||
                                   tool == _translate3DTool.get();
            if (!_selection.isEmpty() && !selecting)
                _propertiesPanel->setTab(PropertiesPanel::Tab::Tool);
            else if (!_selection.isEmpty())
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

    // Object snapping on by default, grid snapping off.
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
    const auto showStatus = [this](const QString& message) { statusBar()->showMessage(message, 5000); };
    _assetWarpTool = std::make_unique<AssetWarpTool>(stack, showStatus,
        [this](fla::Element* art, fla::Frame* frame) { return makePuppet(art, frame); });
    _rotate3DTool = std::make_unique<ThreeDTool>(ThreeDTool::Mode::Rotate, stack, showStatus);
    _translate3DTool = std::make_unique<ThreeDTool>(ThreeDTool::Mode::Translate, stack, showStatus);
    _widthTool = std::make_unique<WidthTool>(stack);
    _boneTool = std::make_unique<BoneTool>(
        [this](PhoenixView&, const BoneTool::Link& link) { addBone(link); },
        [this](PhoenixView&, fla::Frame* frame, size_t child, int joint) { editJointLimits(frame, child, joint); });
    _bindTool = std::make_unique<BindTool>(
        [this](PhoenixView&, fla::Frame* frame, size_t child, const fla::ShapeBinding& binding) {
            rebindShape(frame, child, binding);
        });
    _cameraTool = std::make_unique<CameraTool>(stack, [this]() -> fla::Layer* {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        fla::Timeline* scene = document ? document->currentScene() : nullptr;
        if (!scene)
            return nullptr;
        if (!fla::camera::filmingOf(*scene))
            addCamera();
        return fla::camera::filmingOf(*scene);
    });
    _brushTool = std::make_unique<BrushTool>(BrushTool::Kind::Fluid, _selection, stack, _drawingStyle);
    _classicBrushTool = std::make_unique<BrushTool>(BrushTool::Kind::Classic, _selection, stack, _drawingStyle);
    _inkBrushTool = std::make_unique<BrushTool>(BrushTool::Kind::Ink, _selection, stack, _drawingStyle);
    _eraserTool = std::make_unique<EraserTool>(_selection, stack, _drawingStyle);
    _paintBucketTool = std::make_unique<PaintBucketTool>(stack, _drawingStyle);
    _inkBottleTool = std::make_unique<InkBottleTool>(stack, _drawingStyle);
    // Sampling hands over to the bucket or the bottle.
    _eyedropperTool = std::make_unique<EyedropperTool>(_drawingStyle, [this](bool stroke) {
        refreshStyleSwatches();
        QAction* next = stroke ? _inkBottleAction : _paintBucketAction;
        if (next)
            next->trigger();
    });
    _penTool = std::make_unique<PenTool>(_selection, stack, _drawingStyle);
    _pencilTool = std::make_unique<PencilTool>(_selection, stack, _drawingStyle);
    _paintBrushTool = std::make_unique<PencilTool>(_selection, stack, _drawingStyle, "Paint Brush");
    // A brush from the brush library comes into the document with the
    // first stroke any tool draws with it.
    _drawingStyle.paintBrushIndex = [this](const std::string& guid) {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        return document ? paintBrushIndexFor(*document, _editContext.commandStack(), guid) : 0;
    };
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
    // A lesson asked for on the command line opens a document of its own.
    // A window opened to take a tab starts empty.
    const bool first = startup && !_secondary;
    const bool lesson = s_skipStartScreen;
    s_skipStartScreen = false;
    // The documents open last time come back, when that is wanted.
    const bool reopen = first && initialFile.isEmpty() && !lesson &&
        QSettings().value("reopenDocuments", true).toBool() &&
        !QSettings().value("session/documents").toStringList().isEmpty();
    const bool startScreen = initialFile.isEmpty() && !lesson && !reopen &&
        QSettings().value("showStartDialog", true).toBool();
    const QString startFile = !initialFile.isEmpty() ? initialFile
        : (_recentFiles.isEmpty() || startScreen || lesson || reopen ? QString() : _recentFiles.first());
    // Work a crash left behind comes first, unless a file was asked for.
    if (first && !startFile.isEmpty())
    {
        QTimer::singleShot(0, this, [this, startFile, initialFile]() {
            if (!initialFile.isEmpty() || !offerRecovery())
                loadFLAFile(startFile);
        });
    }
    else if (first)
    {
        QTimer::singleShot(0, this, [this, startScreen]() {
            const bool recovered = offerRecovery();
            const bool reopened = reopenSession();
            if (!recovered && !reopened && startScreen)
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

    // A copy of unsaved work every few minutes, for after a crash, is
    // written for each document (giveAutoSave).

    // AI assistants attach through MCP when Preferences allows it.
    if (first)
    {
        _mcpServer = new McpServer(this);
        _mcpServer->applySettings();
    }

    // Look for an update once the start-up file has had its turn.
    connect(_updater, &Updater::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, message.isEmpty() ? 0 : 8000);
    });
    if (first)
        QTimer::singleShot(3000, this, [this]() { _updater->checkForUpdates(false); });
}

MainWindow::~MainWindow()
{
    s_windows.removeAll(this);
    s_hearing.removeAll(this);
    if (!_secondary)
        return;
    // A window opened for tabs lets its documents go as it goes; the first
    // window's go with the process. Each history first: it points into its
    // document.
    _switchingTabs = true;
    releaseDocument();
    for (const std::unique_ptr<DocumentTab>& tab : _tabs)
    {
        tab->history.clear();
        delete tab->document;
        tab->document = nullptr;
    }
}

QList<MainWindow*> MainWindow::windows()
{
    return s_windows;
}

bool MainWindow::documentOpenAnywhere(const fla::Document* document)
{
    for (const MainWindow* window : s_windows)
        if (window->documentAlive(document))
            return true;
    return false;
}

MainWindow* MainWindow::openSecondaryWindow()
{
    s_openingSecondary = true;
    MainWindow* window = new MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    return window;
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

/// An extension package, installed rather than opened.
static bool isExtensionPackage(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "zxp";
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;
    const bool open = _flaDocument && _flaDocument->document;
    for (const QUrl& url : event->mimeData()->urls())
    {
        const QString path = url.toLocalFile();
        if (url.isLocalFile() && (isDocumentPath(path) || isExtensionPackage(path) ||
            (open && (MediaImport::isImage(path) || MediaImport::isSound(path) || MediaImport::isArtwork(path)))))
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
        if (isExtensionPackage(path))
        {
            event->acceptProposedAction();
            ScriptRunner::installExtension(path, this);
            return;
        }
        if (MediaImport::isImage(path) || MediaImport::isSound(path) || MediaImport::isArtwork(path))
            media << path;
    }
    if (media.isEmpty() || !_flaDocument || !_flaDocument->document)
        return;
    event->acceptProposedAction();

    // Files dropped on the stage land where they were dropped; anywhere else
    // in the window they go into the library.
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
    // Nothing a tool is in the middle of may outlive the document, nor a
    // field being typed in a panel.
    if (_flaDocument)
    {
        finishPanelEdits();
        // A script being typed is kept in the frame it was typed for.
        if (_actionsPanel)
            _actionsPanel->commit();
        settleGestures();
    }
    // Playback stops with the document it was playing.
    if (_player && _player->isPlaying())
        _player->setPlaying(false);
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
        // The document is going; so is the copy kept of it for recovery.
        if (AutoSave* autoSave = activeAutoSave())
            autoSave->discard();
        forgetDocument(_flaDocument);
        delete _flaDocument;
        _flaDocument = nullptr;
    }
    // Every panel lets go of what it showed of the document, whether it is
    // in view or not: one closed now is not refreshed until it opens.
    if (_movieExplorer)
        _movieExplorer->refresh();
    if (_findReplace)
        _findReplace->refresh();
    if (_motionEditor)
        _motionEditor->refresh();
    if (_functionCurves)
        _functionCurves->refresh();
    if (_notes)
        _notes->refresh();
    if (_scenes)
        _scenes->refresh();
    if (_controllers)
        _controllers->refresh();
    if (_palettes)
        _palettes->refresh();
    if (_xsheet)
        _xsheet->refresh();
    if (_layerDepth)
        _layerDepth->refresh();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
    if (_transformPanel)
        _transformPanel->refresh();
    if (_infoPanel)
        _infoPanel->refresh();
    if (_actionsPanel)
        _actionsPanel->refresh();
    _shownScene = nullptr;
}

void MainWindow::forgetDocument(const fla::FLADocument* document)
{
    if (!document)
        return;
    scriptEvent("documentClosed");
    if (_scripts)
        _scripts->forgetDocument(document);
    _featuresAcknowledged.remove(document);
    // Copied layers point at this document's symbols, and only paste back
    // into it: they go with it, before another document can take its
    // address.
    if (document->document && document->document == _copiedLayersFrom)
    {
        _copiedLayers.clear();
        _copiedLayersFrom = nullptr;
    }
    // Bitmaps out in an editor for this document have nowhere to go.
    for (auto it = _editedBitmapFiles.begin(); it != _editedBitmapFiles.end();)
    {
        if (it->second.document == document->document)
        {
            if (_editedBitmaps)
                _editedBitmaps->removePath(it->second.file);
            it = _editedBitmapFiles.erase(it);
        }
        else
            ++it;
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
    // Filled while it is in view; one opened later fills itself then.
    if (_movieExplorer && _movieExplorerDock && _movieExplorerDock->isVisible())
        _movieExplorer->refresh();

    if (_flaDocument)
        _player->setCurrentFrame(0);
    syncFontSymbols();
    warnAboutFonts();
    if (_flaDocument)
    {
        if (!_switchingTabs)
            scriptEvent(filePath.isEmpty() && !_flaDocument->fromSwf ? "documentNew" : "documentOpened");
        scriptEvent("documentChanged");
    }
    // Shared palettes that other documents changed come in once it shows.
    if (_palettes && _flaDocument)
        QTimer::singleShot(0, _palettes, &PalettesPanel::syncFiles);
    // A document shown that has no tab yet (the first one, a recovered one)
    // gets one; a tab whose document failed to open goes.
    if (!_switchingTabs)
    {
        if (_flaDocument && (_activeTab < 0 || _activeTab >= static_cast<int>(_tabs.size())))
        {
            addTab();
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

void MainWindow::finishPanelEdits()
{
    // A field losing focus finishes its edit, now, while its document and
    // history are still the ones in hand.
    QWidget* focused = QApplication::focusWidget();
    if (!focused)
        return;
    for (QWidget* panel : std::initializer_list<QWidget*>{_propertiesPanel, _behaviors})
        if (panel && panel->isAncestorOf(focused))
        {
            focused->clearFocus();
            return;
        }
}

void MainWindow::parkActiveDocument()
{
    if (!_flaDocument || _activeTab < 0 || _activeTab >= static_cast<int>(_tabs.size()))
        return;
    // Text being typed goes into the document it was typed in, in a panel's
    // field too.
    finishPanelEdits();
    settleGestures();
    DocumentTab& tab = *_tabs[static_cast<size_t>(_activeTab)];
    if (_phoenixView)
    {
        _phoenixView->exitToDepth(0);
        tab.view = _phoenixView->viewState();
    }
    tab.path = _documentPath;
    tab.name = _documentName;
    tab.frame = _player ? _player->currentFrame() : 0;
    _editContext.commandStack().swapHistory(tab.history);
    tab.document = _flaDocument;
    // Off the window without deleting it: the panels and the history let
    // go of it as releaseDocument has them do.
    _flaDocument = nullptr;
    releaseDocument();
    releaseParkedMedia(tab);
}

void MainWindow::releaseParkedMedia(DocumentTab& tab)
{
    if (!tab.document || !tab.document->document)
        return;
    // A recovery copy being written reads the media too.
    if (tab.autoSave)
        tab.autoSave->waitForWrite();
    fla::media::releaseLoadedMedia(*tab.document->document);
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
    // As it was left; a document shown for the first time is fitted.
    if (_phoenixView && tab.view.valid)
        _phoenixView->setViewState(tab.view);
    _switchingTabs = false;
    updateEditState();
    updateWindowTitle();
    refreshTabs();
}

MainWindow::DocumentTab& MainWindow::addTab()
{
    _tabs.push_back(std::make_unique<DocumentTab>());
    DocumentTab& tab = *_tabs.back();
    tab.owner = this;
    giveAutoSave(tab);
    return tab;
}

void MainWindow::giveAutoSave(DocumentTab& tab)
{
    // Headless windows write no recovery copies.
    if (!_interactive || tab.autoSave)
        return;
    // The tab is reached through the window it is in now, which a move to
    // another window changes.
    DocumentTab* reach = &tab;
    AutoSave::Links links;
    links.document = [reach]() -> const fla::Document* { return reach->owner ? reach->owner->tabDocument(*reach) : nullptr; };
    links.modified = [reach]() { return reach->owner && reach->owner->tabModified(*reach); };
    links.path = [reach]() { return reach->owner ? reach->owner->tabPath(*reach) : QString(); };
    links.name = [reach]() { return reach->owner ? reach->owner->tabName(*reach) : QString(); };
    links.busy = []() { return QApplication::mouseButtons() != Qt::NoButton; };
    tab.autoSave = std::make_unique<AutoSave>(links);
}

const fla::Document* MainWindow::tabDocument(const DocumentTab& tab) const
{
    const bool active = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) &&
        _tabs[static_cast<size_t>(_activeTab)].get() == &tab;
    if (active)
        return _flaDocument ? _flaDocument->document : nullptr;
    return tab.document ? tab.document->document : nullptr;
}

QString MainWindow::tabPath(const DocumentTab& tab) const
{
    const bool active = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) &&
        _tabs[static_cast<size_t>(_activeTab)].get() == &tab;
    return active ? _documentPath : tab.path;
}

QString MainWindow::tabName(const DocumentTab& tab) const
{
    const bool active = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) &&
        _tabs[static_cast<size_t>(_activeTab)].get() == &tab;
    const QString name = active ? _documentName : tab.name;
    return name.isEmpty() ? QString("Untitled") : name;
}

bool MainWindow::tabModified(const DocumentTab& tab) const
{
    const bool active = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) &&
        _tabs[static_cast<size_t>(_activeTab)].get() == &tab;
    if (active)
        return _flaDocument && _editContext.isModified();
    return tab.document && !tab.history.isClean();
}

AutoSave* MainWindow::activeAutoSave() const
{
    if (_activeTab < 0 || _activeTab >= static_cast<int>(_tabs.size()))
        return nullptr;
    return _tabs[static_cast<size_t>(_activeTab)]->autoSave.get();
}

void MainWindow::makeRoomForDocument()
{
    if (!_flaDocument)
        return;
    // Text being typed makes a new document one worth keeping.
    settleGestures();
    // A blank document nobody has touched is simply replaced.
    if (_documentPath.isEmpty() && !_editContext.isModified())
        return;
    _switchingTabs = true;
    parkActiveDocument();
    addTab();
    _activeTab = static_cast<int>(_tabs.size()) - 1;
    _switchingTabs = false;
}

void MainWindow::openInTab(const QString& path)
{
    if (path.isEmpty())
        return;
    // Open already, here or in another window: its tab.
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (MainWindow* window : s_windows.isEmpty() ? QList<MainWindow*>{this} : s_windows)
    {
        for (size_t i = 0; i < window->_tabs.size(); ++i)
        {
            const QString tabPath = window->tabPath(*window->_tabs[i]);
            if (!tabPath.isEmpty() && QFileInfo(tabPath).absoluteFilePath() == wanted)
            {
                window->activateTab(static_cast<int>(i));
                if (window != this)
                {
                    window->raise();
                    window->activateWindow();
                }
                return;
            }
        }
    }
    makeRoomForDocument();
    loadFLAFile(path);
}

bool MainWindow::closeTab(int index, bool ask)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()))
        return true;
    if (index != _activeTab && (!ask || !tabModified(*_tabs[static_cast<size_t>(index)])))
    {
        // Waiting and nothing to ask about: it goes without being shown.
        std::unique_ptr<DocumentTab> tab = std::move(_tabs[static_cast<size_t>(index)]);
        _tabs.erase(_tabs.begin() + index);
        if (index < _activeTab)
            --_activeTab;
        if (tab->autoSave)
            tab->autoSave->discard();
        forgetDocument(tab->document);
        // The history points into the document, so it goes first.
        tab->history.clear();
        delete tab->document;
        refreshTabs();
        return true;
    }
    if (index != _activeTab)
        activateTab(index);
    // Asking spins the event loop, where tabs can come and go: the one to
    // close is found again after, and must still be the one shown.
    const DocumentTab* closing = _tabs[static_cast<size_t>(index)].get();
    if (ask && !confirmDiscardChanges())
        return false;
    const auto still = std::find_if(_tabs.begin(), _tabs.end(),
        [closing](const std::unique_ptr<DocumentTab>& tab) { return tab.get() == closing; });
    if (still == _tabs.end())
        return true;
    index = static_cast<int>(still - _tabs.begin());
    if (index != _activeTab)
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
        // The last document gone from the window Phoenix started with: the
        // start screen comes back rather than an empty editor (not while
        // quitting, and not when it was turned off).
        if (!_secondary && !s_quitting && _interactive && isVisible() &&
            QSettings().value("showStartDialog", true).toBool())
        {
            QTimer::singleShot(0, this, [this]() {
                if (_tabs.empty() && !s_quitting && isVisible())
                    showStartDialog();
            });
        }
    }
    refreshTabs();
    return true;
}

bool MainWindow::closeOtherTabs(int keep)
{
    if (keep < 0 || keep >= static_cast<int>(_tabs.size()))
        return false;
    const DocumentTab* kept = _tabs[static_cast<size_t>(keep)].get();
    if (!settleUnsaved({this}, this, kept))
        return false;
    for (int i = static_cast<int>(_tabs.size()) - 1; i >= 0; --i)
    {
        if (_tabs[static_cast<size_t>(i)].get() != kept)
            closeTab(i, false);
    }
    return true;
}

bool MainWindow::closeAllTabs()
{
    if (!settleUnsaved({this}, this))
        return false;
    // The waiting ones first, so none is shown on its way out.
    for (int i = static_cast<int>(_tabs.size()) - 1; i >= 0; --i)
    {
        if (i != _activeTab)
            closeTab(i, false);
    }
    if (!_tabs.empty())
        closeTab(_activeTab, false);
    return true;
}

void MainWindow::cycleTabs(int step)
{
    const int count = static_cast<int>(_tabs.size());
    if (count < 2)
        return;
    activateTab(((_activeTab + step) % count + count) % count);
}

void MainWindow::revealInFileManager(const QString& path)
{
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
#if defined(Q_OS_WIN)
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", info.absoluteFilePath()});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

void MainWindow::showTabMenu(int index, const QPoint& globalPos)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()))
        return;
    // The tab is found again by itself afterward: closing others moves it.
    DocumentTab* tab = _tabs[static_cast<size_t>(index)].get();
    const auto indexOf = [this, tab]() {
        for (size_t i = 0; i < _tabs.size(); ++i)
            if (_tabs[i].get() == tab)
                return static_cast<int>(i);
        return -1;
    };
    const QString path = tabPath(*tab);
    QMenu menu(this);
    menu.addAction("&Close", this, [this, indexOf]() { closeTab(indexOf()); });
    QAction* others = menu.addAction("Close &Others", this, [this, indexOf]() { closeOtherTabs(indexOf()); });
    others->setEnabled(_tabs.size() > 1);
    menu.addAction("Close &All", this, [this]() { closeAllTabs(); });
    menu.addSeparator();
    QAction* copyPath = menu.addAction("Copy &Path", this, [path]() {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    });
    copyPath->setEnabled(!path.isEmpty());
#if defined(Q_OS_MACOS)
    const QString reveal = "Reveal in &Finder";
#elif defined(Q_OS_WIN)
    const QString reveal = "Show in &Explorer";
#else
    const QString reveal = "Show in &Folder";
#endif
    QAction* show = menu.addAction(reveal, this, [path]() { revealInFileManager(path); });
    show->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
    menu.addSeparator();
    QAction* newWindow = menu.addAction("Move to &New Window", this, [this, indexOf]() {
        moveTabToNewWindow(indexOf());
    });
    newWindow->setEnabled(_tabs.size() > 1);
    QMenu* toWindow = menu.addMenu("Move to &Window");
    for (MainWindow* window : s_windows)
    {
        if (window == this || !window->isVisible())
            continue;
        toWindow->addAction(window->windowTitle(), this, [this, indexOf, window]() {
            if (s_windows.contains(window))
                moveTabToWindow(indexOf(), window);
        });
    }
    toWindow->setEnabled(!toWindow->isEmpty());
    menu.exec(globalPos);
}

std::unique_ptr<MainWindow::DocumentTab> MainWindow::detachTab(int index)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()))
        return nullptr;
    const bool wasActive = index == _activeTab;
    _switchingTabs = true;
    if (wasActive)
        parkActiveDocument();
    std::unique_ptr<DocumentTab> tab = std::move(_tabs[static_cast<size_t>(index)]);
    _tabs.erase(_tabs.begin() + index);
    tab->owner = nullptr;
    if (wasActive)
        _activeTab = -1;
    else if (index < _activeTab)
        --_activeTab;
    _switchingTabs = false;
    if (wasActive && !_tabs.empty())
        activateTab(std::min(index, static_cast<int>(_tabs.size()) - 1));
    else if (wasActive)
    {
        adoptDocument(nullptr, QString(), QString());
        updateEditState();
    }
    refreshTabs();
    return tab;
}

void MainWindow::attachTab(std::unique_ptr<DocumentTab> tab, int at)
{
    if (!tab)
        return;
    tab->owner = this;
    giveAutoSave(*tab);
    if (at < 0 || at > static_cast<int>(_tabs.size()))
        at = static_cast<int>(_tabs.size());
    _tabs.insert(_tabs.begin() + at, std::move(tab));
    if (_activeTab >= at)
        ++_activeTab;
    activateTab(at);
    refreshTabs();
}

void MainWindow::moveTabToWindow(int index, MainWindow* target, int at)
{
    if (!target || target == this || index < 0 || index >= static_cast<int>(_tabs.size()))
        return;
    std::unique_ptr<DocumentTab> tab = detachTab(index);
    if (!tab)
        return;
    // What this window kept about it (copied layers, bitmaps out in an
    // editor) stays behind; undoing there keeps that window's selection in
    // step, not this one's.
    forgetDocument(tab->document);
    tab->history.retargetSelection(&_selection, &target->_selection);
    target->attachTab(std::move(tab), at);
    target->show();
    target->raise();
    target->activateWindow();
    // A window opened for tabs goes once it has none.
    if (_tabs.empty() && _secondary)
        close();
}

void MainWindow::moveTabToNewWindow(int index, const QPoint* globalPos)
{
    if (index < 0 || index >= static_cast<int>(_tabs.size()) || _tabs.size() < 2)
        return;
    MainWindow* window = openSecondaryWindow();
    if (isMaximized() || isFullScreen())
        window->showNormal();
    if (globalPos)
        window->move(*globalPos - QPoint(window->width() / 3, 20));
    else
        window->move(pos() + QPoint(40, 40));
    window->keepOnScreen();
    moveTabToWindow(index, window);
}

void MainWindow::tabDraggedOut(int index)
{
    if (!_tabBar || index < 0 || index >= static_cast<int>(_tabs.size()))
        return;
    QDrag* drag = new QDrag(_tabBar);
    QMimeData* data = new QMimeData;
    data->setData(kTabMime, QByteArray::number(index));
    drag->setMimeData(data);
    drag->setPixmap(_tabBar->grab(_tabBar->tabRect(index)));
    // Let go over another window's tabs, that window takes it (its bar's
    // drop does the move); anywhere else, a window of its own.
    if (drag->exec(Qt::MoveAction) == Qt::IgnoreAction && _tabs.size() > 1)
    {
        const QPoint at = QCursor::pos();
        moveTabToNewWindow(index, &at);
    }
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
        const DocumentTab& tab = *_tabs[i];
        _tabBar->setTabText(static_cast<int>(i), tabName(tab) + (tabModified(tab) ? "*" : ""));
        _tabBar->setTabToolTip(static_cast<int>(i), QDir::toNativeSeparators(tabPath(tab)));
    }
    if (_activeTab >= 0)
        _tabBar->setCurrentIndex(_activeTab);
    // One document needs no tabs, except in a window opened to hold one,
    // whose tab can be dragged back.
    _tabBar->setVisible(_tabs.size() > 1 || (_secondary && !_tabs.empty()));
    _switchingTabs = false;
}

bool MainWindow::settleUnsaved(const QList<MainWindow*>& windows, QWidget* parent, const DocumentTab* except)
{
    struct Unsaved
    {
        QPointer<MainWindow> window;
        const DocumentTab* tab;
    };
    std::vector<Unsaved> unsaved;
    for (MainWindow* window : windows)
    {
        // Text being typed counts as a change to save.
        if (window->_flaDocument)
            window->settleGestures();
        for (const std::unique_ptr<DocumentTab>& tab : window->_tabs)
            if (tab.get() != except && window->tabModified(*tab))
                unsaved.push_back({window, tab.get()});
    }
    if (unsaved.empty())
        return true;
    // Each is shown in turn to be asked about or saved.
    const auto show = [](const Unsaved& entry) {
        if (!entry.window)
            return false;
        for (size_t i = 0; i < entry.window->_tabs.size(); ++i)
        {
            if (entry.window->_tabs[i].get() != entry.tab)
                continue;
            entry.window->activateTab(static_cast<int>(i));
            entry.window->raise();
            entry.window->activateWindow();
            return true;
        }
        return false;
    };
    const auto review = [&show](const Unsaved& entry) {
        return !show(entry) || entry.window->confirmDiscardChanges();
    };
    if (unsaved.size() == 1)
        return review(unsaved.front());

    QStringList names;
    for (const Unsaved& entry : unsaved)
        names << entry.window->tabName(*entry.tab);
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Unsaved Changes");
    box.setText(QString("%1 documents have unsaved changes.").arg(unsaved.size()));
    box.setInformativeText(names.join("\n"));
    QPushButton* saveAll = box.addButton("Save All", QMessageBox::AcceptRole);
    QPushButton* discardAll = box.addButton("Discard All", QMessageBox::DestructiveRole);
    QPushButton* reviewEach = box.addButton("Review Each…", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(saveAll);
    box.exec();
    const QAbstractButton* chosen = box.clickedButton();
    if (chosen == discardAll)
        return true;
    if (chosen == saveAll)
    {
        for (const Unsaved& entry : unsaved)
        {
            if (!show(entry))
                continue;
            entry.window->saveFile();
            // Save As canceled, or a save that failed: the rest wait.
            if (entry.window->_editContext.isModified())
                return false;
        }
        return true;
    }
    if (chosen == reviewEach)
    {
        for (const Unsaved& entry : unsaved)
            if (!review(entry))
                return false;
        return true;
    }
    return false;
}

void MainWindow::quitAll()
{
    // The window quitting from first: the document it shows comes back
    // shown next time.
    QList<MainWindow*> all = s_windows;
    all.removeAll(this);
    all.prepend(this);
    if (!settleUnsaved(all, this))
        return;
    saveSession(all);
    s_quitting = true;
    for (MainWindow* window : all)
        if (window != this)
            window->close();
    close();
    s_quitting = false;
}

void MainWindow::saveSession(const QList<MainWindow*>& windows)
{
    QSettings settings;
    if (!settings.value("reopenDocuments", true).toBool())
    {
        settings.remove("session/documents");
        return;
    }
    std::vector<std::vector<fla::session::OpenDocument>> open;
    for (const MainWindow* window : windows)
    {
        std::vector<fla::session::OpenDocument> documents;
        for (size_t i = 0; i < window->_tabs.size(); ++i)
            documents.push_back({window->tabPath(*window->_tabs[i]).toStdString(), static_cast<int>(i) == window->_activeTab});
        open.push_back(documents);
    }
    QStringList paths;
    for (const std::string& path : fla::session::reopenList(open))
        paths << QString::fromStdString(path);
    settings.setValue("session/documents", paths);
}

bool MainWindow::reopenSession()
{
    QSettings settings;
    if (!settings.value("reopenDocuments", true).toBool())
        return false;
    bool any = false;
    for (const QString& path : settings.value("session/documents").toStringList())
    {
        if (!QFileInfo::exists(path))
            continue;
        openInTab(path);
        any = any || _flaDocument;
    }
    return any;
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
    if (AutoSave* autoSave = activeAutoSave())
        autoSave->waitForWrite();
    if (!phx::save(*_flaDocument->document, path.toStdString(), options, error))
        QMessageBox::warning(this, "Save as Template", QString::fromStdString(error));
    else
        statusBar()->showMessage(QString("Saved the template %1.").arg(name), 3000);
}

void MainWindow::serveMcpOnStdio()
{
    if (!_mcpServer)
        _mcpServer = new McpServer(this);
    _mcpServer->serveStdio();
}

void MainWindow::refreshAfterScript()
{
    // A script may have renamed scenes, or undone a change to them.
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (document && _shownScene && _shownScene != document->currentScene())
        goToScene(-1);
    rebuildBreadcrumb();
    if (_timelineView)
        _timelineView->refresh();
    if (_documentView)
        _documentView->setDocument(_flaDocument);
    if (_phoenixView)
    {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    updateEditState();
    updateSelectionState();
}

bool MainWindow::isReadOnlyDocument() const
{
    return _documentName.endsWith(" (read-only)");
}

void MainWindow::skipStartScreen()
{
    s_skipStartScreen = true;
}

bool MainWindow::hasDocument(const fla::FLADocument* document) const
{
    if (!document)
        return false;
    if (document == _flaDocument)
        return true;
    for (const auto& tab : _tabs)
    {
        if (tab && tab->document == document)
            return true;
    }
    return false;
}

QString MainWindow::toolName(const Tool* tool) const
{
    if (!tool || !_toolbox)
        return QString();
    for (const auto& [reach, action] : _toolActionList)
    {
        if (reach() != tool)
            continue;
        for (const QString& id : _toolbox->toolIds())
        {
            if (_toolbox->tool(id) == action)
                return id;
        }
    }
    return QString();
}

QString MainWindow::selectionKind() const
{
    for (fla::DOMElement* selected : _selection.elements())
    {
        const fla::Element* element = dynamic_cast<const fla::Element*>(selected);
        if (!element)
            continue;
        switch (element->elementType())
        {
        case fla::Element::Type::Shape:
            return "shape";
        case fla::Element::Type::SymbolInstance:
            return "instance";
        case fla::Element::Type::StaticText:
            return "text";
        case fla::Element::Type::Group:
            return "group";
        default:
            return "other";
        }
    }
    return "none";
}

void MainWindow::newLessonDocument(int width, int height, double frameRate)
{
    makeRoomForDocument();
    createNewDocument(width, height, frameRate, Qt::white);
}

void MainWindow::openLessonDocument(const QString& path)
{
    // As New from Template does: a copy, untitled, never the file itself.
    makeRoomForDocument();
    loadFLAFile(path);
    forgetRecentFile(path);
    if (_flaDocument)
    {
        _documentPath.clear();
        _documentName = "Untitled";
        _editContext.markUnsaved();
        updateWindowTitle();
        updateEditState();
    }
    if (_phoenixView)
        QTimer::singleShot(0, this, [this]() { _phoenixView->zoomToFit(); });
}

void MainWindow::openLessonFinal(const QString& path, const QString& name)
{
    // A tab of its own even beside an untouched lesson document, which
    // the lesson goes on with.
    if (_flaDocument)
    {
        settleGestures();
        _switchingTabs = true;
        parkActiveDocument();
        addTab();
        _activeTab = static_cast<int>(_tabs.size()) - 1;
        _switchingTabs = false;
    }
    loadFLAFile(path);
    forgetRecentFile(path);
    if (_flaDocument)
    {
        _documentPath.clear();
        _documentName = name;
        _editContext.markUnsaved();
        updateWindowTitle();
        updateEditState();
    }
    if (_phoenixView)
        QTimer::singleShot(0, this, [this]() { _phoenixView->zoomToFit(); });
}

void MainWindow::showTutorials()
{
    TutorialsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || dialog.chosen().isEmpty())
        return;
    if (!dialog.finalChosen())
    {
        startTutorial(dialog.chosen());
        return;
    }
    for (const TutorialLesson& lesson : tutorials::allLessons())
        if (lesson.id == dialog.chosen())
        {
            QString error;
            if (!tutorials::showFinal(*this, lesson, error))
                QMessageBox::warning(this, "Tutorials", QString("The final could not be shown: %1").arg(error));
            return;
        }
}

void MainWindow::startTutorial(const QString& id)
{
    for (const TutorialLesson& lesson : tutorials::allLessons())
    {
        if (lesson.id != id)
            continue;
        if (!_tutorial)
            _tutorial = new TutorialRunner(this);
        QString error;
        if (!_tutorial->start(lesson, error))
            QMessageBox::warning(this, "Tutorials", QString("The lesson could not start: %1").arg(error));
        return;
    }
    QMessageBox::warning(this, "Tutorials", QString("There is no lesson called %1.").arg(id));
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
    forgetRecentFile(files[index].absoluteFilePath());
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
    // Ctrl+I), in a group of their own.
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

    // The Motion Editor, below the stage, closed until asked.
    MotionEditorLinks motionLinks;
    motionLinks.commands = [this]() { return &_editContext.commandStack(); };
    motionLinks.span = [this]() { return currentMotionSpan(); };
    motionLinks.remapped = [this]() -> fla::SymbolInstance* {
        const std::vector<fla::Element*> elements = selectedElements();
        if (elements.size() != 1 || !elements.front() ||
            elements.front()->elementType() != fla::Element::Type::SymbolInstance)
            return nullptr;
        auto* instance = static_cast<fla::SymbolInstance*>(elements.front());
        for (const fla::behaviors::Applied& applied : fla::behaviors::of(*instance))
            if (applied.type == "timeremap" && applied.enabled)
                return instance;
        return nullptr;
    };
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
    motionLinks.behaviorsChanged = [this]() {
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
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

    // Actions, closed until asked.
    if (!_actionsDock)
    {
        ActionsLinks links;
        links.commands = [this]() -> fla::CommandStack* {
            return _flaDocument && _flaDocument->document ? &_editContext.commandStack() : nullptr;
        };
        links.document = [this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; };
        links.timeline = [this]() -> fla::Timeline* {
            if (!_flaDocument || !_flaDocument->document)
                return nullptr;
            fla::Timeline* scope = _phoenixView ? _phoenixView->scopeTimeline() : nullptr;
            return scope ? scope : _flaDocument->document->currentScene();
        };
        links.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        links.selection = [this]() { return selectedElements(); };
        _actionsPanel = new ActionsPanel(links);
        _actionsDock = new QDockWidget("Actions", this);
        _actionsDock->setObjectName("actionsDock");
        _actionsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _actionsPanel->setParent(_actionsDock);
        _actionsDock->setWidget(_actionsPanel);
        addDockWidget(Qt::BottomDockWidgetArea, _actionsDock);
        splitDockWidget(_timelineDock, _actionsDock, Qt::Horizontal);
        _actionsDock->hide();
        connect(_actionsDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
            if (shown)
                _actionsPanel->refresh();
            else
                _actionsPanel->commit();
        });
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

    // Behaviors, closed until asked.
    if (!_behaviorsDock)
    {
        BehaviorsLinks links;
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.instances = [this]() {
            std::vector<fla::SymbolInstance*> instances;
            for (fla::DOMElement* selected : _selection.elements())
                if (auto* instance = dynamic_cast<fla::SymbolInstance*>(selected))
                {
                    fla::Frame* frame = frameOfElement(instance);
                    auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
                    if (!layer || !layer->isLocked())
                        instances.push_back(instance);
                }
            return instances;
        };
        links.changed = [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        };
        links.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        links.shapes = [this]() {
            std::vector<fla::Shape*> shapes;
            for (fla::DOMElement* selected : _selection.elements())
                if (auto* shape = dynamic_cast<fla::Shape*>(selected))
                {
                    fla::Frame* frame = frameOfElement(shape);
                    auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
                    if (!layer || !layer->isLocked())
                        shapes.push_back(shape);
                }
            return shapes;
        };
        links.adjustmentLayer = [this]() -> fla::Layer* {
            fla::Timeline* timeline = nullptr;
            int index = 0;
            if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
                index >= static_cast<int>(timeline->layers.size()))
                return nullptr;
            fla::Layer* layer = timeline->layers[static_cast<size_t>(index)];
            return layer && fla::compositing::isAdjustment(*layer) ? layer : nullptr;
        };
        links.texts = [this]() {
            std::vector<fla::StaticText*> texts;
            for (fla::DOMElement* selected : _selection.elements())
                if (auto* text = dynamic_cast<fla::StaticText*>(selected))
                {
                    fla::Frame* frame = frameOfElement(text);
                    auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
                    if (!layer || !layer->isLocked())
                        texts.push_back(text);
                }
            return texts;
        };
        links.bake = [this]() { bakeSelectedBehaviors(); };
        links.openScript = [this](const QString& path) { openScriptEditor(path); };
        links.objectNames = [this]() {
            QStringList names;
            if (fla::Timeline* timeline = editedTimeline())
                for (const fla::Layer* layer : timeline->layers)
                    for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                        for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                            if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
                                if (!instance->name.empty() && !names.contains(QString::fromStdString(instance->name)))
                                    names << QString::fromStdString(instance->name);
            names.sort(Qt::CaseInsensitive);
            return names;
        };
        links.pickObject = [this, objectNames = links.objectNames](std::function<void(const QString&)> done) {
            if (!_phoenixView)
                return;
            statusBar()->showMessage("Click the instance to link to on the stage. Esc cancels.");
            _phoenixView->pickInstance([this, done, objectNames](fla::SymbolInstance* instance) {
                statusBar()->clearMessage();
                if (!instance)
                    return;
                std::string name = instance->name;
                if (name.empty())
                {
                    // Named after its symbol, in a way scripts can use too.
                    QString base = QFileInfo(QString::fromStdString(instance->libraryItemName)).fileName();
                    base.remove(QRegularExpression("[^A-Za-z0-9_]"));
                    if (base.isEmpty() || base[0].isDigit())
                        base = "object" + base;
                    base[0] = base[0].toLower();
                    const QStringList taken = objectNames ? objectNames() : QStringList();
                    QString chosen = base;
                    for (int n = 2; taken.contains(chosen); ++n)
                        chosen = base + QString::number(n);
                    name = chosen.toStdString();
                    _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(
                        instance->name, instance->name, name, "Instance Name")));
                }
                done(QString::fromStdString(name));
            });
        };
        links.controllerNames = [this]() {
            QStringList names;
            if (_flaDocument && _flaDocument->document)
                for (const fla::controllers::Controller& controller : fla::controllers::load(*_flaDocument->document))
                    names << QString::fromStdString(controller.name);
            return names;
        };
        _behaviors = new BehaviorsPanel(links);
        _behaviorsDock = new QDockWidget("Behaviors", this);
        _behaviorsDock->setObjectName("behaviorsDock");
        _behaviorsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _behaviors->setParent(_behaviorsDock);
        _behaviorsDock->setWidget(_behaviors);
        addDockWidget(Qt::RightDockWidgetArea, _behaviorsDock);
        // A tab beside Properties, where its settings have room.
        if (QDockWidget* properties = findChild<QDockWidget*>("propertiesDock"))
            tabifyDockWidget(properties, _behaviorsDock);
        _behaviorsDock->hide();
        // Behavior files read again redraw the stage; their errors go to
        // the Output panel.
        BehaviorLibrary& library = BehaviorLibrary::instance();
        if (!library.report)
            library.report = [](const QString& message) {
                for (MainWindow* window : s_windows)
                    if (window && window->_scripts && window->_scripts->output())
                    {
                        window->_scripts->output()->appendError(message);
                        break;
                    }
            };
        connect(&library, &BehaviorLibrary::changed, this, [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        });
    }

    // The Movie Explorer, closed until asked; filled when shown.
    if (!_movieExplorerDock)
    {
        _movieExplorer = new MovieExplorer([this]() -> fla::Document* { return _flaDocument ? _flaDocument->document : nullptr; });
        _movieExplorerDock = new QDockWidget("Movie Explorer", this);
        _movieExplorerDock->setObjectName("movieExplorerDock");
        _movieExplorerDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _movieExplorer->setParent(_movieExplorerDock);
        _movieExplorerDock->setWidget(_movieExplorer);
        addDockWidget(Qt::RightDockWidgetArea, _movieExplorerDock);
        _movieExplorerDock->hide();
        connect(_movieExplorerDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            if (visible)
                _movieExplorer->refresh();
        });
        connect(_movieExplorer, &MovieExplorer::goToRequested, this, &MainWindow::goToMatch);
        connect(_movieExplorer, &MovieExplorer::showInLibraryRequested, this, [this](const QString& name) {
            if (!_libraryPanel)
                return;
            for (QWidget* parent = _libraryPanel->parentWidget(); parent; parent = parent->parentWidget())
                if (auto* dock = qobject_cast<QDockWidget*>(parent))
                {
                    dock->show();
                    dock->raise();
                    break;
                }
            _libraryPanel->refreshIfStale();
            _libraryPanel->selectItem(name);
        });
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

    // The Node Graph, closed until asked, a tab beside the Timeline.
    if (!_nodeGraphDock)
    {
        NodeGraphLinks links;
        links.commands = [this]() { return &_editContext.commandStack(); };
        links.timeline = [this]() { return editedTimeline(); };
        links.timelineName = [this]() {
            const fla::Timeline* timeline = editedTimeline();
            return timeline ? QString::fromStdString(timeline->name) : QString();
        };
        links.layerNames = [this]() {
            QStringList names;
            if (const fla::Timeline* timeline = editedTimeline())
                for (const fla::Layer* layer : timeline->layers)
                    if (layer && !names.contains(QString::fromStdString(layer->name)))
                        names << QString::fromStdString(layer->name);
            return names;
        };
        links.pictures = [this](const fla::nodegraph::Graph& graph, const QSize& size, QMap<int, QString>* problems) {
            return _phoenixView ? _phoenixView->nodePictures(editedTimeline(), graph, size, problems) : QMap<int, QImage>();
        };
        links.stageAspect = [this]() {
            const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            return document && document->height > 0 ? double(document->width) / document->height : 16.0 / 9.0;
        };
        links.view = [this](int node) {
            if (_phoenixView)
                _phoenixView->setGraphView(editedTimeline(), node);
        };
        links.changed = [this]() {
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
        };
        links.currentFrame = [this]() { return _player ? _player->currentFrame() : 0; };
        links.pickLayer = [this](const QString& name) {
            const fla::Timeline* timeline = editedTimeline();
            if (!timeline || !_timelineView)
                return;
            for (const fla::Layer* layer : timeline->layers)
                if (layer && QString::fromStdString(layer->name) == name)
                {
                    _timelineView->setCurrentLayer(layer);
                    break;
                }
        };
        _nodeGraph = new NodeGraphPanel(links);
        _nodeGraphDock = new QDockWidget("Node Graph", this);
        _nodeGraphDock->setObjectName("nodeGraphDock");
        _nodeGraphDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _nodeGraph->setParent(_nodeGraphDock);
        _nodeGraphDock->setWidget(_nodeGraph);
        addDockWidget(Qt::BottomDockWidgetArea, _nodeGraphDock);
        if (_timelineDock)
            tabifyDockWidget(_timelineDock, _nodeGraphDock);
        _nodeGraphDock->hide();
        connect(_nodeGraphDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
            if (shown)
                _nodeGraph->refresh();
            else if (_phoenixView)
                _phoenixView->setGraphView(nullptr, 0);
        });
        // The pictures follow the frame shown, and the timeline being
        // edited.
        connect(_player, &Player::currentFrameChanged, _nodeGraph, [this]() {
            if (_nodeGraphDock->isVisible() && !_player->isPlaying())
                _nodeGraph->refreshPictures();
        });
        connect(_phoenixView, &PhoenixView::scopeChanged, _nodeGraph, [this]() {
            if (_nodeGraphDock->isVisible())
                _nodeGraph->refresh();
        });
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
        links.folder = [this]() {
            return _documentPath.isEmpty() ? QString() : QFileInfo(_documentPath).absolutePath();
        };
        links.message = [this](const QString& text) { statusBar()->showMessage(text, 6000); };
        _palettes = new PalettesPanel(links);
        _palettesDock = new QDockWidget("Palettes", this);
        _palettesDock->setObjectName("palettesDock");
        _palettesDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        _palettes->setParent(_palettesDock);
        _palettesDock->setWidget(_palettes);
        addDockWidget(Qt::RightDockWidgetArea, _palettesDock);
        _palettesDock->hide();
    }

    // The X-sheet, a view of the exposures, closed until asked.
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
    // The Window menu: every panel, ticked when open. Choosing an
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
    // A side's panels folded to a strip of their names, to give the stage
    // the room; each opens beside the strip.
    for (const auto& [area, label] : {std::pair<Qt::DockWidgetArea, const char*>{Qt::RightDockWidgetArea,
                                          "Collapse &Right Panels to Icons"},
             {Qt::LeftDockWidgetArea, "Collapse &Left Panels to Icons"}})
    {
        QAction* collapse = windowMenu->addAction(label);
        collapse->setCheckable(true);
        collapse->setStatusTip("Fold the panels docked on that side into a strip of their names; click one to open it");
        const Qt::DockWidgetArea side = area;
        connect(collapse, &QAction::toggled, this, [this, side](bool on) { setPanelsCollapsed(side, on); });
        _collapseActions[side] = collapse;
    }
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
            {"&Actions", _actionsDock, QKeySequence(Qt::Key_F9), "The script of the frame at the playhead, or of the button or movie clip selected"},
            {"&History", _historyDock, QKeySequence(Qt::CTRL | Qt::Key_F10), "Every step that can be undone; click one to go back or forward to it"},
            {"&Output", _scripts ? _scripts->outputDock() : nullptr, QKeySequence(Qt::Key_F2), "What scripts trace, and the errors they stop on"},
            {"&Review Notes", _notesDock, QKeySequence(), "Notes on frames from whoever reviews the shot; they never export"},
            {"Co&ntrollers", _controllersDock, QKeySequence(), "Master controllers: one slider driving properties on several layers"},
            {"&Behaviors", _behaviorsDock, QKeySequence(), "Motion worked out every frame on the selected symbols: wiggle, oscillate, follow with lag and more"},
            {"Sc&ene", _scenesDock, QKeySequence(Qt::SHIFT | Qt::Key_F2), "The document's scenes: switch, add, duplicate, delete, rename and reorder them"},
            {"Mo&vie Explorer", _movieExplorerDock, QKeySequence(Qt::ALT | Qt::Key_F3), "The whole document as a tree: scenes, layers, frames, objects and symbols"},
            {"F&unction Curves", _functionCurvesDock, QKeySequence(), "Any property of the current layer as a curve over time, its keyframes dragged up or down"},
            {"&Node Graph", _nodeGraphDock, QKeySequence(), "Composite the timeline's layers through a graph of nodes: effects, merges, mattes"},
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
    motionPresetsAction->setStatusTip("Saved motions to put on an object");
    connect(motionPresetsAction, &QAction::triggered, this, &MainWindow::showMotionPresets);
    QAction* brushLibraryAction = windowMenu->addAction("Brush &Library…");
    brushLibraryAction->setStatusTip("Brushes to paint with in any document, by category");
    connect(brushLibraryAction, &QAction::triggered, this, &MainWindow::showBrushLibrary);
    QAction* brushesAction = windowMenu->addAction("Document &Brushes…");
    brushesAction->setStatusTip("The paint brushes and symbols the document's strokes are drawn with");
    connect(brushesAction, &QAction::triggered, this, &MainWindow::manageBrushes);
    QAction* renderQueueAction = windowMenu->addAction("&Render Queue…");
    renderQueueAction->setStatusTip("Renders lined up and run one after another while you work");
    connect(renderQueueAction, &QAction::triggered, this, [this]() {
        if (!_renderQueue)
        {
            // Add Current renders the saved file: unsaved changes are saved
            // first, or the document is left out.
            _renderQueue = new RenderQueue([this]() -> QString {
                if (!_flaDocument || !_flaDocument->document)
                    return QString();
                if (_documentPath.isEmpty() || _editContext.isModified())
                {
                    if (QMessageBox::question(this, "Render Queue", "Save the document before queueing it?") != QMessageBox::Yes)
                        return QString();
                    saveFile();
                    if (_documentPath.isEmpty() || _editContext.isModified())
                        return QString();
                }
                return _documentPath;
            }, this);
            _renderQueue->setAttribute(Qt::WA_DeleteOnClose, false);
        }
        _renderQueue->show();
        _renderQueue->raise();
        _renderQueue->activateWindow();
    });
    QMenu* extensionsMenu = windowMenu->addMenu("E&xtensions");
    connect(extensionsMenu, &QMenu::aboutToShow, this, [this, extensionsMenu]() { _cepPanels->fillMenu(extensionsMenu); });
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
    connect(_behaviorsDock, &QDockWidget::visibilityChanged, this, [this](bool shown) {
        if (shown)
            _behaviors->refresh();
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
    // The New Document dialog: a preset, or a size and frame rate.
    NewDocumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    makeRoomForDocument();
    createNewDocument(dialog.documentWidth(), dialog.documentHeight(), dialog.frameRate(), dialog.stageColor(),
        dialog.canvas());
}

bool MainWindow::offerRecovery()
{
    QList<AutoSave::Orphan> orphans = AutoSave::orphans();
    if (orphans.isEmpty())
        return false;
    std::sort(orphans.begin(), orphans.end(),
        [](const AutoSave::Orphan& a, const AutoSave::Orphan& b) { return a.saved > b.saved; });

    // One dialog for every copy left behind, newest first: recover any or
    // all, each in a tab of its own, discard any or all, or keep them for
    // next time.
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
        recover->setEnabled(!list->selectedItems().isEmpty());
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
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QList<AutoSave::Orphan> chosen;
    for (int row = 0; row < orphans.size() && row < list->count(); ++row)
        if (list->item(row)->isSelected())
            chosen.push_back(orphans[row]);

    // The newest last, so it is the one shown.
    std::reverse(chosen.begin(), chosen.end());
    QStringList recovered;
    for (const AutoSave::Orphan& orphan : chosen)
    {
        const QString name = orphan.name.isEmpty() ? QString("Untitled") : orphan.name;
        std::string error;
        std::string warning;
        bool readOnly = false;
        fla::FLADocument* document = phx::load(orphan.file.toStdString(), error, warning, readOnly);
        if (!document)
        {
            QMessageBox::warning(this, "Recover Unsaved Work",
                QString("The copy of %1 could not be read: %2").arg(name, QString::fromStdString(error)));
            continue;
        }
        // Back where it came from, and unsaved until it is saved. The copies
        // not chosen are offered again next time.
        makeRoomForDocument();
        adoptDocument(document, orphan.originalPath, name);
        _editContext.markUnsaved();
        updateEditState();
        AutoSave::remove(orphan);
        recovered << name;
    }
    if (recovered.isEmpty())
        return false;
    statusBar()->showMessage(QString("Recovered %1. Save to keep them.").arg(recovered.join(", ")), 8000);
    return true;
}

void MainWindow::showStartDialog()
{
    StartDialog dialog(_recentFiles, this);
    // An update offer already up goes over the dialog rather than under it.
    QTimer::singleShot(0, &dialog, [this, &dialog]() {
        if (_updater)
            _updater->placeOffer(&dialog);
    });
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
    case StartDialog::Choice::Tutorials:
        showTutorials();
        break;
    case StartDialog::Choice::None:
        break;
    }
}

void MainWindow::createNewDocument(int width, int height, double frameRate, const QColor& stage, bool canvas)
{
    fla::FLADocument* document = fla::createEmptyDocument(width, height, frameRate);
    if (document && document->document)
    {
        // An HTML5 Canvas document: its publish profile says so.
        if (canvas)
            fla::canvas::makeCanvasDocument(*document->document, "Untitled");
        document->document->backgroundColor[0] = static_cast<uint8_t>(stage.red());
        document->document->backgroundColor[1] = static_cast<uint8_t>(stage.green());
        document->document->backgroundColor[2] = static_cast<uint8_t>(stage.blue());
        document->document->backgroundColor[3] = 255;
        // The safe zones the user starts every document with.
        document->document->extraAttributes =
            fla::safezones::withSettings(*document->document, SafeZonesDialog::newDocumentDefault());
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
    if (isExtensionPackage(filePath))
    {
        ScriptRunner::installExtension(filePath, isVisible() ? this : nullptr);
        return;
    }
    // The first window may have been closed while others stay open: one of
    // those takes it.
    if (!isVisible())
    {
        for (MainWindow* window : s_windows)
        {
            if (window != this && window->isVisible())
            {
                window->openInTab(filePath);
                window->raise();
                window->activateWindow();
                return;
            }
        }
    }
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

        if (document->fromSwf)
        {
            // A movie brought back as a document: it is saved as one, under
            // a name of its own, never over the movie.
            adoptDocument(document, QString(), fileInfo.completeBaseName() + ".fla");
            addToRecentFiles(filePath);
            if (isVisible())
                QMessageBox::information(this, "Open SWF Movie",
                    QString("%1\n\n%2").arg(fileInfo.fileName(), QString::fromStdString(parser.warningString()).trimmed()));
            else
                std::fprintf(stderr, "%s\n", parser.warningString().c_str());
            return;
        }
        adoptDocument(document, filePath, displayName);
        addToRecentFiles(filePath);
        // With no window (exporting from the command line), a box would wait
        // for a click that never comes.
        if (!parser.warningString().empty() && !isVisible())
        {
            std::fprintf(stderr, "%s\n", parser.warningString().c_str());
        }
        else if (!parser.warningString().empty())
        {
            QMessageBox::information(this, "Open Document",
                QString("%1\n\n%2").arg(fileInfo.fileName(),
                    QString::fromStdString(parser.warningString()).trimmed()));
        }
    }
    else if (!isVisible())
    {
        adoptDocument(nullptr, QString(), QString());
        std::fprintf(stderr, "%s: %s\n", QFileInfo(filePath).fileName().toLocal8Bit().constData(),
            parser.errorString().empty() ? "the file could not be read." : parser.errorString().c_str());
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

    // Every panel docks: the panels down the right take the
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
    // On a narrow window the breadcrumb gives up its room before the stage
    // controls beside it do.
    _breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    _breadcrumbRow = breadcrumbRow;

    QWidget* stageColumn = new QWidget(this);
    QVBoxLayout* stageLayout = new QVBoxLayout(stageColumn);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    _tabBar = new DocumentTabBar(this);
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
        // The list follows the bar; the tab shown stays the one shown,
        // wherever it went.
        const DocumentTab* shown = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size())
            ? _tabs[static_cast<size_t>(_activeTab)].get() : nullptr;
        std::unique_ptr<DocumentTab> moved = std::move(_tabs[static_cast<size_t>(from)]);
        _tabs.erase(_tabs.begin() + from);
        _tabs.insert(_tabs.begin() + to, std::move(moved));
        for (size_t i = 0; i < _tabs.size(); ++i)
            if (_tabs[i].get() == shown)
                _activeTab = static_cast<int>(i);
    });
    _tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_tabBar, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        showTabMenu(_tabBar->tabAt(at), _tabBar->mapToGlobal(at));
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
    // Behaviors that follow sound hear the soundtrack of the scene they are
    // in, from the window that has its document open.
    s_hearing.push_back(this);
    fla::behaviors::setRigging([](const fla::Layer& layer, int frame, double matrix[6]) {
        const fla::Document* document = nullptr;
        for (const fla::DOMElement* up = layer.parent; up && !document; up = up->parent)
            document = dynamic_cast<const fla::Document*>(up);
        for (MainWindow* window : s_hearing)
        {
            if (!window->_phoenixView || !window->_flaDocument || window->_flaDocument->document != document)
                continue;
            const fla::Frame* key = nullptr;
            for (const fla::Frame* each : layer.frames)
                if (each && each->index <= frame && frame < each->index + std::max(1, each->duration))
                    key = each;
            if (!key)
                return false;
            const QTransform t = window->_phoenixView->parentingAt(&layer, key, frame);
            matrix[0] = t.m11();
            matrix[1] = t.m12();
            matrix[2] = t.m21();
            matrix[3] = t.m22();
            matrix[4] = t.dx();
            matrix[5] = t.dy();
            return true;
        }
        return false;
    });
    fla::behaviors::setHearing([](const fla::Document& document, int frame) {
        for (MainWindow* window : s_hearing)
            if (window->_flaDocument && window->_flaDocument->document == &document)
                return window->loudnessAt(document, frame);
        return 0.0;
    });
    fla::behaviors::setListening(
        [](const fla::Document& document, int frame, std::vector<double>& bands, std::vector<double>& wave) {
            for (MainWindow* window : s_hearing)
                if (window->_flaDocument && window->_flaDocument->document == &document)
                    return window->spectrumAt(document, frame, bands, wave);
            return false;
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
        scriptEvent("timelineChanged");
    });
    rebuildBreadcrumb();

    // Locking and the current layer change what the stage can pick and where
    // it draws; a repaint is all it needs, since both are read live.
    connect(_timelineView, &TimelineView::layerLockChanged,
            this, &MainWindow::onVisibilityChanged);
    connect(_timelineView, &TimelineView::currentLayerChanged,
            this, &MainWindow::onVisibilityChanged);
    // Scripts listening for events (fl.addEventListener) hear of the
    // layer, the frame and the selection changing.
    connect(_timelineView, &TimelineView::currentLayerChanged, this, [this]() { scriptEvent("layerChanged"); });
    connect(_player, &Player::currentFrameChanged, this, [this](int) {
        scriptEvent("frameChanged");
        // Keyed behavior settings show their value on the frame shown.
        if (_behaviors && _behaviorsDock && _behaviorsDock->isVisible() && !_player->isPlaying())
            _behaviors->refresh();
    });
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
    connect(_timelineView, &TimelineView::soundDropped, this, [this](const fla::Layer* layer, int frame, const QString& name) {
        attachSoundTo(const_cast<fla::Layer*>(layer), frame, name);
    });
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
    connect(_timelineView, &TimelineView::compositingRequested, this, [this](fla::Layer* layer, const QString& what) {
        if (!layer)
            return;
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        Attributes after;
        QString name;
        // Objects whose attributes change with the layer's: a feather of
        // their own on a mask.
        std::vector<std::pair<fla::Element*, Attributes>> elementChanges;
        if (what.startsWith("matte:"))
        {
            const QString kind = what.mid(6);
            const fla::compositing::Matte matte = kind == "alpha" ? fla::compositing::Matte::Alpha
                : kind == "alphaInverted"                       ? fla::compositing::Matte::AlphaInverted
                : kind == "luma"                                ? fla::compositing::Matte::Luma
                : kind == "lumaInverted"                        ? fla::compositing::Matte::LumaInverted
                                                                : fla::compositing::Matte::None;
            after = fla::compositing::withMatte(*layer, matte);
            name = "Track Matte";
        }
        else if (what.startsWith("mode:"))
        {
            const QString kind = what.mid(5);
            const fla::compositing::LayerMode mode = kind == "stencil" ? fla::compositing::LayerMode::Stencil
                : kind == "silhouette"                                 ? fla::compositing::LayerMode::Silhouette
                : kind == "preserve"                                   ? fla::compositing::LayerMode::Preserve
                                                                       : fla::compositing::LayerMode::Normal;
            after = fla::compositing::withLayerMode(*layer, mode);
            name = "Layer Mode";
        }
        else if (what == "time")
        {
            const fla::motionblur::LayerTime was = fla::motionblur::layerTimeOf(*layer);
            QDialog dialog(this);
            dialog.setWindowTitle("Echo and Posterize Time");
            QFormLayout* form = new QFormLayout(&dialog);
            HotSpinBox* echoes = new HotSpinBox(&dialog);
            echoes->setRange(0, 32);
            echoes->setValue(was.echoes);
            echoes->setSpecialValueText("None");
            echoes->setToolTip("How many earlier poses trail behind the layer");
            HotSpinBox* interval = new HotSpinBox(&dialog);
            interval->setRange(1, 240);
            interval->setValue(was.interval);
            interval->setSuffix(" frames");
            interval->setToolTip("How far back each echo is from the one before");
            HotSpinBox* decay = new HotSpinBox(&dialog);
            decay->setRange(0, 100);
            decay->setValue(static_cast<int>(std::lround(was.decay * 100.0)));
            decay->setSuffix("%");
            decay->setToolTip("How much of the one before each echo keeps: low fades fast");
            HotSpinBox* hold = new HotSpinBox(&dialog);
            hold->setRange(1, 240);
            hold->setValue(was.posterize);
            hold->setSpecialValueText("Every frame");
            hold->setSuffix(" frames a pose");
            hold->setToolTip("Holds each pose this many frames: 2 for animation on twos, more for a jerky look");
            form->addRow("Echoes", echoes);
            form->addRow("Each", interval);
            form->addRow("Keeps", decay);
            form->addRow("Posterize Time", hold);
            QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if (dialog.exec() != QDialog::Accepted)
                return;
            fla::motionblur::LayerTime time;
            time.echoes = echoes->value();
            time.interval = interval->value();
            time.decay = decay->value() / 100.0;
            time.posterize = hold->value();
            after = fla::motionblur::withLayerTime(*layer, time);
            name = "Echo and Posterize Time";
        }
        else if (what == "maskedge")
        {
            const fla::compositing::MaskEdge was = fla::compositing::maskEdgeOf(*layer);
            QDialog dialog(this);
            dialog.setWindowTitle("Mask Edge");
            QFormLayout* form = new QFormLayout(&dialog);
            HotDoubleSpinBox* feather = new HotDoubleSpinBox(&dialog);
            feather->setRange(0.0, 1000.0);
            feather->setDecimals(2);
            feather->setSuffix(" px");
            feather->setValue(was.feather);
            feather->setToolTip("How far the edge fades, softly, either side");
            HotDoubleSpinBox* expand = new HotDoubleSpinBox(&dialog);
            expand->setRange(-1000.0, 1000.0);
            expand->setDecimals(2);
            expand->setSuffix(" px");
            expand->setValue(was.expand);
            expand->setToolTip("Grows the mask out, or shrinks it in with less than 0");
            QCheckBox* invert = new QCheckBox("Show what is outside the mask instead", &dialog);
            invert->setChecked(was.invert);
            form->addRow("Feather", feather);
            form->addRow("Expand", expand);
            form->addRow("Invert", invert);
            // Variable feather: the selected objects on this mask layer
            // can have a feather of their own.
            std::vector<fla::Element*> onMask;
            for (fla::Element* element : selectedElements())
            {
                const fla::Frame* frame = element ? frameOfElement(element) : nullptr;
                if (frame && frame->parent == layer)
                    onMask.push_back(element);
            }
            HotDoubleSpinBox* own = nullptr;
            if (!onMask.empty())
            {
                const double first = fla::compositing::elementFeatherOf(*onMask.front(), -1.0);
                own = new HotDoubleSpinBox(&dialog);
                own->setRange(-1.0, 1000.0);
                own->setDecimals(2);
                own->setSuffix(" px");
                own->setSpecialValueText("The layer's");
                own->setValue(first);
                own->setToolTip("A feather of their own for the selected objects on this mask, in place of the layer's; "
                                "each keyframe keeps its own and a tween blends between them");
                form->addRow(onMask.size() == 1 ? QString("Selected object") : QString("Selected objects"), own);
            }
            QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if (dialog.exec() != QDialog::Accepted)
                return;
            fla::compositing::MaskEdge edge;
            edge.feather = feather->value();
            edge.expand = expand->value();
            edge.invert = invert->isChecked();
            after = fla::compositing::withMaskEdge(*layer, edge);
            name = "Mask Edge";
            if (own)
                for (fla::Element* element : onMask)
                {
                    Attributes attributes = fla::compositing::withElementFeather(*element, own->value() < 0.0 ? -1.0 : own->value());
                    if (attributes != element->extraAttributes)
                        elementChanges.emplace_back(element, attributes);
                }
        }
        else if (what.startsWith("maskwith:"))
        {
            // Another mask layer of the timeline added to the layer's own
            // mask, cut out of it, overlapped with it, or taken off.
            const QStringList parts = what.split(':');
            const auto* timeline = dynamic_cast<const fla::Timeline*>(layer->parent);
            const int index = parts.value(1).toInt();
            if (!timeline || index < 0 || index >= static_cast<int>(timeline->layers.size()) || !timeline->layers[index])
                return;
            const std::string other = timeline->layers[index]->name;
            const QString mode = parts.value(2);
            std::vector<fla::compositing::MoreMask> list = fla::compositing::moreMasksOf(*layer);
            list.erase(std::remove_if(list.begin(), list.end(),
                           [&other](const fla::compositing::MoreMask& each) { return each.layer == other; }),
                list.end());
            if (mode != "none")
            {
                fla::compositing::MoreMask added;
                added.layer = other;
                added.mode = mode == "subtract" ? fla::compositing::MaskCombine::Subtract
                    : mode == "intersect"       ? fla::compositing::MaskCombine::Intersect
                                                : fla::compositing::MaskCombine::Add;
                list.push_back(added);
            }
            after = fla::compositing::withMoreMasks(*layer, list);
            name = "Also Masked By";
        }
        else if (what.startsWith("motionblur:"))
        {
            after = fla::motionblur::withOn(*layer, what == "motionblur:on");
            name = "Motion Blur";
        }
        else
        {
            after = fla::compositing::withAdjustment(*layer, what == "adjustment:on");
            name = "Adjustment Layer";
        }
        if (after == layer->extraAttributes && elementChanges.empty())
            return;
        fla::CommandStack& stack = _editContext.commandStack();
        if (!elementChanges.empty())
            stack.beginMacro(name.toStdString());
        if (after != layer->extraAttributes)
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(layer->extraAttributes,
                layer->extraAttributes, after, name.toStdString())));
        for (const auto& [element, attributes] : elementChanges)
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(element->extraAttributes,
                element->extraAttributes, attributes, name.toStdString())));
        if (!elementChanges.empty())
            stack.endMacro();
        stack.breakMergeChain();
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        // The layer's icon says what it is now.
        if (_timelineView)
            _timelineView->refresh();
        if (_behaviors && _behaviorsDock)
        {
            if (what == "adjustment:on")
            {
                _behaviorsDock->show();
                _behaviorsDock->raise();
            }
            _behaviors->refresh();
        }
    });
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
            // One keyframe at each selected frame.
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
    const fla::FLADocument* document = _flaDocument;
    QTimer::singleShot(0, this, [this, layer, frame, document]() {
        if (!stillInDocument(document, layer) || !layer->isVisible() || layer->isLocked() || !_phoenixView)
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
    HotDoubleSpinBox* blur = new HotDoubleSpinBox(&dialog);
    blur->setRange(0.0, 200.0);
    blur->setSuffix(" px");
    blur->setValue(effect.blur);
    form->addRow("Softness:", blur);
    HotSpinBox* opacity = new HotSpinBox(&dialog);
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
    // The library's sounds, starting with the voice on the mouth's timeline.
    QStringList sounds;
    for (const fla::Document::SoundInfo& info : _flaDocument->document->sounds)
        sounds << QString::fromStdString(info.name);
    QString voice;
    if (const auto* timeline = dynamic_cast<const fla::Timeline*>(layer->parent))
        for (const fla::Layer* other : timeline->layers)
            if (other)
                for (const fla::Frame* key : other->frames)
                    if (key && voice.isEmpty())
                        voice = QString::fromStdString(fla::frame_sound::read(*key).name);
    LipSyncDialog::SoundSource soundOf;
    if (_timelineAudio)
        soundOf = [this](const QString& name) { return _timelineAudio->librarySound(name.toStdString()); };
    LipSyncDialog dialog(QString::fromStdString(mouth->libraryItemName), saved, fla::lipsync::labelsOf(*mouth), sounds,
        voice, soundOf, _player ? _player->currentFrame() : 0, fps, this);
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

namespace {

/// The frames of an armature's span \a frame frames in, drawn there, as
/// the span's first frame has them: carried with the joint \a joint hangs
/// a new bone from, turning with it.
QTransform backToFirstFrame(const fla::IKArmature& armature, int joint, int frame)
{
    const std::vector<fla::IKJoint> here = armature.shownAt(frame);
    const std::vector<fla::IKJoint> first = armature.shownAt(0);
    if (frame <= 0 || joint < 0 || static_cast<size_t>(joint) >= here.size() || static_cast<size_t>(joint) >= first.size())
        return QTransform();
    QTransform carry;
    carry.translate(first[static_cast<size_t>(joint)].position.x, first[static_cast<size_t>(joint)].position.y);
    carry.rotateRadians(first[static_cast<size_t>(joint)].angle - here[static_cast<size_t>(joint)].angle);
    carry.translate(-here[static_cast<size_t>(joint)].position.x, -here[static_cast<size_t>(joint)].position.y);
    return carry;
}

/// Which of a frame's kept children is an armature, or -1.
int armatureChild(const fla::Frame* frame)
{
    for (size_t i = 0; frame && i < frame->extraChildren.size(); ++i)
    {
        if (frame->extraChildren[i].find("<IKTree") != std::string::npos)
            return static_cast<int>(i);
    }
    return -1;
}

std::string poseLocationsOf(const fla::Frame& frame)
{
    for (const auto& attribute : frame.extraAttributes)
    {
        if (attribute.first == "poseLocations")
            return attribute.second;
    }
    return "0";
}

/// The highest numbers in use on \a timeline for armature nodes, trees and
/// armature layers, so new ones go past them.
void armatureNumbers(const fla::Timeline& timeline, int& nodes, int& trees, int& armatures)
{
    nodes = 0;
    trees = 0;
    armatures = 0;
    const QRegularExpression nodePattern("ikNode_(\\d+)");
    const QRegularExpression treePattern("ikTreeName(\\d+)");
    for (const fla::Layer* layer : timeline.layers)
    {
        const QRegularExpressionMatch named = QRegularExpression("^Armature_(\\d+)$").match(QString::fromStdString(layer->name));
        if (named.hasMatch())
            armatures = std::max(armatures, named.captured(1).toInt());
        for (const fla::Frame* frame : layer->frames)
        {
            for (const std::string& child : frame->extraChildren)
            {
                const QString text = QString::fromStdString(child);
                for (auto it = nodePattern.globalMatch(text); it.hasNext();)
                    nodes = std::max(nodes, it.next().captured(1).toInt());
                for (auto it = treePattern.globalMatch(text); it.hasNext();)
                    trees = std::max(trees, it.next().captured(1).toInt());
            }
        }
    }
}

/// The betweenFrameMatrixList for the armature of symbols in \a frame's
/// IKTree \a xml: the instances there whose referenceIDs its joints name.
std::string betweenFramesOf(const fla::Frame& frame, const std::string& xml)
{
    fla::IKArmature armature;
    if (!armature.parse(xml) || armature.bendsShape())
        return std::string();
    std::vector<const fla::Transform*> placed(armature.jointsAt(0).size(), nullptr);
    for (size_t joint = 0; joint < placed.size(); ++joint)
    {
        const std::string reference = armature.referenceOf(static_cast<int>(joint));
        for (const fla::Element* element : frame.elements)
        {
            const fla::SymbolInstance* instance = dynamic_cast<const fla::SymbolInstance*>(element);
            for (const auto& attribute : instance ? instance->extraAttributes : std::vector<std::pair<std::string, std::string>>())
            {
                if (!reference.empty() && attribute.first == "referenceID" && attribute.second == reference)
                    placed[joint] = &instance->transform;
            }
        }
    }
    return armature.betweenFrameMatrixList(placed);
}

} // namespace

void MainWindow::addBone(const BoneTool::Link& link)
{
    if (link.inShape())
    {
        addShapeBone(link);
        return;
    }
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView || !link.from || !link.to || !link.fromFrame || !link.toFrame)
        return;
    fla::Layer* fromLayer = dynamic_cast<fla::Layer*>(link.fromFrame->parent);
    fla::Layer* toLayer = dynamic_cast<fla::Layer*>(link.toFrame->parent);
    fla::Timeline* timeline = fromLayer ? dynamic_cast<fla::Timeline*>(fromLayer->parent) : nullptr;
    if (!timeline || !toLayer || fromLayer->isLocked() || toLayer->isLocked())
    {
        statusBar()->showMessage("Bones join symbol instances on unlocked layers.", 4000);
        return;
    }
    if (armatureChild(link.toFrame) >= 0)
    {
        statusBar()->showMessage("That instance is already in an armature.", 4000);
        return;
    }

    // Numbers for new nodes, trees and armature layers, past those in use.
    int nodes = 0;
    int trees = 0;
    int armatures = 0;
    armatureNumbers(*timeline, nodes, trees, armatures);
    const auto guid = []() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper().toStdString(); };

    _phoenixView->flushPendingMerge();
    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    const char* name = "Add Bone";

    // Moves an instance onto the pose frame, turning about its joint (its
    // transformation point), named and referenced as its node.
    const auto enlist = [&](fla::Frame* from, fla::SymbolInstance* instance, fla::Frame* pose, const QPointF& joint,
                            const std::string& node, const std::string& reference) {
        const fla::Transform& m = instance->transform;
        bool invertible = false;
        const QPointF local = QTransform(m.m11, m.m12, m.m21, m.m22, m.tx, m.ty).inverted(&invertible).map(joint);
        auto attributes = instance->extraAttributes;
        attributes.erase(std::remove_if(attributes.begin(), attributes.end(),
                             [](const std::pair<std::string, std::string>& attribute) {
                                 return attribute.first == "referenceID" || attribute.first == "animationSymbolMode" ||
                                     attribute.first == "animationFrameOffset";
                             }),
            attributes.end());
        attributes.emplace_back("referenceID", reference);
        attributes.emplace_back("animationSymbolMode", "2");
        attributes.emplace_back("animationFrameOffset", "0");
        // Onto the pose layer: one step that moves it, so the instance always
        // has exactly one owner, whichever frame it is in.
        if (from != pose)
            stack.push(fla::CommandPtr(new fla::MoveElementCommand(from, pose, instance, name)));
        if (invertible)
        {
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Point>(instance->transformationPoint,
                instance->transformationPoint, fla::Point(local.x(), local.y()), name)));
        }
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
            instance->extraAttributes, instance->extraAttributes, attributes, name)));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(instance->name, instance->name, node, name)));
    };
    const auto jointFor = [](fla::SymbolInstance* instance, const QPointF& at, int parent, const std::string& node,
                              const std::string& reference) {
        fla::ArmatureJoint joint;
        joint.position = fla::Point(at.x(), at.y());
        joint.parent = parent;
        joint.node = node;
        joint.reference = reference;
        joint.registration = fla::Point(instance->transform.tx, instance->transform.ty);
        return joint;
    };

    const int child = armatureChild(link.fromFrame);
    if (child < 0)
    {
        // A new armature: a pose layer above the first instance's layer,
        // posed over the frames that instance was on.
        const std::string armatureName = "Armature_" + std::to_string(armatures + 1);
        const int length = std::max(1, link.fromFrame->duration);
        fla::Frame* pose = nullptr;
        fla::Layer* layer = fla::newArmatureLayer(*timeline, *fromLayer, armatureName, link.fromFrame->index, length, pose);
        std::vector<fla::ArmatureJoint> joints;
        joints.push_back(jointFor(link.from, link.head, -1, "ikNode_" + std::to_string(nodes + 1), guid()));
        joints.push_back(jointFor(link.to, link.tail, 0, "ikNode_" + std::to_string(nodes + 2), guid()));
        pose->extraChildren.push_back(fla::symbolArmatureXml(armatureName,
            "ikTreeName" + std::to_string(trees + 1), joints, length));

        int at = 0;
        for (size_t i = 0; i < timeline->layers.size(); ++i)
        {
            if (timeline->layers[i] == fromLayer)
                at = static_cast<int>(i);
        }
        stack.beginMacro(name);
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, at, layer, name)));
        enlist(link.fromFrame, link.from, pose, link.head, joints[0].node, joints[0].reference);
        enlist(link.toFrame, link.to, pose, link.tail, joints[1].node, joints[1].reference);
        // Each instance's place in its joint's frame.
        std::vector<std::string> children = pose->extraChildren;
        fla::setBetweenFrames(children, betweenFramesOf(*pose, children.back()));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(pose->extraChildren,
            pose->extraChildren, children, name)));
        stack.endMacro();
    }
    else
    {
        // A bone more, from the joint of the instance dragged from.
        fla::IKArmature armature;
        if (!armature.parse(link.fromFrame->extraChildren[static_cast<size_t>(child)]) || armature.bendsShape())
        {
            statusBar()->showMessage("Bones can be added to an armature of symbols.", 4000);
            return;
        }
        std::string reference;
        for (const auto& attribute : link.from->extraAttributes)
        {
            if (attribute.first == "referenceID")
                reference = attribute.second;
        }
        std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
        int parent = -1;
        for (size_t i = 0; i < joints.size(); ++i)
        {
            if (!reference.empty() && joints[i].reference == reference)
                parent = static_cast<int>(i);
        }
        if (parent < 0)
            return;
        // Drawn on a later frame of the span, the bone and its instance are
        // carried back to where they would be on the first, which the
        // armature is built from, turning with the joint they hang from.
        const int drawnAt = std::clamp((_player ? _player->currentFrame() : 0) - link.fromFrame->index, 0,
            std::max(0, armature.frames() - 1));
        const QTransform carry = backToFirstFrame(armature, parent, drawnAt);
        stack.beginMacro(name);
        if (!carry.isIdentity())
        {
            const fla::Transform& m = link.to->transform;
            const QTransform placed = QTransform(m.m11, m.m12, m.m21, m.m22, m.tx, m.ty) * carry;
            fla::Transform first = m;
            first.m11 = placed.m11();
            first.m12 = placed.m12();
            first.m21 = placed.m21();
            first.m22 = placed.m22();
            first.tx = placed.dx();
            first.ty = placed.dy();
            stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Transform>(link.to->transform, m, first, name)));
        }
        joints.push_back(jointFor(link.to, carry.map(link.tail), parent, "ikNode_" + std::to_string(nodes + 1), guid()));
        const std::string before = link.fromFrame->extraChildren[static_cast<size_t>(child)];
        // Built again with the bone more, keeping what the old nodes carried,
        // and posed again as each later pose was.
        fla::IKArmature rebuilt;
        rebuilt.parse(fla::symbolArmatureXml(armature.name(), armature.treeName(), joints, armature.frames()));
        rebuilt.keepAttributesOf(armature);
        fla::carryPoses(armature, rebuilt);
        const std::string after = rebuilt.xml();
        const std::string poses = poseLocationsOf(*link.fromFrame);
        stack.push(fla::CommandPtr(new fla::PoseArmatureCommand(link.fromFrame, static_cast<size_t>(child), before, after, poses, poses)));
        enlist(link.toFrame, link.to, link.fromFrame, carry.map(link.tail), joints.back().node, joints.back().reference);
        std::vector<std::string> children = link.fromFrame->extraChildren;
        fla::setBetweenFrames(children, betweenFramesOf(*link.fromFrame, after));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(link.fromFrame->extraChildren,
            link.fromFrame->extraChildren, children, name)));
        stack.endMacro();
    }
    stack.breakMergeChain();
    if (_timelineView)
        _timelineView->refresh();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    statusBar()->showMessage("Bone added. Drag a bone with the Selection tool to pose the armature.", 4000);
}

void MainWindow::addShapeBone(const BoneTool::Link& link)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
        return;
    fla::CommandStack& stack = _editContext.commandStack();
    const char* name = "Add Bone";
    const fla::Layer* armatureLayerOf = link.armature ? dynamic_cast<const fla::Layer*>(link.armature->parent) : nullptr;
    fla::Layer* shapeLayer = link.shapeFrame ? dynamic_cast<fla::Layer*>(link.shapeFrame->parent) : nullptr;
    fla::Timeline* timeline = shapeLayer ? dynamic_cast<fla::Timeline*>(shapeLayer->parent)
        : armatureLayerOf ? dynamic_cast<fla::Timeline*>(armatureLayerOf->parent) : nullptr;
    if (!timeline || (shapeLayer && shapeLayer->isLocked()) || (armatureLayerOf && armatureLayerOf->isLocked()))
    {
        statusBar()->showMessage("Bones go in shapes on unlocked layers.", 4000);
        return;
    }
    int nodes = 0;
    int trees = 0;
    int armatures = 0;
    armatureNumbers(*timeline, nodes, trees, armatures);
    _phoenixView->flushPendingMerge();

    if (link.armature)
    {
        // A bone more, from a joint of a boned shape: the armature built
        // again with it and every point bound again to its nearest bone.
        BoundShape bound;
        if (!BoundShape::find(link.armature, link.child, 0, bound))
        {
            statusBar()->showMessage("Bones can be added to a boned shape.", 4000);
            return;
        }
        std::vector<fla::ArmatureJoint> joints = bound.armature.symbolJoints();
        if (link.joint < 0 || static_cast<size_t>(link.joint) >= joints.size())
            return;
        // Drawn on a later frame of the span, the bone is carried back to
        // where it would be on the first, which the binding is made on.
        const int drawnAt = std::clamp((_player ? _player->currentFrame() : 0) - link.armature->index, 0,
            std::max(0, bound.armature.frames() - 1));
        const QPointF tail = backToFirstFrame(bound.armature, link.joint, drawnAt).map(link.tail);
        fla::ArmatureJoint added;
        added.position = fla::Point(tail.x(), tail.y());
        added.registration = added.position;
        added.parent = link.joint;
        added.node = "ikNode_" + std::to_string(nodes + 1);
        joints.push_back(added);
        fla::IKArmature rebuilt;
        if (!rebuilt.parse(fla::armatureXml(bound.armature.name(), bound.armature.treeName(), joints,
                bound.armature.frames(), true)))
        {
            return;
        }
        rebuilt.keepAttributesOf(bound.armature);
        rebuilt.setBinding(fla::bindToNearestBones(bound.anchors, rebuilt.jointsAt(0)), bound.anchors,
            bound.instance->transform, fla::contourStyle(*bound.rest));
        fla::carryPoses(bound.armature, rebuilt);
        const std::string poses = poseLocationsOf(*link.armature);
        stack.push(fla::CommandPtr(new fla::PoseArmatureCommand(link.armature, link.child,
            link.armature->extraChildren[link.child], rebuilt.xml(), poses, poses)));
    }
    else
    {
        if (!link.shape || !link.shapeFrame)
            return;
        // The shape moves into a symbol of its own, an ik container holding
        // it on every frame of the span, placed about its middle on a new
        // pose layer above the shape's layer.
        std::string symbolName;
        for (int n = 1;; ++n)
        {
            symbolName = "lsd_" + std::to_string(n);
            if (!document->symbolList || document->symbolList->symbolMap.count(symbolName) == 0)
                break;
        }
        std::vector<fla::ArmatureJoint> joints(2);
        joints[0].position = fla::Point(link.head.x(), link.head.y());
        joints[0].node = "ikNode_" + std::to_string(nodes + 1);
        joints[1].position = fla::Point(link.tail.x(), link.tail.y());
        joints[1].parent = 0;
        joints[1].node = "ikNode_" + std::to_string(nodes + 2);
        const fla::BonedShape boned = fla::boneShape(*timeline, *shapeLayer, *link.shapeFrame, *link.shape, joints,
            symbolName, "Armature_" + std::to_string(armatures + 1), "ikTreeName" + std::to_string(trees + 1));
        if (!boned.symbol)
            return;
        const std::time_t now = std::time(nullptr);
        char id[40];
        std::snprintf(id, sizeof(id), "%08lx-%08lx", static_cast<unsigned long>(now),
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(boned.symbol) & 0xffffffffu));
        boned.symbol->itemId = id;
        boned.symbol->lastModified = std::to_string(static_cast<long long>(now));

        int at = 0;
        for (size_t i = 0; i < timeline->layers.size(); ++i)
        {
            if (timeline->layers[i] == shapeLayer)
                at = static_cast<int>(i);
        }
        _selection.clear();
        stack.beginMacro(name);
        stack.push(fla::CommandPtr(new fla::AddSymbolCommand(document, boned.symbol, name)));
        stack.push(fla::CommandPtr(new fla::RemoveElementCommand(link.shapeFrame, link.shape, name, &_selection)));
        stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, at, boned.layer, name)));
        stack.endMacro();
        _documentView->setDocument(_flaDocument);
    }
    stack.breakMergeChain();
    if (_timelineView)
        _timelineView->refresh();
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    _phoenixView->update();
    statusBar()->showMessage("Bone added. Drag a bone with the Selection tool to pose the shape; the Bind tool "
        "changes which bone a point follows.", 5000);
}

void MainWindow::rebindShape(fla::Frame* frame, size_t child, const fla::ShapeBinding& binding)
{
    BoundShape bound;
    if (!_phoenixView || !BoundShape::find(frame, child, 0, bound))
        return;
    fla::IKArmature armature = bound.armature;
    armature.setBinding(binding, bound.anchors, bound.instance->transform, fla::contourStyle(*bound.rest));
    const std::string after = armature.xml();
    if (after == frame->extraChildren[child])
        return;
    std::unique_ptr<fla::CompoundCommand> compound(new fla::CompoundCommand("Bind Points"));
    const std::string poses = poseLocationsOf(*frame);
    compound->addCommand(fla::CommandPtr(new fla::PoseArmatureCommand(frame, child, frame->extraChildren[child], after,
        poses, poses)));
    // Every other frame's shape bent again from the first's by the new
    // binding, where its joints are not the first frame's.
    const std::vector<fla::IKJoint> first = armature.shownAt(0);
    const fla::Timeline* inside = bound.instance->symbol->timelines.front();
    for (int k = 1; k < armature.frames(); ++k)
    {
        const std::vector<fla::IKJoint> joints = armature.shownAt(k);
        for (const fla::Layer* layer : inside->layers)
        {
            for (fla::Frame* own : layer ? layer->frames : std::vector<fla::Frame*>())
            {
                if (!own || own->index != bound.instance->firstFrame + k || own->duration > 1)
                    continue;
                for (fla::Element* element : own->elements)
                {
                    if (!element || element->elementType() != fla::Element::Type::Shape)
                        continue;
                    fla::Shape* shape = static_cast<fla::Shape*>(element);
                    std::unique_ptr<fla::Shape> bent(fla::cloneShape(*bound.rest, nullptr));
                    bent->transform = bound.rest->transform;
                    fla::Transform toArmature = bound.rest->transform;
                    const fla::Transform& placed = bound.instance->transform;
                    toArmature = fla::Transform(toArmature.m11 * placed.m11 + toArmature.m12 * placed.m21,
                        toArmature.m11 * placed.m12 + toArmature.m12 * placed.m22,
                        toArmature.m21 * placed.m11 + toArmature.m22 * placed.m21,
                        toArmature.m21 * placed.m12 + toArmature.m22 * placed.m22,
                        toArmature.tx * placed.m11 + toArmature.ty * placed.m21 + placed.tx,
                        toArmature.tx * placed.m12 + toArmature.ty * placed.m22 + placed.ty);
                    fla::bindShape(*bent, toArmature, binding, first, joints);
                    bent->transform = shape->transform;
                    compound->addCommand(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *bent, "Bind Points")));
                    _phoenixView->forgetShape(shape);
                }
            }
        }
    }
    _editContext.commandStack().push(std::move(compound));
    _editContext.commandStack().breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::editJointLimits(fla::Frame* frame, size_t child, int joint)
{
    if (!frame || child >= frame->extraChildren.size())
        return;
    fla::IKArmature armature;
    if (!armature.parse(frame->extraChildren[child]))
        return;
    const std::vector<fla::JointLimit> limits = armature.limits();
    if (joint < 0 || static_cast<size_t>(joint) >= limits.size())
        return;
    constexpr double kDegrees = 180.0 / 3.14159265358979323846;
    const fla::JointLimit limit = limits[static_cast<size_t>(joint)];

    QDialog dialog(this);
    dialog.setWindowTitle("Joint Rotation and Spring");
    QFormLayout* form = new QFormLayout(&dialog);
    QCheckBox* constrain = new QCheckBox("Constrain", &dialog);
    constrain->setChecked(limit.constrained);
    HotDoubleSpinBox* minimum = new HotDoubleSpinBox(&dialog);
    HotDoubleSpinBox* maximum = new HotDoubleSpinBox(&dialog);
    for (HotDoubleSpinBox* box : {minimum, maximum})
    {
        box->setRange(-180.0, 180.0);
        box->setDecimals(1);
        box->setSuffix(QString(QChar(0x00B0)));
    }
    minimum->setValue(limit.constrained ? limit.min * kDegrees : -45.0);
    maximum->setValue(limit.constrained ? limit.max * kDegrees : 45.0);
    const auto enable = [=](bool on) {
        minimum->setEnabled(on);
        maximum->setEnabled(on);
    };
    enable(limit.constrained);
    connect(constrain, &QCheckBox::toggled, &dialog, enable);
    form->addRow("Rotation", constrain);
    form->addRow("Min", minimum);
    form->addRow("Max", maximum);
    // The bone's spring: it trails the motion above it and of its own poses,
    // overshoots and settles.
    const fla::BoneSpring spring = armature.spring(joint);
    HotSpinBox* strength = new HotSpinBox(&dialog);
    strength->setRange(0, 100);
    strength->setValue(static_cast<int>(std::lround(spring.strength)));
    strength->setSpecialValueText("None");
    strength->setToolTip("How stiffly the bone springs back to its pose: low swings wide and slow, high quick and small; None for no spring");
    HotSpinBox* damping = new HotSpinBox(&dialog);
    damping->setRange(0, 100);
    damping->setValue(spring.present ? static_cast<int>(std::lround(spring.damping)) : 30);
    damping->setToolTip("How quickly its swinging dies away: 0 goes on bouncing, 100 settles at once");
    damping->setEnabled(strength->value() > 0);
    connect(strength, &HotText::valueChanged, &dialog, [damping](double value) { damping->setEnabled(value > 0); });
    form->addRow("Spring", strength);
    form->addRow("Damping", damping);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    fla::JointLimit chosen;
    chosen.constrained = constrain->isChecked();
    chosen.min = minimum->value() / kDegrees;
    chosen.max = maximum->value() / kDegrees;
    armature.setLimit(joint, chosen);
    fla::BoneSpring springChosen;
    springChosen.present = strength->value() > 0;
    springChosen.strength = strength->value();
    springChosen.damping = damping->value();
    if (springChosen.strength != spring.strength || (springChosen.present && springChosen.damping != spring.damping))
        armature.setSpring(joint, springChosen);
    const std::string before = frame->extraChildren[child];
    const std::string after = armature.xml();
    if (before == after)
        return;
    const std::string poses = poseLocationsOf(*frame);
    _editContext.commandStack().push(fla::CommandPtr(new fla::PoseArmatureCommand(frame, child, before, after, poses, poses)));
    _editContext.commandStack().breakMergeChain();
}

void MainWindow::addCamera()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::Timeline* scene = document ? document->currentScene() : nullptr;
    if (!scene)
        return;
    if (fla::camera::filmingOf(*scene))
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

fla::Frame* MainWindow::currentPoseSpan(size_t& child, int& into)
{
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
        return nullptr;
    const int at = _player ? _player->currentFrame() : 0;
    fla::Frame* frame = fla::timeline::spanAt(*timeline->layers[index], at);
    if (!frame || frame->otherTweenType != "IK pose")
        return nullptr;
    for (size_t i = 0; i < frame->extraChildren.size(); ++i)
    {
        if (frame->extraChildren[i].compare(0, 7, "<IKTree") == 0)
        {
            child = i;
            into = at - frame->index;
            return frame;
        }
    }
    return nullptr;
}

void MainWindow::poseCommand(PoseCommand command)
{
    size_t child = 0;
    int into = 0;
    fla::Frame* span = currentPoseSpan(child, into);
    if (!span || !_phoenixView)
    {
        statusBar()->showMessage("Pose commands work on an armature layer's frames.", 4000);
        return;
    }
    fla::IKArmature armature;
    if (!armature.parse(span->extraChildren[child]))
        return;
    std::vector<double> shown;
    for (const fla::IKJoint& joint : armature.shownAt(into))
        shown.push_back(joint.angle);
    if (command == PoseCommand::Copy)
    {
        _copiedPose = shown;
        statusBar()->showMessage(QString("Copied the pose at frame %1.").arg(span->index + into + 1), 3000);
        return;
    }
    IKPoser poser;
    bool done = false;
    if (command == PoseCommand::Insert)
        done = poser.change(*_phoenixView, _editContext.commandStack(), span, child,
            [into, shown](fla::IKArmature& posed) {
                const std::vector<int> poses = posed.poses();
                return std::find(poses.begin(), poses.end(), into) == poses.end() && posed.setPose(into, shown);
            },
            "Insert Pose");
    else if (command == PoseCommand::Paste)
    {
        if (_copiedPose.size() != shown.size())
        {
            statusBar()->showMessage(_copiedPose.empty() ? "Copy a pose first."
                                                         : "That pose is of an armature with other bones.", 4000);
            return;
        }
        const std::vector<double> pasted = _copiedPose;
        done = poser.change(*_phoenixView, _editContext.commandStack(), span, child,
            [into, pasted](fla::IKArmature& posed) { return posed.setPose(into, pasted); }, "Paste Pose");
    }
    else if (command == PoseCommand::Clear)
        done = poser.change(*_phoenixView, _editContext.commandStack(), span, child,
            [into](fla::IKArmature& posed) { return posed.removePose(into); }, "Clear Pose");
    if (!done)
    {
        const char* why = command == PoseCommand::Insert ? "There is a pose on this frame already."
            : command == PoseCommand::Clear              ? "There is no pose here to clear (the first one stays)."
                                                         : "The pose could not be pasted here.";
        statusBar()->showMessage(why, 4000);
        return;
    }
    if (_timelineView)
        _timelineView->update();
    _phoenixView->clearCaches();
    _phoenixView->update();
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
    _frameClipboardFrom = _flaDocument ? _flaDocument->document : nullptr;
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

void MainWindow::bakeBehaviors()
{
    const double rate = _flaDocument && _flaDocument->document ? _flaDocument->document->frameRate : 24.0;
    auto errors = std::make_shared<std::vector<std::string>>();
    editFrames("Bake Behaviors", [this, rate, errors](fla::Layer& layer, int first, int last, std::vector<fla::Frame*>&) {
        fla::behaviors::bake(BehaviorLibrary::instance().engine(), layer, first, last, rate, errors.get());
        bakeShapeOperators(layer, first, last);
    });
    for (const std::string& error : *errors)
        BehaviorLibrary::instance().reportOnce(QString::fromStdString(error));
}

void MainWindow::bakeSelectedBehaviors()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    const double rate = _flaDocument->document->frameRate;
    // The run of keyframes each selected instance with behaviors is on.
    std::vector<std::pair<fla::Layer*, std::pair<int, int>>> runs;
    std::vector<fla::Layer*> layers;
    for (fla::DOMElement* selected : _selection.elements())
    {
        auto* instance = dynamic_cast<fla::SymbolInstance*>(selected);
        if (!instance || !fla::behaviors::any(*instance))
            continue;
        auto* frame = dynamic_cast<fla::Frame*>(instance->parent);
        auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
        if (!layer || layer->isLocked())
            continue;
        const auto place = std::find(frame->elements.begin(), frame->elements.end(), instance);
        int start = 0;
        int end = 0;
        fla::behaviors::runOf(*layer, *frame, static_cast<size_t>(place - frame->elements.begin()),
            instance->libraryItemName, start, end);
        runs.push_back({layer, {start, end}});
        if (std::find(layers.begin(), layers.end(), layer) == layers.end())
            layers.push_back(layer);
    }
    // Shapes with operators: the keyframe each is on.
    std::vector<std::pair<fla::Layer*, std::pair<int, int>>> shapeRuns;
    for (fla::DOMElement* selected : _selection.elements())
    {
        auto* shape = dynamic_cast<fla::Shape*>(selected);
        auto* text = dynamic_cast<fla::StaticText*>(selected);
        if (!(shape && fla::shapeops::any(*shape)) && !(text && fla::textanim::any(*text)))
            continue;
        fla::Frame* frame = frameOfElement(static_cast<fla::Element*>(selected));
        auto* layer = frame ? dynamic_cast<fla::Layer*>(frame->parent) : nullptr;
        if (!layer || layer->isLocked())
            continue;
        shapeRuns.push_back({layer, {frame->index, frame->index + std::max(1, frame->duration) - 1}});
        if (std::find(layers.begin(), layers.end(), layer) == layers.end())
            layers.push_back(layer);
    }
    if (runs.empty() && shapeRuns.empty())
    {
        statusBar()->showMessage("Select symbol instances with behaviors, or shapes with operators, to bake them.", 3000);
        return;
    }
    auto errors = std::make_shared<std::vector<std::string>>();
    _selection.clear();
    auto* command = new fla::EditFramesCommand(layers, "Bake Behaviors", [this, runs, shapeRuns, rate, errors](std::vector<fla::Frame*>&) {
        for (const auto& [layer, range] : runs)
            fla::behaviors::bake(BehaviorLibrary::instance().engine(), *layer, range.first, range.second, rate, errors.get());
        for (const auto& [layer, range] : shapeRuns)
            bakeShapeOperators(*layer, range.first, range.second);
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
    for (const std::string& error : *errors)
        BehaviorLibrary::instance().reportOnce(QString::fromStdString(error));
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
}

std::vector<fla::StaticText*> MainWindow::bakedText(const fla::StaticText& text, int at)
{
    std::vector<fla::StaticText*> made;
    const std::vector<fla::textanim::Change> changes = _phoenixView->textChanges(text, at);
    int offset = 0;
    for (size_t r = 0; r < text.runs.size(); ++r)
    {
        const QString runText = QString::fromStdString(text.runs[r].text);
        for (int k = 0; k < runText.size(); ++k, ++offset)
        {
            const fla::textanim::Change change =
                offset < static_cast<int>(changes.size()) ? changes[static_cast<size_t>(offset)] : fla::textanim::Change();
            if (runText[k].isSpace() || change.alpha <= 0.0 || change.scale == 0.0)
                continue;
            auto* letter = static_cast<fla::StaticText*>(fla::cloneElement(text, nullptr));
            if (!letter)
                continue;
            letter->extraAttributes = fla::behaviors::withList(*letter, fla::textanim::kAttribute, {});
            fla::TextRun run = text.runs[r];
            run.text = change.character ? QString(QChar(static_cast<char16_t>(change.character))).toStdString()
                                        : QString(runText[k]).toStdString();
            run.alignment = fla::TextRun::Alignment::Left;
            run.fillColor[3] = static_cast<uint8_t>(std::lround(run.fillColor[3] * std::min(1.0, change.alpha)));
            if (change.tint > 0.0)
            {
                const double u = std::min(1.0, change.tint);
                for (int c = 0; c < 3; ++c)
                {
                    const double to = (change.tintColor >> (16 - 8 * c)) & 0xFF;
                    run.fillColor[c] = static_cast<uint8_t>(std::lround(run.fillColor[c] + (to - run.fillColor[c]) * u));
                }
            }
            letter->runs = {run};
            letter->autoExpand = true;
            letter->width = 0.0;
            // Where the letter stood in the field, as Break Apart puts it.
            const QRectF was = TextLayout::caretRect(text, offset);
            const QRectF now = TextLayout::caretRect(*letter, 0);
            letter->left += was.left() - now.left();
            letter->top += was.top() - now.top();
            // Then moved, turned and scaled about its middle, in the field's
            // own space.
            const QRectF box = TextLayout::localBounds(*letter);
            const double cx = box.center().x();
            const double cy = box.center().y();
            QTransform local;
            local.translate(cx + change.x, cy + change.y);
            local.rotate(change.rotation);
            // A turn in depth flattens the letter the way it would look face on.
            local.scale(change.scale * std::cos(change.turnY * 3.14159265358979323846 / 180.0),
                change.scale * std::cos(change.turnX * 3.14159265358979323846 / 180.0));
            local.translate(-cx, -cy);
            const QTransform placed = local * QTransform(text.transform.m11, text.transform.m12, text.transform.m21,
                                                  text.transform.m22, text.transform.tx, text.transform.ty);
            letter->transform = fla::Transform(placed.m11(), placed.m12(), placed.m21(), placed.m22(), placed.dx(), placed.dy());
            made.push_back(letter);
        }
    }
    return made;
}

std::vector<std::pair<int, int>> MainWindow::shapeOperatorRuns(const fla::Layer& layer)
{
    std::vector<std::pair<int, int>> runs;
    for (const fla::Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        bool holds = false;
        for (const fla::Element* element : frame->elements)
            holds = holds || (element && element->elementType() == fla::Element::Type::Shape &&
                                 fla::shapeops::any(*static_cast<const fla::Shape*>(element))) ||
                (element && element->elementType() == fla::Element::Type::StaticText &&
                    fla::textanim::any(*static_cast<const fla::StaticText*>(element)));
        if (holds)
            runs.emplace_back(frame->index, frame->index + std::max(1, frame->duration) - 1);
    }
    return runs;
}

int MainWindow::bakeShapeOperators(fla::Layer& layer, int first, int last, bool shapes)
{
    if (!_phoenixView)
        return 0;
    bool any = false;
    for (const auto& [from, to] : shapeOperatorRuns(layer))
        any = any || (from <= last && to >= first);
    if (!any)
        return 0;
    const double rate = _flaDocument && _flaDocument->document ? _flaDocument->document->frameRate : 24.0;
    fla::frames::convertToFrameByFrame(layer, first, last, rate);
    // Every frame's letters worked out before any frame changes: a text's
    // animation counts from the first of the keyframes holding it, which
    // the frames before it must still show.
    std::map<const fla::StaticText*, std::vector<fla::StaticText*>> letters;
    for (fla::Frame* frame : layer.frames)
    {
        if (!frame || frame->index < first || frame->index > last)
            continue;
        for (fla::Element* element : frame->elements)
            if (auto* text = dynamic_cast<fla::StaticText*>(element); text && fla::textanim::any(*text))
                letters[text] = bakedText(*text, frame->index);
    }
    int baked = 0;
    for (fla::Frame* frame : layer.frames)
    {
        if (!frame || frame->index < first || frame->index > last)
            continue;
        for (size_t i = frame->elements.size(); i-- > 0;)
        {
            // A text with letter animators: a text field a letter, each as
            // its animators leave it.
            if (auto* text = dynamic_cast<fla::StaticText*>(frame->elements[i]); text && letters.count(text))
            {
                std::vector<fla::StaticText*> made = letters[text];
                frame->elements.erase(frame->elements.begin() + static_cast<std::ptrdiff_t>(i));
                for (size_t k = 0; k < made.size(); ++k)
                {
                    made[k]->parent = frame;
                    frame->elements.insert(frame->elements.begin() + static_cast<std::ptrdiff_t>(i + k), made[k]);
                }
                delete text;
                ++baked;
                continue;
            }
            auto* shape = dynamic_cast<fla::Shape*>(frame->elements[i]);
            if (!shapes || !shape || !fla::shapeops::any(*shape))
                continue;
            std::vector<fla::Shape*> made = _phoenixView->bakedShape(*shape, frame->index);
            frame->elements.erase(frame->elements.begin() + static_cast<std::ptrdiff_t>(i));
            for (size_t k = 0; k < made.size(); ++k)
            {
                made[k]->parent = frame;
                frame->elements.insert(frame->elements.begin() + static_cast<std::ptrdiff_t>(i + k), made[k]);
            }
            delete shape;
            ++baked;
        }
    }
    // A keyframe only where something changes.
    fla::frames::mergeHeldFrames(layer, first, last);
    return baked;
}

void MainWindow::exportBakedDocument()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    QSettings settings;
    const QString start = QDir(settings.value("export/bakedFolder", QDir::homePath()).toString())
                              .filePath(QFileInfo(_documentPath.isEmpty() ? QString("Untitled") : _documentPath)
                                            .completeBaseName() + ".fla");
    const QString path = QFileDialog::getSaveFileName(this, "Export FLA for Other Apps", start,
        "FLA documents (*.fla);;XFL folders (*.xfl)");
    if (path.isEmpty())
        return;
    settings.setValue("export/bakedFolder", QFileInfo(path).absolutePath());
    QString error;
    int bakedLayers = 0;
    const bool written = writeForOtherApps(path, error, &bakedLayers);
    if (!written)
        QMessageBox::warning(this, "Export FLA for Other Apps", QString("The copy could not be written: %1").arg(error));
    else
        statusBar()->showMessage(QString("Exported %1%2").arg(QFileInfo(path).fileName(),
                                     bakedLayers == 0 ? QString(" (no behaviors to bake)") : QString(" with behaviors baked")),
            4000);
}

int MainWindow::exportForOtherAppsFromCommandLine(const QString& source, const QString& output)
{
    if (!QFileInfo::exists(source))
    {
        std::fprintf(stderr, "No such file: %s\n", qPrintable(source));
        return 1;
    }
    loadFLAFile(source);
    if (!_flaDocument || !_flaDocument->document)
    {
        std::fprintf(stderr, "Could not open %s\n", qPrintable(source));
        return 1;
    }
    QString error;
    if (!writeForOtherApps(QFileInfo(output).absoluteFilePath(), error, nullptr))
    {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    std::printf("Wrote %s for other apps\n", qPrintable(output));
    return 0;
}

void MainWindow::bakeNodeGraphs(fla::Document& copy, std::vector<std::string>& notes)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
        return;
    // Each timeline of the document with a graph, and its twin in the copy:
    // scenes by their place, symbols' by the symbol's name.
    struct Pair
    {
        const fla::Timeline* source;
        fla::Timeline* target;
        const fla::Symbol* symbol;
        std::string name;
    };
    std::vector<Pair> pairs;
    for (size_t i = 0; i < document->timelines.size() && i < copy.timelines.size(); ++i)
        if (document->timelines[i] && fla::nodegraph::active(*document->timelines[i]))
            pairs.push_back({document->timelines[i], copy.timelines[i], nullptr, document->timelines[i]->name});
    if (document->symbolList && copy.symbolList)
        for (const fla::Symbol* symbol : document->symbolList->symbols)
        {
            if (!symbol || symbol->timelines.empty() || !fla::nodegraph::active(*symbol->timelines.front()))
                continue;
            for (fla::Symbol* twin : copy.symbolList->symbols)
                if (twin && twin->name == symbol->name && !twin->timelines.empty())
                    pairs.push_back({symbol->timelines.front(), twin->timelines.front(), symbol, symbol->name});
        }

    for (const Pair& pair : pairs)
    {
        int frames = 1;
        for (const fla::Layer* layer : pair.source->layers)
            if (layer)
                for (const fla::Frame* frame : layer->frames)
                    if (frame)
                        frames = std::max(frames, frame->index + std::max(1, frame->duration));
        // A scene's stage, or all a symbol shows, with room for what blurs
        // and glows past its edge.
        QRectF area(0, 0, document->width, document->height);
        if (pair.symbol)
        {
            area = _phoenixView->symbolFramesBounds(pair.symbol, 0, frames - 1);
            if (!area.isValid() || area.isEmpty())
                continue;
            const double margin = 24.0 + std::max(area.width(), area.height()) * 0.1;
            area = area.adjusted(-margin, -margin, margin, margin).toAlignedRect();
        }
        // The pictures, one a frame, a frame like the one before held on.
        fla::Layer* layer = fla::newLayerFor(*pair.target, false);
        layer->name = "Node Graph";
        layer->autoNamed = false;
        QImage held;
        fla::Frame* span = nullptr;
        int made = 0;
        for (int at = 0; at < frames; ++at)
        {
            const QImage image = _phoenixView->renderTimelineFrame(pair.source, at, area, 1.0);
            if (image.isNull())
                break;
            if (span && image == held)
            {
                ++span->duration;
                continue;
            }
            held = image;
            std::vector<uint32_t> pixels(static_cast<size_t>(image.width()) * image.height());
            for (int y = 0; y < image.height(); ++y)
                std::memcpy(pixels.data() + static_cast<size_t>(y) * image.width(), image.constScanLine(y),
                    static_cast<size_t>(image.width()) * 4);
            const std::string name = fla::media::freeName(copy, pair.name + " graph " + std::to_string(at + 1) + ".png");
            fla::Bitmap* bitmap = fla::media::newBitmap(copy, name, std::string(), image.width(), image.height(),
                fla::encodeLosslessBitmap(image.width(), image.height(), pixels), false);
            fla::AddBitmapCommand(&copy, bitmap, "Bake Node Graph").redo();
            span = new fla::Frame(layer);
            span->index = at;
            span->duration = 1;
            auto* picture = new fla::BitmapInstance(span);
            picture->libraryItemName = name;
            picture->transform = fla::Transform(1.0, 0.0, 0.0, 1.0, area.left(), area.top());
            span->elements.push_back(picture);
            layer->frames.push_back(span);
            ++made;
        }
        // The layers it was made from are kept, as guides that do not show.
        for (fla::Layer* each : pair.target->layers)
        {
            if (!each)
                continue;
            if (each->layerType != fla::Layer::Type::Folder)
                each->layerType = fla::Layer::Type::Guide;
            if (each->parentLayerIndex >= 0)
                ++each->parentLayerIndex;
        }
        pair.target->layers.insert(pair.target->layers.begin(), layer);
        pair.target->extraAttributes = fla::nodegraph::withGraph(*pair.target, nullptr);
        notes.push_back("The node graph of " + pair.name + " is baked into a layer of " + std::to_string(made) +
                        " pictures; the layers it composited are kept as guide layers.");
    }
}

std::unique_ptr<fla::EditFramesCommand> MainWindow::bakeBehaviorsForCopy(std::shared_ptr<std::vector<std::string>> errors,
    int* bakedLayers, bool shapes)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (bakedLayers)
        *bakedLayers = 0;
    if (!document)
        return nullptr;
    // Every timeline's layers with behaviors, baked for the copy and put
    // back afterward.
    std::vector<fla::Layer*> layers;
    const auto collect = [&layers](fla::Timeline* timeline) {
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            if (layer && (!fla::behaviors::runsWithBehaviors(*layer).empty() || !shapeOperatorRuns(*layer).empty()))
                layers.push_back(layer);
    };
    for (fla::Timeline* timeline : document->timelines)
        collect(timeline);
    if (document->symbolList)
        for (fla::Symbol* symbol : document->symbolList->symbols)
            for (fla::Timeline* timeline : symbol ? symbol->timelines : std::vector<fla::Timeline*>())
                collect(timeline);
    // Layers holding controls (Slider, Angle, Point, Color, Checkbox
    // Control) last: baking takes behaviors off, and the instances linked
    // to a control read it while they are baked.
    const auto holdsControls = [](const fla::Layer* layer) {
        for (const fla::Frame* frame : layer->frames)
            for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                if (element && element->elementType() == fla::Element::Type::SymbolInstance)
                    for (const fla::behaviors::Applied& applied :
                        fla::behaviors::of(*static_cast<const fla::SymbolInstance*>(element)))
                        if (applied.type != "linkcontrol" && applied.type.size() > 7 &&
                            applied.type.compare(applied.type.size() - 7, 7, "control") == 0)
                            return true;
        return false;
    };
    std::stable_partition(layers.begin(), layers.end(), [&](const fla::Layer* layer) { return !holdsControls(layer); });
    const double rate = document->frameRate;
    std::unique_ptr<fla::EditFramesCommand> baked;
    if (!layers.empty())
    {
        _selection.clear();
        baked.reset(new fla::EditFramesCommand(layers, "Bake Behaviors", [this, layers, rate, errors, shapes](std::vector<fla::Frame*>&) {
            for (fla::Layer* layer : layers)
            {
                fla::behaviors::bakeAll(BehaviorLibrary::instance().engine(), *layer, rate, errors.get());
                for (const auto& [first, last] : shapeOperatorRuns(*layer))
                    bakeShapeOperators(*layer, first, last, shapes);
            }
        }));
        baked->redo();
    }
    if (bakedLayers)
        *bakedLayers = static_cast<int>(layers.size());
    return baked;
}

fla::otherapps::Measure MainWindow::measureForCopies()
{
    fla::otherapps::Measure measure;
    measure.textReach = [](const fla::StaticText& text, fla::Rect& reach) {
        const QRectF box = PhoenixView::textReach(text);
        reach = fla::Rect(fla::Point(box.left(), box.top()), fla::Point(box.right(), box.bottom()));
        return true;
    };
    measure.symbolBounds = [this](const fla::Symbol& symbol, int frame, fla::Rect& bounds) {
        const QRectF box = _phoenixView ? _phoenixView->symbolFramesBounds(&symbol, frame, frame) : QRectF();
        if (!box.isValid())
            return false;
        bounds = fla::Rect(fla::Point(box.left(), box.top()), fla::Point(box.right(), box.bottom()));
        return true;
    };
    return measure;
}

bool MainWindow::publishHtml5(const Html5Publish::Settings& settings, QStringList& written, QStringList& notes,
    QString& error)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
    {
        error = "No document is open.";
        return false;
    }
    // Behaviors, shape operators and text animators go out as the
    // keyframes they make, put back afterward.
    auto errors = std::make_shared<std::vector<std::string>>();
    // Shapes with operators are left for the publish to draw as pictures,
    // which keeps them exactly as Phoenix draws them.
    std::unique_ptr<fla::EditFramesCommand> baked = bakeBehaviorsForCopy(errors, nullptr, false);
    bool published = false;
    // Compiled clips carrying their movie come in as symbols of it.
    const std::map<std::string, std::vector<uint8_t>> compiled = fla::html5::compiledClips(*document);
    if (fla::otherapps::needsTemplateBaking(*document) || !compiled.empty())
    {
        // Template values, responsive time and compiled clips are written
        // into a copy read back from a temporary file, published through a
        // view of its own, so the document stays as it is.
        QTemporaryDir folder;
        const QString temporary = folder.filePath("copy.fla");
        if (!folder.isValid())
            error = "There is no room for a temporary copy.";
        else if (writeDocumentFile(*document, temporary, error))
        {
            FLAParser parser;
            std::unique_ptr<fla::FLADocument> copy(parser.parse(temporary.toStdString()));
            if (copy && copy->document)
            {
                std::vector<std::string> baking;
                const fla::otherapps::Measure measure = measureForCopies();
                fla::otherapps::bakeTemplates(*copy->document, baking, &measure);
                for (const std::string& each : baking)
                    notes << QString::fromStdString(each);
                // A compiled clip's movie read as Phoenix imports SWF movies.
                std::vector<std::unique_ptr<fla::Command>> embedded;
                std::vector<std::string> brought;
                fla::html5::embedCompiledClips(*copy->document, compiled,
                    [](const std::vector<uint8_t>& swf) -> std::unique_ptr<fla::FLADocument> {
                        QTemporaryDir folder;
                        QFile file(folder.filePath("clip.swf"));
                        if (!folder.isValid() || !file.open(QIODevice::WriteOnly) ||
                            file.write(reinterpret_cast<const char*>(swf.data()), static_cast<qint64>(swf.size())) !=
                                static_cast<qint64>(swf.size()))
                            return nullptr;
                        file.close();
                        FLAParser parser;
                        return std::unique_ptr<fla::FLADocument>(parser.parse(file.fileName().toStdString()));
                    },
                    embedded, brought);
                for (const std::string& each : brought)
                    notes << QString::fromStdString(each);
                Player player;
                PhoenixView view(&player);
                view.setDocument(copy.get());
                published = Html5Publish::run(settings, view, _timelineAudio, *copy->document, written, notes, error);
                view.setDocument(nullptr);
            }
            else
                error = QString::fromStdString(parser.errorString());
        }
    }
    else
        published = Html5Publish::run(settings, *_phoenixView, _timelineAudio, *document, written, notes, error);
    if (baked)
        baked->undo();
    for (const std::string& each : *errors)
        notes << QString::fromStdString(each);
    _phoenixView->clearCaches();
    _phoenixView->update();
    return published;
}

bool MainWindow::writeForOtherApps(const QString& path, QString& error, int* bakedLayers)
{
    if (!_flaDocument || !_flaDocument->document)
    {
        error = "No document is open.";
        return false;
    }
    fla::Document* document = _flaDocument->document;
    auto errors = std::make_shared<std::vector<std::string>>();
    std::unique_ptr<fla::EditFramesCommand> baked = bakeBehaviorsForCopy(errors, bakedLayers, true);
    bool written = false;
    std::vector<std::string> notes;
    // Node graphs are baked into pictures in the copy too.
    bool graphs = false;
    for (const fla::Timeline* timeline : document->timelines)
        graphs = graphs || (timeline && fla::nodegraph::active(*timeline));
    if (document->symbolList)
        for (const fla::Symbol* symbol : document->symbolList->symbols)
            graphs = graphs || (symbol && !symbol->timelines.empty() && fla::nodegraph::active(*symbol->timelines.front()));
    if (fla::otherapps::needsBaking(*document) || graphs)
    {
        // Mattes, adjustment layers and template values are baked into a
        // copy read back from a temporary file, so the document stays as
        // it is.
        QTemporaryDir folder;
        const QString temporary = folder.filePath("copy.fla");
        if (!folder.isValid())
            error = "There is no room for a temporary copy.";
        else if (writeDocumentFile(*document, temporary, error))
        {
            FLAParser parser;
            std::unique_ptr<fla::FLADocument> copy(parser.parse(temporary.toStdString()));
            if (copy && copy->document)
            {
                // Pinned parts are placed by how big the words and the art
                // are drawn.
                const fla::otherapps::Measure measure = measureForCopies();
                // Effects with no filter in other apps are drawn into
                // bitmaps, by a view of the copy of its own, as an exported
                // movie draws them (no camera: other apps have none).
                Player player;
                PhoenixView view(&player);
                view.setDocument(copy.get());
                view.setCameraView(false);
                const fla::Document* drawnDocument = copy->document;
                const fla::otherapps::Renderer render = [&view, drawnDocument](const fla::otherapps::Drawing& drawing) {
                    fla::otherapps::Picture picture;
                    const fla::Timeline* timeline = drawing.timeline;
                    if (!timeline)
                        return picture;
                    // A scene's stage, just as a movie's frame shows it, so
                    // an adjustment's effects work on the same picture
                    // (with room round it for an instance partly off it);
                    // or what a symbol draws on the frame, and as far as
                    // the effects reach past it.
                    QRectF area;
                    const auto& scenes = drawnDocument->timelines;
                    const double margin = std::min(drawing.margin, 2000.0) + 2.0;
                    if (std::find(scenes.begin(), scenes.end(), timeline) != scenes.end())
                    {
                        area = QRectF(0, 0, drawnDocument->width, drawnDocument->height);
                        if (drawing.element)
                            area.adjust(-area.width() / 4.0 - margin, -area.height() / 4.0 - margin,
                                area.width() / 4.0 + margin, area.height() / 4.0 + margin);
                    }
                    else if (const auto* symbol = dynamic_cast<const fla::Symbol*>(timeline->parent))
                        area = view.symbolFramesBounds(symbol, drawing.frame, drawing.frame)
                                   .adjusted(-margin, -margin, margin, margin);
                    if (!area.isValid() || area.isEmpty())
                        return picture;
                    // A pixel a stage pixel, unless that is huge.
                    const double scale = std::min(1.0, 4096.0 / std::max(area.width(), area.height()));
                    const QImage image =
                        view.renderTimelinePart(timeline, drawing.from, drawing.to, drawing.element, drawing.frame, area, scale);
                    if (image.isNull())
                        return picture;
                    picture.width = image.width();
                    picture.height = image.height();
                    picture.x = area.left();
                    picture.y = area.top();
                    picture.scale = scale;
                    picture.pixels.resize(static_cast<size_t>(image.width()) * static_cast<size_t>(image.height()));
                    for (int y = 0; y < image.height(); ++y)
                    {
                        const auto* line = reinterpret_cast<const uint32_t*>(image.constScanLine(y));
                        std::copy(line, line + image.width(),
                            picture.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * image.width()));
                    }
                    return picture;
                };
                fla::otherapps::bakeCompositing(*copy->document, notes, render, &measure);
                view.setDocument(nullptr);
                bakeNodeGraphs(*copy->document, notes);
                written = writeDocumentFile(*copy->document, path, error);
            }
            else
                error = QString::fromStdString(parser.errorString());
        }
    }
    else
        written = writeDocumentFile(*document, path, error);
    if (baked)
        baked->undo();
    for (const std::string& each : *errors)
        BehaviorLibrary::instance().reportOnce(QString::fromStdString(each));
    for (const std::string& each : notes)
        BehaviorLibrary::instance().reportOnce(QString("Export FLA for Other Apps: %1").arg(QString::fromStdString(each)));
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    return written;
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
    for (const auto& spans : _frameClipboard)
        for (const auto& frame : spans)
            for (const fla::Element* element : frame->elements)
                if (element && fla::placingNestsSymbol(*element, _phoenixView->scopeSymbol()))
                {
                    statusBar()->showMessage("The frames contain the symbol being edited, "
                                             "which cannot go inside itself.", 4000);
                    return;
                }

    // Copied from another document still open: the symbols, bitmaps and
    // sounds the frames use come along first, as one step with the paste.
    const char* stepName = overwrite ? "Paste and Overwrite Frames" : "Paste Frames";
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::CommandStack& stack = _editContext.commandStack();
    std::map<std::string, std::string> renamed;
    bool macro = false;
    if (document && _frameClipboardFrom && _frameClipboardFrom != document && documentOpenAnywhere(_frameClipboardFrom))
    {
        std::vector<const fla::Frame*> all;
        for (const auto& spans : _frameClipboard)
            for (const auto& frame : spans)
                all.push_back(frame.get());
        const std::vector<std::string> used = fla::transfer::usedByFrames(all);
        if (!fla::transfer::withDependencies(*_frameClipboardFrom, used).empty())
        {
            stack.beginMacro(stepName);
            macro = true;
            if (!bringLibraryItems(*_frameClipboardFrom, used, renamed))
            {
                stack.endMacro();
                return;
            }
        }
    }
    bool rename = std::any_of(renamed.begin(), renamed.end(),
        [](const auto& name) { return name.first != name.second; });
    // The paint brushes the frames' strokes draw with come too, each once by
    // guid, the copies drawing with them as this document numbers them.
    std::map<int, int> brushIndexes;
    if (document && _frameClipboardFrom && _frameClipboardFrom != document && documentOpenAnywhere(_frameClipboardFrom))
    {
        std::vector<const fla::Element*> drawn;
        for (const auto& spans : _frameClipboard)
            for (const auto& frame : spans)
                for (const fla::Element* element : frame->elements)
                    drawn.push_back(element);
        const std::vector<fla::PaintBrushDefinition> brushes = fla::paintBrushesFor(*_frameClipboardFrom, drawn);
        if (fla::CommandPtr command =
                brushes.empty() ? nullptr : fla::adoptPaintBrushes(*document, brushes, brushIndexes, stepName))
        {
            if (!macro)
            {
                stack.beginMacro(stepName);
                macro = true;
            }
            stack.push(std::move(command));
        }
        if (std::any_of(brushIndexes.begin(), brushIndexes.end(),
                [](const auto& index) { return index.first != index.second; }))
            rename = true;
    }

    // The copies may come from another document: their instances are
    // pointed at this one's symbols before they are pasted.
    const fla::SymbolList* library = document ? document->symbolList : nullptr;
    relinkFrameClipboard(library);

    // Items that came in under new names: the frames pasted are copies
    // naming them so, kept for as long as the step is.
    auto renamedFrames = std::make_shared<std::vector<std::unique_ptr<fla::Frame>>>();
    std::vector<std::vector<const fla::Frame*>> clips;
    for (const auto& spans : _frameClipboard)
    {
        std::vector<const fla::Frame*> clip;
        for (const auto& frame : spans)
        {
            if (!rename)
            {
                clip.push_back(frame.get());
                continue;
            }
            fla::Frame* copy = fla::timeline::cloneFrame(*frame, nullptr);
            fla::transfer::renameUses(*copy, renamed);
            for (fla::Element* element : copy->elements)
                if (element)
                {
                    fla::relinkSymbols(*element, library);
                    fla::remapPaintBrushes(*element, brushIndexes);
                }
            renamedFrames->emplace_back(copy);
            clip.push_back(copy);
        }
        clips.push_back(clip);
    }
    const int at = selection.first;

    _selection.clear();
    auto* command = new fla::EditFramesCommand(targets, stepName,
        [targets, clips, at, overwrite, renamedFrames](std::vector<fla::Frame*>& removed) {
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
    if (macro)
        stack.endMacro();
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

    // Above the current layer; at the top otherwise.
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
    // A timeline keeps at least one layer that can hold frames.
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
    // Asked for a moment ago: the timeline may have gone since.
    if (!stillInDocument(_flaDocument, timeline) || layerIndex < 0 || layerIndex >= static_cast<int>(timeline->layers.size()))
        return;
    fla::Layer* layer = timeline->layers[layerIndex];
    if (!layer)
        return;
    const std::string after = name.toStdString();
    if (after == layer->name)
        return;

    const std::string before = layer->name;
    // A typed name stops being automatic, so it is never renumbered.
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Rename Layer");
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(layer->name, layer->name, after, "Rename Layer")));
    stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(layer->autoNamed, layer->autoNamed, false, "Rename Layer")));
    // The timeline's node graph follows the layer by its new name.
    auto* owner = const_cast<fla::Timeline*>(timeline);
    const std::vector<std::pair<std::string, std::string>> graphAfter = fla::nodegraph::withLayerRenamed(*owner, before, after);
    if (graphAfter != owner->extraAttributes)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
            owner->extraAttributes, owner->extraAttributes, graphAfter, "Rename Layer")));
    // Layers also masked by a renamed mask layer follow it to its new name.
    if (layer->layerType == fla::Layer::Type::Mask)
        for (fla::Layer* other : timeline->layers)
        {
            if (!other)
                continue;
            std::vector<fla::compositing::MoreMask> more = fla::compositing::moreMasksOf(*other);
            bool named = false;
            for (fla::compositing::MoreMask& each : more)
                if (each.layer == before)
                {
                    each.layer = after;
                    named = true;
                }
            if (named)
            {
                using Attributes = std::vector<std::pair<std::string, std::string>>;
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(other->extraAttributes, other->extraAttributes,
                    fla::compositing::withMoreMasks(*other, more), "Rename Layer")));
            }
        }
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
    QAction* closeAllAction = fileMenu->addAction("Close A&ll");
    closeAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W));
    closeAllAction->setStatusTip("Close every document in this window, asking about unsaved changes");
    connect(closeAllAction, &QAction::triggered, this, [this]() { closeAllTabs(); });
    QAction* nextTabAction = new QAction(this);
    nextTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    connect(nextTabAction, &QAction::triggered, this, [this]() { cycleTabs(1); });
    addAction(nextTabAction);
    // Shift+Tab arrives as Backtab on most systems.
    QAction* previousTabAction = new QAction(this);
    previousTabAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)});
    connect(previousTabAction, &QAction::triggered, this, [this]() { cycleTabs(-1); });
    addAction(previousTabAction);

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
    QAction* externalLibraryAction = importMenu->addAction("Open &External Library...");
    externalLibraryAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    externalLibraryAction->setStatusTip("Another file's library beside this one, to copy items from");
    connect(externalLibraryAction, &QAction::triggered, this, &MainWindow::openExternalLibrary);
    QAction* importAnimaticAction = importMenu->addAction("Import &Animatic...");
    importAnimaticAction->setStatusTip("An animatic's cut from Storyboard Pro or an editor (EDL, Final Cut XML): its shots as labels, boards and audio");
    connect(importAnimaticAction, &QAction::triggered, this, &MainWindow::importAnimatic);
    QAction* importVideoAction = new QAction("Import &Video...", this);
    importVideoAction->setStatusTip("Bring in a video to animate over, on a guide layer");
    connect(importVideoAction, &QAction::triggered, this, &MainWindow::importVideo);
    importMenu->addAction(importVideoAction);

    QMenu* exportMenu = fileMenu->addMenu("Export");

    QAction* exportBakedAction = new QAction("Export FLA for Other &Apps...", this);
    exportBakedAction->setStatusTip("Save a copy with every behavior baked into keyframes, for apps that open FLA files but not Phoenix's own motion");
    connect(exportBakedAction, &QAction::triggered, this, &MainWindow::exportBakedDocument);
    exportMenu->addAction(exportBakedAction);
    QAction* renderDataAction = new QAction("Render from &Data...", this);
    renderDataAction->setStatusTip("Render the movie once for each row of a CSV file, its template fields filled from the row");
    connect(renderDataAction, &QAction::triggered, this, &MainWindow::renderFromData);
    exportMenu->addAction(renderDataAction);
    exportMenu->addSeparator();

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
    QAction* exportSpritesAction = new QAction("Export S&prite Sheet...", this);
    exportSpritesAction->setStatusTip("Lay the frames out on one image, with a JSON description for game engines");
    connect(exportSpritesAction, &QAction::triggered, this, [this]() { exportSpriteSheet(); });
    exportMenu->addAction(exportSpritesAction);
    QAction* exportShotsAction = new QAction("Export S&hots...", this);
    exportShotsAction->setStatusTip("Split the episode into shot files, by scene, frame label or camera cut");
    connect(exportShotsAction, &QAction::triggered, this, &MainWindow::exportShots);
    exportMenu->addAction(exportShotsAction);
    QAction* exportSwfAction = new QAction("Export S&WF Movie...", this);
    exportSwfAction->setStatusTip("The scene as an SWF movie of vector shapes, for players and pipelines that read SWF");
    connect(exportSwfAction, &QAction::triggered, this, [this]() {
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document)
            return;
        const QString base = _documentPath.isEmpty() ? QString("movie") : QFileInfo(_documentPath).completeBaseName();
        const QString folder = _lastDirectory.isEmpty() ? QFileInfo(_documentPath).absolutePath() : _lastDirectory;
        const QString path = QFileDialog::getSaveFileName(this, "Export SWF Movie", QDir(folder).filePath(base + ".swf"),
            "SWF Movie (*.swf)");
        if (path.isEmpty())
            return;
        QString error;
        if (exportSwf(path, QSize(document->width, document->height), error))
        {
            _lastDirectory = QFileInfo(path).absolutePath();
            statusBar()->showMessage(QString("Exported %1").arg(QDir::toNativeSeparators(path)), 5000);
        }
        else if (!error.isEmpty())
            QMessageBox::warning(this, "Export Failed", error);
    });
    exportMenu->addAction(exportSwfAction);

    // Publish: Publish Settings, then the document's publish configurations,
    // the current one first (Shift+F12), each publishing as it says.
    QMenu* publishMenu = fileMenu->addMenu("Publish");
    QAction* publishSettingsAction = publishMenu->addAction("Publish Settings...");
    publishSettingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F12));
    publishSettingsAction->setStatusTip("The publish configurations: which formats each writes, where, and their options");
    connect(publishSettingsAction, &QAction::triggered, this, &MainWindow::publishSettings);
    publishMenu->addSeparator();
    QAction* publishAction = publishMenu->addAction("Publish");
    publishAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    publishAction->setStatusTip("Publish with the current publish configuration");
    connect(publishAction, &QAction::triggered, this, [this]() { publish(); });
    connect(publishMenu, &QMenu::aboutToShow, this, [this, publishMenu, publishAction, publishSettingsAction]() {
        // The other configurations, made again each time the menu opens.
        for (QAction* action : publishMenu->actions())
            if (action != publishAction && action != publishSettingsAction && !action->isSeparator())
            {
                publishMenu->removeAction(action);
                action->deleteLater();
            }
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        const std::string xml = document ? publishXml(*document) : std::string();
        const std::string current = fla::publish::read(xml, "Untitled").profile;
        publishAction->setText(QString::fromStdString(current));
        publishAction->setEnabled(document != nullptr);
        for (const std::string& name : fla::publish::profiles(xml))
        {
            if (name == current)
                continue;
            const QString profile = QString::fromStdString(name);
            QAction* other = publishMenu->addAction(profile);
            other->setStatusTip(QString("Publish with the configuration %1").arg(profile));
            connect(other, &QAction::triggered, this, [this, profile]() { publish(profile); });
        }
    });

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
    // QKeySequence::Redo is Ctrl+Y on Windows; Ctrl+Shift+Z is what many
    // will reach for, so accept both.
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
    QAction* findNextAction = editMenu->addAction("Find &Next");
    findNextAction->setShortcut(QKeySequence(Qt::Key_F3));
    findNextAction->setStatusTip("Go to the next place what Find and Replace looks for is found");
    connect(findNextAction, &QAction::triggered, this, [this]() {
        if (!_findReplace)
        {
            showFindReplace();
            return;
        }
        if (!_findReplace->findNext())
            statusBar()->showMessage("Nothing found.", 3000);
    });
    _fontMappingAction = editMenu->addAction("&Font Mapping...");
    _fontMappingAction->setStatusTip("Choose the installed fonts missing ones are shown in");
    connect(_fontMappingAction, &QAction::triggered, this, [this]() { fontMappingDialog(false); });
    QAction* spellingAction = editMenu->addAction("Check &Spelling...");
    spellingAction->setStatusTip("Check the spelling of every text field in the document");
    connect(spellingAction, &QAction::triggered, this, &MainWindow::checkSpelling);
    editMenu->addSeparator();
    QAction* shortcutsAction = editMenu->addAction("&Keyboard Shortcuts...");
    shortcutsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_K));
    shortcutsAction->setStatusTip("See and change the key for any command or tool");
    connect(shortcutsAction, &QAction::triggered, this, [this]() {
        ShortcutsDialog dialog(_shortcuts, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        Shortcuts::setCurrentSet(dialog.chosen());
        Shortcuts::apply(_shortcuts);
    });
    QAction* preferencesAction = editMenu->addAction("Pre&ferences...");
    preferencesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    preferencesAction->setMenuRole(QAction::PreferencesRole);
    preferencesAction->setStatusTip("Undo levels, auto-recovery, highlight color, click accuracy and pen display");
    connect(preferencesAction, &QAction::triggered, this, [this]() {
        PreferencesDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        dialog.save();
        _editContext.commandStack().setLimit(static_cast<size_t>(Preferences::undoLevels()));
        if (_mcpServer)
            _mcpServer->applySettings();
        if (_phoenixView)
            _phoenixView->update();
    });

    // Modify menu.
    // Insert: the timeline commands, which also live on the frames'
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
    QAction* bakeBehaviorsAction = editTimelineMenu->addAction("&Bake Behaviors to Keyframes");
    bakeBehaviorsAction->setStatusTip("Leave the selected frames' behaviors as keyframes, a keyframe a frame, their copies as instances");
    connect(bakeBehaviorsAction, &QAction::triggered, this, &MainWindow::bakeBehaviors);
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
    // An armature layer's poses.
    QMenu* poseMenu = new QMenu("&Pose", this);
    const auto poseAction = [this, poseMenu](const char* text, const char* tip, PoseCommand command) {
        QAction* action = poseMenu->addAction(text);
        action->setStatusTip(tip);
        connect(action, &QAction::triggered, this, [this, command]() { poseCommand(command); });
    };
    poseAction("&Insert Pose", "Make the armature's look on this frame a pose", PoseCommand::Insert);
    poseAction("&Copy Pose", "Copy the armature's pose on this frame", PoseCommand::Copy);
    poseAction("&Paste Pose", "Pose the armature on this frame as the pose copied", PoseCommand::Paste);
    poseAction("C&lear Pose", "Take away the pose on this frame; the armature tweens past it", PoseCommand::Clear);
    _framesMenu->addSeparator();
    QAction* poseMenuAction = _framesMenu->addMenu(poseMenu);
    connect(_framesMenu, &QMenu::aboutToShow, this, [this, poseMenuAction]() {
        size_t child = 0;
        int into = 0;
        poseMenuAction->setVisible(currentPoseSpan(child, into) != nullptr);
    });
    editTimelineMenu->addSeparator();
    editTimelineMenu->addMenu(poseMenu);

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
    _templateFieldsAction = new QAction("&Template Fields...", this);
    _templateFieldsAction->setStatusTip("Fill in the words and colors of the selected instance's symbol, for this instance only");
    connect(_templateFieldsAction, &QAction::triggered, this, &MainWindow::editTemplateFields);
    symbolMenu->addAction(_templateFieldsAction);
    QMenu* bitmapMenu = modifyMenu->addMenu("&Bitmap");
    _swapBitmapAction = bitmapMenu->addAction("&Swap Bitmap...");
    _swapBitmapAction->setStatusTip("Show another library bitmap in the selected bitmaps");
    connect(_swapBitmapAction, &QAction::triggered, this, &MainWindow::swapBitmap);
    _traceBitmapAction = bitmapMenu->addAction("&Trace Bitmap...");
    _traceBitmapAction->setStatusTip("Turn the selected bitmaps into vector artwork: areas of like color become fills");
    connect(_traceBitmapAction, &QAction::triggered, this, &MainWindow::traceBitmaps);

    modifyMenu->addSeparator();

    // Modify > Transform.
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
    QAction* splitAudioAction = modifyTimelineMenu->addAction("Split &Audio");
    splitAudioAction->setStatusTip("Cut the stream sound on the current layer at the playhead into two keyframes");
    connect(splitAudioAction, &QAction::triggered, this, &MainWindow::splitAudio);

    // Modify > Shape.
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

    // Modify > Align: the Align panel's buttons, with keys.
    {
        QMenu* alignMenu = modifyMenu->addMenu("A&lign");
        const auto alignItem = [this, alignMenu](const char* name, const QKeySequence& key,
                                   std::function<void(AlignPanel&)> act) {
            QAction* action = alignMenu->addAction(name);
            action->setShortcut(key);
            connect(action, &QAction::triggered, this, [this, act]() {
                if (_alignPanel)
                    act(*_alignPanel);
            });
            return action;
        };
        alignItem("&Left", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1), [](AlignPanel& p) { p.align(AlignPanel::Edge::Left); });
        alignItem("&Horizontal Center", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2), [](AlignPanel& p) { p.align(AlignPanel::Edge::HCenter); });
        alignItem("&Right", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3), [](AlignPanel& p) { p.align(AlignPanel::Edge::Right); });
        alignItem("&Top", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_4), [](AlignPanel& p) { p.align(AlignPanel::Edge::Top); });
        alignItem("&Vertical Center", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_5), [](AlignPanel& p) { p.align(AlignPanel::Edge::VCenter); });
        alignItem("&Bottom", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_6), [](AlignPanel& p) { p.align(AlignPanel::Edge::Bottom); });
        alignMenu->addSeparator();
        alignItem("Make Same &Width", QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_7), [](AlignPanel& p) { p.matchSize(true, false); });
        alignItem("Make Same H&eight", QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_9), [](AlignPanel& p) { p.matchSize(false, true); });
        alignMenu->addSeparator();
        QAction* toStage = alignItem("To &Stage", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_8),
            [](AlignPanel& p) { p.setAlignToStage(!p.alignToStage()); });
        toStage->setCheckable(true);
        toStage->setStatusTip("Line up with the stage rather than with each other, as the Align panel's box does");
        connect(alignMenu, &QMenu::aboutToShow, this, [this, toStage]() {
            toStage->setChecked(_alignPanel && _alignPanel->alignToStage());
        });
    }

    // Text: the style and alignment of the text being typed, or of the
    // text fields selected.
    {
        QMenu* textMenu = menuBar->addMenu("&Text");
        // The setting where the caret is, or of the first field selected.
        const auto current = [this](const std::string& key) -> std::string {
            const fla::StaticText* text = _textTool && _textTool->isEditing() ? _textTool->editedText() : nullptr;
            if (text && !text->runs.empty())
            {
                int at = _textTool->caretIndex();
                for (const fla::TextRun& run : text->runs)
                {
                    const int length = static_cast<int>(QString::fromStdString(run.text).size());
                    if (at < length)
                        return fla::textstyle::get(run, key);
                    at -= length;
                }
                return fla::textstyle::get(text->runs.back(), key);
            }
            for (fla::DOMElement* selected : _selection.elements())
                if (auto* field = dynamic_cast<fla::StaticText*>(selected); field && !field->runs.empty())
                    return fla::textstyle::get(field->runs.front(), key);
            return std::string();
        };
        QMenu* styleMenu = textMenu->addMenu("&Style");
        const auto toggle = [this, styleMenu, current](const char* name, const char* key, Qt::Key letter) {
            QAction* action = styleMenu->addAction(name);
            action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | letter));
            connect(action, &QAction::triggered, this, [this, key, current]() {
                applyTextStyle(key, current(key) == "true" ? "false" : "true");
            });
        };
        toggle("&Bold", "bold", Qt::Key_B);
        toggle("&Italic", "italic", Qt::Key_I);
        QMenu* paragraphMenu = textMenu->addMenu("&Align");
        const auto aligned = [this, paragraphMenu](const char* name, const char* value, Qt::Key letter) {
            QAction* action = paragraphMenu->addAction(name);
            action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | letter));
            connect(action, &QAction::triggered, this, [this, value]() { applyTextStyle("alignment", value); });
        };
        aligned("Align &Left", "left", Qt::Key_L);
        aligned("&Center", "center", Qt::Key_C);
        aligned("Align &Right", "right", Qt::Key_R);
        aligned("&Justify", "justify", Qt::Key_J);
    }

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

    // The stage shows one scene at a time; these pick which.
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

    // Sound plays with the timeline; Control > Mute Sounds silences it.
    QAction* muteAction = new QAction("&Mute Sounds", this);
    muteAction->setCheckable(true);
    muteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M));
    muteAction->setStatusTip("Play the timeline without its sounds");
    connect(muteAction, &QAction::toggled, this, [this](bool on) {
        if (_timelineAudio)
            _timelineAudio->setMuted(on);
    });
    viewMenu->addAction(muteAction);

    // Movie clips play their own sounds as they play on the stage; this
    // leaves only the scene's. Exports always include them.
    QAction* muteClipsAction = new QAction("Mute Movie &Clip Sounds", this);
    muteClipsAction->setCheckable(true);
    muteClipsAction->setChecked(QSettings().value("sound/muteMovieClips", false).toBool());
    muteClipsAction->setStatusTip("Play only the scene's own sounds, not those inside movie clips");
    if (_timelineAudio)
        _timelineAudio->setMovieClipSounds(!muteClipsAction->isChecked());
    connect(muteClipsAction, &QAction::toggled, this, [this](bool on) {
        if (_timelineAudio)
            _timelineAudio->setMovieClipSounds(!on);
        QSettings().setValue("sound/muteMovieClips", on);
    });
    viewMenu->addAction(muteClipsAction);

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
        // A setting of the document, saved with it.
        if (_flaDocument && _flaDocument->document)
            _flaDocument->document->guidesSnapTo = on;
    });
    QAction* lockGuidesAction = guidesMenu->addAction("&Lock Guides");
    lockGuidesAction->setCheckable(true);
    lockGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
    lockGuidesAction->setChecked(QSettings().value("view/guidesLocked", false).toBool());
    _phoenixView->setGuidesLocked(lockGuidesAction->isChecked());
    connect(lockGuidesAction, &QAction::toggled, this, [this](bool on) {
        _phoenixView->setGuidesLocked(on);
        QSettings().setValue("view/guidesLocked", on);
    });
    QAction* editGuidesAction = guidesMenu->addAction("&Edit Guides...");
    editGuidesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_G));
    connect(editGuidesAction, &QAction::triggered, this,
        [this, showGuidesAction, snapGuidesAction, lockGuidesAction]() {
            if (!_flaDocument || !_flaDocument->document || !_phoenixView)
                return;
            fla::Document* document = _flaDocument->document;
            QDialog dialog(this);
            dialog.setWindowTitle("Guides");
            QFormLayout* form = new QFormLayout(&dialog);
            QColor color(document->guidesColor[0], document->guidesColor[1], document->guidesColor[2]);
            QPushButton* colorButton = new QPushButton(color.name(), &dialog);
            connect(colorButton, &QPushButton::clicked, &dialog, [&]() {
                const QColor chosen = QColorDialog::getColor(color, &dialog, "Guide Color");
                if (chosen.isValid())
                {
                    color = chosen;
                    colorButton->setText(color.name());
                }
            });
            form->addRow("Color", colorButton);
            QCheckBox* show = new QCheckBox("Show guides", &dialog);
            show->setChecked(showGuidesAction->isChecked());
            QCheckBox* snap = new QCheckBox("Snap to guides", &dialog);
            snap->setChecked(snapGuidesAction->isChecked());
            QCheckBox* lock = new QCheckBox("Lock guides", &dialog);
            lock->setChecked(lockGuidesAction->isChecked());
            form->addRow(QString(), show);
            form->addRow(QString(), snap);
            form->addRow(QString(), lock);
            QPushButton* clearAll = new QPushButton("Clear All", &dialog);
            bool cleared = false;
            connect(clearAll, &QPushButton::clicked, &dialog, [&]() { cleared = true; });
            QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            buttons->addButton(clearAll, QDialogButtonBox::ResetRole);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if (dialog.exec() != QDialog::Accepted)
                return;
            document->guidesColor[0] = static_cast<uint8_t>(color.red());
            document->guidesColor[1] = static_cast<uint8_t>(color.green());
            document->guidesColor[2] = static_cast<uint8_t>(color.blue());
            _editContext.commandStack().setDirty();
            updateEditState();
            showGuidesAction->setChecked(show->isChecked());
            snapGuidesAction->setChecked(snap->isChecked());
            lockGuidesAction->setChecked(lock->isChecked());
            if (cleared)
                _phoenixView->clearGuides();
            _phoenixView->update();
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

    // The View menu: Zoom In and Out step through the magnification
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
    // B belongs to the brush and Ctrl+Shift+B to Text > Bold.
    showBoundsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_B));
    showBoundsAction->setStatusTip("Show element bounding boxes used for culling (Green=rendered, Red=culled, Blue=visible area)");
    connect(showBoundsAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowBounds);
    viewMenu->addAction(showBoundsAction);

    QAction* hideEdgesAction = viewMenu->addAction("Hide &Edges");
    hideEdgesAction->setCheckable(true);
    hideEdgesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    hideEdgesAction->setStatusTip("Hide the selection's highlight, to see the art as it is while it stays selected");
    connect(hideEdgesAction, &QAction::toggled, _phoenixView, &PhoenixView::setHideEdges);
    QAction* shapeHintsAction = viewMenu->addAction("Show Shape &Hints");
    shapeHintsAction->setCheckable(true);
    shapeHintsAction->setChecked(true);
    shapeHintsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    shapeHintsAction->setStatusTip("Show the lettered shape hints of a shape tween on its keyframes");
    connect(shapeHintsAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowShapeHints);

    viewMenu->addSeparator();

    QAction* showGridAction = new QAction("Show &Grid", this);
    showGridAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
    showGridAction->setCheckable(true);
    showGridAction->setStatusTip("Show the document grid");
    connect(showGridAction, &QAction::toggled, _phoenixView, &PhoenixView::setShowGrid);
    viewMenu->addAction(showGridAction);

    QAction* snapToGridAction = new QAction("Snap to Grid", this);
    snapToGridAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Apostrophe));
    snapToGridAction->setCheckable(true);
    snapToGridAction->setStatusTip("Snap dragged objects to the document grid");
    connect(snapToGridAction, &QAction::toggled, this, [this](bool on) {
        _snapper.setGridEnabled(on);
        saveSettings();
    });
    viewMenu->addAction(snapToGridAction);

    QAction* editGridAction = viewMenu->addAction("&Edit Grid...");
    editGridAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
    editGridAction->setStatusTip("The grid's color and spacing, and whether it shows over the art");
    connect(editGridAction, &QAction::triggered, this, [this, showGridAction, snapToGridAction]() {
        if (!_flaDocument || !_flaDocument->document || !_phoenixView)
            return;
        fla::Document* document = _flaDocument->document;
        QDialog dialog(this);
        dialog.setWindowTitle("Grid");
        QFormLayout* form = new QFormLayout(&dialog);
        QColor color(document->gridColor[0], document->gridColor[1], document->gridColor[2], document->gridColor[3]);
        QPushButton* colorButton = new QPushButton(color.name(), &dialog);
        connect(colorButton, &QPushButton::clicked, &dialog, [&]() {
            const QColor chosen = QColorDialog::getColor(color, &dialog, "Grid Color");
            if (chosen.isValid())
            {
                color = chosen;
                colorButton->setText(color.name());
            }
        });
        form->addRow("Color", colorButton);
        QCheckBox* show = new QCheckBox("Show grid", &dialog);
        show->setChecked(showGridAction->isChecked());
        QCheckBox* over = new QCheckBox("Show over objects", &dialog);
        over->setChecked(_phoenixView->gridOver());
        QCheckBox* snap = new QCheckBox("Snap to grid", &dialog);
        snap->setChecked(snapToGridAction->isChecked());
        form->addRow(QString(), show);
        form->addRow(QString(), over);
        form->addRow(QString(), snap);
        HotSpinBox* across = new HotSpinBox(&dialog);
        HotSpinBox* down = new HotSpinBox(&dialog);
        for (HotSpinBox* box : {across, down})
        {
            box->setRange(1, 2000);
            box->setSuffix(" px");
        }
        across->setValue(document->gridSpacingX);
        down->setValue(document->gridSpacingY);
        form->addRow("Spacing across", across);
        form->addRow("Spacing down", down);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted)
            return;
        // The grid is the document's, saved with it.
        document->gridColor[0] = static_cast<uint8_t>(color.red());
        document->gridColor[1] = static_cast<uint8_t>(color.green());
        document->gridColor[2] = static_cast<uint8_t>(color.blue());
        document->gridSpacingX = across->value();
        document->gridSpacingY = down->value();
        _editContext.commandStack().setDirty();
        updateEditState();
        showGridAction->setChecked(show->isChecked());
        snapToGridAction->setChecked(snap->isChecked());
        _phoenixView->setGridOver(over->isChecked());
        QSettings().setValue("view/gridOver", over->isChecked());
        _phoenixView->update();
    });
    _phoenixView->setGridOver(QSettings().value("view/gridOver", false).toBool());

    QAction* clipAction = viewMenu->addAction("&Clip Content Outside Stage");
    clipAction->setCheckable(true);
    clipAction->setChecked(QSettings().value("view/clipOutsideStage", false).toBool());
    clipAction->setStatusTip("Hide what lies off the stage");
    _phoenixView->setClipOutsideStage(clipAction->isChecked());
    connect(clipAction, &QAction::toggled, this, [this](bool on) {
        _phoenixView->setClipOutsideStage(on);
        QSettings().setValue("view/clipOutsideStage", on);
    });
    QAction* pasteboardAction = viewMenu->addAction("&Pasteboard");
    pasteboardAction->setCheckable(true);
    pasteboardAction->setChecked(QSettings().value("view/pasteboard", true).toBool());
    pasteboardAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    pasteboardAction->setStatusTip("Show the area round the stage and what is on it");
    _phoenixView->setShowPasteboard(pasteboardAction->isChecked());
    connect(pasteboardAction, &QAction::toggled, this, [this](bool on) {
        _phoenixView->setShowPasteboard(on);
        QSettings().setValue("view/pasteboard", on);
    });
    QAction* centerAction = viewMenu->addAction("Center &Stage");
    centerAction->setStatusTip("Bring the stage to the middle of the view");
    connect(centerAction, &QAction::triggered, _phoenixView, &PhoenixView::centerStage);

    QAction* snapToObjectsAction = new QAction("Snap to &Objects", this);
    _snapToObjectsAction = snapToObjectsAction;
    snapToObjectsAction->setCheckable(true);
    snapToObjectsAction->setChecked(true);
    snapToObjectsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Slash));
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
    dimOutsideDocumentAction->setChecked(QSettings().value("view/dimOutside", false).toBool());
    dimOutsideDocumentAction->setStatusTip("Dim the area outside the document rectangle");
    _phoenixView->setDimOutsideDocument(dimOutsideDocumentAction->isChecked());
    connect(dimOutsideDocumentAction, &QAction::toggled, this, [this](bool on) {
        _phoenixView->setDimOutsideDocument(on);
        QSettings().setValue("view/dimOutside", on);
    });
    viewMenu->addAction(dimOutsideDocumentAction);

    // Safe zones and aspect-ratio frames: whether they show is the viewer's
    // choice, remembered; what they are belongs to the document.
    QAction* safeZonesAction = new QAction("Safe Zones", this);
    safeZonesAction->setObjectName("viewSafeZones");
    safeZonesAction->setCheckable(true);
    safeZonesAction->setChecked(QSettings().value("view/safeZones", false).toBool());
    safeZonesAction->setStatusTip("Show title and action safe and the aspect-ratio frames over the stage");
    _phoenixView->setShowSafeZones(safeZonesAction->isChecked());
    connect(safeZonesAction, &QAction::toggled, this, [this](bool on) {
        _phoenixView->setShowSafeZones(on);
        QSettings().setValue("view/safeZones", on);
    });
    viewMenu->addAction(safeZonesAction);
    // New settings as one undo step; the overlay shows them.
    const auto setSafeZones = [this, safeZonesAction](const fla::safezones::Settings& settings, const std::string& name) {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document)
            return;
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        const Attributes after = fla::safezones::withSettings(*document, settings);
        if (after != document->extraAttributes)
            _editContext.commandStack().push(fla::CommandPtr(
                new fla::SetPropertyCommand<Attributes>(document->extraAttributes, document->extraAttributes, after, name)));
        safeZonesAction->setChecked(true);
        _phoenixView->safeZonesChanged();
    };
    QMenu* framesMenu = viewMenu->addMenu("Aspect Ratio Frames");
    framesMenu->setObjectName("viewAspectRatioFrames");
    connect(framesMenu, &QMenu::aboutToShow, this, [this, framesMenu, setSafeZones]() {
        framesMenu->clear();
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        const fla::safezones::Settings settings = document ? fla::safezones::of(*document) : fla::safezones::defaults();
        for (size_t i = 0; i < settings.frames.size(); ++i)
        {
            QAction* frame = framesMenu->addAction(QString::fromStdString(settings.frames[i].name()));
            frame->setCheckable(true);
            frame->setChecked(settings.frames[i].on);
            frame->setEnabled(document != nullptr);
            connect(frame, &QAction::toggled, this, [this, i, setSafeZones](bool on) {
                const fla::Document* current = _flaDocument ? _flaDocument->document : nullptr;
                if (!current)
                    return;
                fla::safezones::Settings changed = fla::safezones::of(*current);
                if (i < changed.frames.size())
                {
                    changed.frames[i].on = on;
                    setSafeZones(changed, on ? "Show Aspect Ratio Frame" : "Hide Aspect Ratio Frame");
                }
            });
        }
        framesMenu->addSeparator();
        QAction* settingsAction = framesMenu->addAction("Safe Zone Settings...");
        settingsAction->setEnabled(document != nullptr);
        connect(settingsAction, &QAction::triggered, this, [this]() { safeZoneSettings(); });
    });
    QAction* safeZoneSettingsAction = new QAction("Safe Zone Settings...", this);
    safeZoneSettingsAction->setObjectName("viewSafeZoneSettings");
    safeZoneSettingsAction->setStatusTip("Choose the safe zones and aspect-ratio frames, with presets");
    connect(safeZoneSettingsAction, &QAction::triggered, this, [this]() { safeZoneSettings(); });
    viewMenu->addAction(safeZoneSettingsAction);
    _safeZonesAction = safeZonesAction;

    // The common view controls on the edit bar, at its right.
    if (_breadcrumbRow)
        _breadcrumbRow->layout()->addWidget(new StageControls(_phoenixView, clipAction, dimOutsideDocumentAction,
            [this]() { chooseTool(_rotationTool.get()); }, safeZonesAction, _breadcrumbRow));

    // Help menu
    // Control menu: playback. They act while the stage
    // or the timeline has the focus, so Enter in a field stays the field's.
    QMenu* controlMenu = menuBar->addMenu("&Control");
    // Test Movie: the scene played with its scripts, its buttons clickable.
    QAction* testMovieAction = new QAction("&Test Movie", this);
    testMovieAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    testMovieAction->setStatusTip("Play the scene with its ActionScript running, so its buttons can be clicked");
    connect(testMovieAction, &QAction::triggered, this, &MainWindow::testMovie);
    controlMenu->addAction(testMovieAction);
    // Motion Sketch: the selected instance follows a path drawn in time.
    QAction* sketchAction = new QAction("Motion &Sketch", this);
    sketchAction->setStatusTip("Press on the stage and move: the movie plays and the selected instance follows where you point");
    connect(sketchAction, &QAction::triggered, this, &MainWindow::motionSketch);
    controlMenu->addAction(sketchAction);
    controlMenu->addSeparator();
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
    controlMenu->addSeparator();
    // Off by default: playback and export run every frame unless asked.
    QAction* frameActionsAction = addControl("Enable Simple &Frame Actions",
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), []() {});
    frameActionsAction->setCheckable(true);
    frameActionsAction->setChecked(QSettings().value("control/simpleFrameActions", false).toBool());
    frameActionsAction->setStatusTip("Have playback and exported movie clips follow the stop(), play() and goto calls "
                                     "in frame scripts");
    const auto applyFrameActions = [this, timelines](bool on) {
        if (TimelinesPanel* panel = timelines())
            panel->setFrameActions(on);
        if (_phoenixView)
            _phoenixView->setFrameActions(on);
    };
    applyFrameActions(frameActionsAction->isChecked());
    connect(frameActionsAction, &QAction::toggled, this, [applyFrameActions](bool on) {
        applyFrameActions(on);
        QSettings().setValue("control/simpleFrameActions", on);
    });

    // Commands: Claude Code, and JSFL scripts from a file or the user's
    // Commands folder.
    QAction* claudeAction = new QAction("Open &Claude Code", this);
    claudeAction->setStatusTip("Open Claude Code in a panel, connected to Phoenix so it can work on your documents");
    connect(claudeAction, &QAction::triggered, this, [this]() {
        if (!_mcpServer)
            _mcpServer = new McpServer(this);
        if (!_claudeDock)
        {
            _claudePanel = new ClaudeCodePanel(*_mcpServer);
            _claudeDock = new QDockWidget("Claude Code", this);
            _claudeDock->setObjectName("claudeCodeDock");
            _claudeDock->setAllowedAreas(Qt::AllDockWidgetAreas);
            _claudePanel->setParent(_claudeDock);
            _claudeDock->setWidget(_claudePanel);
            addDockWidget(Qt::RightDockWidgetArea, _claudeDock);
            _claudeDock->setFloating(true);
            _claudeDock->resize(760, 520);
        }
        const QString folder = _documentPath.isEmpty() ? QString() : QFileInfo(_documentPath).absolutePath();
        _claudeDock->show();
        _claudeDock->raise();
        _claudePanel->start(folder);
        _claudePanel->focusTerminal();
    });
    QAction* scriptEditorAction = new QAction("&Script Editor...", this);
    scriptEditorAction->setStatusTip("Write, open and run JSFL and Python scripts, and edit ActionScript files");
    connect(scriptEditorAction, &QAction::triggered, this, [this]() { openScriptEditor(); });
    _scripts->fillCommandsMenu(menuBar->addMenu("&Commands"), {claudeAction, scriptEditorAction});

    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* guideAction = helpMenu->addAction("Phoenix &Help");
    guideAction->setShortcut(QKeySequence(Qt::Key_F1));
    guideAction->setStatusTip("Open Phoenix's user guide in your browser");
    connect(guideAction, &QAction::triggered, this,
        []() { QDesktopServices::openUrl(QUrl("https://phoenixanimate.com/")); });
    helpMenu->addSeparator();
    QAction* startAction = new QAction("&Start Screen...", this);
    startAction->setStatusTip("New from a preset, or open a recent file");
    connect(startAction, &QAction::triggered, this, &MainWindow::showStartDialog);
    helpMenu->addAction(startAction);

    // Interactive tutorials: lessons that walk through a task step by step.
    QAction* tutorialsAction = new QAction("&Tutorials...", this);
    tutorialsAction->setStatusTip("Lessons that walk through Phoenix step by step, pointing at what to use next");
    connect(tutorialsAction, &QAction::triggered, this, &MainWindow::showTutorials);
    helpMenu->addAction(tutorialsAction);

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
        tr("Documents (*.phx *.fla *.xfl DOMDocument.xml *.swf);;Phoenix Documents (*.phx);;FLA Files (*.fla);;"
           "XFL Documents (*.xfl DOMDocument.xml);;SWF Movies (*.swf);;All Files (*)"));

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

bool MainWindow::confirmPhoenixFeatures()
{
    if (!_flaDocument || !_flaDocument->document || _featuresAcknowledged.contains(_flaDocument))
        return true;
    const std::vector<fla::features::Use> uses = fla::features::used(*_flaDocument->document);
    if (uses.empty())
        return true;

    QString list;
    for (const fla::features::Use& use : uses)
        list += QString("<li><b>%1</b>: %2</li>")
                    .arg(QString::fromUtf8(use.feature->title), QString::fromUtf8(use.feature->elsewhere).toHtmlEscaped());
    QMessageBox box(QMessageBox::Information, "Phoenix Features in an FLA",
        QString("This document uses features of Phoenix's own. Phoenix keeps them in the FLA or XFL file and reads "
                "them back, but other FLA editors leave them out, showing:<ul>%1</ul>If one of them saves the file, "
                "these features are gone from it. A Phoenix document (.phx) is the safe place for the master copy.")
            .arg(list),
        QMessageBox::Save | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    QCheckBox* dontAsk = new QCheckBox("Don't ask again for this document", &box);
    box.setCheckBox(dontAsk);
    if (box.exec() != QMessageBox::Save)
        return false;
    if (dontAsk->isChecked())
        _featuresAcknowledged.insert(_flaDocument);
    return true;
}

bool MainWindow::writeDocumentFile(const fla::Document& document, const QString& filePath, QString& error)
{
    error.clear();
    if (isPhxPath(filePath))
    {
        phx::SaveOptions options;
        options.application = QString("Phoenix %1").arg(QCoreApplication::applicationVersion()).toStdString();
        if (_phoenixView && _flaDocument && _flaDocument->document == &document)
        {
            // A small picture of the first frame, for file browsers.
            const int width = 256;
            const int height = std::max(1, width * std::max(1, document.height) / std::max(1, document.width));
            const QImage picture = _phoenixView->renderFrame(0, QSize(width, height), false);
            QByteArray png;
            QBuffer buffer(&png);
            if (buffer.open(QIODevice::WriteOnly) && picture.save(&buffer, "PNG"))
                options.thumbnailPng.assign(png.begin(), png.end());
        }
        std::string phxError;
        if (!phx::save(document, filePath.toStdString(), options, phxError))
            error = QString::fromStdString(phxError);
    }
    else if (filePath.endsWith(".fla", Qt::CaseInsensitive))
    {
        // Written beside the original and moved into place once complete,
        // so a save that fails part way leaves the original untouched.
        const QString temporary = filePath + ".saving";
        fla::FLAWriter writer;
        if (!writer.write(document, temporary.toStdString()))
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
        // which other apps open just as happily and which stays readable on disk.
        fla::XFLFolderWriter writer;
        if (!writer.write(document, filePath.toStdString()))
            error = QString::fromStdString(writer.errorString());
    }
    return error.isEmpty();
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

    if (!isPhxPath(filePath) && (!confirmLossySave() || !confirmPhoenixFeatures()))
        return false;

    // The recovery copy being written reads the media this save moves.
    if (AutoSave* autoSave = activeAutoSave())
        autoSave->waitForWrite();
    QString error;
    writeDocumentFile(*_flaDocument->document, filePath, error);

    if (!error.isEmpty())
    {
        QMessageBox::warning(this, "Save Failed",
            QString("Failed to save:\n%1\n\n%2").arg(filePath).arg(error));
        return false;
    }

    _documentPath = filePath;
    _documentName = QFileInfo(filePath).fileName();
    _editContext.markSaved();
    if (AutoSave* autoSave = activeAutoSave())
        autoSave->discard();
    _flaDocument->fromBinary = false;
    updateEditState();
    addToRecentFiles(filePath);
    statusBar()->showMessage(QString("Saved %1").arg(filePath), 5000);
    scriptEvent("documentSaved");
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
    if (output.endsWith(".swf", Qt::CaseInsensitive) && !perLayer)
    {
        // A movie with its scripts, or of vector shapes, with its soundtrack.
        QString error;
        const QString path = QFileInfo(output).absoluteFilePath();
        if (!exportSwf(path, QSize(document->width, document->height), error))
        {
            std::fprintf(stderr, "%s\n", qPrintable(error.isEmpty() ? QString("Export cancelled.") : error));
            return 1;
        }
        for (const QString& note : _swfNotes)
            std::fprintf(stderr, "%s\n", qPrintable(note));
        std::printf("Exported %d frames to %s\n", std::max(1, _swfFrames), qPrintable(path));
        return 0;
    }
    MovieExportSettings settings;
    settings.format = output.endsWith(".png", Qt::CaseInsensitive) ?MovieExportSettings::Format::PngSequence
        : output.endsWith(".gif", Qt::CaseInsensitive)               ? MovieExportSettings::Format::Gif
        : output.endsWith(".wav", Qt::CaseInsensitive)               ? MovieExportSettings::Format::Wav
        : output.endsWith(".svg", Qt::CaseInsensitive)               ? MovieExportSettings::Format::SvgSequence
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
    // [--safe-zones]
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
    // --scene NAME: that scene, before anything is counted.
    const int sceneOption = static_cast<int>(options.indexOf("--scene"));
    if (sceneOption >= 0)
    {
        const QString wanted = options.value(sceneOption + 1);
        int found = -1;
        for (int i = 0; i < static_cast<int>(document->timelines.size()); ++i)
            if (document->timelines[static_cast<size_t>(i)] &&
                QString::fromStdString(document->timelines[static_cast<size_t>(i)]->name) == wanted)
                found = i;
        if (found < 0)
        {
            std::fprintf(stderr, "No scene named %s\n", qPrintable(wanted));
            return 1;
        }
        goToScene(found);
    }
    // --data rows.csv: a movie for each row, its template fields filled
    // from the row, into the --out folder as --format files.
    const int dataOption = static_cast<int>(options.indexOf("--data"));
    if (dataOption >= 0)
    {
        QFile file(options.value(dataOption + 1));
        if (!file.open(QIODevice::ReadOnly))
        {
            std::fprintf(stderr, "Could not read %s\n", qPrintable(options.value(dataOption + 1)));
            return 1;
        }
        const std::vector<fla::templates::Values> rows = rowsOfData(file);
        if (rows.empty())
        {
            std::fprintf(stderr, "%s has no rows to render\n", qPrintable(file.fileName()));
            return 1;
        }
        const int outOption = static_cast<int>(options.indexOf("--out"));
        const int formatOption = static_cast<int>(options.indexOf("--format"));
        const QString folder = outOption >= 0 ? options.value(outOption + 1)
                                              : QFileInfo(source).absoluteDir().filePath(QFileInfo(source).completeBaseName() + "_renders");
        QString error;
        if (!renderRows(rows, QFileInfo(folder).absoluteFilePath(), formatOption >= 0 ? options.value(formatOption + 1) : QString("mp4"), error))
        {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        return 0;
    }
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
        else if (name == "--safe-zones")
            settings.safeZones = true;
        else if (name == "--no-audio")
            settings.audio = false;
        else if (name == "--per-layer")
            settings.format = MovieExportSettings::Format::LayerSequences;
        else if (name == "--passes")
            settings.format = MovieExportSettings::Format::Passes;
        else if (name == "--exr")
            settings.format = MovieExportSettings::Format::Exr;
        else if (name == "--prores")
            settings.codec = MovieExportSettings::Codec::ProRes422;
        else if (name == "--prores4444")
            settings.codec = MovieExportSettings::Codec::ProRes4444;
        else if (name == "--everything")
            settings.everything = true;
        else if (name == "--scene" && !value.isEmpty())
            ++i;
        else if (name == "--all-scenes")
            settings.scenes = MovieExportSettings::Scenes::All;
        else if (name == "--each-scene")
            settings.scenes = MovieExportSettings::Scenes::Each;
        else if (name == "--dither")
            settings.gifDither = true;
        else if (name == "--link-images")
            settings.svgLinked = true;
        else if (name == "--colors" && !value.isEmpty())
        {
            settings.gifColors = std::clamp(value.toInt(), 2, 256);
            ++i;
        }
        else if (name == "--layers" && !value.isEmpty())
        {
            settings.layers = value.split(',', Qt::SkipEmptyParts);
            ++i;
        }
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
    else if (suffix == "mp4" || suffix == "mov" || suffix == "gif")
    {
        settings.format = suffix == "gif" ? MovieExportSettings::Format::Gif : MovieExportSettings::Format::Video;
        QDir().mkpath(QFileInfo(out).absolutePath());
    }
    else if (suffix == "svg")
    {
        settings.format = MovieExportSettings::Format::SvgSequence;
        QDir().mkpath(QFileInfo(out).absolutePath());
    }
    else if (suffix == "html" || suffix == "htm")
    {
        // A page that plays the movie with Phoenix's JavaScript runtime,
        // its files beside it, as Publish's HTML5 format writes it.
        const fla::publish::Settings publish = fla::publish::read(publishXml(*document), base.toStdString());
        Html5Publish::Settings html5;
        html5.page = QFileInfo(out).absoluteFilePath();
        html5.fit = publish.html5Fit;
        html5.loop = publish.html5Loop;
        html5.source = _documentPath;
        QStringList files, notes;
        QString error;
        if (!publishHtml5(html5, files, notes, error))
        {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        for (const QString& note : notes)
            std::fprintf(stderr, "%s\n", qPrintable(note));
        std::printf("Published %s as %s (%d files)\n", qPrintable(source), qPrintable(html5.page),
            static_cast<int>(files.size()));
        return 0;
    }
    else if (suffix == "swf")
    {
        // A movie with its scripts, or of vector shapes, the scene's frames
        // all.
        QDir().mkpath(QFileInfo(out).absolutePath());
        QString error;
        if (!exportSwf(QFileInfo(out).absoluteFilePath(), settings.size, error))
        {
            std::fprintf(stderr, "%s\n", qPrintable(error.isEmpty() ? QString("Render stopped.") : error));
            return 1;
        }
        for (const QString& note : _swfNotes)
            std::fprintf(stderr, "%s\n", qPrintable(note));
        std::printf("Rendered %d frames of %s to %s\n", std::max(1, _phoenixView->frameCount()), qPrintable(source),
            qPrintable(QFileInfo(out).absoluteFilePath()));
        return 0;
    }
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
    for (const fla::Timeline* timeline : document->timelines)
        settings.sceneNames << (timeline ? QString::fromStdString(timeline->name) : QString());
    settings.sceneCount = std::max(1, static_cast<int>(settings.sceneNames.size()));
    settings.currentScene = document->currentSceneIndex();
    settings.showScene = [this](int index) {
        goToScene(index);
        return std::max(1, _phoenixView->frameCount());
    };
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

bool MainWindow::openArtwork(const QString& path, QStringList& problems)
{
    // A stage of a pixel, so the bounds are the art's (and its origin's).
    createNewDocument(1, 1, 24.0, Qt::white);
    importMedia({path}, true, nullptr, &problems);
    fla::Document* data = _flaDocument ? _flaDocument->document : nullptr;
    fla::Timeline* scene = data ? data->currentScene() : nullptr;
    bool any = false;
    for (fla::Layer* layer : scene ? scene->layers : std::vector<fla::Layer*>())
        for (fla::Frame* frame : layer->frames)
            any = any || !frame->elements.empty();
    const QRectF art = _phoenixView && any ? _phoenixView->framesBounds(0, 0) : QRectF();
    if (!scene || !art.isValid() || art.isEmpty())
        return false;
    data->width = std::max(1, static_cast<int>(std::ceil(art.width())));
    data->height = std::max(1, static_cast<int>(std::ceil(art.height())));
    for (fla::Layer* layer : scene->layers)
        for (fla::Frame* frame : layer->frames)
            for (fla::Element* element : frame->elements)
            {
                element->transform.tx -= art.left();
                element->transform.ty -= art.top();
            }
    _phoenixView->invalidateBounds();
    _phoenixView->clearCaches();
    return true;
}

int MainWindow::viewFromCommandLine(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        QMessageBox::warning(nullptr, "Phoenix Viewer", QString("There is no file at %1.").arg(QDir::toNativeSeparators(path)));
        return 1;
    }
    const QString suffix = info.suffix().toLower();
    const bool document = suffix == "phx" || suffix == "fla" || suffix == "xfl" || suffix == "swf" ||
        info.fileName() == "DOMDocument.xml" || phx::isPhx(path.toStdString());
    if (document)
        loadFLAFile(info.absoluteFilePath());
    else if (MediaImport::isArtwork(path) || MediaImport::isImage(path))
    {
        QStringList problems;
        if (!openArtwork(info.absoluteFilePath(), problems))
        {
            QMessageBox::warning(nullptr, "Phoenix Viewer",
                problems.isEmpty() ? QString("%1 has nothing Phoenix can show.").arg(info.fileName())
                                   : problems.join("\n"));
            return 1;
        }
    }
    else
    {
        QMessageBox::warning(nullptr, "Phoenix Viewer",
            QString("%1 is not a file Phoenix shows: documents (PHX, FLA, XFL, SWF), artwork (SVG, AI, EPS, PDF, PSD) "
                    "and images open in the viewer.")
                .arg(info.fileName()));
        return 1;
    }
    if (!_flaDocument || !_flaDocument->document)
        return 1;
    // Played as Test Movie plays a document, its scripts too; what they
    // print goes to standard error, not to the person looking.
    const auto print = [](const QString& line) { std::fprintf(stderr, "%s\n", qPrintable(line)); };
    TestMovieWindow viewer(*_flaDocument, info.fileName(), print,
        [print](const QString& line, const fla::DOMElement*, int) { print(line); }, nullptr, info.absoluteFilePath());
    viewer.setWindowTitle(QString("%1 - Phoenix Viewer").arg(info.fileName()));
    viewer.setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    viewer.exec();
    return 0;
}

int MainWindow::exportImageFromCommandLine(const QString& source, const QString& output, const QStringList& options)
{
    // Artwork (SVG, Illustrator, EPS, PDF...) on a stage fitted to it.
    if (MediaImport::isArtwork(source))
    {
        QStringList problems;
        const bool opened = openArtwork(QFileInfo(source).absoluteFilePath(), problems);
        for (const QString& problem : problems)
            std::fprintf(stderr, "%s\n", qPrintable(problem));
        if (!opened)
        {
            std::fprintf(stderr, "Nothing could be read from %s\n", qPrintable(source));
            return 1;
        }
    }
    else
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
    // Options and their values, in pairs; flags have none.
    QStringList pairs = options;
    pairs.removeAll("--dither");
    for (int i = 0; i + 1 < pairs.size(); i += 2)
    {
        const QString& name = pairs.at(i);
        const QString& value = pairs.at(i + 1);
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
        else if (name == "--colors")
        {
            // A palette: an 8-bit PNG, or a GIF's color count.
            settings.colors = std::clamp(value.toInt(), 2, 256);
            settings.png8 = true;
        }
    }
    // A flag, with no value after it.
    settings.dither = options.contains("--dither");
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
    QStringList scenes;
    for (const fla::Timeline* timeline : document->timelines)
        scenes << (timeline ? QString::fromStdString(timeline->name) : QString());
    dialog.setScenes(scenes, document->currentSceneIndex());
    QStringList layers;
    if (const fla::Timeline* scene = document->currentScene())
        for (const fla::Layer* layer : scene->layers)
            if (layer && (layer->layerType == fla::Layer::Type::Normal || layer->layerType == fla::Layer::Type::Masked))
                layers << QString::fromStdString(layer->name);
    dialog.setLayers(layers);
    if (dialog.exec() != QDialog::Accepted)
        return;
    MovieExportSettings settings = dialog.settings();
    settings.showScene = [this](int index) {
        goToScene(index);
        return std::max(1, _phoenixView->frameCount());
    };

    if (settings.format == MovieExportSettings::Format::Video &&
        findFfmpeg("Export Movie").isEmpty())
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

void MainWindow::openBrushProperties()
{
    // The brush in hand decides whose settings and presets show.
    const Tool* tool = _phoenixView ? _phoenixView->activeTool() : nullptr;
    std::string list = fla::brushpresets::kFlow;
    if (tool && tool == _classicBrushTool.get())
        list = fla::brushpresets::kNib;
    else if (tool && tool == _inkBrushTool.get())
        list = fla::brushpresets::kInk;
    if (_brushProperties)
        _brushProperties->close();
    BrushPropertiesDialog::Library library;
    library.bitmapNames = [this]() {
        QStringList names;
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (document)
            for (const fla::Resource* resource : document->resources)
                if (resource && resource->resourceType() == fla::Resource::Type::Bitmap)
                    names << QString::fromStdString(static_cast<const fla::Bitmap*>(resource)->name);
        return names;
    };
    library.bitmapImage = [this](const QString& name) {
        return _phoenixView ? _phoenixView->libraryBitmap(name).toImage() : QImage();
    };
    _brushProperties = new BrushPropertiesDialog(_drawingStyle, list, library, this);
    _brushProperties->setAttribute(Qt::WA_DeleteOnClose);
    connect(_brushProperties, &BrushPropertiesDialog::changed, this, [this]() {
        if (_propertiesPanel)
            _propertiesPanel->refreshToolOptions();
        if (_phoenixView)
            _phoenixView->update();
    });
    _brushProperties->show();
}

namespace {

const char* const kPublishFile = "PublishSettings.xml";

/// The document's publish settings file, as text; empty when it has none.
std::string publishXml(const fla::Document& document)
{
    for (const auto& [path, bytes] : document.extraFiles)
        if (path == kPublishFile)
        {
            const std::vector<uint8_t>& data = bytes.get();
            return std::string(data.begin(), data.end());
        }
    return std::string();
}

} // namespace

QString MainWindow::publishDocumentFolder() const
{
    if (!_documentPath.isEmpty())
        return QFileInfo(_documentPath).absolutePath();
    // A document never saved publishes where the last one chose, at first
    // the Documents folder.
    const QString last = QSettings().value("publish/parentFolder").toString();
    return !last.isEmpty() ? last : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

bool MainWindow::editPublishSettings(bool fromPublish)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return false;
    const QString name = _documentPath.isEmpty() ? QString("Untitled") : QFileInfo(_documentPath).completeBaseName();
    const std::string xml = publishXml(*document);
    PublishSettingsDialog dialog(xml, name, document->width, document->height, publishDocumentFolder(),
        !_documentPath.isEmpty(), this);
    if (fromPublish)
        dialog.preferPublish();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    // The parent folder is offered again for the next document never saved.
    QSettings().setValue("publish/parentFolder", dialog.parentFolder());
    const std::string written = dialog.xml();
    if (written != xml)
    {
        // One undo step: the package file replaced, or added.
        using Files = std::vector<std::pair<std::string, fla::LazyBytes>>;
        Files after = document->extraFiles;
        bool found = false;
        for (auto& [path, bytes] : after)
            if (path == kPublishFile)
            {
                bytes = fla::LazyBytes(std::vector<uint8_t>(written.begin(), written.end()));
                found = true;
            }
        if (!found)
            after.emplace_back(kPublishFile, fla::LazyBytes(std::vector<uint8_t>(written.begin(), written.end())));
        _editContext.commandStack().push(fla::CommandPtr(
            new fla::SetPropertyCommand<Files>(document->extraFiles, document->extraFiles, after, "Publish Settings")));
        _editContext.commandStack().breakMergeChain();
    }
    return dialog.publishRequested();
}

void MainWindow::publishSettings()
{
    if (editPublishSettings(false))
        publish();
}

void MainWindow::publish(const QString& profile)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
        return;
    // A document never saved has no folder of its own: the first time, the
    // settings come up to choose where its files go.
    if (_documentPath.isEmpty() && _publishAskedFor != document)
    {
        _publishAskedFor = document;
        if (!editPublishSettings(true))
            return;
    }
    QStringList written;
    QStringList problems;
    publishFiles(written, problems, profile);
    const QString folder = QDir::toNativeSeparators(_publishedFolder);

    // What was written, in the Output panel.
    if (OutputPanel* output = _scripts ? _scripts->output() : nullptr)
    {
        QString name = _documentPath.isEmpty() ? QString("Untitled") : QFileInfo(_documentPath).fileName();
        const std::vector<std::string> configurations = fla::publish::profiles(publishXml(*document));
        if (configurations.size() > 1)
            name += QString(" (%1)").arg(profile.isEmpty()
                    ? QString::fromStdString(fla::publish::currentProfile(publishXml(*document)))
                    : profile);
        output->append(written.isEmpty() ? QString("Publish %1: nothing was published.").arg(name)
                                         : QString("Published %1 to %2:").arg(name, folder));
        for (const QString& file : written)
            output->append("    " + file);
        for (const QString& problem : problems)
            output->appendError(problem);
    }
    statusBar()->showMessage(written.isEmpty() ? QString("Nothing was published.")
                                               : QString("Published %1").arg(written.join(", ")),
        6000);

    // And a dialog that opens the page, or shows the files, unless asked
    // not to.
    QSettings options;
    if (!options.value("publish/showComplete", true).toBool() && problems.isEmpty())
        return;
    // The page to open: the HTML5 page, which plays without the SWF, or
    // else the SWF's page.
    const QDir out(_publishedFolder);
    const std::string base =
        (_documentPath.isEmpty() ? QString("Untitled") : QFileInfo(_documentPath).completeBaseName()).toStdString();
    const fla::publish::Settings settings = fla::publish::read(publishXml(*document), base, profile.toStdString());
    QString page;
    const auto pageOf = [&](bool on, const char* format) {
        const QString file = QString::fromStdString(fla::publish::fileName(settings, format, base));
        if (on && written.contains(file))
            page = out.filePath(file);
    };
    pageOf(settings.html, "html");
    pageOf(settings.html5, "html5");
    QMessageBox box(this);
    box.setWindowTitle("Publish");
    box.setIcon(problems.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    if (written.isEmpty())
        box.setText("Nothing was published.");
    else
        box.setText(QString("Published %1 %2 to<br><b>%3</b>")
                        .arg(written.size())
                        .arg(written.size() == 1 ? "file" : "files")
                        .arg(folder.toHtmlEscaped()));
    box.setInformativeText(problems.isEmpty() ? QString() : problems.join("\n"));
    box.setDetailedText(written.join("\n"));
    QPushButton* openPage = page.isEmpty() ? nullptr : box.addButton("Open in Browser", QMessageBox::ActionRole);
    if (openPage)
        openPage->setToolTip(QDir::toNativeSeparators(page));
#if defined(Q_OS_MACOS)
    const char* showLabel = "Show in Finder";
#elif defined(Q_OS_WIN)
    const char* showLabel = "Show in Explorer";
#else
    const char* showLabel = "Show Files";
#endif
    QPushButton* showFiles = written.isEmpty() ? nullptr : box.addButton(showLabel, QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    QCheckBox* again = new QCheckBox("Show this after publishing", &box);
    again->setChecked(options.value("publish/showComplete", true).toBool());
    box.setCheckBox(again);
    box.exec();
    options.setValue("publish/showComplete", again->isChecked());
    if (openPage && box.clickedButton() == openPage)
        QDesktopServices::openUrl(QUrl::fromLocalFile(page));
    else if (showFiles && box.clickedButton() == showFiles)
        revealInFileManager(page.isEmpty() ? out.filePath(written.front()) : page);
}

bool MainWindow::publishFiles(QStringList& written, QStringList& problems, const QString& profile)
{
    scriptEvent("prePublish");
    struct Published
    {
        MainWindow& window;
        ~Published() { window.scriptEvent("postPublish"); }
    } published{*this};
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
    {
        problems << "There is no document to publish.";
        return false;
    }
    const QString name = _documentPath.isEmpty() ? QString("Untitled") : QFileInfo(_documentPath).completeBaseName();
    const fla::publish::Settings settings =
        fla::publish::read(publishXml(*document), name.toStdString(), profile.toStdString());
    // The files go in the publish folder, made in the parent folder if need
    // be, or in the parent folder itself.
    _publishedFolder = publishFolderPath(settings, publishDocumentFolder());
    const QDir folder(_publishedFolder);
    if (!QDir().mkpath(_publishedFolder))
    {
        problems << QString("Could not make the folder %1.").arg(QDir::toNativeSeparators(_publishedFolder));
        return false;
    }
    const QSize size = settings.matchMovie || settings.width <= 0 || settings.height <= 0
        ? QSize(std::max(1, document->width), std::max(1, document->height))
        : QSize(settings.width, settings.height);
    // Still images show the frame labeled #Static, or the
    // first.
    int still = 0;
    if (const fla::Timeline* scene = document->currentScene())
        for (const fla::Layer* layer : scene->layers)
            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                if (frame && frame->name == "#Static")
                    still = frame->index;
    const auto pathOf = [&](const char* format) {
        return folder.filePath(QString::fromStdString(fla::publish::fileName(settings, format, name.toStdString())));
    };
    const auto image = [&](ImageExportSettings::Format format, const char* key) {
        ImageExportSettings out;
        out.format = format;
        out.path = pathOf(key);
        out.size = size;
        out.frame = still;
        out.quality = settings.jpegQuality;
        if (format == ImageExportSettings::Format::Png)
        {
            out.matte = settings.pngBitDepth == "24-bit" ? ImageExportSettings::Matte::Stage
                                                         : ImageExportSettings::Matte::Transparent;
            out.png8 = settings.pngBitDepth == "8-bit";
            out.colors = settings.pngColors;
        }
        if (format == ImageExportSettings::Format::Gif)
            out.colors = settings.gifColors;
        QString error;
        if (ImageExport::run(out, *_phoenixView, error))
            written << QFileInfo(out.path).fileName();
        else
            problems << QString("%1: %2").arg(QFileInfo(out.path).fileName(), error);
    };
    if (settings.png)
        image(ImageExportSettings::Format::Png, "png");
    if (settings.jpeg)
        image(ImageExportSettings::Format::Jpeg, "jpeg");
    if (settings.gif && !settings.gifAnimated)
        image(ImageExportSettings::Format::Gif, "gif");
    if (settings.gif && settings.gifAnimated)
    {
        MovieExportSettings movie;
        movie.format = MovieExportSettings::Format::Gif;
        movie.path = pathOf("gif");
        movie.size = size;
        movie.first = 0;
        movie.last = std::max(1, _phoenixView->frameCount()) - 1;
        movie.loop = settings.gifLoop || settings.gifLoopCount > 0;
        movie.gifRepeat = settings.gifLoop ? 0 : settings.gifLoopCount;
        movie.gifColors = settings.gifColors;
        QString error;
        if (MovieExport::run(movie, *_phoenixView, _timelineAudio, document->frameRate, this, error))
            written << QFileInfo(movie.path).fileName();
        else if (!error.isEmpty())
            problems << QString("%1: %2").arg(QFileInfo(movie.path).fileName(), error);
    }
    if (settings.svg)
    {
        const QString path = pathOf("svg");
        QStringList linked;
        if (_phoenixView->exportFrameSvg(path, still, false, settings.svgHiddenLayers, !settings.svgEmbed, QRectF(), &linked))
            written << QFileInfo(path).fileName();
        else
            problems << QString("%1 could not be written.").arg(QFileInfo(path).fileName());
    }
    if (settings.flash)
    {
        const QString path = pathOf("flash");
        QString error;
        if (exportSwf(path, size, error))
            written << QFileInfo(path).fileName();
        else
            problems << QString("%1: %2").arg(QFileInfo(path).fileName(), error);
    }
    if (settings.html)
    {
        // A page showing what was published: the movie, or else an image.
        const QString path = pathOf("html");
        QString body;
        QString scripts;
        const QString title = name.toHtmlEscaped();
        if (settings.flash && written.contains(QFileInfo(pathOf("flash")).fileName()))
        {
            // Browsers no longer play SWF themselves: Ruffle, an SWF player
            // in JavaScript and WebAssembly (MIT or Apache-2.0), loaded from
            // a CDN at a fixed release, finds the object and plays it.
            scripts = "<script src=\"https://unpkg.com/@ruffle-rs/ruffle@0.7.1/ruffle.js\"></script>\n";
            body = QString("<object data=\"%1\" type=\"application/x-shockwave-flash\" width=\"%2\" height=\"%3\">"
                           "<p>This page plays an SWF movie with Ruffle, which needs JavaScript and an internet "
                           "connection.</p></object>")
                       .arg(QFileInfo(pathOf("flash")).fileName().toHtmlEscaped())
                       .arg(size.width())
                       .arg(size.height());
        }
        for (const char* key : {"gif", "svg", "png", "jpeg"})
        {
            if (!body.isEmpty())
                break;
            const QString file = QFileInfo(pathOf(key)).fileName();
            if (written.contains(file))
                body = QString("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"%4\">")
                           .arg(file.toHtmlEscaped())
                           .arg(size.width())
                           .arg(size.height())
                           .arg(title);
        }
        if (body.isEmpty())
            problems << "The HTML page has nothing to show: publish a movie or an image with it.";
        else
        {
            QFile page(path);
            const QColor stage(document->backgroundColor[0], document->backgroundColor[1], document->backgroundColor[2]);
            const QString html = QString("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                                         "<title>%1</title>\n<style>body { margin: 0; background: %2; }</style>\n"
                                         "%3</head>\n<body>\n%4\n</body>\n</html>\n")
                                     .arg(title, stage.name(), scripts, body);
            if (page.open(QIODevice::WriteOnly | QIODevice::Text) && page.write(html.toUtf8()) > 0)
                written << QFileInfo(path).fileName();
            else
                problems << QString("%1 could not be written.").arg(QFileInfo(path).fileName());
        }
    }
    if (settings.html5)
    {
        // A page that plays the movie with Phoenix's JavaScript runtime.
        Html5Publish::Settings html5;
        html5.page = pathOf("html5");
        html5.fit = settings.html5Fit;
        html5.loop = settings.html5Loop;
        html5.source = _documentPath;
        QStringList files, notes;
        QString error;
        if (publishHtml5(html5, files, notes, error))
            written << QFileInfo(html5.page).fileName();
        else
            problems << QString("%1: %2").arg(QFileInfo(html5.page).fileName(), error);
        for (const QString& note : notes)
            statusBar()->showMessage(note, 6000);
    }
    _phoenixView->update();
    return problems.isEmpty();
}

bool MainWindow::exportSwf(const QString& path, const QSize& size, QString& error)
{
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !_phoenixView)
    {
        error = "No document is open.";
        return false;
    }
    _swfNotes.clear();
    // The movie with its structure (movie clips, buttons, names, scripts)
    // has every scene in a row; unless Phoenix draws something there a
    // player cannot: then the scene shown, as frames of shapes.
    const std::string flat = fla::swfmovie::unstructured(*document);
    const bool structured = flat.empty();
    std::vector<int> sceneLengths;
    if (structured)
        for (const fla::Timeline* scene : document->timelines)
        {
            int length = 1;
            for (const fla::Layer* layer : scene ? scene->layers : std::vector<fla::Layer*>())
                if (layer)
                    length = std::max(length, fla::timeline::layerEnd(*layer));
            if (scene)
                sceneLengths.push_back(length);
        }
    const int frames = structured && !sceneLengths.empty()
        ? std::accumulate(sceneLengths.begin(), sceneLengths.end(), 0)
        : std::max(1, _phoenixView->frameCount());
    swf::Writer writer(size.width(), size.height(), document->frameRate,
        swf::Color{document->backgroundColor[0], document->backgroundColor[1], document->backgroundColor[2], 255});
    // Shapes and bitmaps written once and placed in every frame they show in.
    SwfArt art(writer);
    // The soundtrack, mixed as playback mixes it, streamed a block a frame
    // as Publish Settings say.
    const fla::publish::Settings publish = fla::publish::read(publishXml(*document), std::string());
    swf::SoundStream sound;
    bool streaming = false;
    if (_timelineAudio && publish.soundCompression != "none")
    {
        // Event, start and stop sounds go in as events (below), streams mixed:
        // every scene's in a row for the movie with its structure, as it
        // plays them.
        std::vector<float> mixed;
        if (structured && sceneLengths.size() > 1)
        {
            fla::Document* scenes = _flaDocument->document;
            const int shown = scenes->currentSceneIndex();
            for (size_t i = 0; i < sceneLengths.size(); ++i)
            {
                scenes->setCurrentScene(static_cast<int>(i));
                const std::vector<float> part = _timelineAudio->mixdown(0, sceneLengths[i] - 1, false);
                mixed.insert(mixed.end(), part.begin(), part.end());
            }
            scenes->setCurrentScene(shown);
        }
        else
            mixed = _timelineAudio->mixdown(0, frames - 1, false);
        const bool audible = std::any_of(mixed.begin(), mixed.end(), [](float sample) { return sample != 0.0f; });
        if (audible)
        {
            const int channels = publish.soundStereo ? 2 : 1;
            int rate = publish.soundRate >= 44100 ? 44100
                : publish.soundRate >= 22050      ? 22050
                : publish.soundRate >= 11025      ? 11025
                                                  : 5512;
            if (publish.soundCompression == "mp3")
            {
                // MP3 has no 5.5 kHz; without an MP3 encoder, ADPCM.
                rate = std::max(rate, 11025);
                std::vector<uint8_t> mp3;
                QString mp3Error;
                if (MovieExport::canEncodeMp3() &&
                    MovieExport::encodeMp3(mixed, rate, channels, std::clamp(publish.soundBitRate, 8, 320), mp3, mp3Error))
                {
                    // LAME starts with 1105 samples of its own.
                    sound = swf::mp3Stream(mp3, channels, rate, document->frameRate, frames, 1105);
                    streaming = true;
                }
                else
                    statusBar()->showMessage("MP3 is not available here: the SWF's sound is ADPCM.", 6000);
            }
            if (!streaming)
            {
                const std::vector<int16_t> pcm = swf::pcm16(mixed, PcmBuffer::kRate, rate, channels);
                sound = publish.soundCompression == "raw"
                    ? swf::rawStream(pcm, channels, rate, document->frameRate, frames)
                    : swf::adpcmStream(pcm, channels, rate, publish.soundAdpcmBits, document->frameRate, frames);
                streaming = true;
            }
            writer.soundStream(sound.codec, sound.rate, sound.stereo, sound.samplesPerFrame, sound.latency);
        }
    }
    // Event, start and stop sounds: each sound defined once (DefineSound),
    // as its library item or the Publish Settings say, and started on its
    // frame (StartSound): on the timeline it is on in the movie with its
    // structure, so a clip stopped starts none; else the scene's and its
    // movie clips' on the scene's frames.
    fla::SwfEventSounds events(*document, publish,
        [](const std::vector<float>& samples, int rate, int channels, int kbps, std::vector<uint8_t>& mp3) {
            QString mp3Error;
            return MovieExport::canEncodeMp3() && MovieExport::encodeMp3(samples, rate, channels, kbps, mp3, mp3Error);
        });
    if (!structured)
        events.define(writer, frames);
    QProgressDialog progress("Exporting SWF...", "Cancel", 0, frames, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    // This frame's part of the soundtrack and, in the movie of frames, its
    // event sounds, for frame \a frame of the movie.
    const auto sounds = [&](int frame) {
        if (streaming && frame >= 0 && static_cast<size_t>(frame) < sound.blocks.size())
            writer.soundBlock(sound.blocks[static_cast<size_t>(frame)]);
        if (!structured && frame >= 0 && frame < frames)
            events.frame(writer, frame);
    };

    if (structured)
    {
        SwfViewDrawer drawer(*_phoenixView, art);
        fla::swfmovie::Options options;
        // The classes as Test Movie finds them: ActionScript 2's on the
        // class path, ActionScript 3's there and then among Phoenix's own.
        const fla::play::ClassPath classPath = movieClassPath(*document, _documentPath);
        fla::play::ClassPath ownOnly = classPath;
        ownOnly.builtIn = nullptr;
        options.findClass = [ownOnly](const std::string& name) -> std::optional<std::string> {
            const std::optional<fla::play::ClassFile> found = ownOnly.find(name);
            return found ? std::optional<std::string>(found->source) : std::nullopt;
        };
        fla::swfmovie::ActionScript3Options as3;
        as3.findClass = [classPath](const std::string& name) -> std::optional<std::string> {
            const std::optional<fla::play::ClassFile> found = classPath.find(name);
            return found ? std::optional<std::string>(found->source) : std::nullopt;
        };
        const QString folder = _documentPath.isEmpty() ? QString() : QFileInfo(_documentPath).absolutePath();
        as3.readFile = [folder](const std::string& path) -> std::optional<std::string> {
            QFile file(QDir(folder).filePath(QString::fromStdString(path)));
            if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
                return std::nullopt;
            return file.readAll().toStdString();
        };
        as3.documentName = _documentPath.isEmpty() ? std::string("Untitled")
                                                   : QFileInfo(_documentPath).completeBaseName().toStdString();
        options.actionScript3 = [&](swf::Writer& out, const std::map<const fla::Symbol*, int>& sprites) {
            for (const std::string& note : fla::swfmovie::writeActionScript3(*document, out, sprites, as3))
                _swfNotes << QString::fromStdString(note);
        };
        options.scaleX = document->width > 0 ? size.width() / document->width : 1.0;
        options.scaleY = document->height > 0 ? size.height() / document->height : 1.0;
        options.eventSound = [&](const fla::Frame& key, int& id, swf::SoundInfo& info) {
            return events.keySound(writer, key, id, info);
        };
        fla::swfmovie::Movie movie(*document, writer, drawer, options);
        bool cancelled = false;
        movie.write([&](int frame) {
            progress.setValue(std::clamp(frame, 0, frames));
            if (progress.wasCanceled())
            {
                cancelled = true;
                return false;
            }
            sounds(frame);
            return true;
        });
        if (cancelled)
            return false;
        for (const std::string& problem : movie.problems())
            _swfNotes << QString::fromStdString(problem);
    }
    else
    {
        if (fla::swfmovie::hasScripts(*document))
            _swfNotes << QString("The movie's scripts are left out: it is drawn as frames of shapes for %1.")
                             .arg(QString::fromStdString(flat));
        int placed = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            progress.setValue(frame);
            if (progress.wasCanceled())
                return false;
            // What last frame showed goes; this frame's things go on in order.
            for (int depth = 1; depth <= placed; ++depth)
                writer.remove(depth);
            placed = 0;
            for (const RecordedItem& item : _phoenixView->recordFrame(frame, size))
            {
                double alpha = 1.0;
                swf::Writer::Blend blend = swf::Writer::Blend::Normal;
                const int id = art.shapeOf(item, alpha, blend);
                if (id == 0)
                    continue;
                writer.place(id, ++placed, alpha, blend);
            }
            sounds(frame);
            writer.showFrame();
        }
    }
    progress.setValue(frames);
    if (events.mp3Missing())
        statusBar()->showMessage("MP3 is not available here: the SWF's event sounds are ADPCM.", 6000);
    if (!_swfNotes.isEmpty())
        statusBar()->showMessage(_swfNotes.size() == 1 ? _swfNotes.front()
                                                       : QString("%1 (and %2 more)").arg(_swfNotes.front()).arg(_swfNotes.size() - 1),
            10000);
    _swfFrames = writer.frames();
    const std::vector<uint8_t> bytes = writer.finish(true);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
    {
        error = QString("Could not write %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

void MainWindow::exportSpriteSheet(const QString& symbolName)
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        QMessageBox::information(this, "No Document", "No FLA document is currently loaded.");
        return;
    }
    const fla::Document* document = _flaDocument->document;
    const fla::Symbol* symbol = nullptr;
    if (!symbolName.isEmpty() && document->symbolList)
    {
        const auto found = document->symbolList->symbolMap.find(symbolName.toStdString());
        symbol = found != document->symbolList->symbolMap.end() ? found->second : nullptr;
    }
    if (!symbolName.isEmpty() && (!symbol || symbol->timelines.empty()))
        return;
    QString source;
    int count = 0;
    SpriteSheetDialog::Renderer render;
    PhoenixView* view = _phoenixView;
    if (symbol)
    {
        source = symbolName.section('/', -1);
        for (const fla::Layer* layer : symbol->timelines.front()->layers)
            if (layer)
                count = std::max(count, fla::timeline::layerEnd(*layer));
        // One area for every frame, so they line up.
        const QRectF area = view->symbolFramesBounds(symbol, 0, std::max(0, count - 1)).adjusted(-1, -1, 1, 1);
        const QColor stage(document->backgroundColor[0], document->backgroundColor[1], document->backgroundColor[2]);
        render = [view, symbol, area, stage](int frame, double scale, bool background) {
            QImage image = view->renderSymbolFrame(symbol, frame, area, scale);
            if (background && !image.isNull())
            {
                QImage under(image.size(), QImage::Format_ARGB32_Premultiplied);
                under.fill(stage);
                QPainter painter(&under);
                painter.drawImage(0, 0, image);
                painter.end();
                image = under;
            }
            return image;
        };
    }
    else
    {
        const fla::Timeline* scene = document->currentScene();
        source = scene ? QString::fromStdString(scene->name) : QString("Scene");
        count = std::max(1, view->frameCount());
        const QSize stage(std::max(1, static_cast<int>(std::lround(document->width))),
            std::max(1, static_cast<int>(std::lround(document->height))));
        render = [view, stage](int frame, double scale, bool background) {
            const QSize size(std::max(1, static_cast<int>(std::lround(stage.width() * scale))),
                std::max(1, static_cast<int>(std::lround(stage.height() * scale))));
            return view->renderFrame(frame, size, !background);
        };
    }
    // A scene's sheet is named after the document, and the scene when there
    // are more.
    QString base = source;
    if (!symbol && !_documentPath.isEmpty())
        base = QFileInfo(_documentPath).completeBaseName() + (document->timelines.size() > 1 ? "_" + source : QString());
    base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    const QString folder = _lastDirectory.isEmpty() ? QFileInfo(_documentPath).absolutePath() : _lastDirectory;
    SpriteSheetDialog dialog(source, count, document->frameRate, render, QDir(folder).filePath(base + ".png"), this);
    _phoenixView->update();
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString error;
    if (!dialog.write(error))
    {
        QMessageBox::warning(this, "Export Failed", error);
        return;
    }
    statusBar()->showMessage(QString("Exported the sprite sheet of %1").arg(source), 5000);
}

void MainWindow::quit()
{
    // Every window goes, after one round of asking about every unsaved
    // document in any of them.
    quitAll();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Phoenix",
        QString("<b>Phoenix</b> %1<br><br>Vector Animation Production Software.<br><br>"
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

    // DOMDocument.xml as saving would write it now. The text read from the
    // file is not kept: on a production document it is many megabytes.
    QString docText;
    std::vector<fla::XFLEntry> entries;
    std::vector<std::string> unsupported;
    std::string problem;
    if (fla::XFLContent::build(*_flaDocument->document, std::string(), entries, unsupported, problem))
        for (const fla::XFLEntry& entry : entries)
            if (entry.path == "DOMDocument.xml")
                docText = QString::fromStdString(entry.text);

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
        connect(_findReplace, &FindReplaceDialog::goToRequested, this, &MainWindow::goToMatch);
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

/// The workspaces Phoenix comes with.
const QStringList kBuiltInWorkspaces = {"Essentials", "Animator", "Designer", "Classic"};

QString workspaceKey(const QString& name)
{
    return "workspaces/" + QString::fromUtf8(QUrl::toPercentEncoding(name));
}

} // namespace

void MainWindow::setPanelsCollapsed(Qt::DockWidgetArea area, bool collapsed)
{
    CollapsedSide& side = _collapsed[area];
    if (collapsed == (side.strip != nullptr))
        return;
    if (collapsed)
    {
        // The panels open on that side, docked.
        for (QDockWidget* dock : findChildren<QDockWidget*>())
        {
            if (dock->parentWidget() == this && dockWidgetArea(dock) == area && !dock->isFloating() &&
                dock->toggleViewAction()->isChecked())
                side.docks.push_back(dock);
        }
        if (side.docks.empty())
        {
            if (QAction* action = _collapseActions[area])
            {
                const QSignalBlocker block(action);
                action->setChecked(false);
            }
            statusBar()->showMessage("No panels are docked on that side.", 4000);
            return;
        }
        side.strip = new QToolBar("Collapsed Panels", this);
        side.strip->setObjectName(area == Qt::RightDockWidgetArea ? "collapsedRight" : "collapsedLeft");
        side.strip->setMovable(false);
        side.strip->setFloatable(false);
        side.strip->setOrientation(Qt::Vertical);
        side.strip->setToolButtonStyle(Qt::ToolButtonTextOnly);
        addToolBar(area == Qt::RightDockWidgetArea ? Qt::RightToolBarArea : Qt::LeftToolBarArea, side.strip);
        QToolBar* strip = side.strip;
        for (QDockWidget* dock : side.docks)
        {
            dock->hide();
            QAction* open = strip->addAction(dock->windowTitle());
            open->setToolTip(QString("Open %1").arg(dock->windowTitle()));
            connect(open, &QAction::triggered, this, [this, area, dock, strip, open]() {
                // Open beside the strip, by its button; again, it closes.
                const bool shown = dock->isVisible();
                for (QDockWidget* other : _collapsed[area].docks)
                    if (other->isFloating())
                        other->hide();
                if (shown)
                    return;
                dock->setFloating(true);
                QWidget* button = strip->widgetForAction(open);
                const QPoint at = button ? button->mapToGlobal(QPoint(0, 0)) : strip->mapToGlobal(QPoint(0, 0));
                const int width = std::max(dock->width(), 260);
                const int height = std::max(dock->height(), 320);
                dock->resize(width, height);
                dock->move(area == Qt::RightDockWidgetArea ? at.x() - width - 4 : at.x() + strip->width() + 4, at.y());
                dock->show();
                dock->raise();
                dock->activateWindow();
            });
        }
        strip->addSeparator();
        QAction* expand = strip->addAction("Expand");
        expand->setToolTip("Put the panels back as they were docked");
        connect(expand, &QAction::triggered, this, [this, area]() { setPanelsCollapsed(area, false); });
    }
    else
    {
        for (QDockWidget* dock : side.docks)
        {
            if (dock->isFloating())
                dock->setFloating(false);
            dock->show();
        }
        removeToolBar(side.strip);
        side.strip->deleteLater();
        side = CollapsedSide();
    }
    if (QAction* action = _collapseActions[area])
    {
        const QSignalBlocker block(action);
        action->setChecked(collapsed);
    }
}

void MainWindow::expandCollapsedPanels()
{
    for (const Qt::DockWidgetArea area : {Qt::RightDockWidgetArea, Qt::LeftDockWidgetArea})
        setPanelsCollapsed(area, false);
}

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
        // The classic arrangement: the timeline over the stage, the
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
    // Panels folded to icons are saved where they are docked.
    expandCollapsedPanels();
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
    settleGestures();
    // Not while an edit is still being put together (an AI assistant's or a
    // script's): its steps point at what the history holds now.
    if (_editContext.commandStack().isInMacro())
    {
        statusBar()->showMessage("An edit is still in progress; Undo is available once it is done.", 4000);
        return;
    }
    _editContext.commandStack().undo();
    pruneToDocument();
}

void MainWindow::redo()
{
    settleGestures();
    // Not while an edit is still being put together (an AI assistant's or a
    // script's): its steps point at what the history holds now.
    if (_editContext.commandStack().isInMacro())
    {
        statusBar()->showMessage("An edit is still in progress; Redo is available once it is done.", 4000);
        return;
    }
    _editContext.commandStack().redo();
    pruneToDocument();
}

void MainWindow::settleGestures()
{
    // Whatever a tool or a panel is in the middle of points into the
    // document as it is now: the gesture ends before the history moves.
    // Text being typed and a path being drawn are committed, so an undo
    // then takes them back as a step.
    if (_phoenixView && _phoenixView->activeTool())
        _phoenixView->activeTool()->deactivate(*_phoenixView);
    if (_motionEditor)
        _motionEditor->cancelDrag();
    if (_layerDepth)
        _layerDepth->cancelDrag();
}

bool MainWindow::stillInDocument(const fla::FLADocument* document, const fla::DOMElement* element) const
{
    if (!document || document != _flaDocument || !_flaDocument->document || !element)
        return false;
    return _flaDocument->document->reachable().count(element) > 0;
}

void MainWindow::pruneToDocument()
{
    // An undone edit owns again what it took out, and frees it when the next
    // edit is made: nothing may keep pointing at it.
    if (_phoenixView)
        _phoenixView->pruneToDocument();
    if (_flaDocument && _flaDocument->document)
    {
        const std::unordered_set<const fla::DOMElement*> reachable = _flaDocument->document->reachable();
        std::vector<fla::DOMElement*> kept;
        for (fla::DOMElement* element : _selection.elements())
            if (reachable.count(element))
                kept.push_back(element);
        if (kept.size() != _selection.elements().size())
            _selection.select(kept);
        if (_documentView)
            _documentView->pruneTo(reachable);
    }
    // These hold raw pointers into the document; they rebuild from it.
    if (_movieExplorer)
        _movieExplorer->refresh();
    if (_findReplace)
        _findReplace->refresh();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        // The safe zones may have changed with the step: new snap edges.
        _phoenixView->safeZonesChanged();
    }
}

void MainWindow::refreshActions()
{
    if (_actionsPanel && _actionsDock && _actionsDock->isVisible())
        _actionsPanel->refresh();
}

void MainWindow::openScriptEditor(const QString& path)
{
    if (!_scriptWindow)
    {
        _scriptWindow = new ScriptWindow([this](const QString& script) { return _scripts && _scripts->runFile(script); },
            ScriptRunner::commandsFolder(), this);
        // A blank script to start with, unless it opens on a file.
        if (path.isEmpty())
            _scriptWindow->newScript(CodeEditor::Language::JavaScript);
    }
    if (!path.isEmpty())
        _scriptWindow->openFile(path);
    _scriptWindow->show();
    _scriptWindow->raise();
    _scriptWindow->activateWindow();
}

void MainWindow::updateEditState()
{
    // An edit that took out what is selected (a cleared keyframe, a deleted
    // layer) leaves it to the history: the selection lets go of it, so the
    // next edit is not made on it. Not part way through an edit, which may
    // put it back.
    {
        const fla::CommandStack& history = _editContext.commandStack();
        if (!_selection.elements().empty() && !history.isInMacro() && !history.isExecuting() && _flaDocument &&
            _flaDocument->document)
        {
            const std::unordered_set<const fla::DOMElement*> reachable = _flaDocument->document->reachable();
            std::vector<fla::DOMElement*> kept;
            for (fla::DOMElement* element : _selection.elements())
                if (reachable.count(element))
                    kept.push_back(element);
            if (kept.size() != _selection.elements().size())
                _selection.select(kept);
        }
    }
    emit editStateChanged();
    refreshActions();
    const fla::CommandStack& stack = _editContext.commandStack();
    if (AutoSave* autoSave = activeAutoSave())
        autoSave->noteChanged();

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
    if (_behaviors && _behaviorsDock && _behaviorsDock->isVisible() && !_editContext.commandStack().isInMacro())
        _behaviors->refresh();
    if (_nodeGraph && _nodeGraphDock && _nodeGraphDock->isVisible() && !_editContext.commandStack().isInMacro())
        _nodeGraph->refresh();
    if (_palettes && _palettesDock && _palettesDock->isVisible() && !_editContext.commandStack().isInMacro())
        _palettes->refresh();
    if (_functionCurves && _functionCurvesDock && _functionCurvesDock->isVisible() && !_editContext.commandStack().isInMacro())
        _functionCurves->refresh();
    if (!_editContext.commandStack().isInMacro())
    {
        // An undo may have put back a scene, or taken the shown one away,
        // with the panel closed too: the stage's scene menu changes scenes.
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (document && _shownScene && !document->timelines.empty() && _shownScene != document->currentScene())
            goToScene(-1);
        if (_scenes && _scenesDock && _scenesDock->isVisible())
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
    // The document's paint brushes changed (renamed, given other options,
    // or an undo of that): the strokes drawn with them are laid out again.
    if (_phoenixView && _flaDocument && _flaDocument->document && !stack.isInMacro())
    {
        fla::paintBrushesOf(*_flaDocument->document);
        const void* brushes = _flaDocument->document->paintBrushesSource;
        if (brushes != _paintBrushesShown)
        {
            _paintBrushesShown = brushes;
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
    }

    if (_undoAction)
    {
        _undoAction->setEnabled(stack.canUndo() && !stack.isInMacro());
        // Name the specific edit: "Undo Draw Rectangle".
        const QString name = QString::fromStdString(stack.undoName());
        _undoAction->setText(name.isEmpty() ? QString("&Undo") : QString("&Undo %1").arg(name));
    }

    if (_redoAction)
    {
        _redoAction->setEnabled(stack.canRedo() && !stack.isInMacro());
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
    // Text being typed counts as a change to save.
    settleGestures();
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
    // The Tools panel: a dock down the left of the stage, the tools
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
    // Too short for its columns (or narrow for its rows), the panel takes
    // more, its minimum size pushing the dock out; given room again, the
    // dock goes back to the columns chosen. Queued, so the dock is not
    // resized while it lays out.
    connect(_toolbox, &Toolbox::preferredSizeChanged, this, [this]() {
        if (_toolsDock->isFloating() || !_toolsDock->isVisible())
            return;
        const QSize size = _toolbox->sizeHint();
        const bool vertical = _toolbox->orientation() == Qt::Vertical;
        resizeDocks({_toolsDock}, {vertical ? size.width() : size.height()}, vertical ? Qt::Horizontal : Qt::Vertical);
    }, Qt::QueuedConnection);
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
        _toolActionList.emplace_back(tool, action);
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
    // The free transform slot: Free Transform and Gradient Transform.
    addSlot({
        makeTool(ToolIcons::Tool::FreeTransform, "Free Transform", Qt::Key_Q,
            "Scale, rotate and skew the selection by its handles.",
            [this]() -> Tool* { return _freeTransformTool.get(); }),
        makeTool(ToolIcons::Tool::GradientTransform, "Gradient Transform", Qt::Key_F,
            "Click a gradient or bitmap fill, then drag its handles to move, scale and turn it.",
            [this]() -> Tool* { return _gradientTransformTool.get(); }),
    });
    // The lasso slot: the Lasso, the Polygon tool and the Magic Wand.
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
    addSlot({
        makeTool(ToolIcons::Tool::Bone, "Bone", Qt::Key_M,
            "Drag from one symbol instance to another to join them with a bone, or drag inside a shape to "
            "bone it; drag from an instance or joint in an armature to add a bone to it. Click a bone to "
            "limit how far its joint turns.",
            [this]() -> Tool* { return _boneTool.get(); }),
        makeTool(ToolIcons::Tool::Bind, "Bind", Qt::Key_unknown,
            "Click a bone of a boned shape to see the points that follow it; click a point to bind it to the "
            "bone, Shift-click to bind or unbind it, Ctrl-click to unbind it.",
            [this]() -> Tool* { return _bindTool.get(); }),
        makeTool(ToolIcons::Tool::AssetWarp, "Asset Warp", Qt::SHIFT | Qt::Key_W,
            "Click a puppet to pin it, drag a pin to bend the art round the others. Alt-click a pin, or "
            "select it and press Delete, to take it away.",
            [this]() -> Tool* { return _assetWarpTool.get(); }),
    });
    // The 3D tools share a slot.
    addSlot({
        makeTool(ToolIcons::Tool::Rotate3D, "3D Rotation", Qt::Key_W,
            "Drag a movie clip across to turn it about its vertical axis, up and down to tip it back or "
            "forward. Alt-click sets it flat again.",
            [this]() -> Tool* { return _rotate3DTool.get(); }),
        makeTool(ToolIcons::Tool::Translate3D, "3D Translation", Qt::Key_G,
            "Drag a movie clip up to push it into the stage, down to bring it nearer; it shrinks and grows "
            "toward the vanishing point.",
            [this]() -> Tool* { return _translate3DTool.get(); }),
    });

    addLine();

    // P for the pen. B is already the bounding-box toggle, and a
    // clash leaves Qt firing neither.
    // The pen slot: the Pen and the anchor point tools.
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
    addSlot({makeTool(ToolIcons::Tool::Width, "Width", Qt::Key_U,
        "Drag out from a stroke to widen it there, in to thin it; drag a width point to change it, select one "
        "and press Delete to take it away. Alt changes one side only.",
        [this]() -> Tool* { return _widthTool.get(); })});
    addSlot({makeTool(ToolIcons::Tool::Pencil, "Pencil", Qt::SHIFT | Qt::Key_Y,
        "Draw freehand. The stroke is fitted on release; the mode is in the Properties panel.",
        [this]() -> Tool* { return _pencilTool.get(); })});
    // The brushes share a slot: the Brush, Nib, Ink and Paint brushes; B is
    // the Brush's, Shift+B the Nib Brush's and Y the Paint Brush's.
    addSlot({
        makeTool(ToolIcons::Tool::FluidBrush, "Brush", Qt::Key_B,
            "Paint a fill whose width follows the pen's pressure and speed, with tapered ends "
            "and a smoothed line. Settings are in the Properties panel.",
            [this]() -> Tool* { return _brushTool.get(); }),
        makeTool(ToolIcons::Tool::Brush, "Nib Brush", Qt::SHIFT | Qt::Key_B,
            "Paint a fill with a shaped nib: round, oval, square or slanted. Use Pressure varies "
            "the width; Use Tilt turns the nib with the pen.",
            [this]() -> Tool* { return _classicBrushTool.get(); }),
        makeTool(ToolIcons::Tool::InkBrush, "Ink Brush", Qt::Key_unknown,
            "Paint steady inked lines: a stabilizer trails the pen, the pressure response is "
            "adjustable, and tilting the pen broadens the line.",
            [this]() -> Tool* { return _inkBrushTool.get(); }),
        makeTool(ToolIcons::Tool::PaintBrush, "Paint Brush", Qt::Key_Y,
            "Paint a stroke with an art or pattern brush, chosen under Brush in the Properties panel or in "
            "Window > Brush Library; a pen's pressure sets its width.",
            [this]() -> Tool* { return _paintBrushTool.get(); }),
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

    // The hand slot: the Hand, the Rotation tool and the Time Scrub
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
    // JSFL tools from the Tools folder, in a slot of their own at the end.
    {
        std::vector<QAction*> scriptActions;
        const QDir folder(QDir(ScriptRunner::configFolder()).filePath("Tools"));
        for (const QFileInfo& file : folder.entryInfoList({"*.jsfl"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        {
            auto script = std::make_unique<jsfl::ToolScript>(_scripts->jsflHost(), file.absoluteFilePath().toStdString());
            std::string error;
            if (!script->load(error))
            {
                _scripts->jsflHost().trace(error);
                continue;
            }
            ScriptTool* tool = _scriptTools.emplace_back(std::make_unique<ScriptTool>(_scripts->jsflHost(), std::move(script))).get();
            QAction* action = new QAction(tool->name(), this);
            action->setIcon(tool->icon());
            action->setCheckable(true);
            action->setToolTip(tool->toolTip().isEmpty() ? tool->name() : tool->toolTip());
            action->setStatusTip(QString("A tool from %1").arg(file.fileName()));
            connect(action, &QAction::triggered, this, [this, tool]() {
                tool->activate();
                _phoenixView->setActiveTool(tool);
            });
            _toolActionList.emplace_back([tool]() -> Tool* { return tool; }, action);
            toolGroup->addAction(action);
            addAction(action);
            _toolbox->addTool(action->text(), action);
            scriptActions.push_back(action);
        }
        if (!scriptActions.empty())
        {
            addLine();
            addSlot(scriptActions);
        }
    }
    _toolbox->setDefaultLayout(defaultTools);
    Toolbox::Layout saved =
        Toolbox::Layout::fromJson(QJsonDocument::fromJson(QSettings().value("toolbox/layout").toByteArray()).object());
    // The Paint Brush shared the Pencil's slot until 0.18; a layout that still
    // has that slot as it was made has the Paint Brush moved to the brushes.
    for (Toolbox::Slot& slot : saved.entries)
        if (slot.size() == 2 && slot.contains("Pencil") && slot.contains("Paint Brush"))
        {
            slot.removeAll("Paint Brush");
            for (Toolbox::Slot& brushes : saved.entries)
                if (brushes.contains("Brush") && !brushes.contains("Paint Brush"))
                    brushes.append("Paint Brush");
            break;
        }
    _toolbox->setLayout(saved);
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

    // The stroke and fill colors as one unit across the panel: the
    // stroke a ring, the fill solid, so the two never read as each other,
    // with Black and White and Swap beside them.
    _colorWell = new ColorWell(toolBar);
    _colorWell->strokeSwatch()->setObjectName("toolboxStroke");
    _colorWell->fillSwatch()->setObjectName("toolboxFill");
    connect(_colorWell, &ColorWell::strokeClicked, this, [this]() { chooseStyleColor(true); });
    connect(_colorWell, &ColorWell::fillClicked, this, [this]() { chooseStyleColor(false); });
    connect(_colorWell, &ColorWell::blackWhiteClicked, this, [this]() {
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
    connect(_colorWell, &ColorWell::swapClicked, this, [this]() {
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
    addExtra(_colorWell);
    refreshStyleSwatches();

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
    objectDrawingButton->setObjectName("toolboxObjectDrawing");
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
    if (!_textTool || !_textTool->isEditing())
        recolorSelection(stroke);
}

void MainWindow::recolorSelection(bool stroke)
{
    if (!_phoenixView)
        return;
    std::vector<fla::Shape*> shapes;
    for (fla::Element* element : selectedElements())
    {
        if (element && element->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<fla::Shape*>(element));
    }
    if (shapes.empty())
        return;
    if (!stroke)
    {
        if (_drawingStyle.fillKind == fla::DrawingStyle::FillKind::Solid)
            applyFillToSelection();
        return;
    }
    _phoenixView->flushPendingMerge();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Stroke Color");
    for (fla::Shape* shape : shapes)
    {
        if (shape->strokes.empty())
            continue;
        std::unique_ptr<fla::Element> copy(fla::cloneElement(*shape, shape->parent));
        fla::Shape* after = dynamic_cast<fla::Shape*>(copy.get());
        if (!after)
            continue;
        for (fla::StrokeStyle* line : after->strokes)
        {
            auto* solid = dynamic_cast<fla::SolidColor*>(line->fill);
            if (!solid)
            {
                delete line->fill;
                solid = new fla::SolidColor(line);
                line->fill = solid;
            }
            std::copy(_drawingStyle.strokeColor, _drawingStyle.strokeColor + 4, solid->color);
        }
        stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, "Stroke Color")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
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

    // The Library sits in a tab beside Properties.
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
    connect(_propertiesPanel, &PropertiesPanel::editEnvelopeRequested, this, &MainWindow::editEnvelope);
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

    // Color and Swatches, in the same group.
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
    connect(_colorPanel, &ColorPanel::applyToStrokesRequested, this, &MainWindow::applyFillToStrokes);
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
        if (!_textTool || !_textTool->isEditing())
            recolorSelection(stroke);
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
    _libraryPanel->setAnimationProvider(
        [this](const QString& name) {
            if (!_flaDocument || !_flaDocument->document || !_flaDocument->document->symbolList)
                return 0;
            const auto& symbols = _flaDocument->document->symbolList->symbolMap;
            const auto found = symbols.find(name.toStdString());
            if (found == symbols.end() || !found->second || found->second->timelines.empty())
                return 0;
            int frames = 0;
            for (const fla::Layer* layer : found->second->timelines.front()->layers)
                if (layer)
                    frames = std::max(frames, fla::timeline::layerEnd(*layer));
            return frames;
        },
        [this](const QString& name, int frame, const QSize& size) {
            return _phoenixView ? _phoenixView->libraryPreview("symbol", name, size, frame) : QImage();
        });
    connect(_libraryPanel, &LibraryPanel::bitmapPropertiesRequested, this, &MainWindow::bitmapProperties);
    connect(_libraryPanel, &LibraryPanel::updateBitmapsRequested, this, &MainWindow::updateBitmaps);
    connect(_libraryPanel, &LibraryPanel::editBitmapWithRequested, this, &MainWindow::editBitmapWith);
    connect(_libraryPanel, &LibraryPanel::soundPropertiesRequested, this, &MainWindow::soundProperties);
    connect(_libraryPanel, &LibraryPanel::videoPropertiesRequested, this, [this](const QString& name) {
        const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document)
            return;
        for (const fla::Document::VideoInfo& video : document->videos)
        {
            if (QString::fromStdString(video.name) != name)
                continue;
            QStringList lines;
            lines << QString("<b>%1</b>").arg(name.toHtmlEscaped());
            lines << (video.embedded ? QString("Embedded in the document") : QString("Linked to its file"));
            if (video.width > 0 && video.height > 0)
                lines << QString("%1 x %2 pixels").arg(video.width).arg(video.height);
            if (video.fps > 0.0)
                lines << QString("%1 frames a second").arg(video.fps, 0, 'g', 4);
            if (!video.source.empty())
                lines << QString("Imported from %1").arg(QString::fromStdString(video.source).toHtmlEscaped());
            lines << QString("Phoenix keeps the video and its placements as they are, and shows each placement as "
                             "a frame; it does not play the video. To animate over footage, use File &gt; Import &gt; "
                             "Import Video.");
            QMessageBox::information(this, "Video Properties", lines.join("<br>"));
            return;
        }
    });
    connect(_libraryPanel, &LibraryPanel::spriteSheetRequested, this, [this](const QString& name) { exportSpriteSheet(name); });
    connect(_libraryPanel, &LibraryPanel::newSymbolRequested, this, &MainWindow::newSymbol);
    _libraryPanel->setCanPaste([this]() {
        return !_libraryClipboard.isEmpty() && documentOpenAnywhere(_libraryClipboardSource) &&
            (!_flaDocument || _libraryClipboardSource != _flaDocument->document);
    });
    connect(_libraryPanel, &LibraryPanel::copyItemsRequested, this, [this](const QStringList& names) {
        _libraryClipboardSource = _flaDocument ? _flaDocument->document : nullptr;
        _libraryClipboard = names;
        statusBar()->showMessage("Paste them into another document's library.", 4000);
    });
    connect(_libraryPanel, &LibraryPanel::pasteItemsRequested, this, [this]() {
        if (_libraryClipboardSource && documentOpenAnywhere(_libraryClipboardSource))
            copyLibraryItems(*_libraryClipboardSource, _libraryClipboard);
    });
    // An item dragged from this library may land in another window, whose
    // stage or library copies it in with what it uses.
    connect(_libraryPanel, &LibraryPanel::itemDragStarted, this, [this](const QString&, const QString& name) {
        _externalDragSource = _flaDocument ? _flaDocument->document : nullptr;
        _externalDragName = name;
    });
    connect(_libraryPanel, &LibraryPanel::itemDragEnded, this, [this]() {
        // After the drop has been handled.
        QTimer::singleShot(0, this, []() { _externalDragSource = nullptr; });
    });
    connect(_libraryPanel, &LibraryPanel::foreignItemDropped, this, [this](const QString& name) {
        const fla::Document* source = _externalDragSource;
        const fla::Document* own = _flaDocument ? _flaDocument->document : nullptr;
        if (source && source != own && _externalDragName == name && documentOpenAnywhere(source))
            copyLibraryItems(*source, {name});
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
            // name.
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
            // Ask before taking instances with the symbols.
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
    // Properties...: the symbol's type, its linkage and 9-slice scaling, one
    // step to undo.
    connect(_libraryPanel, &LibraryPanel::symbolPropertiesRequested, this,
        [this, symbolNamed, afterLibraryEdit](const QString& name) {
            fla::Symbol* symbol = symbolNamed(name);
            if (!symbol)
                return;
            QDialog dialog(this);
            dialog.setWindowTitle("Symbol Properties");
            auto* form = new QFormLayout(&dialog);
            form->addRow("Name:", new QLabel(name, &dialog));
            auto* type = new QComboBox(&dialog);
            type->addItems({"Movie Clip", "Button", "Graphic"});
            type->setCurrentIndex(static_cast<int>(symbol->symbolType));
            type->setEnabled(symbol->otherSymbolType.empty());
            form->addRow("Type:", type);
            auto* className = new QLineEdit(QString::fromStdString(fla::library::linkageOf(*symbol)), &dialog);
            className->setPlaceholderText("Not exported for ActionScript");
            form->addRow("Class:", className);
            auto* slices = new QCheckBox("Enable guides for 9-slice scaling", &dialog);
            slices->setChecked(symbol->scale9);
            slices->setToolTip("Scale a movie clip by its middle and edges, keeping its corners: edit the symbol to place the guides");
            form->addRow(QString(), slices);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if (dialog.exec() != QDialog::Accepted)
                return;

            fla::CommandStack& stack = _editContext.commandStack();
            stack.beginMacro("Symbol Properties");
            const auto chosenType = static_cast<fla::SymbolType>(type->currentIndex());
            if (chosenType != symbol->symbolType)
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::SymbolType>(
                    symbol->symbolType, symbol->symbolType, chosenType, "Symbol Type")));
            const std::string linkage = className->text().trimmed().toStdString();
            if (linkage != fla::library::linkageOf(*symbol))
            {
                auto after = fla::library::withLinkage(symbol->extraAttributes, linkage);
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::pair<std::string, std::string>>>(
                    symbol->extraAttributes, symbol->extraAttributes, after, "Linkage")));
            }
            if (slices->isChecked() != symbol->scale9)
            {
                stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(
                    symbol->scale9, symbol->scale9, slices->isChecked(), "9-Slice Scaling")));
                // Guides never set start a third of the way in from each
                // edge of the art.
                const bool unset = symbol->scaleGridLeft == 0.0 && symbol->scaleGridRight == 0.0 &&
                    symbol->scaleGridTop == 0.0 && symbol->scaleGridBottom == 0.0;
                const QRectF art = _phoenixView ? _phoenixView->symbolFramesBounds(symbol, 0, 0) : QRectF();
                if (slices->isChecked() && unset && art.isValid() && !art.isEmpty())
                {
                    const double values[4] = {art.left() + art.width() / 3.0, art.right() - art.width() / 3.0,
                        art.top() + art.height() / 3.0, art.bottom() - art.height() / 3.0};
                    double* fields[4] = {&symbol->scaleGridLeft, &symbol->scaleGridRight, &symbol->scaleGridTop,
                        &symbol->scaleGridBottom};
                    for (int i = 0; i < 4; ++i)
                        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<double>(
                            *fields[i], *fields[i], std::round(values[i] * 20.0) / 20.0, "9-Slice Scaling")));
                }
            }
            stack.endMacro();
            if (_phoenixView)
            {
                _phoenixView->clearCaches();
                _phoenixView->update();
            }
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
    connect(_propertiesPanel, &PropertiesPanel::brushPropertiesRequested, this, &MainWindow::openBrushProperties);
    connect(_propertiesPanel, &PropertiesPanel::toolSettingChanged, this, [this](const QString& key) {
        if (key == "fillColor" || key == "strokeColor")
        {
            refreshStyleSwatches();
            // Text being typed takes the color on its selection too.
            if (key == "fillColor" && _textTool)
                _textTool->applyColor(_drawingStyle.fillColor);
        }
        else if (key == "hasFill" || key == "hasStroke")
        {
            refreshStyleSwatches();
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

    // Picking a tool shows its options.
    connect(_phoenixView, &PhoenixView::activeToolChanged, this, [this](Tool* tool) {
        _propertiesPanel->setActiveTool(tool, iconForTool(tool));
        _propertiesPanel->setTab(PropertiesPanel::Tab::Tool);
    });

    // The frame shown and the current layer decide what the Frame tab shows.
    // The stage lets go of the selection when the playhead moves (it hears
    // the signal first), so with nothing left selected the frame is what
    // the panel shows, as when a frame is clicked.
    connect(_player, &Player::currentFrameChanged, this, [this](int) {
        // Rebuilding every tab on every frame of playback costs more than
        // the frame: the panel catches up when playback stops.
        if (_player->isPlaying())
            return;
        _propertiesPanel->refresh();
        if (_selection.isEmpty())
            _propertiesPanel->setTab(PropertiesPanel::Tab::Frame);
        refreshActions();
    });
    connect(_player, &Player::playingChanged, this, [this](bool playing) {
        if (playing)
            return;
        _propertiesPanel->refresh();
        if (_selection.isEmpty())
            _propertiesPanel->setTab(PropertiesPanel::Tab::Frame);
    });
    connect(_timelineView, &TimelineView::currentLayerChanged, this, [this]() {
        _propertiesPanel->refresh();
        refreshActions();
    });
    // Playback runs on from one scene into the next.
    _timelineView->timelines()->setSceneAdvance([this](bool wrap) {
        fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
        if (!document || document->timelines.size() < 2)
            return false;
        int next = document->currentSceneIndex() + 1;
        if (next >= static_cast<int>(document->timelines.size()))
        {
            if (!wrap)
                return false;
            next = 0;
        }
        goToScene(next);
        return true;
    });
}

void MainWindow::placeLibraryItem(const QString& kind, const QString& droppedName, const QPointF& position)
{
    if (!_phoenixView || !_flaDocument || !_flaDocument->document)
        return;
    QString name = droppedName;
    // From an external library: copied in first, with what it uses.
    if (_externalDragSource && _externalDragSource != _flaDocument->document && _externalDragName == droppedName &&
        documentOpenAnywhere(_externalDragSource))
    {
        const fla::Document* source = _externalDragSource;
        _externalDragSource = nullptr;
        const std::vector<std::string> added = copyLibraryItems(*source, {droppedName});
        if (added.empty())
            return;
        name = QString::fromStdString(added.front());
    }
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
        if (fla::symbolContains(found->second, _phoenixView->scopeSymbol()))
        {
            statusBar()->showMessage(
                QString("%1 cannot go inside itself.").arg(QString::fromStdString(itemName)), 4000);
            return;
        }
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

    // Centered on the drop.
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
    "All importable files (*.png *.jpg *.jpeg *.gif *.bmp *.psd *.psb *.svg *.ai *.eps *.ps *.pdf *.wav *.aif *.aiff *.mp3 *.flac *.ogg *.fla *.xfl *.swf);;"
    "Images (*.png *.jpg *.jpeg *.gif *.bmp);;"
    "Photoshop documents (*.psd *.psb);;"
    "Vector artwork (*.svg *.ai *.eps *.ps *.pdf);;"
    "Sounds (*.wav *.aif *.aiff *.mp3 *.flac *.ogg);;"
    "Documents and movies (*.fla *.xfl *.swf);;"
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

    // One numbered image of a sequence offers the rest.
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

QString MainWindow::findFfmpeg(const QString& title)
{
    const QString found = MovieExport::ffmpegPath();
    if (found.isEmpty())
        QMessageBox::warning(this, title, MovieExport::missingFfmpeg());
    return found;
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
    const QString ffmpeg = findFfmpeg("Import Video");
    if (ffmpeg.isEmpty())
        return;

    // The video import choices that matter for a reference.
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

void MainWindow::importMedia(const QStringList& paths, bool toStage, const QPointF* at, QStringList* problemsOut)
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
        else if (MediaImport::isArtwork(path))
            importArtwork(path, toStage, at, frame, problems, placed);
        else if (fla::documentImport::handles(path.toStdString()))
        {
            // Another document or a movie: its library, and its main
            // timeline as a movie clip, placed where its stage was.
            fla::documentImport::Result result;
            std::string failure;
            if (!fla::documentImport::read(*document, path.toStdString(), result, failure))
            {
                problems << QString::fromStdString(failure);
                continue;
            }
            if (!result.notes.empty())
                problems << QString::fromStdString(result.notes);
            stack.push(std::move(result.command));
            fla::Symbol* symbol = nullptr;
            if (document->symbolList)
            {
                const auto found = document->symbolList->symbolMap.find(result.symbol);
                if (found != document->symbolList->symbolMap.end())
                    symbol = found->second;
            }
            if (!frame || !symbol)
                continue;
            auto* instance = new fla::SymbolInstance(frame);
            instance->libraryItemName = symbol->name;
            instance->symbol = symbol;
            instance->symbolType = symbol->symbolType;
            instance->loopType = fla::LoopType::Loop;
            if (at)
            {
                instance->transform.tx = at->x();
                instance->transform.ty = at->y();
            }
            stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, instance, name, &_selection)));
            placed.push_back(instance);
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
    if (problemsOut)
        *problemsOut << problems;
    else if (!problems.isEmpty())
        QMessageBox::warning(this, name, problems.join("\n"));
}

void MainWindow::importArtwork(const QString& path, bool toStage, const QPointF* at, fla::Frame* frame,
    QStringList& problems, std::vector<fla::DOMElement*>& placed)
{
    fla::Document* document = _flaDocument->document;
    fla::CommandStack& stack = _editContext.commandStack();
    const char* name = toStage ? "Import to Stage" : "Import to Library";
    const QString fileName = QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        problems << QString("%1 could not be read.").arg(fileName);
        return;
    }
    const QByteArray data = file.readAll();
    const std::vector<uint8_t> bytes(data.begin(), data.end());
    const auto note = [&](const std::vector<std::string>& notes) {
        for (const std::string& text : notes)
            problems << QString("%1: %2").arg(fileName, QString::fromStdString(text));
    };

    if (QFileInfo(path).suffix().toLower() == "psd" || QFileInfo(path).suffix().toLower() == "psb")
    {
        const fla::psd::Image image = fla::psd::read(bytes);
        note(image.notes);
        if (!image.ok)
            return;
        // A pixel layer's bitmap, its opacity in its pixels.
        const auto bitmapFor = [&](const fla::psd::Layer& layer) -> std::string {
            QImage pixels(layer.width, layer.height, QImage::Format_RGBA8888);
            for (int y = 0; y < layer.height; ++y)
            {
                uchar* line = pixels.scanLine(y);
                std::memcpy(line, layer.rgba.data() + static_cast<size_t>(y) * layer.width * 4, static_cast<size_t>(layer.width) * 4);
                if (layer.opacity < 255)
                    for (int x = 0; x < layer.width; ++x)
                        line[x * 4 + 3] = static_cast<uchar>(line[x * 4 + 3] * layer.opacity / 255);
            }
            fla::Bitmap* bitmap = MediaImport::bitmapFromImage(*document, pixels,
                QString::fromStdString(layer.name.empty() ? std::string("Layer") : layer.name));
            if (!bitmap)
            {
                problems << QString("%1: the layer %2 is too large.").arg(fileName, QString::fromStdString(layer.name));
                return std::string();
            }
            const std::string itemName = bitmap->name;
            stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, name)));
            return itemName;
        };

        fla::Timeline* timeline = nullptr;
        int index = 0;
        if (!toStage || !_timelineView || !_timelineView->currentLayer(timeline, index) || !timeline)
        {
            std::function<void(const std::vector<fla::psd::Layer>&)> addBitmaps = [&](const std::vector<fla::psd::Layer>& layers) {
                for (const fla::psd::Layer& layer : layers)
                {
                    if (layer.group)
                        addBitmaps(layer.children);
                    else
                        bitmapFor(layer);
                }
            };
            addBitmaps(image.layers);
            return;
        }

        // A layer for each, above the current layer and in its folder, the
        // document's top-left at the stage's or centered where dropped.
        fla::Layer* folder = nullptr;
        if (index >= 0 && index < static_cast<int>(timeline->layers.size()))
        {
            fla::Layer* current = timeline->layers[static_cast<size_t>(index)];
            if (current && current->parentLayer && current->parentLayer->layerType == fla::Layer::Type::Folder)
                folder = current->parentLayer;
        }
        int insertAt = std::max(0, index);
        bool clippedLayers = false;
        const double dx = at ? at->x() - image.width / 2.0 : 0.0;
        const double dy = at ? at->y() - image.height / 2.0 : 0.0;
        std::function<void(const std::vector<fla::psd::Layer>&, fla::Layer*)> addLayers =
            [&](const std::vector<fla::psd::Layer>& layers, fla::Layer* parent) {
                for (const fla::psd::Layer& source : layers)
                {
                    std::string itemName;
                    if (!source.group)
                    {
                        itemName = bitmapFor(source);
                        if (itemName.empty())
                            continue;
                    }
                    fla::Layer* layer = fla::newLayerFor(*timeline, source.group);
                    if (!source.name.empty())
                        layer->name = source.name;
                    layer->autoNamed = false;
                    layer->parentLayer = parent;
                    layer->visible = !source.hidden;
                    if (!source.group && !layer->frames.empty() && source.blend.empty())
                    {
                        fla::Frame* first = layer->frames.front();
                        fla::BitmapInstance* instance = new fla::BitmapInstance(first);
                        instance->libraryItemName = itemName;
                        instance->transform.tx = dx + source.left;
                        instance->transform.ty = dy + source.top;
                        first->elements.push_back(instance);
                    }
                    else if (!source.group && !layer->frames.empty())
                    {
                        // Blends apply to movie clips only: the layer's bitmap
                        // goes in one, which takes the blend.
                        if (!document->symbolList)
                            document->symbolList = new fla::SymbolList(document);
                        std::string symbolName = itemName + " Blend";
                        for (int n = 2; !fla::library::nameIsFree(*document, symbolName); ++n)
                            symbolName = itemName + " Blend " + std::to_string(n);
                        auto* symbol = new fla::Symbol(document->symbolList);
                        symbol->name = symbolName;
                        symbol->href = symbolName + ".xml";
                        symbol->symbolType = fla::SymbolType::MovieClip;
                        symbol->itemId = fla::media::newItemId();
                        auto* inside = new fla::Timeline(symbol);
                        inside->name = symbolName;
                        fla::Layer* insideLayer = fla::newLayerFor(*inside, false);
                        inside->layers.push_back(insideLayer);
                        symbol->timelines.push_back(inside);
                        if (!insideLayer->frames.empty())
                        {
                            auto* bitmap = new fla::BitmapInstance(insideLayer->frames.front());
                            bitmap->libraryItemName = itemName;
                            insideLayer->frames.front()->elements.push_back(bitmap);
                        }
                        stack.push(fla::CommandPtr(new fla::AddSymbolCommand(document, symbol, name)));
                        fla::Frame* first = layer->frames.front();
                        auto* clip = new fla::SymbolInstance(first);
                        clip->libraryItemName = symbolName;
                        clip->symbol = symbol;
                        clip->symbolType = fla::SymbolType::MovieClip;
                        clip->blendMode = source.blend;
                        clip->transform.tx = dx + source.left;
                        clip->transform.ty = dy + source.top;
                        first->elements.push_back(clip);
                    }
                    if (source.clipped)
                        clippedLayers = true;
                    stack.push(fla::CommandPtr(new fla::AddLayerCommand(timeline, insertAt++, layer, name)));
                    if (source.group)
                        addLayers(source.children, layer);
                }
            };
        addLayers(image.layers, folder);
        if (clippedLayers)
            problems << QString("%1: layers clipped to the layer below came in unclipped.").arg(fileName);
        if (_timelineView)
            _timelineView->refresh();
        return;
    }

    const bool svg = QFileInfo(path).suffix().toLower() == "svg";
    fla::vectors::Artwork art;
    try
    {
        art = svg ? fla::vectors::readSvg(std::string(data.constData(), static_cast<size_t>(data.size())))
                  : fla::vectors::readPdf(bytes);
    }
    catch (const std::exception&)
    {
        // Damaged past what the readers guard against (out of memory, say).
        problems << QString("%1 could not be read.").arg(fileName);
        return;
    }
    note(art.notes);
    const double dx = at ? at->x() - art.width / 2.0 : 0.0;
    const double dy = at ? at->y() - art.height / 2.0 : 0.0;
    const auto onStage = [&](const fla::vectors::Placement& p) {
        return fla::Transform(p.a, p.b, p.c, p.d, p.tx + dx, p.ty + dy);
    };
    // Everything, in drawing order: paths as drawing objects, images as
    // bitmaps placed, text as static text.
    std::vector<fla::Element*> shapes;
    std::vector<std::pair<fla::Bitmap*, bool>> newBitmaps;
    const QDir folder = QFileInfo(path).absoluteDir();
    std::vector<std::pair<fla::vectors::Artwork::Kind, size_t>> order = art.order;
    if (order.empty())
        for (size_t i = 0; i < art.paths.size(); ++i)
            order.emplace_back(fla::vectors::Artwork::Kind::Path, i);
    bool missingImage = false;
    for (const auto& [kind, index] : order)
    {
        if (kind == fla::vectors::Artwork::Kind::Path && index < art.paths.size())
        {
            fla::vectors::Painted painted = art.paths[index];
            for (fla::vectors::Subpath& subpath : painted.subpaths)
                for (fla::vectors::Segment& segment : subpath.segments)
                    for (fla::Point& point : segment.points)
                    {
                        point.x += dx;
                        point.y += dy;
                    }
            if (painted.gradient)
            {
                painted.gradient->placement.tx += dx;
                painted.gradient->placement.ty += dy;
            }
            // Clips that hold the whole path (a page-sized one, as
            // Illustrator writes first) cut nothing.
            if (!painted.clips.empty())
            {
                double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
                for (const fla::vectors::Subpath& subpath : painted.subpaths)
                    for (const fla::vectors::Segment& segment : subpath.segments)
                        for (const fla::Point& point : segment.points)
                        {
                            left = std::min(left, point.x);
                            right = std::max(right, point.x);
                            top = std::min(top, point.y);
                            bottom = std::max(bottom, point.y);
                        }
                const double margin = painted.stroke ? painted.strokeWidth / 2.0 : 0.0;
                const QRectF reach(left - margin, top - margin, right - left + 2 * margin, bottom - top + 2 * margin);
                std::vector<std::vector<fla::vectors::Subpath>> cutting;
                for (const std::vector<fla::vectors::Subpath>& clip : painted.clips)
                {
                    QPainterPath shape;
                    shape.setFillRule(Qt::OddEvenFill);
                    for (const fla::vectors::Subpath& subpath : clip)
                    {
                        for (const fla::vectors::Segment& segment : subpath.segments)
                        {
                            const QPointF p0(segment.points[0].x + dx, segment.points[0].y + dy);
                            if (segment.kind == fla::vectors::Segment::Kind::Move)
                                shape.moveTo(p0);
                            else if (segment.kind == fla::vectors::Segment::Kind::Line)
                                shape.lineTo(p0);
                            else if (segment.points.size() >= 3)
                                shape.cubicTo(p0, QPointF(segment.points[1].x + dx, segment.points[1].y + dy),
                                    QPointF(segment.points[2].x + dx, segment.points[2].y + dy));
                        }
                        shape.closeSubpath();
                    }
                    if (!shape.contains(reach))
                        cutting.push_back(clip);
                }
                painted.clips = cutting;
            }
            if (!painted.clips.empty())
            {
                // Cut to its clipping paths: a stroke first becomes the area
                // it covers, filled in its color.
                const auto toPath = [dx, dy](const std::vector<fla::vectors::Subpath>& subpaths, bool shift) {
                    QPainterPath out;
                    out.setFillRule(Qt::OddEvenFill);
                    for (const fla::vectors::Subpath& subpath : subpaths)
                    {
                        for (const fla::vectors::Segment& segment : subpath.segments)
                        {
                            const auto at = [&](size_t i) {
                                return QPointF(segment.points[i].x + (shift ? dx : 0.0), segment.points[i].y + (shift ? dy : 0.0));
                            };
                            if (segment.kind == fla::vectors::Segment::Kind::Move)
                                out.moveTo(at(0));
                            else if (segment.kind == fla::vectors::Segment::Kind::Line)
                                out.lineTo(at(0));
                            else if (segment.points.size() >= 3)
                                out.cubicTo(at(0), at(1), at(2));
                        }
                        if (subpath.closed)
                            out.closeSubpath();
                    }
                    return out;
                };
                QPainterPath area = toPath(painted.subpaths, false);
                if (!painted.fill && painted.stroke)
                {
                    QPainterPathStroker stroker;
                    stroker.setWidth(painted.strokeWidth);
                    stroker.setCapStyle(Qt::RoundCap);
                    stroker.setJoinStyle(Qt::RoundJoin);
                    area = stroker.createStroke(area).simplified();
                    painted.fill = painted.stroke;
                    painted.stroke.reset();
                }
                for (const std::vector<fla::vectors::Subpath>& clip : painted.clips)
                    area = area.intersected(toPath(clip, true));
                painted.subpaths.clear();
                fla::vectors::Subpath current;
                for (int i = 0; i < area.elementCount(); ++i)
                {
                    const QPainterPath::Element e = area.elementAt(i);
                    if (e.type == QPainterPath::MoveToElement)
                    {
                        if (current.segments.size() > 1)
                        {
                            current.closed = true;
                            painted.subpaths.push_back(current);
                        }
                        current = fla::vectors::Subpath();
                        current.segments.push_back({fla::vectors::Segment::Kind::Move, {fla::Point(e.x, e.y)}});
                    }
                    else if (e.type == QPainterPath::LineToElement)
                        current.segments.push_back({fla::vectors::Segment::Kind::Line, {fla::Point(e.x, e.y)}});
                    else if (e.type == QPainterPath::CurveToElement && i + 2 < area.elementCount())
                    {
                        const QPainterPath::Element c2 = area.elementAt(i + 1);
                        const QPainterPath::Element end = area.elementAt(i + 2);
                        current.segments.push_back({fla::vectors::Segment::Kind::Cubic,
                            {fla::Point(e.x, e.y), fla::Point(c2.x, c2.y), fla::Point(end.x, end.y)}});
                        i += 2;
                    }
                }
                if (current.segments.size() > 1)
                {
                    current.closed = true;
                    painted.subpaths.push_back(current);
                }
                if (painted.subpaths.empty())
                    continue;
            }
            if (fla::Shape* shape = fla::vectors::toShape(painted))
                shapes.push_back(shape);
        }
        else if (kind == fla::vectors::Artwork::Kind::Image && index < art.images.size())
        {
            const fla::vectors::Image& source = art.images[index];
            QImage pixels;
            if (!source.encoded.empty())
            {
                pixels.loadFromData(source.encoded.data(), static_cast<int>(source.encoded.size()));
                // Ink where the decoder read inverted CMYK.
                if (source.inkCmyk && pixels.format() == QImage::Format_CMYK8888)
                    for (int y = 0; y < pixels.height(); ++y)
                    {
                        uchar* line = pixels.scanLine(y);
                        for (int x = 0; x < pixels.width() * 4; ++x)
                            line[x] = static_cast<uchar>(255 - line[x]);
                    }
                // Pixels in order at the size the file gives rather than the
                // JPEG's own, as PostScript reads them (Illustrator 8 swaps
                // its JPEGs' width and height).
                const qint64 count = static_cast<qint64>(source.width) * source.height;
                if (!pixels.isNull() && source.width > 0 && source.height > 0 &&
                    pixels.size() != QSize(source.width, source.height) &&
                    static_cast<qint64>(pixels.width()) * pixels.height() == count)
                {
                    const QImage from = pixels.convertToFormat(QImage::Format_ARGB32);
                    QImage shaped(source.width, source.height, QImage::Format_ARGB32);
                    for (qint64 i = 0; i < count; ++i)
                        reinterpret_cast<QRgb*>(shaped.scanLine(static_cast<int>(i / source.width)))[i % source.width] =
                            reinterpret_cast<const QRgb*>(from.constScanLine(static_cast<int>(i / from.width())))[i % from.width()];
                    pixels = shaped;
                }
            }
            else if (!source.rgba.empty())
            {
                pixels = QImage(source.width, source.height, QImage::Format_RGBA8888);
                for (int y = 0; y < source.height; ++y)
                    std::memcpy(pixels.scanLine(y), source.rgba.data() + static_cast<size_t>(y) * source.width * 4,
                        static_cast<size_t>(source.width) * 4);
            }
            else if (!source.href.empty())
                pixels.load(folder.filePath(QUrl::fromPercentEncoding(QByteArray::fromStdString(source.href))));
            if (pixels.isNull())
            {
                missingImage = true;
                continue;
            }
            fla::Bitmap* bitmap = MediaImport::bitmapFromImage(*document, pixels,
                QFileInfo(path).completeBaseName() + " Image");
            if (!bitmap)
                continue;
            stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, name)));
            auto* instance = new fla::BitmapInstance(nullptr);
            instance->libraryItemName = bitmap->name;
            // The placement takes the unit square; the bitmap is its pixels.
            fla::Transform unit = onStage(source.placement);
            const double sx = 1.0 / std::max(1, pixels.width());
            const double sy = 1.0 / std::max(1, pixels.height());
            instance->transform = fla::Transform(unit.m11 * sx, unit.m12 * sx, unit.m21 * sy, unit.m22 * sy, unit.tx, unit.ty);
            shapes.push_back(instance);
        }
        else if (kind == fla::vectors::Artwork::Kind::Text && index < art.texts.size())
        {
            const fla::vectors::Text& source = art.texts[index];
            auto* text = new fla::StaticText(nullptr);
            text->autoExpand = true;
            // The box's top above the baseline by the font's ascent.
            text->left = 0.0;
            text->top = -source.size * 0.9;
            fla::TextRun run;
            run.text = source.text;
            run.face = source.family;
            if (source.bold || source.italic)
                run.face += std::string("-") + (source.bold ? "Bold" : "") + (source.italic ? "Italic" : "");
            run.size = source.size;
            for (int i = 0; i < 4; ++i)
                run.fillColor[i] = source.color[static_cast<size_t>(i)];
            text->runs.push_back(run);
            text->transform = onStage(source.placement);
            shapes.push_back(text);
        }
    }
    if (missingImage)
        problems << QString("%1: an image it shows could not be found or read.").arg(fileName);
    if (shapes.empty())
    {
        if (art.notes.empty())
            problems << QString("%1 has no artwork to import.").arg(fileName);
        return;
    }

    if (toStage && frame)
    {
        // Each path a drawing object, grouped as one piece of artwork.
        std::vector<fla::Element*> added;
        for (fla::Element* shape : shapes)
        {
            shape->parent = frame;
            stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, name, &_selection)));
            added.push_back(shape);
        }
        if (added.size() > 1)
        {
            auto* group = new fla::GroupElementsCommand(frame, added, name, &_selection);
            fla::Group* made = group->group();
            stack.push(fla::CommandPtr(group));
            placed.push_back(made);
        }
        else
            placed.push_back(added.front());
        return;
    }

    // In the library, a graphic symbol named for the file.
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);
    const std::string base = QFileInfo(path).completeBaseName().toStdString();
    std::string symbolName = base;
    for (int n = 2; !fla::library::nameIsFree(*document, symbolName); ++n)
        symbolName = base + " " + std::to_string(n);
    auto* symbol = new fla::Symbol(document->symbolList);
    symbol->name = symbolName;
    symbol->href = symbolName + ".xml";
    symbol->symbolType = fla::SymbolType::Graphic;
    symbol->itemId = fla::media::newItemId();
    auto* timeline = new fla::Timeline(symbol);
    timeline->name = symbolName;
    fla::Layer* layer = fla::newLayerFor(*timeline, false);
    timeline->layers.push_back(layer);
    symbol->timelines.push_back(timeline);
    if (!layer->frames.empty())
    {
        fla::Frame* first = layer->frames.front();
        for (fla::Element* shape : shapes)
        {
            shape->parent = first;
            first->elements.push_back(shape);
        }
    }
    else
    {
        for (fla::Element* shape : shapes)
            delete shape;
    }
    stack.push(fla::CommandPtr(new fla::AddSymbolCommand(document, symbol, name)));
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
    attachSoundTo(timeline->layers[index], _player ? _player->currentFrame() : 0, name);
}

void MainWindow::attachSoundTo(fla::Layer* layer, int at, const QString& name)
{
    // Dropped a moment ago: the layer may have gone since.
    if (!stillInDocument(_flaDocument, layer))
        return;
    if (layer->isLocked())
    {
        statusBar()->showMessage("The layer is locked.", 3000);
        return;
    }
    if (layer->layerType == fla::Layer::Type::Folder)
        return;
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
            // A sound replaces whatever the keyframe played, with the
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
    // The tools exist by now: each action is matched to its tool.
    if (_toolActions.empty())
        for (const auto& [reach, action] : _toolActionList)
            if (Tool* made = reach())
                _toolActions[made] = action;
    const auto found = _toolActions.find(tool);
    if (found != _toolActions.end() && found->second)
        found->second->trigger();
    else if (_phoenixView && tool)
        _phoenixView->setActiveTool(tool);
}

void MainWindow::scriptEvent(const char* type)
{
    // Not in the middle of an edit (a selection changing as a command runs):
    // a script that edits in answer would land inside it. Told just after.
    if (_editContext.commandStack().isExecuting())
    {
        QTimer::singleShot(0, this, [this, type]() { scriptEvent(type); });
        return;
    }
    if (_scripts)
        _scripts->dispatchEvent(type);
    for (const auto& tool : _scriptTools)
        tool->dispatch(type);
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
    if (tool == _boneTool.get()) return IconTool::Bone;
    if (tool == _bindTool.get()) return IconTool::Bind;
    if (tool == _assetWarpTool.get()) return IconTool::AssetWarp;
    if (tool == _rotate3DTool.get()) return IconTool::Rotate3D;
    if (tool == _translate3DTool.get()) return IconTool::Translate3D;
    if (tool == _widthTool.get()) return IconTool::Width;
    if (tool == _paintBrushTool.get()) return IconTool::PaintBrush;
    if (tool == _textTool.get()) return IconTool::Text;
    if (dynamic_cast<const ScriptTool*>(tool)) return IconTool::Script;
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

    // Back out one level with the edit bar's arrow.
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
        if (level == 0)
        {
            // The scene drops down: the scenes to switch to and the scene
            // commands. Inside a symbol a click still steps back out to it.
            button->setEnabled(true);
            button->setToolTip("Scenes");
            button->setPopupMode(depth > 0 ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
            QMenu* scenes = new QMenu(button);
            connect(scenes, &QMenu::aboutToShow, this, [this, scenes]() {
                scenes->clear();
                const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
                const int count = document ? static_cast<int>(document->timelines.size()) : 0;
                const int current = document ? document->currentSceneIndex() : -1;
                for (int i = 0; i < count; ++i)
                {
                    const std::string& name = document->timelines[static_cast<size_t>(i)]->name;
                    QAction* scene = scenes->addAction(
                        name.empty() ? QString("Scene %1").arg(i + 1) : QString::fromStdString(name), this,
                        [this, i]() {
                            if (_phoenixView)
                                _phoenixView->exitToDepth(0);
                            goToScene(i);
                        });
                    scene->setCheckable(true);
                    scene->setChecked(i == current);
                }
                if (count > 0)
                    scenes->addSeparator();
                scenes->addAction("&Add Scene", this, [this]() {
                    if (_scenes)
                        _scenes->addScene(false);
                })->setEnabled(document != nullptr);
                scenes->addAction("D&uplicate Scene", this, [this]() {
                    if (_scenes)
                        _scenes->addScene(true);
                })->setEnabled(count > 0);
                scenes->addAction("&Delete Scene", this, [this]() {
                    if (_scenes)
                        _scenes->deleteScene();
                })->setEnabled(count > 1);
                scenes->addSeparator();
                scenes->addAction("Open Scenes &Panel", this, [this]() {
                    if (!_scenesDock)
                        return;
                    _scenesDock->show();
                    _scenesDock->raise();
                });
            });
            button->setMenu(scenes);
        }
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
    if (!_colorWell)
        return;
    const auto colorOf = [](const uint8_t rgba[4]) { return QColor(rgba[0], rgba[1], rgba[2], rgba[3]); };
    _colorWell->setStroke(_drawingStyle.hasStroke ? QBrush(colorOf(_drawingStyle.strokeColor)) : QBrush());
    // The fill as new art gets it: a gradient drawn as one.
    QBrush fill;
    if (_drawingStyle.hasFill)
    {
        using Kind = fla::DrawingStyle::FillKind;
        QGradient gradient;
        if (_drawingStyle.fillKind == Kind::Linear)
            gradient = QLinearGradient(0.0, 0.0, 1.0, 0.0);
        else if (_drawingStyle.fillKind == Kind::Radial)
            gradient = QRadialGradient(0.5, 0.5, 0.5);
        if (gradient.type() != QGradient::NoGradient && !_drawingStyle.gradient.empty())
        {
            gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
            for (const fla::DrawingStyle::Stop& stop : _drawingStyle.gradient)
                gradient.setColorAt(std::clamp(stop.ratio, 0.0, 1.0), colorOf(stop.color));
            fill = QBrush(gradient);
        }
        else
        {
            fill = QBrush(colorOf(_drawingStyle.fillColor));
        }
    }
    _colorWell->setFill(fill);
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
    // Smooth is a fixed, gentle amount each time it is applied.
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
    HotSpinBox* below = new HotSpinBox(&dialog);
    below->setRange(0, 180);
    below->setSuffix(QString::fromUtf8("\u00b0"));
    below->setValue(settings.value("advancedSmooth/below", 90).toInt());
    form->addRow(belowOn, below);
    QCheckBox* aboveOn = new QCheckBox("Smooth angles above", &dialog);
    aboveOn->setChecked(settings.value("advancedSmooth/aboveOn", true).toBool());
    HotSpinBox* above = new HotSpinBox(&dialog);
    above->setRange(0, 180);
    above->setSuffix(QString::fromUtf8("\u00b0"));
    above->setValue(settings.value("advancedSmooth/above", 90).toInt());
    form->addRow(aboveOn, above);
    HotSpinBox* strength = new HotSpinBox(&dialog);
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
    HotSpinBox* strength = new HotSpinBox(&dialog);
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

/// The Expand Fill and Soften Fill Edges dialogs: a distance, for
/// softening a number of steps, and whether to go outward or inward.
bool askFillOffset(QWidget* parent, const QString& title, bool withSteps,
    double& distance, int& steps, bool& inset)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QFormLayout* form = new QFormLayout(&dialog);

    HotDoubleSpinBox* distanceBox = new HotDoubleSpinBox(&dialog);
    distanceBox->setRange(0.1, 1000.0);
    distanceBox->setDecimals(1);
    distanceBox->setSuffix(" px");
    distanceBox->setValue(distance);
    form->addRow("Distance:", distanceBox);

    HotSpinBox* stepsBox = nullptr;
    if (withSteps)
    {
        stepsBox = new HotSpinBox(&dialog);
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
    // The last settings are offered again.
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
    // fill; flushing it would merge it back into its source first.

    // The first name not already in the library, numbered,
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

        // The name has to be one the library does not have; Phoenix refuses
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

void MainWindow::safeZoneSettings()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    const Attributes before = document->extraAttributes;
    // Shown as it is edited, so the overlay is on while the dialog is open.
    const bool wasShown = _phoenixView->showSafeZones();
    _phoenixView->setShowSafeZones(true);
    SafeZonesDialog dialog(fla::safezones::of(*document), [this, document](const fla::safezones::Settings& settings) {
        document->extraAttributes = fla::safezones::withSettings(*document, settings);
        _phoenixView->safeZonesChanged();
    }, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    // The dialog may have outlived the document only if something replaced
    // it while open; nothing does, but be safe.
    if (!_flaDocument || _flaDocument->document != document)
        return;
    const Attributes after = fla::safezones::withSettings(*document, dialog.settings());
    document->extraAttributes = before;
    if (accepted && after != before)
    {
        _editContext.commandStack().push(fla::CommandPtr(
            new fla::SetPropertyCommand<Attributes>(document->extraAttributes, before, after, "Safe Zone Settings")));
        if (_safeZonesAction)
            _safeZonesAction->setChecked(true);
    }
    else if (_safeZonesAction)
    {
        _phoenixView->setShowSafeZones(wasShown);
        _safeZonesAction->setChecked(wasShown);
    }
    _phoenixView->safeZonesChanged();
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
    HotDoubleSpinBox* width = new HotDoubleSpinBox(&dialog);
    HotDoubleSpinBox* height = new HotDoubleSpinBox(&dialog);
    // The size in pixels, shown in the units chosen.
    double pixelWidth = document->width;
    double pixelHeight = document->height;
    const auto showSize = [&]() {
        const Unit unit = static_cast<Unit>(units->currentData().toInt());
        const double per = fla::docsettings::pixelsPer(unit);
        for (HotDoubleSpinBox* box : {width, height})
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
    connect(width, &HotDoubleSpinBox::valueChanged, &dialog, [&](double value) { pixelWidth = value * perUnit(); });
    connect(height, &HotDoubleSpinBox::valueChanged, &dialog, [&](double value) { pixelHeight = value * perUnit(); });
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
    // Anchor: which part of the stage the art keeps to when the stage grows
    // or shrinks around it unscaled.
    QWidget* anchorGrid = new QWidget(&dialog);
    QGridLayout* anchorLayout = new QGridLayout(anchorGrid);
    anchorLayout->setContentsMargins(0, 0, 0, 0);
    anchorLayout->setSpacing(2);
    QButtonGroup* anchors = new QButtonGroup(&dialog);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
        {
            QToolButton* cell = new QToolButton(anchorGrid);
            cell->setCheckable(true);
            cell->setFixedSize(22, 22);
            cell->setToolTip(QString("Keep the art to the %1%2")
                                 .arg(row == 0 ? "top" : row == 1 ? "middle" : "bottom")
                                 .arg(column == 0 ? " left" : column == 1 ? (row == 1 ? "" : " middle") : " right"));
            anchors->addButton(cell, row * 3 + column);
            anchorLayout->addWidget(cell, row, column);
        }
    anchors->button(0)->setChecked(true);
    anchorLayout->setColumnStretch(3, 1);
    form->addRow("Anchor:", anchorGrid);
    connect(scaleContent, &QCheckBox::toggled, anchorGrid, [anchorGrid](bool on) { anchorGrid->setEnabled(!on); });
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
    // The anchor moves the art with the side it keeps to; Match Contents
    // has already placed it.
    if (!scaleContent->isChecked() && moveX == 0.0 && moveY == 0.0)
    {
        const int cell = std::max(0, anchors->checkedId());
        moveX = (newWidth - document->width) * (cell % 3) / 2.0;
        moveY = (newHeight - document->height) * (cell / 3) / 2.0;
    }
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

std::vector<fla::StrokeStyle*> MainWindow::allStrokes() const
{
    std::vector<fla::StrokeStyle*> strokes;
    for (fla::Shape* shape : allShapes())
        strokes.insert(strokes.end(), shape->strokes.begin(), shape->strokes.end());
    return strokes;
}

std::vector<fla::Shape*> MainWindow::allShapes() const
{
    std::vector<fla::Shape*> shapes;
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return shapes;
    std::vector<fla::Timeline*> timelines(document->timelines.begin(), document->timelines.end());
    if (document->symbolList)
        for (fla::Symbol* symbol : document->symbolList->symbols)
            if (symbol)
                timelines.insert(timelines.end(), symbol->timelines.begin(), symbol->timelines.end());
    std::function<void(fla::Element*)> visit = [&](fla::Element* element) {
        if (auto* shape = dynamic_cast<fla::Shape*>(element))
            shapes.push_back(shape);
        if (auto* group = dynamic_cast<fla::Group*>(element))
            for (fla::Element* member : group->members)
                visit(member);
    };
    for (fla::Timeline* timeline : timelines)
        for (fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
            for (fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                for (fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                    visit(element);
    return shapes;
}

void MainWindow::manageBrushes()
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    const auto refreshView = [this]() {
        if (_phoenixView)
        {
            _phoenixView->clearCaches();
            _phoenixView->update();
        }
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    };
    // Whether \a stroke is drawn with \a brush.
    const auto drawsWith = [](const fla::StrokeStyle& stroke, const fla::DocumentBrush& brush) {
        if (brush.paintBrush)
            return stroke.style() == fla::StrokeStyle::Style::PaintBrush &&
                stroke.paintBrushDefinition == brush.definition.index;
        for (const auto& attribute : stroke.extraAttributes)
            if (attribute.first == "phoenixBrush")
                return attribute.second == brush.symbol;
        return false;
    };
    DocumentBrushesDialog::Hooks hooks;
    hooks.drawWith = [this](const fla::DocumentBrush& brush) {
        if (brush.paintBrush)
        {
            drawWithPaintBrush(brush.definition);
            return;
        }
        _drawingStyle.strokeLook.brush = brush.symbol;
        _drawingStyle.strokeLook.brushPattern = brush.pattern;
        if (_drawingStyle.strokeLook.kind == fla::StrokeStyle::Style::PaintBrush)
            _drawingStyle.strokeLook.kind = fla::StrokeStyle::Style::Solid;
        if (_propertiesPanel)
            _propertiesPanel->refresh();
    };
    hooks.selectStrokes = [this, drawsWith](const fla::DocumentBrush& brush) {
        fla::Frame* frame = _phoenixView ? _phoenixView->activeFrame() : nullptr;
        const fla::Layer* layer = frame ? dynamic_cast<const fla::Layer*>(frame->parent) : nullptr;
        const fla::Timeline* timeline = layer ? dynamic_cast<const fla::Timeline*>(layer->parent) : nullptr;
        if (!timeline || !_phoenixView)
            return 0;
        // What each layer shows at the playhead, unlocked.
        const int at = frame->index;
        std::vector<fla::DOMElement*> picked;
        std::function<bool(const fla::Element&)> uses = [&](const fla::Element& element) {
            if (const auto* shape = dynamic_cast<const fla::Shape*>(&element))
                return std::any_of(shape->strokes.begin(), shape->strokes.end(),
                    [&](const fla::StrokeStyle* stroke) { return stroke && drawsWith(*stroke, brush); });
            if (const auto* group = dynamic_cast<const fla::Group*>(&element))
                return std::any_of(group->members.begin(), group->members.end(),
                    [&](const fla::Element* member) { return member && uses(*member); });
            return false;
        };
        for (fla::Layer* each : timeline->layers)
        {
            if (!each || each->isLocked())
                continue;
            for (fla::Frame* shown : each->frames)
                if (shown && shown->index <= at && at < shown->index + std::max(1, shown->duration))
                    for (fla::Element* element : shown->elements)
                        if (element && uses(*element))
                            picked.push_back(element);
        }
        _selection.select(picked);
        _phoenixView->update();
        return static_cast<int>(picked.size());
    };
    hooks.takeOff = [this, drawsWith, refreshView](const fla::DocumentBrush& brush) {
        fla::CommandStack& stack = _editContext.commandStack();
        stack.beginMacro("Take Off Brush");
        for (fla::Shape* shape : allShapes())
        {
            if (std::none_of(shape->strokes.begin(), shape->strokes.end(),
                    [&](const fla::StrokeStyle* stroke) { return stroke && drawsWith(*stroke, brush); }))
                continue;
            // The strokes drawn with it become plain ones of their color and
            // weight, kept where the edges find them.
            std::unique_ptr<fla::Shape> after(static_cast<fla::Shape*>(fla::cloneElement(*shape, nullptr)));
            for (fla::StrokeStyle*& style : after->strokes)
            {
                if (!style || !drawsWith(*style, brush))
                    continue;
                fla::StrokeLook look = fla::StrokeLook::of(*style);
                look.brush.clear();
                look.brushPattern = false;
                if (look.kind == fla::StrokeStyle::Style::PaintBrush)
                {
                    look.kind = fla::StrokeStyle::Style::Solid;
                    look.paintBrush = 0;
                }
                fla::StrokeStyle* plain = look.applyTo(*style, after.get());
                for (auto& entry : after->strokesMap)
                    if (entry.second == style)
                        entry.second = plain;
                delete style;
                style = plain;
            }
            stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, "Take Off Brush")));
        }
        stack.endMacro();
        stack.breakMergeChain();
        refreshView();
    };
    hooks.push = [this, refreshView](fla::CommandPtr command) {
        _editContext.commandStack().push(std::move(command));
        _editContext.commandStack().breakMergeChain();
        refreshView();
    };
    DocumentBrushesDialog dialog(*document, hooks, this);
    dialog.exec();
}

void MainWindow::drawWithPaintBrush(const fla::PaintBrushDefinition& definition)
{
    fla::StrokeLook& look = _drawingStyle.strokeLook;
    look.kind = fla::StrokeStyle::Style::PaintBrush;
    look.hairline = false;
    look.brush.clear();
    look.paintBrushGuid = definition.guid();
    look.paintBrush = 0;
    const fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (const auto own = document ? fla::paintBrushesOf(*document) : nullptr)
        for (const fla::PaintBrushDefinition& candidate : own->definitions)
            if (look.paintBrushGuid.empty() ? candidate.json == definition.json
                                            : candidate.guid() == look.paintBrushGuid)
                look.paintBrush = candidate.index;
    // The Paint Brush tool, as its button would choose it.
    for (const auto& [tool, action] : _toolActionList)
        if (tool() == _paintBrushTool.get() && action && !action->isChecked())
            action->trigger();
    if (_propertiesPanel)
    {
        _propertiesPanel->refreshToolOptions();
        _propertiesPanel->refresh();
    }
    statusBar()->showMessage(
        QString("The Paint Brush draws with %1.").arg(QString::fromStdString(definition.name)), 4000);
}

void MainWindow::showBrushLibrary()
{
    if (!_brushLibrary)
    {
        BrushLibraryDialog::Hooks hooks;
        hooks.document = [this]() -> const fla::Document* {
            return _flaDocument ? _flaDocument->document : nullptr;
        };
        hooks.drawWith = [this](const fla::PaintBrushDefinition& definition) { drawWithPaintBrush(definition); };
        hooks.addToDocument = [this](const fla::PaintBrushDefinition& definition) {
            fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
            if (!document)
                return;
            std::map<int, int> indexes;
            if (fla::CommandPtr command = fla::adoptPaintBrushes(*document, {definition}, indexes, "Add Brush"))
            {
                _editContext.commandStack().push(std::move(command));
                _editContext.commandStack().breakMergeChain();
            }
            if (_propertiesPanel)
                _propertiesPanel->refresh();
        };
        hooks.readDocument = [](const QString& path, QString& error) {
            std::string why;
            std::unique_ptr<fla::FLADocument> loaded = commands::load(
                QFileInfo(path).suffix() == "xfl" ? QFileInfo(path).absolutePath().toStdString() : path.toStdString(),
                why);
            if (!loaded || !loaded->document)
            {
                error = QString("%1 could not be opened. %2").arg(QFileInfo(path).fileName(), QString::fromStdString(why));
                return std::vector<fla::PaintBrushDefinition>();
            }
            const auto library = fla::paintBrushesOf(*loaded->document);
            return library ? library->definitions : std::vector<fla::PaintBrushDefinition>();
        };
        _brushLibrary = new BrushLibraryDialog(BrushLibraryStore::shared(), hooks, this);
        _brushLibrary->setAttribute(Qt::WA_DeleteOnClose);
    }
    _brushLibrary->show();
    _brushLibrary->raise();
    _brushLibrary->activateWindow();
}

fla::Bitmap* MainWindow::bitmapNamed(const QString& name) const
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return nullptr;
    for (fla::Resource* resource : document->resources)
        if (resource && resource->resourceType() == fla::Resource::Type::Bitmap &&
            static_cast<fla::Bitmap*>(resource)->name == name.toStdString())
            return static_cast<fla::Bitmap*>(resource);
    return nullptr;
}

bool MainWindow::reimportBitmap(fla::Bitmap* bitmap, const QString& path, const char* name)
{
    QImage image(path);
    if (image.isNull())
    {
        QMessageBox::warning(this, name, QString("%1 could not be read as an image.").arg(QFileInfo(path).fileName()));
        return false;
    }
    _editContext.commandStack().push(fla::CommandPtr(new fla::SetBitmapPixelsCommand(
        bitmap, MediaImport::losslessData(image), image.width(), image.height(), name)));
    _editContext.commandStack().breakMergeChain();
    bitmap->sourceLastImported = static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch());
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
    if (_libraryPanel)
        _libraryPanel->refresh();
    return true;
}

void MainWindow::updateBitmaps(const QStringList& names)
{
    QStringList missing;
    int updated = 0;
    for (const QString& name : names)
    {
        fla::Bitmap* bitmap = bitmapNamed(name);
        if (!bitmap)
            continue;
        const QString source = QString::fromStdString(bitmap->sourceExternalFilepath);
        if (source.isEmpty() || !QFileInfo::exists(source))
        {
            missing << name;
            continue;
        }
        if (reimportBitmap(bitmap, source, "Update Bitmap"))
            ++updated;
    }
    if (!missing.isEmpty())
        QMessageBox::information(this, "Update",
            QString("The file these came from could not be found:\n\n%1\n\nChoose a new one with Import in the "
                    "bitmap's Properties.").arg(missing.join("\n")));
    else if (updated)
        statusBar()->showMessage(QString("Updated %1 bitmap%2.").arg(updated).arg(updated == 1 ? "" : "s"), 4000);
}

void MainWindow::bitmapProperties(const QString& name)
{
    fla::Bitmap* bitmap = bitmapNamed(name);
    if (!bitmap || !_phoenixView)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Bitmap Properties");
    QHBoxLayout* layout = new QHBoxLayout(&dialog);
    QLabel* preview = new QLabel(&dialog);
    preview->setFixedSize(160, 160);
    preview->setAlignment(Qt::AlignCenter);
    preview->setPixmap(_phoenixView->libraryBitmap(name).scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(preview, 0, Qt::AlignTop);
    QFormLayout* form = new QFormLayout();
    layout->addLayout(form, 1);
    form->addRow("Name", new QLabel(name, &dialog));
    const QString source = QString::fromStdString(bitmap->sourceExternalFilepath);
    QLabel* path = new QLabel(source.isEmpty() ? QString("(not known)") : source, &dialog);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow("Source", path);
    form->addRow("Size", new QLabel(QString("%1 x %2 pixels").arg(bitmap->frameRight / 20).arg(bitmap->frameBottom / 20), &dialog));
    if (bitmap->sourceLastImported > 0)
        form->addRow("Imported", new QLabel(QDateTime::fromSecsSinceEpoch(bitmap->sourceLastImported).toString("yyyy-MM-dd hh:mm"), &dialog));
    QCheckBox* smoothing = new QCheckBox("Allow smoothing", &dialog);
    smoothing->setChecked(bitmap->allowSmoothing);
    form->addRow(QString(), smoothing);
    // How it is compressed when published: kept for that.
    std::string compression = "photo";
    for (const auto& attribute : bitmap->extraAttributes)
        if (attribute.first == "compressionType" || attribute.first == "originalCompressionType")
            compression = attribute.second;
    QComboBox* kind = new QComboBox(&dialog);
    kind->addItems({"Photo (JPEG)", "Lossless (PNG/GIF)"});
    kind->setCurrentIndex(compression == "lossless" ? 1 : 0);
    form->addRow("Compression", kind);
    HotSpinBox* quality = new HotSpinBox(&dialog);
    quality->setRange(0, 100);
    quality->setSpecialValueText("Document default");
    quality->setValue(bitmap->quality);
    quality->setEnabled(kind->currentIndex() == 0);
    connect(kind, &QComboBox::currentIndexChanged, &dialog, [quality](int index) { quality->setEnabled(index == 0); });
    form->addRow("Quality", quality);
    QHBoxLayout* buttonsRow = new QHBoxLayout();
    QPushButton* update = new QPushButton("Update", &dialog);
    update->setEnabled(!source.isEmpty() && QFileInfo::exists(source));
    QPushButton* import = new QPushButton("Import...", &dialog);
    QPushButton* editWith = new QPushButton("Edit With...", &dialog);
    buttonsRow->addWidget(update);
    buttonsRow->addWidget(import);
    buttonsRow->addWidget(editWith);
    form->addRow(buttonsRow);
    connect(update, &QPushButton::clicked, &dialog, [this, bitmap, source, preview, name]() {
        if (reimportBitmap(bitmap, source, "Update Bitmap"))
            preview->setPixmap(_phoenixView->libraryBitmap(name).scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(import, &QPushButton::clicked, &dialog, [this, bitmap, preview, path, name]() {
        const QString chosen = QFileDialog::getOpenFileName(this, "Import Bitmap", QString(),
            "Images (*.png *.jpg *.jpeg *.gif *.bmp)");
        if (chosen.isEmpty() || !reimportBitmap(bitmap, chosen, "Import Bitmap"))
            return;
        bitmap->sourceExternalFilepath = chosen.toStdString();
        path->setText(chosen);
        preview->setPixmap(_phoenixView->libraryBitmap(name).scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(editWith, &QPushButton::clicked, &dialog, [this, name]() { editBitmapWith(name); });
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    Attributes attributes;
    for (const auto& attribute : bitmap->extraAttributes)
        if (attribute.first != "compressionType")
            attributes.push_back(attribute);
    attributes.emplace_back("compressionType", kind->currentIndex() == 1 ? "lossless" : "photo");
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Bitmap Properties");
    if (smoothing->isChecked() != bitmap->allowSmoothing)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<bool>(bitmap->allowSmoothing, bitmap->allowSmoothing,
            smoothing->isChecked(), "Bitmap Properties")));
    if (quality->value() != bitmap->quality)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<int>(bitmap->quality, bitmap->quality, quality->value(),
            "Bitmap Properties")));
    if (attributes != bitmap->extraAttributes)
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(bitmap->extraAttributes,
            bitmap->extraAttributes, attributes, "Bitmap Properties")));
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
}

void MainWindow::editBitmapWith(const QString& name)
{
    fla::Bitmap* bitmap = bitmapNamed(name);
    if (!bitmap || !_phoenixView)
        return;
    // The program: the one chosen before, or ask; Cancel uses the
    // system's own for PNG files.
    QSettings settings;
    QString program = settings.value("library/editWith").toString();
    if (program.isEmpty() || !QFileInfo::exists(program))
    {
        program = QFileDialog::getOpenFileName(this, "Edit With", QString(),
#ifdef Q_OS_WIN
            "Programs (*.exe)"
#else
            "Programs (*)"
#endif
        );
        if (!program.isEmpty())
            settings.setValue("library/editWith", program);
    }
    // The pixels as a PNG beside nothing else, named for the bitmap.
    QString leaf = name.mid(name.lastIndexOf('/') + 1);
    leaf = QFileInfo(leaf).completeBaseName().isEmpty() ? QString("bitmap") : QFileInfo(leaf).completeBaseName();
    const QString folder = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("Phoenix Edit With");
    QDir().mkpath(folder);
    const QString file = QDir(folder).filePath(leaf + ".png");
    if (!_phoenixView->libraryBitmap(name).save(file, "PNG"))
    {
        QMessageBox::warning(this, "Edit With", "The bitmap could not be written out for editing.");
        return;
    }
    if (!_editedBitmaps)
    {
        _editedBitmaps = new QFileSystemWatcher(this);
        connect(_editedBitmaps, &QFileSystemWatcher::fileChanged, this, [this](const QString& changed) {
            for (const auto& [bitmapName, edit] : _editedBitmapFiles)
            {
                if (edit.file != changed)
                    continue;
                const QString path = edit.file;
                const fla::Document* owner = edit.document;
                // Editors write the file in steps; read it once they are done.
                QTimer::singleShot(500, this, [this, bitmapName = bitmapName, path, owner]() {
                    // Only into the document it came from: another tab may
                    // have a bitmap of the same name.
                    if (!_flaDocument || _flaDocument->document != owner)
                    {
                        statusBar()->showMessage(
                            QString("%1 changed in the editor: switch back to its document to take it.").arg(bitmapName),
                            5000);
                        return;
                    }
                    if (fla::Bitmap* edited = bitmapNamed(bitmapName))
                        if (reimportBitmap(edited, path, "Edit With"))
                            statusBar()->showMessage(QString("%1 was updated from the editor.").arg(bitmapName), 4000);
                    // Some editors replace the file, which ends the watch.
                    if (!_editedBitmaps->files().contains(path) && QFileInfo::exists(path))
                        _editedBitmaps->addPath(path);
                });
            }
        });
    }
    _editedBitmapFiles[name] = EditedBitmap{file, _flaDocument ? _flaDocument->document : nullptr};
    _editedBitmaps->addPath(file);
    const bool started = program.isEmpty() ? QDesktopServices::openUrl(QUrl::fromLocalFile(file))
                                           : QProcess::startDetached(program, {QDir::toNativeSeparators(file)});
    if (!started)
        QMessageBox::warning(this, "Edit With", "The program could not be started.");
    else
        statusBar()->showMessage("Save the image in the other program and Phoenix takes the change.", 6000);
}

void MainWindow::soundProperties(const QString& name)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document)
        return;
    const fla::Document::SoundInfo* info = nullptr;
    for (const fla::Document::SoundInfo& sound : document->sounds)
        if (sound.name == name.toStdString())
            info = &sound;
    if (!info)
        return;
    std::shared_ptr<const PcmBuffer> pcm = _timelineAudio ? _timelineAudio->librarySound(info->name) : nullptr;
    QDialog dialog(this);
    dialog.setWindowTitle("Sound Properties");
    QFormLayout* form = new QFormLayout(&dialog);
    form->addRow("Name", new QLabel(name, &dialog));
    form->addRow("Format", new QLabel(info->format.empty() ? QString("MP3") : QString::fromStdString(info->format), &dialog));
    if (pcm)
    {
        const double seconds = static_cast<double>(pcm->frames()) / PcmBuffer::kRate;
        form->addRow("Length", new QLabel(QString("%1 s").arg(seconds, 0, 'f', 2), &dialog));
    }
    QPushButton* play = new QPushButton("Play", &dialog);
    QPushButton* stop = new QPushButton("Stop", &dialog);
    play->setEnabled(pcm != nullptr);
    auto voice = std::make_shared<int>(-1);
    connect(play, &QPushButton::clicked, &dialog, [pcm, voice]() {
        if (*voice >= 0)
            AudioEngine::instance().stop(*voice);
        *voice = AudioEngine::instance().play(pcm, 0, pcm->frames());
    });
    connect(stop, &QPushButton::clicked, &dialog, [voice]() {
        if (*voice >= 0)
            AudioEngine::instance().stop(*voice);
        *voice = -1;
    });
    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(play);
    row->addWidget(stop);
    row->addStretch(1);
    form->addRow(row);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.exec();
    if (*voice >= 0)
        AudioEngine::instance().stop(*voice);
}

bool MainWindow::documentAlive(const fla::Document* document) const
{
    if (!document)
        return false;
    if (_flaDocument && _flaDocument->document == document)
        return true;
    for (const auto& tab : _tabs)
        if (tab && tab->document && tab->document->document == document)
            return true;
    for (const auto& library : _externalLibraries)
        if (library->document && library->document->document == document)
            return true;
    return false;
}

std::vector<std::string> MainWindow::copyLibraryItems(const fla::Document& source, const QStringList& names,
    std::map<std::string, std::string>* renamed, bool* canceled)
{
    std::vector<std::string> added;
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || names.isEmpty())
        return added;
    std::vector<std::string> items;
    for (const QString& name : names)
        items.push_back(name.toStdString());
    fla::transfer::Resolution resolution = fla::transfer::Resolution::Skip;
    const std::vector<std::string> clashes = fla::transfer::conflicts(*document, source, items);
    if (!clashes.empty())
    {
        // Resolve Library Conflicts.
        QDialog dialog(this);
        dialog.setWindowTitle("Resolve Library Conflicts");
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLabel* intro = new QLabel(QString("The library already has %1 of the items being copied:")
                                       .arg(clashes.size() == 1 ? QString("one") : QString::number(clashes.size())),
            &dialog);
        layout->addWidget(intro);
        QListWidget* list = new QListWidget(&dialog);
        for (const std::string& clash : clashes)
            list->addItem(QString::fromStdString(clash));
        list->setMaximumHeight(140);
        layout->addWidget(list);
        QRadioButton* skip = new QRadioButton("Don't replace existing items: the copies use the library's own", &dialog);
        QRadioButton* replace = new QRadioButton("Replace existing items: everything that uses them shows the copies", &dialog);
        QRadioButton* both = new QRadioButton("Keep both: the copies come in under new names", &dialog);
        skip->setChecked(true);
        layout->addWidget(skip);
        layout->addWidget(replace);
        layout->addWidget(both);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted)
        {
            if (canceled)
                *canceled = true;
            return added;
        }
        resolution = replace->isChecked() ? fla::transfer::Resolution::Replace
            : both->isChecked()           ? fla::transfer::Resolution::KeepBoth
                                          : fla::transfer::Resolution::Skip;
    }
    fla::CommandPtr command = fla::transfer::copyItems(*document, source, items, resolution, &added, renamed);
    if (!command)
    {
        // Nothing new: what is wanted is the library's own.
        if (resolution == fla::transfer::Resolution::Skip)
            return items;
        return {};
    }
    _phoenixView->flushPendingMerge();
    _editContext.commandStack().push(std::move(command));
    _editContext.commandStack().breakMergeChain();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    _phoenixView->clearCaches();
    _phoenixView->update();
    statusBar()->showMessage(QString("Copied %1 into the library.").arg(names.join(", ")), 4000);
    return added;
}

void MainWindow::openExternalLibrary()
{
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, "Open External Library",
        settings.value("importFolder").toString(), "Documents (*.fla *.xfl *.phx);;All files (*)");
    if (path.isEmpty())
        return;
    std::string error;
    std::unique_ptr<fla::FLADocument> loaded = commands::load(
        QFileInfo(path).suffix() == "xfl" ? QFileInfo(path).absolutePath().toStdString() : path.toStdString(), error);
    if (!loaded || !loaded->document)
    {
        QMessageBox::warning(this, "Open External Library",
            QString("%1 could not be opened.\n\n%2").arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return;
    }
    auto library = std::make_unique<ExternalLibrary>();
    library->document = std::move(loaded);
    QDockWidget* dock = new QDockWidget("Library - " + QFileInfo(path).fileName(), this);
    dock->setObjectName("externalLibrary" + QString::number(_externalLibraries.size()));
    dock->setAttribute(Qt::WA_DeleteOnClose);
    LibraryPanel* panel = new LibraryPanel(dock);
    panel->setExternal(true);
    panel->setDocument(library->document.get());
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setFloating(true);
    dock->resize(320, 520);
    dock->show();
    library->dock = dock;
    library->panel = panel;
    const fla::Document* source = library->document->document;
    connect(panel, &LibraryPanel::copyToDocumentRequested, this, [this, source](const QStringList& names) {
        if (documentAlive(source))
            copyLibraryItems(*source, names);
    });
    connect(panel, &LibraryPanel::copyItemsRequested, this, [this, source](const QStringList& names) {
        _libraryClipboardSource = source;
        _libraryClipboard = names;
    });
    connect(panel, &LibraryPanel::externalDragStarted, this, [this, source](const QString&, const QString& name) {
        _externalDragSource = source;
        _externalDragName = name;
    });
    connect(panel, &LibraryPanel::externalDragEnded, this, [this]() {
        // After the drop has been handled.
        QTimer::singleShot(0, this, []() { _externalDragSource = nullptr; });
    });
    // Closing the panel lets the file go.
    ExternalLibrary* raw = library.get();
    connect(dock, &QObject::destroyed, this, [this, raw]() {
        if (_libraryClipboardSource && raw->document && _libraryClipboardSource == raw->document->document)
            _libraryClipboard.clear();
        _externalLibraries.erase(std::remove_if(_externalLibraries.begin(), _externalLibraries.end(),
                                     [raw](const std::unique_ptr<ExternalLibrary>& entry) { return entry.get() == raw; }),
            _externalLibraries.end());
    });
    _externalLibraries.push_back(std::move(library));
}

void MainWindow::goToMatch(const fla::find::Match& match)
{
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
    if (match.topElement)
        _selection.select(match.topElement);
    _phoenixView->clearCaches();
    _phoenixView->update();
    updateSelectionState();
}

void MainWindow::testMovie()
{
    if (!_flaDocument || !_flaDocument->document || !_flaDocument->document->currentScene())
        return;
    if (_phoenixView)
        _phoenixView->flushPendingMerge();
    // What the scripts trace, and the errors they stop on, in the Output
    // panel, shown when there is something in it.
    OutputPanel* output = _scripts ? _scripts->output() : nullptr;
    if (output)
        output->clear();
    const auto showOutput = [this]() {
        if (_scripts && _scripts->outputDock() && !_scripts->outputDock()->isVisible())
        {
            _scripts->outputDock()->show();
            _scripts->outputDock()->raise();
        }
    };
    const QString name = _activeTab >= 0 && _activeTab < static_cast<int>(_tabs.size()) ? tabName(*_tabs[static_cast<size_t>(_activeTab)])
                                                                                     : QString("Untitled");
    TestMovieWindow window(*_flaDocument, name,
        [output, showOutput](const QString& line) {
            if (!output)
                return;
            output->append(line);
            showOutput();
        },
        [this, output, showOutput](const QString& line, const fla::DOMElement* source, int at) {
            if (!output)
                return;
            // Double-clicking the error shows its script, at its line.
            std::function<void()> open;
            if (source)
                open = [this, source, at]() { showScript(source, at); };
            output->appendError(line, open);
            showOutput();
        },
        this, _documentPath);
    window.exec();
}

fla::SymbolInstance* MainWindow::makePuppet(fla::Element* art, fla::Frame* frame)
{
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    if (!document || !art || !frame || !_phoenixView || art->parent != frame)
        return nullptr;
    // As the FLA stores one: the art in a graphic symbol (WarpedAsset_n), in
    // a puppet symbol (PuppetShape_n) whose first frame is that graphic,
    // placed where the art was. The pins go on the instance.
    const auto freeName = [document](const std::string& base) {
        for (int n = 1;; ++n)
            if (fla::library::nameIsFree(*document, base + std::to_string(n)))
                return base + std::to_string(n);
    };
    const QRectF bounds = _phoenixView->elementBounds(art);
    const QPointF registration = bounds.isValid() ? bounds.center() : QPointF();
    const bool bitmap = art->elementType() == fla::Element::Type::BitmapInstance;
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Make Puppet");
    auto asset = std::make_unique<fla::ConvertToSymbolCommand>(document, frame, std::vector<fla::Element*>{art},
        freeName("WarpedAsset_"), registration.x(), registration.y(), "Make Puppet", &_selection,
        fla::SymbolType::Graphic);
    fla::ConvertToSymbolCommand* assetCommand = asset.get();
    stack.push(std::move(asset));
    fla::SymbolInstance* assetInstance = assetCommand->instance();
    auto puppet = std::make_unique<fla::ConvertToSymbolCommand>(document, frame,
        std::vector<fla::Element*>{assetInstance}, freeName("PuppetShape_"), registration.x(), registration.y(),
        "Make Puppet", &_selection, fla::SymbolType::Graphic);
    fla::ConvertToSymbolCommand* puppetCommand = puppet.get();
    stack.push(std::move(puppet));
    fla::Symbol* symbol = puppetCommand->symbol();
    fla::SymbolInstance* instance = puppetCommand->instance();
    if (symbol && instance)
    {
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(symbol->otherSymbolType,
            symbol->otherSymbolType, "puppet", "Make Puppet")));
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<std::string>(instance->otherSymbolType,
            instance->otherSymbolType, "puppet", "Make Puppet")));
        auto attributes = symbol->extraAttributes;
        attributes.emplace_back("puppetData", bitmap ? "[{\"type\":\"bitmap\"}]" : "[{\"type\":\"vector\"}]");
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<decltype(attributes)>(symbol->extraAttributes,
            symbol->extraAttributes, attributes, "Make Puppet")));
    }
    stack.endMacro();
    if (_libraryPanel)
        _libraryPanel->refreshIfStale();
    _phoenixView->clearCaches();
    _phoenixView->update();
    return symbol && instance ? instance : nullptr;
}

void MainWindow::showScript(const fla::DOMElement* source, int line)
{
    if (!_actionsDock || !_actionsPanel)
        return;
    _actionsDock->show();
    _actionsDock->raise();
    if (!_actionsPanel->reveal(source, line))
        statusBar()->showMessage("That script is no longer in the document.", 4000);
}

void MainWindow::traceBitmaps()
{
    if (!_phoenixView)
        return;
    std::vector<fla::BitmapInstance*> bitmaps;
    for (fla::DOMElement* selected : _selection.elements())
        if (auto* bitmap = dynamic_cast<fla::BitmapInstance*>(selected))
            bitmaps.push_back(bitmap);
    if (bitmaps.empty())
    {
        statusBar()->showMessage("Select a bitmap on the stage to trace.", 4000);
        return;
    }
    // Each bitmap's pixels, read once for every preview.
    struct Source
    {
        fla::BitmapInstance* bitmap;
        QImage image;
    };
    std::vector<Source> sources;
    for (fla::BitmapInstance* bitmap : bitmaps)
    {
        const QImage image = _phoenixView->libraryBitmap(QString::fromStdString(bitmap->libraryItemName))
                                 .toImage()
                                 .convertToFormat(QImage::Format_RGBA8888);
        if (dynamic_cast<fla::Frame*>(bitmap->parent) && !image.isNull())
            sources.push_back({bitmap, image});
    }
    if (sources.empty())
    {
        statusBar()->showMessage("The bitmap could not be read.", 4000);
        return;
    }
    _phoenixView->flushPendingMerge();
    fla::CommandStack& stack = _editContext.commandStack();
    const auto refresh = [this] {
        _phoenixView->invalidateBounds();
        _phoenixView->clearCaches();
        _phoenixView->update();
    };

    // The trace is made as one step to undo. A preview is that step, taken
    // back without a trace in the history when the settings change or the
    // dialog is canceled, and kept when it is accepted as it stands.
    bool applied = false;
    std::optional<fla::trace::Options> appliedWith;
    std::vector<fla::DOMElement*> traced;
    const auto takeBack = [&] {
        if (!applied)
            return;
        if (!stack.discardLast())
            stack.undo();
        applied = false;
        appliedWith.reset();
        traced.clear();
        std::vector<fla::DOMElement*> back;
        for (const Source& source : sources)
            back.push_back(source.bitmap);
        _selection.select(back);
    };
    const auto trace = [&](const fla::trace::Options& options) {
        takeBack();
        TraceBitmapDialog::Counts counts;
        const size_t before = stack.index();
        stack.beginMacro("Trace Bitmap");
        for (const Source& source : sources)
        {
            fla::BitmapInstance* bitmap = source.bitmap;
            auto* frame = static_cast<fla::Frame*>(bitmap->parent);
            const QImage& image = source.image;
            counts.pixels += static_cast<long long>(image.width()) * image.height();
            std::vector<uint8_t> pixels(static_cast<size_t>(image.width()) * image.height() * 4);
            for (int y = 0; y < image.height(); ++y)
                std::memcpy(pixels.data() + static_cast<size_t>(y) * image.width() * 4, image.constScanLine(y),
                    static_cast<size_t>(image.width()) * 4);
            fla::Shape* shape = fla::trace::traceBitmap(pixels.data(), image.width(), image.height(), options);
            if (!shape)
                continue;
            counts.areas += static_cast<int>(shape->fills.size());
            for (const fla::Edge* edge : shape->edges)
                for (const fla::Path* path : edge->paths)
                    counts.points += static_cast<int>(path->segments.size());
            // In the bitmap's place, as it was placed.
            shape->parent = frame;
            shape->transform = bitmap->transform;
            const auto found = std::find(frame->elements.begin(), frame->elements.end(), bitmap);
            const int index = found == frame->elements.end() ? -1 : static_cast<int>(found - frame->elements.begin());
            stack.push(fla::CommandPtr(new fla::RemoveElementCommand(frame, bitmap, "Trace Bitmap", &_selection)));
            stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, shape, "Trace Bitmap", &_selection, index)));
            traced.push_back(shape);
        }
        stack.endMacro();
        stack.breakMergeChain();
        applied = stack.index() > before;
        if (applied)
            appliedWith = options;
        // Nothing selected while previewing, so the artwork shows as it
        // will be rather than under the selection's hatching.
        _selection.clear();
        refresh();
        return counts;
    };

    TraceBitmapDialog dialog(static_cast<int>(sources.size()), trace, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const fla::trace::Options options = dialog.options();
    const auto same = [](const fla::trace::Options& a, const fla::trace::Options& b) {
        return a.colorThreshold == b.colorThreshold && a.minimumArea == b.minimumArea && a.curveFit == b.curveFit;
    };
    if (!accepted)
        takeBack();
    else if (!appliedWith || !same(*appliedWith, options))
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        trace(options);
        QApplication::restoreOverrideCursor();
    }
    if (accepted && !traced.empty())
        _selection.select(traced);
    refresh();
    updateSelectionState();
}

void MainWindow::editEnvelope(fla::Frame* frame)
{
    if (!frame || !_flaDocument || !_flaDocument->document)
        return;
    const fla::frame_sound::Settings settings = fla::frame_sound::read(*frame);
    if (settings.name.empty())
        return;
    std::shared_ptr<const PcmBuffer> sound = _timelineAudio ? _timelineAudio->librarySound(settings.name) : nullptr;
    if (!sound)
    {
        QMessageBox::information(this, "Edit Envelope", "The sound could not be read.");
        return;
    }
    size_t in = 0, out = 0;
    fla::frame_sound::trim(*frame, in, out);
    EnvelopeDialog dialog(sound, _flaDocument->document->frameRate, fla::frame_sound::envelope(*frame), in, out, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        if (_propertiesPanel)
            _propertiesPanel->refresh();
        return;
    }
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    Attributes attributes = frame->extraAttributes;
    std::vector<std::string> children = frame->extraChildren;
    fla::frame_sound::setEnvelope(dialog.editor()->points, dialog.editor()->in, dialog.editor()->out, attributes, children);
    if (attributes != frame->extraAttributes || children != frame->extraChildren)
    {
        auto* command = new fla::CompoundCommand("Edit Envelope");
        command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
            frame->extraAttributes, frame->extraAttributes, attributes, "Edit Envelope")));
        command->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
            frame->extraChildren, frame->extraChildren, children, "Edit Envelope")));
        _editContext.commandStack().push(fla::CommandPtr(command));
        _editContext.commandStack().breakMergeChain();
    }
    if (_timelineView)
        _timelineView->refreshFrames();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
}

void MainWindow::splitAudio()
{
    if (!_flaDocument || !_flaDocument->document || !_timelineView || !_player)
        return;
    fla::Timeline* timeline = nullptr;
    int index = 0;
    if (!_timelineView->currentLayer(timeline, index) || !timeline || index < 0 ||
        index >= static_cast<int>(timeline->layers.size()))
        return;
    fla::Layer* layer = timeline->layers[static_cast<size_t>(index)];
    const int at = _player->currentFrame();
    const double fps = _flaDocument->document->frameRate;
    auto split = std::make_shared<bool>(false);
    auto* command = new fla::EditFramesCommand({layer}, "Split Audio", [layer, at, fps, split](std::vector<fla::Frame*>&) {
        *split = fla::frame_sound::splitAudio(*layer, at, fps);
    });
    _editContext.commandStack().push(fla::CommandPtr(command));
    _editContext.commandStack().breakMergeChain();
    if (!*split)
    {
        _editContext.commandStack().discardLast();
        statusBar()->showMessage("Split Audio cuts a stream sound inside its span: put the playhead there, on its layer.", 5000);
        return;
    }
    _timelineView->refreshFrames();
    if (_propertiesPanel)
        _propertiesPanel->refresh();
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

void MainWindow::editTemplateFields()
{
    std::vector<fla::SymbolInstance*> instances;
    for (fla::DOMElement* selected : _selection.elements())
        if (auto* instance = dynamic_cast<fla::SymbolInstance*>(selected))
            if (instance->symbol)
                instances.push_back(instance);
    if (instances.empty())
        return;
    // The fields of the first instance's symbol; others of that symbol
    // are filled in with it.
    const fla::Symbol* symbol = instances.front()->symbol;
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                        [symbol](const fla::SymbolInstance* instance) { return instance->symbol != symbol; }),
        instances.end());
    const std::vector<fla::templates::Field> fields = fla::templates::fieldsOf(*symbol);
    if (fields.empty())
    {
        QMessageBox::information(this, "Template Fields",
            QString("%1 has no template fields.\n\nGive a dynamic text field inside it an instance name to make "
                    "its words a field, or a movie clip inside it an instance name to make its color one.")
                .arg(QString::fromStdString(symbol->name)));
        return;
    }
    const fla::templates::Values before = fla::templates::valuesOf(*instances.front());

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Template Fields: %1").arg(QString::fromStdString(symbol->name)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    layout->addLayout(form);
    std::vector<std::pair<std::string, QLineEdit*>> edits;
    for (const fla::templates::Field& field : fields)
    {
        auto* edit = new QLineEdit(&dialog);
        const auto given = before.find(field.name);
        if (given != before.end())
            edit->setText(QString::fromStdString(given->second));
        if (field.kind == fla::templates::Field::Kind::Text)
        {
            edit->setPlaceholderText(QString::fromStdString(field.value));
            form->addRow(QString::fromStdString(field.name), edit);
        }
        else
        {
            // A color: typed as #RRGGBB, or picked.
            edit->setPlaceholderText("as drawn (or #RRGGBB)");
            auto* row = new QHBoxLayout();
            row->addWidget(edit, 1);
            auto* pick = new QPushButton("Pick...", &dialog);
            connect(pick, &QPushButton::clicked, &dialog, [edit, &dialog]() {
                const QColor color = QColorDialog::getColor(QColor(edit->text()), &dialog, "Color");
                if (color.isValid())
                    edit->setText(color.name().toUpper());
            });
            row->addWidget(pick);
            form->addRow(QString::fromStdString(field.name) + " (color)", row);
        }
        edits.emplace_back(field.name, edit);
    }
    auto* note = new QLabel("Left empty, a field shows what the symbol has.", &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(420, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Any that went while the dialog was up are left alone.
    const fla::FLADocument* filled = _flaDocument;
    instances.erase(std::remove_if(instances.begin(), instances.end(),
                        [this, filled](const fla::SymbolInstance* instance) { return !stillInDocument(filled, instance); }),
        instances.end());
    fla::templates::Values after = before;
    for (const auto& [name, edit] : edits)
    {
        after.erase(name);
        if (!edit->text().isEmpty())
            after[name] = edit->text().toStdString();
    }
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    fla::CommandStack& stack = _editContext.commandStack();
    bool any = false;
    for (fla::SymbolInstance* instance : instances)
    {
        const Attributes changed = fla::templates::withValues(*instance, after);
        if (changed == instance->extraAttributes)
            continue;
        if (!any)
            stack.beginMacro("Template Fields");
        any = true;
        stack.push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(instance->extraAttributes,
            instance->extraAttributes, changed, "Template Fields")));
    }
    if (!any)
        return;
    stack.endMacro();
    stack.breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

std::vector<fla::templates::Values> MainWindow::rowsOfData(QFile& file)
{
    std::vector<fla::templates::Values> rows = fla::templates::rowsOfCsv(file.readAll().toStdString());
    // An image named from the file's folder is found there, wherever the
    // document is.
    const QDir folder = QFileInfo(file.fileName()).absoluteDir();
    for (fla::templates::Values& row : rows)
        for (auto& [name, value] : row)
        {
            const QString path = QString::fromStdString(value);
            if (!path.isEmpty() && QFileInfo(path).isRelative() && !path.contains('\n') &&
                QFileInfo(folder.filePath(path)).isFile())
                value = folder.absoluteFilePath(path).toStdString();
        }
    return rows;
}

bool MainWindow::renderRows(const std::vector<fla::templates::Values>& rows, const QString& folder,
    const QString& extension, QString& error, const std::function<bool(int row, int rows)>& progress)
{
    if (!_flaDocument || !_flaDocument->document || !_phoenixView)
    {
        error = "No document is open.";
        return false;
    }
    const fla::Document* document = _flaDocument->document;
    if (!QDir().mkpath(folder))
    {
        error = QString("Could not make the folder %1.").arg(folder);
        return false;
    }
    const QString ext = extension.toLower();
    const QString base = _documentPath.isEmpty() ? QString("render") : QFileInfo(_documentPath).completeBaseName();
    const fla::templates::Values was = _phoenixView->templateValues();
    bool ok = true;
    for (size_t r = 0; r < rows.size() && ok; ++r)
    {
        if (progress && !progress(static_cast<int>(r), static_cast<int>(rows.size())))
        {
            error = "Render cancelled.";
            ok = false;
            break;
        }
        const fla::templates::Values& row = rows[r];
        // Named by its file column when it has one, else numbered.
        QString name;
        for (const char* column : {"file", "File", "filename", "output"})
        {
            const auto found = row.find(column);
            if (found != row.end() && !found->second.empty())
            {
                name = QString::fromStdString(found->second);
                break;
            }
        }
        if (name.isEmpty())
            name = QString("%1-%2").arg(base).arg(static_cast<int>(r) + 1, 3, 10, QChar('0'));
        name.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
        if (QFileInfo(name).suffix().compare(ext, Qt::CaseInsensitive) != 0)
            name += "." + ext;

        _phoenixView->setTemplateValues(row);
        MovieExportSettings settings;
        settings.format = ext == "png" ? MovieExportSettings::Format::PngSequence
            : ext == "gif"             ? MovieExportSettings::Format::Gif
                                       : MovieExportSettings::Format::Video;
        if (ext == "mov")
            settings.codec = MovieExportSettings::Codec::ProRes422;
        settings.path = QDir(folder).absoluteFilePath(name);
        settings.size = QSize(static_cast<int>(std::lround(document->width)), static_cast<int>(std::lround(document->height)));
        settings.first = 0;
        settings.last = std::max(1, _phoenixView->frameCount()) - 1;
        QString why;
        if (!MovieExport::run(settings, *_phoenixView, _timelineAudio, document->frameRate, nullptr, why))
        {
            error = QString("Row %1: %2").arg(static_cast<int>(r) + 1).arg(why.isEmpty() ? QString("cancelled") : why);
            ok = false;
        }
        else
            std::printf("Rendered row %d to %s\n", static_cast<int>(r) + 1, qPrintable(settings.path));
    }
    _phoenixView->setTemplateValues(was);
    return ok;
}

void MainWindow::renderFromData()
{
    if (!_flaDocument || !_flaDocument->document)
        return;
    const QString csv = QFileDialog::getOpenFileName(this, "Render from Data", QString(), "CSV files (*.csv);;All files (*)");
    if (csv.isEmpty())
        return;
    QFile file(csv);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, "Render from Data", QString("Could not read %1.").arg(csv));
        return;
    }
    const std::vector<fla::templates::Values> rows = rowsOfData(file);
    if (rows.empty())
    {
        QMessageBox::information(this, "Render from Data",
            "The file has no rows to render: the first row names the template fields, and each row after it is a movie.");
        return;
    }
    bool chosen = false;
    const QString extension = QInputDialog::getItem(this, "Render from Data",
        QString("%1 rows, each rendered as:").arg(rows.size()), {"mp4", "mov", "gif", "png"}, 0, false, &chosen);
    if (!chosen)
        return;
    const QString folder = QFileDialog::getExistingDirectory(this, "Render into Folder", QFileInfo(csv).absolutePath());
    if (folder.isEmpty())
        return;
    QProgressDialog progress("Rendering...", "Cancel", 0, static_cast<int>(rows.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QString error;
    const bool ok = renderRows(rows, folder, extension, error, [&progress](int row, int count) {
        progress.setLabelText(QString("Rendering row %1 of %2...").arg(row + 1).arg(count));
        progress.setValue(row);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.setValue(static_cast<int>(rows.size()));
    if (!ok)
        QMessageBox::warning(this, "Render from Data", error);
    else
        statusBar()->showMessage(QString("Rendered %1 movies into %2").arg(rows.size()).arg(folder), 5000);
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

    // A duplicate made in the dialog stays, whether or not it is chosen;
    // it is its own undo step.
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
    if (_phoenixView && fla::symbolContains(symbol, _phoenixView->scopeSymbol()))
    {
        QMessageBox::information(this, "Swap Symbol",
            QString("%1 contains the symbol being edited, which cannot go inside itself.")
                .arg(QString::fromStdString(chosen)));
        return;
    }

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

    // About the middle of the selection.
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
    HotDoubleSpinBox* scaleBox = new HotDoubleSpinBox(&dialog);
    scaleBox->setRange(1.0, 10000.0);
    scaleBox->setSuffix(" %");
    scaleBox->setValue(scale);
    HotDoubleSpinBox* rotateBox = new HotDoubleSpinBox(&dialog);
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
    std::string name = dialog.symbolName().trimmed().toStdString();
    // Into the library folder selected.
    const QString folder = _libraryPanel ? _libraryPanel->currentFolder() : QString();
    if (!folder.isEmpty() && name.find('/') == std::string::npos && !name.empty())
        name = folder.toStdString() + "/" + name;
    if (name.empty() || !fla::library::nameIsFree(*document, name))
    {
        QMessageBox::information(this, "Create New Symbol",
            QString("The library already has an item named %1.").arg(QString::fromStdString(name)));
        return;
    }
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);

    // One layer with one empty keyframe.
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
    // The range, as the presets give it, or as dragged.
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
    const fla::SymbolList* library = _flaDocument && _flaDocument->document ? _flaDocument->document->symbolList : nullptr;
    std::vector<fla::CommandPtr> commands =
        fla::layers::distributeToLayers(*timeline, current, selectedElements(), library, &_selection);
    if (commands.empty())
        return;
    _selection.clear();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Distribute to Layers");
    for (fla::CommandPtr& command : commands)
        stack.push(std::move(command));
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
            if (_templateFieldsAction && !fla::templates::fieldsOf(*instance->symbol).empty())
                menu.addAction(_templateFieldsAction);
            menu.addAction("Pin to Text...", this, [this, instance]() { pinToText(instance); })
                ->setStatusTip("Move or stretch this instance with a text field beside it as a template's words change");
        }
        if (auto* group = dynamic_cast<fla::Group*>(_selection.single()))
            menu.addAction("Edit in Place", this, [this, group]() { _phoenixView->enterGroup(group); });
        const auto& chosen = _selection.elements();
        if (std::any_of(chosen.begin(), chosen.end(), [](const fla::DOMElement* e) { return dynamic_cast<const fla::BitmapInstance*>(e); }))
        {
            for (QAction* action : {_traceBitmapAction, _swapBitmapAction})
            {
                if (action)
                    menu.addAction(action);
            }
        }
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

void MainWindow::pinToText(fla::SymbolInstance* instance)
{
    // The named text fields on the instance's own timeline.
    QStringList names;
    const fla::DOMElement* up = instance->parent;
    while (up && up->domType() != fla::DOMElement::DOMType::Layer)
        up = up->parent;
    const auto* timeline = up ? dynamic_cast<const fla::Timeline*>(up->parent) : nullptr;
    for (const fla::Layer* layer : timeline ? timeline->layers : std::vector<fla::Layer*>())
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            for (const fla::Element* element : frame ? frame->elements : std::vector<fla::Element*>())
                if (const auto* text = dynamic_cast<const fla::StaticText*>(element))
                    if (text->textType != fla::StaticText::TextType::Static && !text->instanceName.empty() &&
                        !names.contains(QString::fromStdString(text->instanceName)))
                        names << QString::fromStdString(text->instanceName);
    const fla::templates::Pin was = fla::templates::pinOf(*instance);
    if (names.isEmpty() && !was.any())
    {
        QMessageBox::information(this, "Pin to Text",
            "There is no text field to pin to here.\n\nGive a dynamic text field beside this instance an instance name: "
            "then this instance can move or stretch with it as a template gives it other words.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Pin to Text");
    auto* form = new QFormLayout(&dialog);
    auto* field = new QComboBox(&dialog);
    field->addItem("Not pinned", QString());
    for (const QString& name : names)
        field->addItem(name, name);
    field->setCurrentIndex(std::max(0, field->findData(QString::fromStdString(was.text))));
    auto* mode = new QComboBox(&dialog);
    const std::vector<std::pair<QString, QString>> modes = {{"Stretch to its width", "width"},
        {"Stretch to its height", "height"}, {"Stretch to its width and height", "both"}, {"Move with its right edge", "right"},
        {"Move with its left edge", "left"}, {"Move with its bottom edge", "bottom"}, {"Move with its top edge", "top"}};
    for (const auto& [label, id] : modes)
        mode->addItem(label, id);
    mode->setCurrentIndex(std::max(0, mode->findData(QString::fromStdString(was.mode))));
    form->addRow("Text field", field);
    form->addRow("This instance", mode);
    auto* note = new QLabel("As the words a template gives the field make it longer or shorter than drawn, this instance keeps "
                            "the same room around them: a bar behind a name that fits it.", &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.resize(420, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;
    // While the dialog was up, the instance may have gone.
    if (!stillInDocument(_flaDocument, instance))
        return;
    fla::templates::Pin pin;
    pin.text = field->currentData().toString().toStdString();
    if (!pin.text.empty())
        pin.mode = mode->currentData().toString().toStdString();
    using Attributes = std::vector<std::pair<std::string, std::string>>;
    const Attributes after = fla::templates::withPin(*instance, pin);
    if (after == instance->extraAttributes)
        return;
    _editContext.commandStack().push(fla::CommandPtr(
        new fla::SetPropertyCommand<Attributes>(instance->extraAttributes, instance->extraAttributes, after, "Pin to Text")));
    _editContext.commandStack().breakMergeChain();
    if (_phoenixView)
    {
        _phoenixView->clearCaches();
        _phoenixView->update();
    }
}

void MainWindow::motionSketch()
{
    auto* instance = dynamic_cast<fla::SymbolInstance*>(_selection.single());
    if (!_phoenixView || !instance)
    {
        statusBar()->showMessage("Select one symbol instance to sketch its motion.", 4000);
        return;
    }
    statusBar()->showMessage("Press on the stage and move: the movie plays, and the instance follows where you point until you let go.");
    // The timeline plays from the press, and stops when the button comes up.
    const auto playStop = [this](bool play) {
        TimelinesPanel* panel = _timelineView ? _timelineView->timelines() : nullptr;
        if (panel && _player && _player->isPlaying() != play)
            panel->playStop();
    };
    const fla::FLADocument* sketched = _flaDocument;
    _phoenixView->beginMotionSketch([playStop]() { playStop(true); },
        [this, instance, playStop, sketched](const std::vector<std::pair<int, QPointF>>& points) {
        playStop(false);
        statusBar()->clearMessage();
        // Still the document, and the instance still in it: an undo or a
        // tab change while it waited may have taken it away.
        if (points.size() < 2 || !stillInDocument(sketched, instance))
            return;
        // How far from where the press began, as keys a frame on the
        // Recorded Motion behavior, which replaces one recorded before.
        std::string dx;
        std::string dy;
        for (const auto& [frame, at] : points)
        {
            const auto add = [frame](std::string& keys, double value) {
                char text[64];
                std::snprintf(text, sizeof(text), "%s%d=%.2f", keys.empty() ? "@" : ";", frame, value);
                keys += text;
            };
            add(dx, at.x() - points.front().second.x());
            add(dy, at.y() - points.front().second.y());
        }
        fla::behaviors::Applied recorded = BehaviorLibrary::instance().engine().defaults("recorded", 1);
        recorded.params["dx"] = dx;
        recorded.params["dy"] = dy;
        std::vector<fla::behaviors::Applied> list = fla::behaviors::of(*instance);
        list.erase(std::remove_if(list.begin(), list.end(), [](const fla::behaviors::Applied& each) { return each.type == "recorded"; }),
            list.end());
        list.push_back(recorded);
        using Attributes = std::vector<std::pair<std::string, std::string>>;
        const Attributes after = fla::behaviors::withBehaviors(*instance, list);
        _editContext.commandStack().push(fla::CommandPtr(new fla::SetPropertyCommand<Attributes>(
            instance->extraAttributes, instance->extraAttributes, after, "Motion Sketch")));
        _editContext.commandStack().breakMergeChain();
        _phoenixView->clearCaches();
        _phoenixView->update();
        statusBar()->showMessage(QString("Recorded %1 frames of motion.").arg(points.size()), 4000);
    });
}

void MainWindow::applyFillToStrokes()
{
    if (!_phoenixView || !_drawingStyle.hasFill)
        return;
    std::vector<fla::Shape*> shapes;
    for (fla::Element* element : selectedElements())
        if (element && element->elementType() == fla::Element::Type::Shape)
            shapes.push_back(static_cast<fla::Shape*>(element));
    if (shapes.empty())
    {
        statusBar()->showMessage("Select shapes with strokes.", 3000);
        return;
    }
    _phoenixView->flushPendingMerge();
    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro("Apply to Strokes");
    for (fla::Shape* shape : shapes)
    {
        std::unique_ptr<fla::Element> copy(fla::cloneElement(*shape, shape->parent));
        fla::Shape* after = dynamic_cast<fla::Shape*>(copy.get());
        if (!after || after->strokesMap.empty())
            continue;
        // Each stroke drawn with the Color panel's fill, fitted to its lines.
        for (auto& entry : after->strokesMap)
        {
            if (!entry.second)
                continue;
            fla::FillStyle* fill = _drawingStyle.createFill(entry.second);
            if (!fill)
                continue;
            delete entry.second->fill;
            entry.second->fill = fill;
        }
        fla::DrawingStyle::fitStrokes(*after);
        stack.push(fla::CommandPtr(new fla::SetShapeContentsCommand(shape, *after, "Apply to Strokes")));
    }
    stack.endMacro();
    stack.breakMergeChain();
    _phoenixView->clearCaches();
    _phoenixView->update();
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

    // Merge shapes stay below groups, symbols and drawing objects, as
    // drawn artwork lies under the objects on its layer: each kind moves
    // only within its own band of the stack.
    const auto merge = [](const fla::Element* element) {
        return element->elementType() == fla::Element::Type::Shape &&
            !static_cast<const fla::Shape*>(element)->isDrawingObject;
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
        const int merges = static_cast<int>(std::count_if(elements.begin(), elements.end(),
            [&](const fla::Element* element) { return element && merge(element); }));
        const int low = merge(entry.element) ? 0 : std::min(merges, index);
        const int high = merge(entry.element) ? std::max(merges - 1, index) : count - 1;
        const int target = toEnd ? (step > 0 ? high : low) : index + step;
        if (target == index || target < low || target > high || (!toEnd && selected(elements[target])))
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
    _clipboardFrom = _flaDocument ? _flaDocument->document : nullptr;
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
    return data && (data->hasFormat(kElementsMime) || data->hasImage() || data->hasFormat("image/svg+xml") ||
        data->hasFormat("application/pdf") ||
        (data->hasUrls() && !data->urls().isEmpty() && data->urls().front().isLocalFile()) ||
        (data->hasText() && !data->text().isEmpty()));
}

void MainWindow::putOnSystemClipboard()
{
    // Four forms: the elements as XFL, which another Phoenix window pastes
    // whole; a picture and an SVG of them for other applications; and a
    // lone text field's characters as plain text.
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
        std::vector<const fla::Element*> copied;
        for (const std::unique_ptr<fla::Element>& element : _clipboard)
            copied.push_back(element.get());
        const std::vector<fla::PaintBrushDefinition> brushes =
            _clipboardFrom ? fla::paintBrushesFor(*_clipboardFrom, copied) : std::vector<fla::PaintBrushDefinition>();
        if (!brushes.empty())
            data->setData(kPaintBrushesMime, QByteArray::fromStdString(fla::writePaintBrushDefinitions(brushes)));
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
        // And as vectors, for applications that paste SVG.
        const QByteArray svg = _phoenixView->svgOfElements(shown);
        if (!svg.isEmpty())
            data->setData("image/svg+xml", svg);
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
    else if (_flaDocument && _flaDocument->document && _phoenixView &&
             (data->hasFormat("image/svg+xml") || data->hasFormat("application/pdf") ||
                 (data->hasUrls() && !data->urls().isEmpty() && data->urls().front().isLocalFile())))
    {
        // Vectors from another application (SVG from a browser or an
        // editor, PDF from Illustrator), and files copied in a file
        // manager: imported to the stage as a drop of the file would be.
        QTemporaryDir folder;
        QStringList paths;
        if (data->hasFormat("image/svg+xml") || data->hasFormat("application/pdf"))
        {
            const bool svg = data->hasFormat("image/svg+xml");
            const QString path = folder.filePath(svg ? "Pasted.svg" : "Pasted.pdf");
            QFile file(path);
            if (!folder.isValid() || !file.open(QIODevice::WriteOnly) ||
                file.write(data->data(svg ? "image/svg+xml" : "application/pdf")) < 0)
                return false;
            file.close();
            paths << path;
        }
        else
        {
            for (const QUrl& url : data->urls())
                if (url.isLocalFile())
                    paths << url.toLocalFile();
        }
        const QPointF center = _phoenixView->mapToDocument(
            QPointF(_phoenixView->width() / 2.0, _phoenixView->height() / 2.0));
        importMedia(paths, true, centerInView ? &center : nullptr);
        return true;
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

    // The paint brushes the copied strokes draw with, when the copy carried
    // them, come into the document as one step with the paste, and the
    // strokes draw with them as it numbers them.
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::CommandStack& stack = _editContext.commandStack();
    bool macro = false;
    if (document && data->hasFormat(kPaintBrushesMime))
    {
        const std::vector<fla::PaintBrushDefinition> brushes =
            fla::readPaintBrushDefinitions(data->data(kPaintBrushesMime).toStdString());
        std::map<int, int> indexes;
        if (fla::CommandPtr command = fla::adoptPaintBrushes(*document, brushes, indexes, name))
        {
            if (_phoenixView)
                _phoenixView->flushPendingMerge();
            stack.beginMacro(name);
            macro = true;
            stack.push(std::move(command));
        }
        for (const std::unique_ptr<fla::Element>& element : elements)
            fla::remapPaintBrushes(*element, indexes);
    }

    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : elements)
        sources.push_back(element.get());
    placeCopies(sources, name, centerInView);
    if (macro)
        stack.endMacro();
    return true;
}

void MainWindow::placeCopies(const std::vector<const fla::Element*>& sources, const char* name,
    bool centerInView, bool acrossFrames)
{
    if (!_phoenixView || sources.empty())
        return;

    // Frames chosen on the timeline, the last thing worked in: the copies go
    // onto each keyframe they cover, on every unlocked layer chosen, in
    // place.
    QWidget* focused = QApplication::focusWidget();
    if (acrossFrames && _timelineView && focused && (focused == _timelineView || _timelineView->isAncestorOf(focused)))
    {
        const FrameSelection chosen = _timelineView->frameSelection();
        std::vector<fla::Frame*> targets;
        const auto add = [&targets](const fla::Layer* layer, int index) {
            if (!layer || layer->layerType == fla::Layer::Type::Folder || layer->isLocked())
                return;
            for (fla::Frame* frame : layer->frames)
                if (frame && index >= frame->index && index < frame->index + std::max(1, frame->duration))
                {
                    if (std::find(targets.begin(), targets.end(), frame) == targets.end())
                        targets.push_back(frame);
                    return;
                }
        };
        for (const fla::Layer* layer : chosen.layers)
            for (int index = chosen.first; index <= chosen.last; ++index)
                add(layer, index);
        for (const auto& [layer, index] : chosen.cells)
            add(layer, index);
        if (targets.size() > 1)
        {
            const fla::SymbolList* library = _flaDocument && _flaDocument->document
                ? _flaDocument->document->symbolList : nullptr;
            _phoenixView->flushPendingMerge();
            fla::CommandStack& stack = _editContext.commandStack();
            stack.beginMacro(name);
            for (fla::Frame* target : targets)
                for (const fla::Element* source : sources)
                {
                    fla::Element* copy = fla::cloneElement(*source, target);
                    if (!copy)
                        continue;
                    fla::relinkSymbols(*copy, library);
                    if (fla::placingNestsSymbol(*copy, _phoenixView->scopeSymbol()))
                    {
                        delete copy;
                        continue;
                    }
                    stack.push(fla::CommandPtr(new fla::AddElementCommand(target, copy, name, &_selection)));
                }
            stack.endMacro();
            stack.breakMergeChain();
            _phoenixView->clearCaches();
            _phoenixView->update();
            statusBar()->showMessage(QString("Pasted onto %1 frames.").arg(targets.size()), 3000);
            return;
        }
    }

    fla::Frame* frame = _phoenixView->activeFrame();
    if (!frame)
        return;

    // Anything still waiting to merge goes in first, so the copies land on
    // the artwork as it will be rather than on a drawing about to vanish.
    _phoenixView->flushPendingMerge();

    // A copy's instances point at this document's symbols, whichever
    // document the copy came from.
    const fla::SymbolList* library = _flaDocument && _flaDocument->document
        ? _flaDocument->document->symbolList : nullptr;
    std::vector<std::unique_ptr<fla::Element>> prepared;
    for (const fla::Element* source : sources)
    {
        fla::Element* copy = fla::cloneElement(*source, frame);
        if (!copy)
            continue;
        fla::relinkSymbols(*copy, library);
        prepared.emplace_back(copy);
        if (fla::placingNestsSymbol(*copy, _phoenixView->scopeSymbol()))
        {
            statusBar()->showMessage("What is being pasted contains the symbol being edited, "
                                     "which cannot go inside itself.", 4000);
            return;
        }
    }

    fla::CommandStack& stack = _editContext.commandStack();
    stack.beginMacro(name);
    std::vector<fla::Element*> copies;
    for (std::unique_ptr<fla::Element>& copy : prepared)
    {
        stack.push(fla::CommandPtr(new fla::AddElementCommand(frame, copy.get(), name, &_selection)));
        copies.push_back(copy.release());
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
    pasteElements("Paste", true);
}

void MainWindow::pasteInPlace()
{
    // With frames chosen on the timeline, onto each of them.
    pasteElements("Paste in Place", false, true);
}

void MainWindow::pasteElements(const char* name, bool centerInView, bool acrossFrames)
{
    if (pasteFromSystemClipboard(name, centerInView))
        return;
    std::vector<const fla::Element*> sources;
    for (const std::unique_ptr<fla::Element>& element : _clipboard)
        sources.push_back(element.get());
    if (sources.empty())
        return;

    // Copied from another document still open, in this window or another:
    // the symbols and bitmaps the copies use come along first, with what
    // they use in turn, as one step with the paste.
    fla::Document* document = _flaDocument ? _flaDocument->document : nullptr;
    fla::CommandStack& stack = _editContext.commandStack();
    std::vector<std::unique_ptr<fla::Element>> renamedCopies;
    bool macro = false;
    if (document && _clipboardFrom && _clipboardFrom != document && documentOpenAnywhere(_clipboardFrom))
    {
        const std::vector<std::string> used = fla::transfer::usedBy(sources);
        if (!fla::transfer::withDependencies(*_clipboardFrom, used).empty())
        {
            if (_phoenixView)
                _phoenixView->flushPendingMerge();
            stack.beginMacro(name);
            macro = true;
            std::map<std::string, std::string> renamed;
            if (!bringLibraryItems(*_clipboardFrom, used, renamed))
            {
                stack.endMacro();
                return;
            }
            // Items kept under new names: the copies name them so.
            if (std::any_of(renamed.begin(), renamed.end(), [](const auto& item) { return item.first != item.second; }))
            {
                for (const fla::Element* source : sources)
                    if (fla::Element* copy = fla::cloneElement(*source, nullptr))
                    {
                        fla::transfer::renameUses(*copy, renamed);
                        renamedCopies.emplace_back(copy);
                    }
                sources.clear();
                for (const std::unique_ptr<fla::Element>& copy : renamedCopies)
                    sources.push_back(copy.get());
            }
        }
        // The paint brushes the copies' strokes draw with come too, each once
        // by guid, and the copies draw with them as this document numbers
        // them.
        const std::vector<fla::PaintBrushDefinition> brushes = fla::paintBrushesFor(*_clipboardFrom, sources);
        if (!brushes.empty())
        {
            std::map<int, int> indexes;
            fla::CommandPtr command = fla::adoptPaintBrushes(*document, brushes, indexes, name);
            if (command)
            {
                if (!macro)
                {
                    if (_phoenixView)
                        _phoenixView->flushPendingMerge();
                    stack.beginMacro(name);
                    macro = true;
                }
                stack.push(std::move(command));
            }
            if (std::any_of(indexes.begin(), indexes.end(), [](const auto& index) { return index.first != index.second; }))
            {
                std::vector<std::unique_ptr<fla::Element>> remapped;
                for (const fla::Element* source : sources)
                    if (fla::Element* copy = fla::cloneElement(*source, nullptr))
                    {
                        fla::remapPaintBrushes(*copy, indexes);
                        remapped.emplace_back(copy);
                    }
                renamedCopies = std::move(remapped);
                sources.clear();
                for (const std::unique_ptr<fla::Element>& copy : renamedCopies)
                    sources.push_back(copy.get());
            }
        }
    }
    placeCopies(sources, name, centerInView, acrossFrames);
    if (macro)
        stack.endMacro();
}

bool MainWindow::bringLibraryItems(const fla::Document& source, const std::vector<std::string>& names,
    std::map<std::string, std::string>& renamed)
{
    QStringList list;
    for (const std::string& name : names)
        list << QString::fromStdString(name);
    bool canceled = false;
    copyLibraryItems(source, list, &renamed, &canceled);
    return !canceled;
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
    emit selectionStateChanged();
    refreshActions();
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
    if (_templateFieldsAction)
        _templateFieldsAction->setEnabled(hasInstance);
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
    // The list is every window's: another may have added to it since.
    _recentFiles = QSettings().value("recentFiles").toStringList();
    if (_recentFiles.length() > 0 && _recentFiles[0] == filePath)
        return;

    _recentFiles.removeAll(filePath);
    _recentFiles.prepend(filePath);

    while (_recentFiles.size() > MAX_RECENT_FILES)
    {
        _recentFiles.removeLast();
    }

    storeRecentFiles();
    saveSettings();
}

void MainWindow::forgetRecentFile(const QString& filePath)
{
    _recentFiles = QSettings().value("recentFiles").toStringList();
    _recentFiles.removeAll(filePath);
    storeRecentFiles();
}

void MainWindow::storeRecentFiles()
{
    QSettings().setValue("recentFiles", _recentFiles);
    for (MainWindow* window : s_windows)
    {
        window->_recentFiles = _recentFiles;
        window->updateRecentFilesMenu();
    }
    updateRecentFilesMenu();
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
    // Every setting, the newer ones too, as the presets write them.
    settings.setValue("text", QString::fromStdString(fla::toText(brush)));
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
    const QString text = settings.value("text").toString();
    if (!text.isEmpty())
        fla::fromText(text.toStdString(), brush);
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
    const QString brushStyle = settings.value("brushes/style").toString();
    if (!brushStyle.isEmpty())
    {
        // The painting options alone: the brush they come with is ignored.
        fla::BrushSettings ignored;
        fla::brushpresets::apply(brushStyle.toStdString(), ignored, _drawingStyle);
    }
    _drawingStyle.bitmapBrush = settings.value("brushes/bitmap", false).toBool();
    const int brushMode = settings.value("brushes/mode", 0).toInt();
    _drawingStyle.brushMode = brushMode >= 0 && brushMode <= 5
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
    settings.setValue("lastDirectory", _lastDirectory);
    settings.setValue("view/highQualityAntiAliasing", _highQualityAntiAliasing);
    saveBrush(settings, "brushes/fluid", _drawingStyle.fluidBrush);
    saveBrush(settings, "brushes/classic", _drawingStyle.classicBrush);
    saveBrush(settings, "brushes/ink", _drawingStyle.inkBrush);
    saveBrush(settings, "brushes/eraser", _drawingStyle.eraserBrush);
    settings.setValue("brushes/eraserSync", _drawingStyle.eraserSyncWithBrush);
    settings.setValue("brushes/mode", static_cast<int>(_drawingStyle.brushMode));
    settings.setValue("brushes/style", QString::fromStdString(fla::brushpresets::styleText(_drawingStyle)));
    settings.setValue("brushes/bitmap", _drawingStyle.bitmapBrush);
    // The window's place and the panels' layout are the first window's; a
    // window opened for tabs leaves them as they are.
    if (_secondary)
        return;
    settings.setValue("window/geometry", saveGeometry());
    if (_layoutRestored)
    {
        expandCollapsedPanels();
        settings.setValue("window/state", saveState(kLayoutVersion));
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Scripts being written are asked about first.
    if (_scriptWindow && _scriptWindow->isVisible() && !_scriptWindow->close())
    {
        event->ignore();
        return;
    }
    // The documents with unsaved changes are asked about, unless quitting
    // asked about every window's already.
    if (!s_quitting && !settleUnsaved({this}, this))
    {
        event->ignore();
        return;
    }
    // The last window to close says what to reopen next time.
    const bool last = s_windows.size() == 1 && s_windows.front() == this;
    if (!s_quitting && last)
        saveSession({this});

    saveSettings();
    // What was let go of needs no recovery copy, and while other windows
    // stay open, no memory either: nothing asks about it again.
    for (const std::unique_ptr<DocumentTab>& tab : _tabs)
        tab->autoSave.reset();
    if (!s_quitting && !last)
    {
        while (!_tabs.empty())
            closeTab(static_cast<int>(_tabs.size()) - 1, false);
    }
    s_windows.removeAll(this);
    event->accept();
    // An update the first window downloaded goes in once Phoenix has quit.
    if (!_secondary)
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

bool MainWindow::spectrumAt(const fla::Document& document, int frame, std::vector<double>& bands, std::vector<double>& wave)
{
    // Worked out with the loudness, and kept as long as it is.
    loudnessAt(document, frame);
    return _spectrum.at(frame, bands, wave);
}

double MainWindow::loudnessAt(const fla::Document& document, int frame)
{
    if (!_timelineAudio || !_flaDocument || _flaDocument->document != &document)
        return 0.0;
    // What the scene's sounds are, checked again only after an edit, and
    // the loudness worked out again only when they have changed.
    const unsigned revision = _phoenixView ? _phoenixView->revision() : 0;
    if (!_loudnessChecked || revision != _loudnessRevision)
    {
        _loudnessChecked = true;
        _loudnessRevision = revision;
        std::string sounds;
        const fla::Timeline* scene = document.currentScene();
        int length = 1;
        if (scene)
        {
            sounds = std::to_string(reinterpret_cast<uintptr_t>(scene));
            for (const fla::Layer* layer : scene->layers)
            {
                if (!layer)
                    continue;
                sounds += layer->visible ? "|v" : "|h";
                for (const fla::Frame* key : layer->frames)
                {
                    if (!key)
                        continue;
                    length = std::max(length, key->index + std::max(key->duration, 1));
                    for (const auto& [name, value] : key->extraAttributes)
                        if (name.rfind("sound", 0) == 0 || name.rfind("inPoint", 0) == 0 || name.rfind("outPoint", 0) == 0)
                            sounds += ";" + std::to_string(key->index) + ":" + std::to_string(key->duration) + name + "=" + value;
                }
            }
            sounds += "#" + std::to_string(length);
        }
        for (const fla::Document::SoundInfo& sound : document.sounds)
            sounds += "/" + sound.name;
        if (sounds != _loudnessSounds)
        {
            _loudnessSounds = sounds;
            _loudness.clear();
            const std::vector<float> mixed = scene ? _timelineAudio->mixdown(0, length - 1) : std::vector<float>();
            const double rate = document.frameRate > 0.0 ? document.frameRate : 24.0;
            const double perFrame = PcmBuffer::kRate / rate;
            double loudest = 0.0;
            for (int f = 0; f < length; ++f)
            {
                // The sound's power over the frame (stereo, interleaved).
                const size_t from = static_cast<size_t>(std::llround(f * perFrame)) * 2;
                const size_t to = std::min(mixed.size(), static_cast<size_t>(std::llround((f + 1) * perFrame)) * 2);
                double sum = 0.0;
                for (size_t i = from; i < to; ++i)
                    sum += static_cast<double>(mixed[i]) * mixed[i];
                const double power = to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0.0;
                _loudness.push_back(power);
                loudest = std::max(loudest, power);
            }
            // From 0 to 1 against the loudest frame; silence stays silent.
            for (double& each : _loudness)
                each = loudest > 1e-4 ? each / loudest : 0.0;
            // And its spectrum and wave, frame by frame.
            _spectrum = loudest > 1e-4 ? fla::audio::analyze(mixed, PcmBuffer::kRate, rate, length) : fla::audio::Analysis();
        }
    }
    return frame >= 0 && frame < static_cast<int>(_loudness.size()) ? _loudness[static_cast<size_t>(frame)] : 0.0;
}
