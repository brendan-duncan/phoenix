#pragma once

#include "../edit/templates.h"
#include "phoenix_view.h"
#include "document_view.h"
#include "timeline_view.h"
#include "player.h"
#include "../data/fla_document.h"
#include "../data/bitmap.h"
#include "../edit/find_replace.h"
#include "../data/static_text.h"
#include "free_transform_tool.h"
#include "brush_tools.h"
#include "lasso_tool.h"
#include "magic_wand_tool.h"
#include "gradient_transform_tool.h"
#include "anchor_point_tool.h"
#include "camera_tool.h"
#include "bone_tool.h"
#include "shortcuts_dialog.h"
#include "asset_warp_tool.h"
#include "three_d_tool.h"
#include "width_tool.h"
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

#include <map>
#include <memory>
#include <vector>

#include <QJsonObject>
#include <QMainWindow>

#include "../edit/font_items.h"
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
class HotDoubleSpinBox;

class Updater;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// Opens \a initialFile when given, else the most recent file.
    /// With \a startup false, nothing opens by itself and no update check
    /// runs, as for exporting from the command line.
    explicit MainWindow(const QString& initialFile = QString(), QWidget* parent = nullptr, bool startup = true);
    ~MainWindow() override;

    /// Every window people work in, the one Phoenix started with first.
    static QList<MainWindow*> windows();
    /// Whether \a document is open in any window: shown, in a tab or in an
    /// external library.
    static bool documentOpenAnywhere(const fla::Document* document);

    /// Opens \a source and exports all its frames to \a output: a PNG
    /// sequence for a .png name, a video otherwise. Returns the process exit
    /// code, writing any error to stderr.
    int exportFromCommandLine(const QString& source, const QString& output, bool perLayer = false);
    /// Phoenix render: frames of a document to a PNG sequence, a video or
    /// a GIF, with no window. See the command line help.
    int renderCommand(const QString& source, const QStringList& options);
    /// Every command a key can call, with its default key.
    const std::vector<ShortcutEntry>& shortcutEntries() const { return _shortcuts; }

    /// For the round-trip check: opens \a path and renders \a frames (when
    /// empty, \a samples frames spread over the document, chosen and
    /// returned there) at \a size (when empty, the stage's, no wider than
    /// 960). Empty when the file does not open.
    std::vector<QImage> renderSamples(const QString& path, std::vector<int>& frames, QSize& size, int samples);
    /// For the benchmark: opens \a path as the editor does, then draws its
    /// first \a maxFrames frames at the stage's size twice, with nothing
    /// cached and then cached. Times in milliseconds; false when it does
    /// not open.
    bool benchmarkRender(const QString& path, int maxFrames, double& openMs, std::vector<double>& coldMs,
        std::vector<double>& warmMs, QSize& size);
    /// Phoenix --export-image in out [--frame N] [--size WxH]
    /// [--matte stage|transparent|#rrggbb] [--quality Q]: one frame, no
    /// window. Returns the exit code.
    int exportImageFromCommandLine(const QString& source, const QString& output, const QStringList& options);

    /// Phoenix --view <file>: the file in a player window of its own, no
    /// editor. A document (PHX, FLA, XFL, SWF) plays as Test Movie plays it;
    /// artwork and images (SVG, AI, PDF, PSD, PNG...) show on a stage made
    /// to fit them. Returns when the window closes; 1 when the file could
    /// not be shown.
    int viewFromCommandLine(const QString& path);
   
    void loadFLAFile(const QString& filePath);

    /// Opens a document the system handed over (a double-click in Finder),
    /// asking first about unsaved changes.
    void openDocumentPath(const QString& filePath);

    /// For the interactive tutorials (tutorial_runner.cpp): the parts a
    /// lesson points at and watches.
    PhoenixView* phoenixView() const { return _phoenixView; }
    TimelineView* timelineView() const { return _timelineView; }
    class Toolbox* toolbox() const { return _toolbox; }
    fla::CommandStack& commandStack() { return _editContext.commandStack(); }
    const fla::FLADocument* shownDocument() const { return _flaDocument; }
    /// Edit > Undo, whose text names the edit it undoes.
    QAction* undoAction() const { return _undoAction; }
    /// Whether \a document is open, shown or in another tab.
    bool hasDocument(const fla::FLADocument* document) const;
    /// The toolbox name of \a tool ("Oval", "Selection").
    QString toolName(const Tool* tool) const;
    /// What is selected on the stage: "none", "shape", "instance", "text",
    /// "group" or "other" for the first selected object.
    QString selectionKind() const;
    /// A lesson's document: a new one, or an untitled copy of \a path, in a
    /// tab of its own.
    void newLessonDocument(int width, int height, double frameRate);
    void openLessonDocument(const QString& path);
    /// Help > Tutorials, and starting a lesson by its id.
    void showTutorials();
    void startTutorial(const QString& id);
    /// The next window starts without the start screen, as for a lesson
    /// asked for on the command line.
    static void skipStartScreen();

    /// For scripts and AI assistants (script_api.cpp): everything shown
    /// brought up to date after a change made without the mouse, and
    /// whether the document is one from a newer Phoenix, kept read-only.
    void refreshAfterScript();
    bool isReadOnlyDocument() const;
    /// JSFL scripts run in this window (Commands menu, Output panel,
    /// Phoenix run-script).
    class ScriptRunner* scripts() const { return _scripts; }
    friend class ScriptApi;
    friend class ScriptRunner;
    friend class JsflHost;
    friend struct ScriptContext;
    /// Serves the AI assistant that started Phoenix (`Phoenix --mcp`) on
    /// standard input and output.
    void serveMcpOnStdio();

