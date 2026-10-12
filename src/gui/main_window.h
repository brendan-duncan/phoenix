#pragma once

#include "phoenix_view.h"
#include "document_view.h"
#include "timeline_view.h"
#include "player.h"
#include "../data/fla_document.h"
#include "../data/static_text.h"
#include "free_transform_tool.h"
#include "brush_tools.h"
#include "lasso_tool.h"
#include "magic_wand_tool.h"
#include "gradient_transform_tool.h"
#include "anchor_point_tool.h"
#include "camera_tool.h"
#include "navigation_tools.h"
#include "paint_tools.h"
#include "pen_tool.h"
#include "pencil_tool.h"
#include "primitive_tool.h"
#include "subselection_tool.h"
#include "text_tool.h"
#include "selection_tool.h"
#include "properties_panel.h"
#include "tool_icons.h"
#include "../edit/edit_context.h"
#include "../edit/selection.h"
#include "../edit/drawing_style.h"
#include "../edit/snapping.h"

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QToolButton>

#include "../edit/editable_path.h"

#include <functional>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QString>
#include <QSplitter>
#include <QSettings>
#include <QStringList>

class QDockWidget;
class QDoubleSpinBox;

class Updater;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// Opens \a initialFile when given, else the most recent file.
    /// With \a startup false, nothing opens by itself and no update check
    /// runs, as for exporting from the command line.
    explicit MainWindow(const QString& initialFile = QString(), QWidget* parent = nullptr, bool startup = true);

    /// Opens \a source and exports all its frames to \a output: a PNG
    /// sequence for a .png name, a video otherwise. Returns the process exit
    /// code, writing any error to stderr.
    int exportFromCommandLine(const QString& source, const QString& output, bool perLayer = false);
    /// Phoenix render: frames of a document to a PNG sequence, a video or
    /// a GIF, with no window. See the command line help.
    int renderCommand(const QString& source, const QStringList& options);

    /// For the round-trip check: opens \a path and renders \a frames (when
    /// empty, \a samples frames spread over the document, chosen and
    /// returned there) at \a size (when empty, the stage's, no wider than
    /// 960). Empty when the file does not open.
    std::vector<QImage> renderSamples(const QString& path, std::vector<int>& frames, QSize& size, int samples);
    /// Phoenix --export-image in out [--frame N] [--size WxH]
    /// [--matte stage|transparent|#rrggbb] [--quality Q]: one frame, no
    /// window. Returns the exit code.
    int exportImageFromCommandLine(const QString& source, const QString& output, const QStringList& options);
   
    void loadFLAFile(const QString& filePath);

    /// Opens a document the system handed over (a double-click in Finder),
    /// asking first about unsaved changes.
    void openDocumentPath(const QString& filePath);

private slots:
    void newFile();
    /// The start screen: new from a preset, Open, or a recent file.
    void showStartDialog();
    /// Offers back work a crash left in the recovery folder. True when it
    /// opened some.
    bool offerRecovery();

    void openFile();

    void saveFile();

    void saveFileAs();

    void exportSvg();
    void exportMovie();
    /// File > Export > Export Image: one frame as PNG, JPEG or GIF.
    void exportImage();
    /// File > Import: image and sound files into the library and, for
    /// Import to Stage, images onto the current frame.
    void importToStage();
    void importToLibrary();
    /// File > Import > Import Video: a video as a reference to animate
    /// over, a still per frame on a guide layer with its sound.
    void importVideo();
    /// Shows and edits the scene at index (counting from 0) on its first
    /// frame, leaving any symbol being edited.
    void goToScene(int index);

    void openRecentFile();
    
    void quit();

    void showAbout();
    
    void viewDocument();
    
    void onVisibilityChanged();

    void onHighQualityAntiAliasingToggled(bool checked);

    void undo();

    void redo();

    void selectAll();

    void deselectAll();

    void cut();

    void copy();

    void paste();

    void pasteInPlace();

    void duplicateSelection();

    void groupSelection();

    void ungroupSelection();

    void breakApartSelection();

    void convertToSymbol();
    /// Modify > Symbol > Swap Symbol: the selected instances show another
    /// library symbol, keeping their transform, color and looping.
    void swapSymbol();

    /// Modify > Transform: the selection turned about the center of its
    /// bounds by \a change (a flip, a quarter turn, a scale and rotation).
    void transformSelection(const char* name, const QTransform& change);
    /// Modify > Transform > Scale and Rotate (Ctrl+Alt+S).
    void scaleAndRotate();
    /// Modify > Transform > Remove Transform: back to unscaled and unturned,
    /// staying where it is.
    void removeTransform();
    /// Modify > Timeline > Distribute to Layers (Ctrl+Shift+D): each
    /// selected object on a new layer of its own, under the current one.
    void distributeToLayers();
    /// Modify > Combine Objects: Union, Intersect, Punch, Crop on the
    /// selected shapes (operation as fla::combine::Operation).
    void combineObjects(int operation);
    /// Insert > New Symbol (Ctrl+F8): an empty symbol, opened for drawing.
    void newSymbol();
    /// Edit > Edit Symbols / Edit Document (Ctrl+E): the selected instance's
    /// symbol on its own, or back to the document from a symbol.
    void editSymbolsOrDocument();
    /// Modify > Arrange > Lock (Ctrl+Alt+L) and Unlock All
    /// (Ctrl+Alt+Shift+L): objects that stay put and cannot be picked.
    void lockSelection();
    void unlockAll();
    /// The stage's right-click menu.
    void showStageMenu(const QPoint& globalPos, const QPointF& documentPos);

    /// Modify > Shape: each rewrites every selected shape's paths as one
    /// undo step per shape.
    void smoothSelection();
    void straightenSelection();
    void optimizeSelection();
    void convertLinesToFillsSelection();
    void expandFillSelection();
    void softenFillEdgesSelection();

    /// The timeline being edited: the symbol entered, else the document's.
    fla::Timeline* editedTimeline() const;

    /// The frames a timeline command works on: the frames selected in the
    /// grid, else the current layer at the playhead.
    FrameSelection framesToEdit() const;

    /// Runs \a edit on each layer of the frames to edit as one undo step
    /// named \a name, and redraws what it changed.
    void editFrames(const char* name, const std::function<void(fla::Layer&, int first, int last,
        std::vector<fla::Frame*>& removed)>& edit);
    void insertFrame();
    void insertKeyframe(bool blank);
    void removeFrames();
    void clearKeyframe();
    void setClassicTween(bool on);
    void createShapeTween();
    void createMotionTween();
    /// Insert > Camera: the scene's multiplane camera layer.
    void addCamera();
    /// Layer parenting: \a layer follows \a parent (null for none) from
    /// now on, its objects staying where they are on every keyframe.
    void setParentLayer(fla::Timeline* timeline, fla::Layer* layer, fla::Layer* parent);
    /// Insert > Peg: a peg layer above the current one, which it follows.
    void addPeg();
    /// Insert > Color Art Layer: the current layer's
    /// fills to a color art layer under it, its strokes kept.
    void separateLineAndColor();
    /// Insert > Effect Layer: a tone, highlight or glow over the current
    /// layer; and an effect layer's settings.
    void addEffectLayer(int kind);
    void editEffect(fla::Layer* layer);
    /// Modify > Lip Sync: mouth shapes from a voice track onto the selected
    /// mouth graphic's layer.
    void lipSync();
    /// File > Import > Import Animatic: an EDL or Final Cut XML's cut as
    /// labeled shots, boards and placed audio.
    void importAnimatic();
    /// Adds a shape hint to the shape tween at the playhead, or removes all.
    void editShapeHints(bool add);
    void clearFrames();
    void distributeToKeyframes();
    /// Selects what is on \a layer at \a frame, as clicking the frame does.
    void pickFrameContents(const fla::Layer* layer, int frame);
    /// The object motion tween in hand: the selected object's, or the
    /// current layer's at the playhead; null when there is none.
    fla::Frame* currentMotionSpan();
    void copyMotion();
    /// Paste Motion onto the selected frames (or the current layer at the
    /// playhead); false when nothing took it.
    bool pasteMotion(const std::string& motion);
    void showMotionPresets();
    void moveKeyframe(const fla::Layer* layer, int from, int to);
    /// A span Ctrl-dragged to \a duration frames: frames go in or come out
    /// at its end, and what follows moves along.
    void resizeSpan(const fla::Layer* layer, int keyframe, int duration);
    void moveFrames(const std::vector<const fla::Layer*>& sources, const std::vector<const fla::Layer*>& targets, int first, int last, int to, bool copy);
    void copyFrames();
    void cutFrames();
    void pasteFrames();
    /// Paste and Overwrite Frames: the copies replace the frames from the
    /// selection on, nothing moving (Paste Frames moves them along).
    void pasteFramesOverwrite();
    void pasteFramesAs(bool overwrite);
    /// Modify > Timeline > Convert to Frame-by-Frame Animation and Reverse
    /// Frames.
    void convertToFrameByFrame();
    void reverseFrames();
    void showFramesMenu(const QPoint& globalPos);
    void addLayer(bool folder);
    void deleteCurrentLayer();
    void editLayers(fla::Timeline* timeline, const fla::LayerSnapshot& after, const QString& name);
    void renameLayer(const fla::Timeline* timeline, int layerIndex, const QString& name);

    /// Replaces each selected shape's contents with \a change applied to a
    /// copy, one undo step named \a name. A change that returns false
    /// leaves that shape alone.
    void reshapeSelectedShapes(const char* name, const std::function<bool(fla::Shape&, fla::Shape&)>& change);

    /// Applies \a tidy to every path of every selected shape.
    void tidySelectedShapes(const char* name, const std::function<void(fla::EditablePath&)>& tidy);

    /// Makes every selected text field static, dynamic or input.
    void setSelectedTextType(fla::StaticText::TextType type);

    /// Arrange: moves the selection in the z-order by \a step, or to the
    /// front or back when \a toEnd is set.
    void arrangeSelection(int step, bool toEnd);