signals:
    /// The history changed: an edit made, undone or redone.
    void editStateChanged();
    void selectionStateChanged();

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
    /// The frames of the scene, or of the library symbol \a symbol, on one
    /// image with a description beside it.
    void exportSpriteSheet(const QString& symbol = QString());
    /// File > Publish Settings and File > Publish: the formats the document's
    /// publish settings ask for, written beside the document.
    void publishSettings();
    void publish();
    /// Publishes the shown document beside its file as its publish
    /// settings say, with no dialogs: the files written and what went
    /// wrong. False when something could not be written.
    bool publishFiles(QStringList& written, QStringList& problems);
    /// The current scene as an SWF movie at \a size: its frames as vector
    /// shapes, images and what filters and blends make as bitmaps. False
    /// with \a error when it could not be written.
    bool exportSwf(const QString& path, const QSize& size, QString& error);
    /// Tells scripts listening for \a type (fl.addEventListener), and
    /// JSFL tools, that it happened.
    void scriptEvent(const char* type);
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
    /// The selected instances' template fields, filled in.
    void editTemplateFields();
    /// File > Export > Render from Data.
    void renderFromData();
    /// Each row of \a rows rendered to \a folder; false with \a error when
    /// one could not be.
    bool renderRows(const std::vector<fla::templates::Values>& rows, const QString& folder, const QString& extension,
        QString& error, const std::function<bool(int row, int rows)>& progress = {});

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

    /// The Bone tool's bone: a new armature of the two instances on a pose
    /// layer above the first one's layer, or a bone more on the armature the
    /// first one is in, as one undo step.
    void addBone(const BoneTool::Link& link);
    /// A bone in a shape: the first makes the shape an ik container symbol
    /// on a new pose layer, its points bound to their nearest bones; one
    /// from a joint of a boned shape adds to its armature and binds every
    /// point again.
    void addShapeBone(const BoneTool::Link& link);
    /// The Bind tool's change to which bones a boned shape's points follow,
    /// with every frame of its span bent again by it, as one undo step.
    void rebindShape(fla::Frame* frame, size_t child, const fla::ShapeBinding& binding);

    /// How far a joint of the armature on \a frame may turn, in a dialog.
    void editJointLimits(fla::Frame* frame, size_t child, int joint);

    /// Every stroke style in the document's shapes, main timelines and
    /// symbols alike.
    std::vector<fla::StrokeStyle*> allStrokes() const;

    /// Window > Document Brushes: the art and pattern brushes the
    /// document's strokes are drawn with, how many strokes use each, and
    /// taking one off them or drawing with it.
    void manageBrushes();

    /// The library's bitmap and sound items: their settings, reading a
    /// bitmap again from its file, and editing one in another program.
    fla::Bitmap* bitmapNamed(const QString& name) const;
    void bitmapProperties(const QString& name);
    void updateBitmaps(const QStringList& names);
    /// Gives \a bitmap the pixels of the image file at \a path, as one
    /// undo step; false with a message when it cannot be read.
    bool reimportBitmap(fla::Bitmap* bitmap, const QString& path, const char* name);
    void editBitmapWith(const QString& name);
    void soundProperties(const QString& name);

    /// File > Import > Open External Library: another file's library,
    /// read-only, in a panel of its own, its items copied in by dragging
    /// them to the stage or with Copy to Document.
    void openExternalLibrary();

    /// Modify > Bitmap > Trace Bitmap: the selected bitmaps turned into
    /// vector artwork, as one undo step.
    void traceBitmaps();
    /// Control > Test Movie: the scene played with its scripts.
    void testMovie();

    /// Edit Envelope for a keyframe's sound, as one undo step.
    void editEnvelope(fla::Frame* frame);
    /// Modify > Timeline > Split Audio: the stream sound on the current
    /// layer cut at the playhead.
    void splitAudio();
    /// Copies \a names and what they use from \a source into the open
    /// document, asking what to do with items it already has (Resolve
    /// Library Conflicts). The names they have here, empty when nothing
    /// was copied.
    /// \a renamed, when given, gets the name here of each item and of what
    /// it uses; \a canceled whether the person canceled.
    std::vector<std::string> copyLibraryItems(const fla::Document& source, const QStringList& names,
        std::map<std::string, std::string>* renamed = nullptr, bool* canceled = nullptr);
    /// Whether \a document is still open, in a tab or an external library.
    bool documentAlive(const fla::Document* document) const;
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
    /// Modify > Document: stage size, Match Contents, scaling the content
    /// with the stage, and ruler units.
    void documentSettings();
    /// View > Safe Zone Settings: the dialog, previewed live, one undo step.
    void safeZoneSettings();
    /// Modify > Bitmap > Swap Bitmap: the selected bitmaps show another
    /// library bitmap, keeping their place and size.
    void swapBitmap();
    /// A text setting from Properties, for the text being typed, the text
    /// fields selected, or else new text.
    void applyTextStyle(const QString& key, const QString& value);
    /// The fonts the document's text names that are not installed.
    QStringList missingFonts() const;
    /// Font Mapping: an installed font for each missing one; with
    /// \a onlyMissing, the fonts this document is missing, as a warning on
    /// opening it.
    void fontMappingDialog(bool onlyMissing);
    /// Library font symbols: a new one (an empty name) or one's settings,
    /// in a dialog; then the text layout learns the document's symbols.
    void fontItemDialog(const QString& name);
    void syncFontSymbols();
    /// Modify > Shape > Advanced Smooth and Advanced Straighten: Smooth with
    /// angle limits and a strength, Straighten with a strength and shape
    /// recognition.
    void advancedSmooth();
    void advancedStraighten();
    /// Edit > Check Spelling: every text field of every scene and symbol,
    /// a misspelled word at a time.
    void checkSpelling();
    /// Every text field of the document, scenes first, then the symbols.
    std::vector<fla::StaticText*> allTexts() const;
    /// Text set in one face is set in another, as when a font symbol is
    /// renamed.
    void renameFontFaces(const std::string& from, const std::string& to);
    /// Replaces the document's font symbols, undoably.
    void setFontItems(const std::vector<fla::fonts::FontItem>& items, const char* what);
    /// Shows the Missing Fonts dialog when the document just opened names
    /// fonts that are neither installed nor mapped.
    void warnAboutFonts();
    /// Modify > Deformer: 0 removes, 1 adds a curve, 2 an envelope, 3 resets the pose.
    void deformSelection(int what);
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
    /// Bake to Keyframes: the selected frames' behaviors left as keyframes
    /// (Modify > Timeline), or the whole runs of the selected instances
    /// (the Behaviors panel).
    void bakeBehaviors();
    void bakeSelectedBehaviors();
    /// File > Export > Export FLA for Other Apps: a copy with every
    /// behavior baked, the document itself left as it is.
    void exportBakedDocument();
    /// The shapes with operators on frames \a first to \a last of
    /// \a layer, baked: a keyframe a frame, each shape left as drawing
    /// objects as its operators make it there. Returns how many were baked.
    int bakeShapeOperators(fla::Layer& layer, int first, int last);
    /// The frames of \a layer whose keyframes hold shapes with operators,
    /// as runs (first and last frame).
    static std::vector<std::pair<int, int>> shapeOperatorRuns(const fla::Layer& layer);
    /// \a text as its letter animators leave it on frame \a at: a text field
    /// for each letter shown, placed, turned, scaled and faded. The caller
    /// owns them.
    std::vector<fla::StaticText*> bakedText(const fla::StaticText& text, int at);
    void reverseFrames();
    void showFramesMenu(const QPoint& globalPos);
    void addLayer(bool folder);
    void deleteCurrentLayer();

    /// Document tabs: every open document has a tab over the stage. The one
    /// shown is the window's document; the others wait with their undo
    /// history, file and frame, and come back as they were.
    void openInTab(const QString& path);
    /// Puts the document shown aside so a new one can open in a tab of its
    /// own; a blank, unsaved, unchanged one is just replaced.
    void makeRoomForDocument();
    void activateTab(int index);
    /// Closes the tab, asking about unsaved changes first; false when the
    /// person cancels.
    bool closeTab(int index, bool ask = true);
    void refreshTabs();
    /// File > Revert: the saved file again, its changes dropped.
    void revertDocument();
    /// Templates: documents kept in Phoenix's templates folder, each the
    /// start of new documents. Save as Template keeps the one shown; New
    /// from Template opens a copy of one, untitled, in a tab.
    static QString templatesFolder();
    void saveAsTemplate();
    void newFromTemplate();
    /// Cut (0), Copy (1), Paste (2) or Duplicate (3) the layers picked.
    void layerClipboard(int action);
    /// Deletes the layers at \a indices with what they hold, as one step;
    /// false (and nothing done) when no layer would be left to draw on.
    bool removeLayers(fla::Timeline* timeline, const std::vector<int>& indices, const char* name);
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
    /// A window people use, rather than one a command line runs headless.
    bool _interactive = true;
    QAction* _fontMappingAction = nullptr;
    QAction* _safeZonesAction = nullptr;
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
    /// Writes \a document to \a filePath in the format its name says (.phx,
    /// .fla, else an XFL folder), asking nothing; false with \a error when
    /// it could not.
    bool writeDocumentFile(const fla::Document& document, const QString& filePath, QString& error);

    /// Warns when saving would drop content the writer cannot represent yet.
    /// Returns false if the user would rather not go ahead.
    bool confirmLossySave();

    void updateRecentFilesMenu();

    void addToRecentFiles(const QString& filePath);
    /// The recent files are every window's: written to the settings and
    /// shown in each window's menu.
    void forgetRecentFile(const QString& filePath);
    void storeRecentFiles();

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
    std::unique_ptr<BoneTool> _boneTool;
    std::unique_ptr<BindTool> _bindTool;
    std::unique_ptr<AssetWarpTool> _assetWarpTool;
    std::unique_ptr<ThreeDTool> _rotate3DTool;
    std::unique_ptr<ThreeDTool> _translate3DTool;
    std::unique_ptr<WidthTool> _widthTool;
    std::unique_ptr<PencilTool> _paintBrushTool;
    /// The Brush (Shift+B), the Nib Brush and the Ink Brush,
    /// sharing the toolbox slot.
    std::unique_ptr<BrushTool> _brushTool;
    std::unique_ptr<BrushTool> _classicBrushTool;
    std::unique_ptr<BrushTool> _inkBrushTool;
    /// Brush Properties, open beside the stage while it is shown.
    QPointer<class BrushPropertiesDialog> _brushProperties;
    /// Window > Render Queue, made when first asked for and kept.
    QPointer<class RenderQueue> _renderQueue;
    void openBrushProperties();
    std::unique_ptr<EraserTool> _eraserTool;
    std::unique_ptr<PaintBucketTool> _paintBucketTool;
    std::unique_ptr<InkBottleTool> _inkBottleTool;
    std::unique_ptr<EyedropperTool> _eyedropperTool;
    QAction* _paintBucketAction = nullptr;
    QAction* _inkBottleAction = nullptr;

    /// The edit-in-place breadcrumb above the stage: the scene, then each
    /// symbol entered. Clicking a level steps back out to it.
    QWidget* _breadcrumb = nullptr;
    /// The row the breadcrumb is in, with the stage controls at its right.
    QWidget* _breadcrumbRow = nullptr;

    /// The outliner (the document tree at the left) folds away: by its
    /// arrow at the left of the breadcrumb row, View > Outliner, or by
    /// dragging the splitter to the edge. Its width is kept to unfold to.
    /// The docks that were fixed parts of the window: the outliner, the
    /// timeline and the tools.
    QDockWidget* _outlinerDock = nullptr;
    QDockWidget* _timelineDock = nullptr;
    QDockWidget* _toolsDock = nullptr;
    class Toolbox* _toolbox = nullptr;
    /// Saving as FLA or XFL: lists the Phoenix-only features the document
    /// uses, kept in the file but left out by other FLA editors, and asks.
    bool confirmPhoenixFeatures();
    /// Documents whose Phoenix features were acknowledged for the session.
    QSet<const fla::FLADocument*> _featuresAcknowledged;
    /// The lesson running, if one is (Help > Tutorials).
    class TutorialRunner* _tutorial = nullptr;
    /// AI assistants' way in (MCP), in the window a person works in.
    class McpServer* _mcpServer = nullptr;
    // Commands > Open Claude Code: made the first time it is asked for.
    QDockWidget* _claudeDock = nullptr;
    class ClaudeCodePanel* _claudePanel = nullptr;
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

    /// The toolbox's own stroke and fill swatches, under the tools.
    QToolButton* _toolboxStrokeSwatch = nullptr;

    /// Each tool's toolbox action, so a tool can be chosen as a click on
    /// it would: checked, and shown in its slot.
    std::map<Tool*, QAction*> _toolActions;
    /// Every tool's action, as the toolbox made it, with how to reach its
    /// tool: the toolbox is made before the tools, so _toolActions is filled
    /// from these once they exist.
    std::vector<std::pair<std::function<Tool*()>, QAction*>> _toolActionList;
    /// JSFL tools from the Tools folder.
    std::vector<std::unique_ptr<class ScriptTool>> _scriptTools;
    /// Every command with a key, with Phoenix's own keys, for Edit >
    /// Keyboard Shortcuts.
    std::vector<ShortcutEntry> _shortcuts;
    /// Bitmaps open in another program: the file each was written to, by
    /// bitmap name, watched for saves.
    class QFileSystemWatcher* _editedBitmaps = nullptr;
    /// Bitmaps out in another program: the file, and the document the
    /// bitmap is in, which alone takes the change back.
    struct EditedBitmap
    {
        QString file;
        const fla::Document* document = nullptr;
    };
    std::map<QString, EditedBitmap> _editedBitmapFiles;
    /// Libraries of other files opened beside the document.
    struct ExternalLibrary
    {
        std::unique_ptr<fla::FLADocument> document;
        QDockWidget* dock = nullptr;
        class LibraryPanel* panel = nullptr;
    };
    std::vector<std::unique_ptr<ExternalLibrary>> _externalLibraries;
    /// Library items copied, to paste into another document's library, in
    /// this window or another.
    static const fla::Document* _libraryClipboardSource;
    static QStringList _libraryClipboard;
    /// An item being dragged from a library, an external one or another
    /// window's, onto the stage or into the library: from another document,
    /// it is copied in with what it uses.
    static const fla::Document* _externalDragSource;
    static QString _externalDragName;
    /// Copies \a names (library items) from \a source in, as Paste does
    /// for what it pastes: asking about clashes, and giving the name each
    /// has here in \a renamed. False when the person cancels.
    bool bringLibraryItems(const fla::Document& source, const std::vector<std::string>& names,
        std::map<std::string, std::string>& renamed);
    /// Paste and Paste in Place from the copies Phoenix holds.
    void pasteElements(const char* name, bool centerInView);
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
        /// Anchor Onion: the range stays on these frames as the playhead
        /// moves, rather than going with it.
        bool anchored = false;
        int anchorFirst = 0;
        int anchorLast = 0;
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
    // Window > Actions, and Commands > Script Editor.
    QDockWidget* _actionsDock = nullptr;
    class ActionsPanel* _actionsPanel = nullptr;
    class ScriptWindow* _scriptWindow = nullptr;
    /// The Actions panel follows the selection, the playhead and edits,
    /// while it is open.
    void refreshActions();
    void openScriptEditor(const QString& path = QString());
    /// The Actions panel shows \a source's script (a keyframe or an
    /// instance), at \a line.
    void showScript(const fla::DOMElement* source, int line);
    /// The Asset Warp tool's first pin on art: the art made a puppet in its
    /// place, one undo step; its instance, or null.
    fla::SymbolInstance* makePuppet(fla::Element* art, fla::Frame* frame);
    /// The armature pose span at the playhead on the current layer: its
    /// IKTree's place among its kept children, and how far into the span
    /// the playhead is; null on a layer that is not an armature's.
    fla::Frame* currentPoseSpan(size_t& child, int& into);
    enum class PoseCommand
    {
        Insert,
        Copy,
        Paste,
        Clear,
    };
    /// The timeline's Pose commands, on the armature at the playhead.
    void poseCommand(PoseCommand command);
    /// The joints' angles Copy Pose took.
    std::vector<double> _copiedPose;
    /// Window > Collapse ... Panels to Icons: the panels docked on \a area
    /// folded to a strip of their names beside the stage, each opening
    /// beside it; or put back.
    void setPanelsCollapsed(Qt::DockWidgetArea area, bool collapsed);
    /// Every side put back, as layouts are saved.
    void expandCollapsedPanels();
    struct CollapsedSide
    {
        class QToolBar* strip = nullptr;
        std::vector<QDockWidget*> docks;
    };
    std::map<int, CollapsedSide> _collapsed;
    std::map<int, QAction*> _collapseActions;
    /// Review Notes: notes on frames, kept on a guide layer.
    class NotesPanel* _notes = nullptr;
    QDockWidget* _notesDock = nullptr;
    /// Controllers: master controllers.
    class ControllersPanel* _controllers = nullptr;
    QDockWidget* _controllersDock = nullptr;
    class BehaviorsPanel* _behaviors = nullptr;
    QDockWidget* _behaviorsDock = nullptr;
    struct DocumentTab
    {
        /// Null while it is the document shown.
        fla::FLADocument* document = nullptr;
        fla::CommandStack history;
        QString path;
        QString name;
        int frame = 0;
        /// The zoom, pan and turn it was left at.
        PhoenixView::ViewState view;
        /// Its recovery copy, written while it has unsaved changes, shown
        /// or not. Null in a window that runs headless.
        std::unique_ptr<class AutoSave> autoSave;
        /// The window it is in, which a tab moved to another window changes.
        MainWindow* owner = nullptr;
    };
    std::vector<std::unique_ptr<DocumentTab>> _tabs;
    int _activeTab = -1;
    class DocumentTabBar* _tabBar = nullptr;
    bool _switchingTabs = false;
    /// Takes the document shown off the window, keeping it whole.
    void parkActiveDocument();
    /// What the window keeps about a document beyond the panels (copied
    /// layers, bitmaps out in an editor), let go as the document closes.
    void forgetDocument(const fla::FLADocument* document);
    /// A new tab at the end, with its recovery copy.
    DocumentTab& addTab();
    /// The recovery copy of \a tab, reading the document wherever it is.
    void giveAutoSave(DocumentTab& tab);
    /// The tab's document, file, name and whether it has unsaved changes,
    /// whether it is the one shown or one waiting.
    const fla::Document* tabDocument(const DocumentTab& tab) const;
    QString tabPath(const DocumentTab& tab) const;
    QString tabName(const DocumentTab& tab) const;
    bool tabModified(const DocumentTab& tab) const;
    class AutoSave* activeAutoSave() const;
    /// The right-click menu of the tab at \a index.
    void showTabMenu(int index, const QPoint& globalPos);
    /// Closes every tab but \a keep, or every tab, asking about unsaved
    /// changes as each goes; false when the person cancels.
    bool closeOtherTabs(int keep);
    bool closeAllTabs();
    /// Shows \a path selected in Explorer or the Finder.
    static void revealInFileManager(const QString& path);
    /// Ctrl+Tab and Ctrl+Shift+Tab.
    void cycleTabs(int step);
    /// Takes the tab at \a index out of this window, document, history,
    /// view and all, showing another tab in its place; and puts one in
    /// at \a at, shown.
    std::unique_ptr<DocumentTab> detachTab(int index);
    void attachTab(std::unique_ptr<DocumentTab> tab, int at = -1);
    /// Move to New Window and Move to Window: the tab's document in
    /// another window, which a new one opens at \a globalPos when given.
    void moveTabToNewWindow(int index, const QPoint* globalPos = nullptr);
    void moveTabToWindow(int index, MainWindow* target, int at = -1);
    /// A tab dragged off the bar and let go: onto another window's tab bar
    /// it moves there, anywhere else to a window of its own.
    void tabDraggedOut(int index);
    friend class DocumentTabBar;
    /// Another window to hold documents: its own stage, timeline and
    /// panels, no start screen. It goes when it closes.
    static MainWindow* openSecondaryWindow();
    /// A window opened to take a tab, rather than the one Phoenix started
    /// with: it writes no layout or window place to the settings.
    bool _secondary = false;
    /// Quitting: every window's unsaved documents settled at once, then
    /// every window closed.
    static bool s_quitting;
    void quitAll();
    /// Asks about the unsaved documents in \a windows: one at a time, or
    /// Save All, Discard All or Cancel when there are several. False when
    /// the person cancels.
    static bool settleUnsaved(const QList<MainWindow*>& windows, QWidget* parent,
        const DocumentTab* except = nullptr);
    /// The documents open in every window, for Reopen documents from last
    /// time; and opening them again.
    static void saveSession(const QList<MainWindow*>& windows);
    bool reopenSession();
    /// Lets go of what \a tab's document keeps read in memory while it
    /// waits (bitmaps, sounds); they are read again when it is shown.
    void releaseParkedMedia(DocumentTab& tab);

    /// Copy Layers: copies of the layers, for the document they came from.
    std::vector<std::unique_ptr<fla::Layer>> _copiedLayers;
    const fla::Document* _copiedLayersFrom = nullptr;
    /// Scene: the document's scenes.
    class ScenePanel* _scenes = nullptr;
    QDockWidget* _scenesDock = nullptr;
    /// Movie Explorer: the document as a tree, by scene and symbol.
    class MovieExplorer* _movieExplorer = nullptr;
    QDockWidget* _movieExplorerDock = nullptr;
    /// The scene the editor was last set up for.
    const fla::Timeline* _shownScene = nullptr;
    /// Function Curves: any property of the current layer over time.
    class FunctionCurvesPanel* _functionCurves = nullptr;
    QDockWidget* _functionCurvesDock = nullptr;
    /// Palettes: linked palettes.
    class PalettesPanel* _palettes = nullptr;
    QDockWidget* _palettesDock = nullptr;
    /// JSFL scripts: the Commands menu and the Output panel.
    class ScriptRunner* _scripts = nullptr;
    /// Edit > Find and Replace, made when first asked for.
    class FindReplaceDialog* _findReplace = nullptr;
    void showFindReplace();
    /// Shows where \a match is: its scene or symbol, its frame, the object
    /// selected.
    void goToMatch(const fla::find::Match& match);
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

    /// A color picked for drawing goes onto the selected shapes too: their
    /// fills, or their strokes.
    void recolorSelection(bool stroke);

    /// Shows the selected graphic instance's frames in the Frame Picker.
    void updateFramePicker();
    /// The Frame Picker's choice of first frame, on a new keyframe with
    /// \a createKeyframe.
    void pickInstanceFrame(int frame, bool createKeyframe);

    /// Places an instance of a library item where it was dropped, centered
    /// there.
    void placeLibraryItem(const QString& kind, const QString& name, const QPointF& position);

    /// Brings image and sound files into the library as one undo step,
    /// placing the images on the current frame with \a toStage: their
    /// top-left at the stage's, or centered on \a at when given. Sounds go on
    /// the current keyframe when dropped on the stage.
    /// \a problems, when given, gets what went wrong rather than a message.
    void importMedia(const QStringList& paths, bool toStage, const QPointF* at = nullptr, QStringList* problems = nullptr);
    /// Artwork or an image on a new stage of its own, fitted to it, for the
    /// viewer and the command line. False when nothing came in; \a problems
    /// gets what the import said.
    bool openArtwork(const QString& path, QStringList& problems);

    /// One SVG, Illustrator or PDF file, as drawing objects grouped on the
    /// stage (or a graphic symbol in the library), or a Photoshop document,
    /// as a layer for each of its layers and a folder for each group (or
    /// its layers' bitmaps in the library). Part of importMedia's step.
    void importArtwork(const QString& path, bool toStage, const QPointF* at, fla::Frame* frame,
        QStringList& problems, std::vector<fla::DOMElement*>& placed);

    /// Import to Stage of a numbered sequence: each image in its own blank
    /// keyframe on the current layer, from the playhead on.
    void importSequence(const QStringList& paths);

    /// Puts a library sound on the keyframe the playhead is in on the
    /// current layer, as dragging one onto the stage does.
    void attachSound(const QString& name);
    /// The same on \a layer at \a frame: where a sound dropped on the
    /// timeline goes.
    void attachSoundTo(fla::Layer* layer, int frame, const QString& name);

    /// The ffmpeg that ships with Phoenix; when it is missing, says so with
    /// \a title and gives an empty path.
    QString findFfmpeg(const QString& title);

    /// Ends whatever a tool or a panel is in the middle of, before an undo or
    /// redo moves the history under it.
    void settleGestures();

    /// After an undo or redo: lets go of whatever the document no longer
    /// holds (stage scope, selection, outliner, explorer and search results).
    void pruneToDocument();

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
    /// Shared by every window, with the document they came from.
    static std::vector<std::vector<std::unique_ptr<fla::Frame>>>& _frameClipboard;
    static const fla::Document* _frameClipboardFrom;

    /// Points the copied frames' symbol instances at \a library's symbols
    /// (at nothing for null).
    void relinkFrameClipboard(const fla::SymbolList* library);
    QAction* _onionSkinAction = nullptr;

    /// Plays the document's sounds with the timeline.
    class TimelineAudio* _timelineAudio = nullptr;
    /// The scene's loudness a frame, for behaviors that follow sound:
    /// worked out from its sounds when they change.
    double loudnessAt(const fla::Document& document, int frame);
    std::vector<double> _loudness;
    std::string _loudnessSounds;
    unsigned _loudnessRevision = 0;
    bool _loudnessChecked = false;
    QAction* _ungroupAction = nullptr;
    QAction* _breakApartAction = nullptr;
    QAction* _convertToSymbolAction = nullptr;
    QAction* _swapSymbolAction = nullptr;
    QAction* _templateFieldsAction = nullptr;
    QAction* _swapBitmapAction = nullptr;
    QAction* _traceBitmapAction = nullptr;
    QMenu* _transformMenu = nullptr;
    std::vector<QAction*> _transformActions;
    QAction* _distributeAction = nullptr;
    std::vector<QAction*> _combineActions;
    QAction* _arrangeActions[4] = {nullptr, nullptr, nullptr, nullptr};

    /// Copies of the shapes last cut or copied. Only shapes can be copied so
    /// far, since nothing else can be cloned yet.
    /// What Copy took: copies of the elements, of any kind, in stacking
    /// order. The system clipboard carries them too (see copy()).
    /// Shared by every window, with the document it came from, whose
    /// library items a paste into another document brings along.
    static std::vector<std::unique_ptr<fla::Element>>& _clipboard;
    static const fla::Document* _clipboardFrom;

    /// The shapes currently selected, in selection order.
    std::vector<fla::Shape*> selectedShapes() const;

    /// The elements currently selected, of any kind, in stacking order.
    std::vector<fla::Element*> selectedElements() const;

    /// Adds copies of \a sources to the current frame as one undo step and
    /// selects them. With \a centerInView the copies are moved so that they
    /// sit in the middle of the view, which is where a paste lands.
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