private:
    void setupUI();

    void setupMenus();

    void setupToolBar();

    void setupPropertiesPanel();

    /// Refreshes the actions that only make sense with something selected.
    void updateSelectionState();

    /// Refreshes everything that depends on the undo history: the Edit menu
    /// entries and the modified marker in the title bar.
    void updateEditState();

    /// Rebuilds the title from the open file plus the modified marker.
    void updateWindowTitle();

    /// Offers to abandon unsaved changes. Returns false if the user cancels.
    bool confirmDiscardChanges();

    /// Lets go of the open document and deletes it, leaving nothing open.
    ///
    /// The selection and the undo history both hold pointers into the document,
    /// so both are detached first.
    void releaseDocument();

    /// Takes over  document and shows it, replacing whatever was open.
    ///
    /// Takes ownership. A null document leaves the window with nothing open,
    /// which is what a failed load wants.
    void adoptDocument(fla::FLADocument* document, const QString& filePath,
        const QString& displayName);

    /// Writes the document to \a filePath, as a .fla when the name ends in
    /// .fla and as an uncompressed XFL folder otherwise. Reports failures and
    /// updates the saved state.
    bool saveToPath(const QString& filePath);

    /// Warns when saving would drop content the writer cannot represent yet.
    /// Returns false if the user would rather not go ahead.
    bool confirmLossySave();

    void updateRecentFilesMenu();

    void addToRecentFiles(const QString& filePath);

    void loadSettings();

    void saveSettings();

    void closeEvent(QCloseEvent* event) override;

    /// A .fla or .xfl dropped on the window is opened.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    QIcon createPhoenixIcon();

    static const int MAX_RECENT_FILES = 10;

    fla::FLADocument* _flaDocument;
    fla::EditContext _editContext;
    fla::Selection _selection;
    fla::Snapper _snapper;
    std::unique_ptr<SelectionTool> _selectionTool;
    std::unique_ptr<FreeTransformTool> _freeTransformTool;
    std::unique_ptr<PrimitiveTool> _rectangleTool;
    std::unique_ptr<PrimitiveTool> _ovalTool;
    std::unique_ptr<PrimitiveTool> _lineTool;
    std::unique_ptr<PrimitiveTool> _polyStarTool;
    std::unique_ptr<PrimitiveTool> _rectanglePrimitiveTool;
    std::unique_ptr<PrimitiveTool> _ovalPrimitiveTool;
    std::unique_ptr<HandTool> _handTool;
    std::unique_ptr<ZoomTool> _zoomTool;
    std::unique_ptr<LassoTool> _lassoTool;
    std::unique_ptr<LassoTool> _polygonTool;
    std::unique_ptr<GradientTransformTool> _gradientTransformTool;
    std::unique_ptr<AnchorPointTool> _addAnchorTool;
    std::unique_ptr<AnchorPointTool> _deleteAnchorTool;
    std::unique_ptr<AnchorPointTool> _convertAnchorTool;
    std::unique_ptr<MagicWandTool> _magicWandTool;
    std::unique_ptr<RotationTool> _rotationTool;
    std::unique_ptr<TimeScrubTool> _timeScrubTool;
    std::unique_ptr<CameraTool> _cameraTool;
    /// The Flow Brush (the B key's), the Nib Brush and the Ink Brush,
    /// sharing the toolbox slot.
    std::unique_ptr<BrushTool> _brushTool;
    std::unique_ptr<BrushTool> _classicBrushTool;
    std::unique_ptr<BrushTool> _inkBrushTool;
    std::unique_ptr<EraserTool> _eraserTool;
    std::unique_ptr<PaintBucketTool> _paintBucketTool;
    std::unique_ptr<InkBottleTool> _inkBottleTool;
    std::unique_ptr<EyedropperTool> _eyedropperTool;
    QAction* _paintBucketAction = nullptr;
    QAction* _inkBottleAction = nullptr;

    /// The edit-in-place breadcrumb above the stage: the scene, then each
    /// symbol entered. Clicking a level steps back out to it.
    QWidget* _breadcrumb = nullptr;

    /// The outliner (the document tree at the left) folds away: by its
    /// arrow at the left of the breadcrumb row, View > Outliner, or by
    /// dragging the splitter to the edge. Its width is kept to unfold to.
    /// The docks that were fixed parts of the window: the outliner, the
    /// timeline and the tools.
    QDockWidget* _outlinerDock = nullptr;
    QDockWidget* _timelineDock = nullptr;
    QDockWidget* _toolsDock = nullptr;
    class Toolbox* _toolbox = nullptr;
    /// Edit Toolbar: the tools' slots rearranged.
    void editToolbar();
    /// The panels that open from the Window menu, made at the start.
    void createPanels();
    void setupWindowMenu();
    void fillThemeMenu(QMenu* menu);
    /// F4: hides every panel, or brings back the ones it hid.
    void togglePanels();
    QList<QDockWidget*> _hiddenByF4;

    void rebuildBreadcrumb();

    /// Redraws the fill and stroke swatches after the style changed from
    /// somewhere other than the swatch buttons, such as the eyedropper.
    void refreshStyleSwatches();

    /// The toolbox's own stroke and fill swatches, under the tools as
    /// Animate places them.
    QToolButton* _toolboxStrokeSwatch = nullptr;

    /// Each tool's toolbox action, so a tool can be chosen as a click on
    /// it would: checked, and shown in its slot.
    std::map<Tool*, QAction*> _toolActions;
    void chooseTool(Tool* tool);
    /// The tool to go back to when the pen is turned back from its eraser
    /// end.
    Tool* _toolBeforeEraserPen = nullptr;
    QToolButton* _toolboxFillSwatch = nullptr;

    /// Opens the color dialog for the stroke or the fill and applies the
    /// choice everywhere the color shows.
    void chooseStyleColor(bool stroke);
    std::unique_ptr<PenTool> _penTool;
    std::unique_ptr<PencilTool> _pencilTool;
    std::unique_ptr<TextTool> _textTool;
    std::unique_ptr<SubselectionTool> _subselectionTool;
    fla::DrawingStyle _drawingStyle;
    PropertiesPanel* _propertiesPanel = nullptr;
    class LibraryPanel* _libraryPanel = nullptr;
    class FramePickerPanel* _framePicker = nullptr;
    class AutoSave* _autoSave = nullptr;

    /// The onion skin's settings, kept between sessions.
    struct OnionSettings
    {
        int before = 2;
        int after = 2;
        bool outline = false;
        bool keyframesOnly = false;
        double opacity = 0.45;
        QColor beforeColor = QColor(0, 102, 255);
        QColor afterColor = QColor(0, 170, 60);
    };
    OnionSettings _onion;
    /// Hands the settings to the stage and the timeline, and keeps them.
    void applyOnion();
    /// The onion skin's settings menu, at \a globalPos.
    void showOnionMenu(const QPoint& globalPos);
    void fillOnionMenu(class QMenu* menu);
    class AlignPanel* _alignPanel = nullptr;
    class TransformPanel* _transformPanel = nullptr;
    class InfoPanel* _infoPanel = nullptr;
    QDockWidget* _alignDock = nullptr;
    QDockWidget* _transformDock = nullptr;
    QDockWidget* _infoDock = nullptr;
    class MotionEditorPanel* _motionEditor = nullptr;
    QDockWidget* _motionEditorDock = nullptr;
    class XSheetPanel* _xsheet = nullptr;
    QDockWidget* _xsheetDock = nullptr;
    class LayerDepthPanel* _layerDepth = nullptr;
    QDockWidget* _layerDepthDock = nullptr;
    /// The History panel: every step of the undo history, clicked to go
    /// back or forward to it.
    class QListWidget* _history = nullptr;
    QDockWidget* _historyDock = nullptr;
    /// Review Notes: notes on frames, kept on a guide layer.
    class NotesPanel* _notes = nullptr;
    QDockWidget* _notesDock = nullptr;
    /// Controllers: Harmony's master controllers.
    class ControllersPanel* _controllers = nullptr;
    QDockWidget* _controllersDock = nullptr;
    /// Palettes: Harmony's linked palettes.
    class PalettesPanel* _palettes = nullptr;
    QDockWidget* _palettesDock = nullptr;
    /// Edit > Find and Replace, made when first asked for.
    class FindReplaceDialog* _findReplace = nullptr;
    void showFindReplace();
    /// File > Check Document: the FLA doctor on the open document.
    void checkDocument();
    /// File > Compare With: the diff command against another version.
    void compareWith();
    /// File > Export > Shots: the shots command on the saved episode.
    void exportShots();
    /// The library's master-file links: set one, or bring linked symbols
    /// up to date (all of them when \a names is empty).
    void linkToMaster(const QString& name);
    void updateLinked(const QStringList& names);
    void refreshHistory();
    /// Edit > Repeat: the last Insert, Modify or Timeline command run, to
    /// run again on what is selected now.
    QAction* _repeatAction = nullptr;
    QPointer<QAction> _lastRepeatable;
    /// Workspaces: the dock layout as Phoenix first set it up, and the
    /// layouts saved by name.
    QByteArray _defaultLayout;
    bool _layoutRestored = false;
    void restoreLayout();
    void fillWorkspaceMenu(QMenu* menu);
    /// A workspace: the docks' arrangement, the tools' layout and the
    /// timeline's layers width, as JSON (what Export writes).
    QJsonObject currentWorkspace() const;
    /// False when the docks' arrangement could not be used.
    bool applyWorkspace(const QJsonObject& workspace);
    void applyBuiltInWorkspace(const QString& name);
    void useWorkspace(const QString& name);
    void saveWorkspaceAs(const QString& name);
    QStringList savedWorkspaces() const;
    /// Floating panels, and the window, brought back onto a screen.
    void keepOnScreen();
    /// What Copy Motion copied, a motionObjectXML.
    std::string _copiedMotion;
    QDockWidget* _framePickerDock = nullptr;
    class ColorPanel* _colorPanel = nullptr;
    QDockWidget* _colorDock = nullptr;
    class SwatchesPanel* _swatchesPanel = nullptr;
    QDockWidget* _swatchesDock = nullptr;

    /// The Color panel's Apply to Selection: the selected shapes' fills
    /// become what new art is filled with, fitted to each.
    void applyFillToSelection();

    /// Shows the selected graphic instance's frames in the Frame Picker.
    void updateFramePicker();
    /// The Frame Picker's choice of first frame, on a new keyframe with
    /// \a createKeyframe.
    void pickInstanceFrame(int frame, bool createKeyframe);

    /// Places an instance of a library item where it was dropped, centered
    /// there, as Animate does.
    void placeLibraryItem(const QString& kind, const QString& name, const QPointF& position);

    /// Brings image and sound files into the library as one undo step,
    /// placing the images on the current frame with \a toStage: their
    /// top-left at the stage's, or centered on \a at when given. Sounds go on
    /// the current keyframe when dropped on the stage.
    void importMedia(const QStringList& paths, bool toStage, const QPointF* at = nullptr);

    /// Import to Stage of a numbered sequence: each image in its own blank
    /// keyframe on the current layer, from the playhead on.
    void importSequence(const QStringList& paths);

    /// Puts a library sound on the keyframe the playhead is in on the
    /// current layer, as dragging one onto the stage does in Animate.
    void attachSound(const QString& name);

    /// ffmpeg's path, asking for it when it is not on the PATH; empty when
    /// the person did not say.
    QString findFfmpeg(const QString& title, const QString& why);

    /// Which toolbox icon stands for \a tool, for the Properties panel's
    /// Tool tab.
    ToolIcons::Tool iconForTool(const Tool* tool) const;

    /// True while one of the tree and the stage is being brought in line
    /// with the other, so the change does not bounce back.
    bool _syncingSelection = false;
    QAction* _selectAllAction = nullptr;
    /// The snapping switches the document keeps.
    QAction* _snapToObjectsAction = nullptr;
    QAction* _snapGuidesAction = nullptr;
    QAction* _deselectAllAction = nullptr;
    QAction* _cutAction = nullptr;
    QAction* _copyAction = nullptr;
    QAction* _pasteAction = nullptr;
    QAction* _pasteInPlaceAction = nullptr;
    QAction* _duplicateAction = nullptr;
    QAction* _groupAction = nullptr;
    QAction* _smoothAction = nullptr;
    QAction* _straightenAction = nullptr;
    QAction* _optimizeAction = nullptr;
    QAction* _linesToFillsAction = nullptr;
    QAction* _expandFillAction = nullptr;
    QAction* _softenFillAction = nullptr;
    /// Timeline commands on a right-click in the frames.
    QMenu* _framesMenu = nullptr;

    /// Frames copied with Copy Frames, one list of spans per layer copied,
    /// top layer first, each numbered from 0.
    std::vector<std::vector<std::unique_ptr<fla::Frame>>> _frameClipboard;

    /// Points the copied frames' symbol instances at \a library's symbols
    /// (at nothing for null).
    void relinkFrameClipboard(const fla::SymbolList* library);
    QAction* _onionSkinAction = nullptr;

    /// Plays the document's sounds with the timeline.
    class TimelineAudio* _timelineAudio = nullptr;
    QAction* _ungroupAction = nullptr;
    QAction* _breakApartAction = nullptr;
    QAction* _convertToSymbolAction = nullptr;
    QAction* _swapSymbolAction = nullptr;
    QMenu* _transformMenu = nullptr;
    std::vector<QAction*> _transformActions;
    QAction* _distributeAction = nullptr;
    std::vector<QAction*> _combineActions;
    QAction* _arrangeActions[4] = {nullptr, nullptr, nullptr, nullptr};

    /// Copies of the shapes last cut or copied. Only shapes can be copied so
    /// far, since nothing else can be cloned yet.
    /// What Copy took: copies of the elements, of any kind, in stacking
    /// order. The system clipboard carries them too (see copy()).
    std::vector<std::unique_ptr<fla::Element>> _clipboard;

    /// The shapes currently selected, in selection order.
    std::vector<fla::Shape*> selectedShapes() const;

    /// The elements currently selected, of any kind, in stacking order.
    std::vector<fla::Element*> selectedElements() const;

    /// Adds copies of \a sources to the current frame as one undo step and
    /// selects them. With \a centerInView the copies are moved so that they
    /// sit in the middle of the view, which is where Animate pastes.
    void placeCopies(const std::vector<const fla::Element*>& sources, const char* name,
        bool centerInView);

    /// The media type the system clipboard carries copied elements under,
    /// as an XFL document, between Phoenix windows.
    static constexpr const char* kElementsMime = "application/x-phoenix-xfl-elements";

    /// Puts what Copy took on the system clipboard as well.
    void putOnSystemClipboard();

    /// Pastes what another window or application put on the system
    /// clipboard: copied elements, or text as a text field. False when the
    /// clipboard holds this window's own copy, or nothing it can paste.
    bool pasteFromSystemClipboard(const char* name, bool centerInView);

    /// Whether Paste has something to paste.
    bool canPaste() const;
    QAction* _undoAction = nullptr;
    QAction* _redoAction = nullptr;
    /// Display name of the open file, without the modified marker.
    QString _documentName;
    /// Where the document was last saved or loaded from, for File > Save.
    QString _documentPath;
    Player* _player;
    Updater* _updater;
    DocumentView* _documentView;
    PhoenixView* _phoenixView;
    TimelineView* _timelineView;
    QMenu* _recentFilesMenu;

    QStringList _recentFiles;

    /// A new, empty document of that size, rate and stage color, in place
    /// of the one open.
    void createNewDocument(int width, int height, double frameRate, const QColor& stage);
    QString _lastDirectory;
    bool _highQualityAntiAliasing = true;
};
