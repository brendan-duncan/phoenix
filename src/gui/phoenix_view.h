#pragma once

#include "../edit/behaviors.h"

#include "../data/color_transform.h"
#include "../data/edge.h"
#include "../data/fla_document.h"
#include "../data/group.h"
#include "../data/rect.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../edit/brush_stroke.h"
#include "../edit/command.h"
#include "../edit/brush_texture.h"
#include "../edit/guide_path.h"
#include "../edit/motion_object.h"
#include "../edit/asset_warp.h"
#include "../edit/deformers.h"
#include "../edit/frame_actions.h"

#include <memory>
#include <QCache>
#include <QHash>
#include <QList>
#include <QMap>
#include <QLineF>
#include <QRectF>
#include <QGradient>
#include <QPixmap>

#include <map>
#include <set>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>

class QTimer;

#include "tool.h"
#include "../edit/transform_blend.h"

#include <vector>
#include <QPolygonF>
#include <functional>

namespace fla {
class Bitmap;
class IKArmature;
class StaticText;
class Command;
class CommandStack;
class Selection;
class Snapper;
namespace play {
struct Clip;
class MovieRuntime;
}
}

class Player;

class PhoenixView : public QWidget
{
    Q_OBJECT
public:
    PhoenixView(Player* player, QWidget *parent = nullptr);

    ~PhoenixView();

    void setDocument(const fla::FLADocument* document);

    /// Plays \a movie (Control > Test Movie): the stage draws its clips as
    /// its scripts leave them, buttons show their over and down frames, and
    /// the mouse goes to the movie rather than to a tool. Null goes back to
    /// editing.
    void setMovie(fla::play::MovieRuntime* movie);
    fla::play::MovieRuntime* movie() const { return _movie; }

    void setShowBounds(bool show) { _showBounds = show; update(); }

    bool showBounds() const { return _showBounds; }

    void setDimOutsideDocument(bool dim) { _dimOutsideDocument = dim; update(); }

    bool dimOutsideDocument() const { return _dimOutsideDocument; }

    /// The safe zones and aspect-ratio frames drawn over the stage
    /// (src/edit/safe_zones.h); the tools snap to their edges while shown.
    void setShowSafeZones(bool show);
    bool showSafeZones() const { return _showSafeZones; }
    /// Draws them into exported frames too (renderFrame), for review copies.
    void setExportSafeZones(bool on) { _exportSafeZones = on; }
    bool exportSafeZones() const { return _exportSafeZones; }
    /// The settings changed: redraw, and hand the new edges to the snapper.
    void safeZonesChanged();

    void setHighQualityAntiAliasing(bool on);

    /// Maps document coordinates to widget coordinates: the pan, the zoom, and
    /// the centering that keeps the stage in the middle of the view.
    ///
    /// The single source of truth for painting, hit-testing and the zoom anchor,
    /// so they cannot drift apart.
    QTransform documentToWidget() const;

    QPointF mapToDocument(const QPointF& widgetPos) const;

    /// The stage in the space of the timeline being edited: the document's
    /// own, or a symbol's when one is open.
    QRectF stageInScope() const;

    QPointF mapToWidget(const QPointF& documentPos) const;

    /// Finds the topmost thing under \p documentPos, searching in reverse draw
    /// order so the object a user sees on top is the one they get.
    ///
    /// \p tolerance is in document units and widens the target for strokes and
    /// anchors, which are otherwise impossible to hit exactly.
    HitResult hitTest(const QPointF& documentPos, double tolerance) const;

    /// Tolerance matching a few screen pixels at the current zoom, so picking
    /// feels the same however far in or out the view is.
    double pickTolerance() const;

    /// Converts a document-space tolerance into the element's own space, so the
    /// grab distance stays constant on screen however the element is scaled.
    static double toleranceInLocalSpace(const QTransform& elementToDocument, double tolerance);

    double zoom() const { return _zoom; }

    /// A tablet's pen pressure, 0 to 1, while the pen is down; 1 for a mouse.
    /// The brush reads it to vary its width.
    double pressure() const { return _pressure; }

    /// The pen's readings since they were last taken, while a pen is down:
    /// each with its exact position, pressure, tilt and time together. The
    /// mouse events Qt makes from a pen arrive less often and carry no
    /// pressure, so pairing them with the latest pressure mismatches the two.
    std::vector<fla::BrushSample> takeTabletSamples();

    /// Whether a pen is down on the stage: a press that comes from it has
    /// its readings waiting in takeTabletSamples().
    bool penDown() const { return _penDown; }

    /// Zoom steps follow the magnification presets. The view center
    /// stays put.
    void zoomIn();

    void zoomOut();

    /// Sets the zoom about the view center.
    void setZoomLevel(double zoom);

    /// Sets the zoom keeping the document point under \p widgetAnchor where
    /// it is on screen, which is what a wheel or a zoom-tool click wants.
    void zoomTo(double zoom, const QPointF& widgetAnchor);

    /// Fit in Window: the stage fills the view.
    void zoomToFit();

    /// Where the view stands on the stage: its zoom, pan and turn. A
    /// document waiting in a tab keeps it and is shown that way again.
    struct ViewState
    {
        bool valid = false;
        double zoom = 1.0;
        double panX = 0.0;
        double panY = 0.0;
        double rotation = 0.0;
        QPointF pivot;
    };
    ViewState viewState() const;
    void setViewState(const ViewState& state);

    /// The stage's rotation on screen, in degrees clockwise, as the
    /// Rotation tool sets it: a view setting, not an edit. It turns about a
    /// pivot in the stage's coordinates. Changing the pivot leaves the view
    /// where it is.
    double rotation() const { return _rotation; }
    void setRotation(double degrees);
    QPointF rotationPivot() const { return _rotationPivot; }
    void setRotationPivot(const QPointF& stagePos);

    /// Between the widget and the stage's own coordinates (the scene's,
    /// whatever symbol is being edited).
    QPointF widgetToStage(const QPointF& widgetPos) const;
    QPointF stageToWidget(const QPointF& stagePos) const;

    /// The playhead's player, for tools that move it.
    Player* player() const { return _player; }

    /// How many frames the timeline being edited runs to.
    int frameCount() const;

    /// Show All: the stage and everything placed on or off it.
    void zoomToShowAll();

    /// Zooms so that \p documentRect fills the view, centered.
    void zoomToRect(const QRectF& documentRect);

    /// Removes everything selected from the document as one undo step.
    void deleteSelection();

    /// Symbol editing, edit in place. Entering an instance makes
    /// its symbol's timeline the one the tools work on: the view's coordinate
    /// space becomes the symbol's, the rest of the document draws dimmed
    /// behind it, and the timeline panel follows. Editing a symbol changes
    /// every instance of it, since they all draw the same timeline.
    bool enterSymbol(fla::SymbolInstance* instance);

    /// Edits a group in place, as double-clicking it does: its
    /// members are drawn on and picked as artwork of their own, everything
    /// else stays as it is, dimmed. Leaving gathers what is there back into
    /// the group. Entering and leaving are one undo step, or none when nothing
    /// changed in between.
    bool enterGroup(fla::Group* group);

    /// Leaves every group being edited, innermost first (before saving).
    void exitGroups();

    /// Whether \a element is out of reach while a group is edited: anything
    /// but the group's own content.
    bool outsideGroup(const fla::Element* element) const;

    /// Whether a symbol is being edited (and not only groups at the scene).
    bool inSymbol() const;

    /// Edit Multiple Frames: every keyframe within the onion skin's range is
    /// drawn in full and can be picked and edited with the current one.
    void setEditMultipleFrames(bool on);

    /// What layer parenting places \a layer's \a frame by on frame \a index:
    /// its parent's object there, and that one's parent's.
    QTransform parentingAt(const fla::Layer* layer, const fla::Frame* frame, int index);

    /// The frame the stage shows.
    int playheadFrame() const { return shownFrame(); }

    /// Camera View: the scene seen through its multiplane camera, when it
    /// has one. On by default; off shows the stage as drawn.
    void setCameraView(bool on);
    bool cameraView() const { return _cameraView; }
    bool editMultipleFrames() const { return _editMultiple; }

    /// Rulers along the top and left of the stage, in stage pixels.
    void setRulersVisible(bool on);
    bool rulersVisible() const { return _showRulers; }

    /// Ruler guides: dragged out of a ruler, moved with the selection tool,
    /// dragged back into a ruler to remove. They are the scene's, in stage
    /// coordinates, saved with the document; each change can be undone.
    void setGuidesVisible(bool on);
    bool guidesVisible() const { return _showGuides; }
    void clearGuides();

    /// A picture of a library item for the library's preview: a symbol's
    /// first frame or a bitmap, fitted into \a size with a margin. With
    /// \a frame, the symbol's frame of that index, framed to fit all of its
    /// frames so that pictures of its frames line up, as the Frame Picker
    /// shows them.
    QImage libraryPreview(const QString& kind, const QString& name, const QSize& size, int frame = -1);

    /// Edits \a symbol on its own, as a double-click in the library does:
    /// the document is put away and the symbol shown at the stage's center.
    bool editSymbol(const fla::Symbol* symbol);

    /// Steps out one level, selecting the instance that was being edited.
    bool exitScope();

    /// Steps out until \p depth levels remain; 0 is the scene.
    void exitToDepth(int depth);

    int scopeDepth() const { return static_cast<int>(_scope.size()); }

    /// The timeline being edited, or null for the scene.
    fla::Timeline* scopeTimeline() const;

    /// The innermost symbol being edited, or null on a scene. Nothing that
    /// contains it can be placed while it is open (fla::symbolContains).
    const fla::Symbol* scopeSymbol() const;

    /// The name of a level of the scope, for a breadcrumb: the scene at 0,
    /// then each symbol entered.
    QString scopeName(int depth) const;

signals:
    /// The stage was right-clicked at \a documentPos.
    void stageMenuRequested(const QPoint& globalPos, const QPointF& documentPos);
    /// The cursor moved over the stage, to the document point given and the
    /// widget point it is at (for the Info panel).
    void cursorMoved(const QPointF& documentPos, const QPoint& widgetPos);
    void scopeChanged();

    /// The stage was turned with the Rotation tool or set straight.
    void rotationChanged(double degrees);

    /// The zoom or the turn of the view changed, however it was done; told
    /// as the view next draws.
    void zoomChanged();

    /// The tool that now has the stage.
    void activeToolChanged(Tool* tool);

    /// A library item was dropped at \a position, in the space the tools
    /// work in. \a kind is "symbol" or "bitmap".
    void libraryItemDropped(const QString& kind, const QString& name, const QPointF& position);

    /// The pen was turned over: its eraser end (\a eraser) or its tip is
    /// now the one near the tablet. The eraser end erases.
    void penEndChanged(bool eraser);

public:
    /// Onion skinning: \a before and \a after frames either side of the
    /// playhead drawn faded under it, those before tinted blue and those
    /// after green.
    void setOnionSkin(bool on, int before, int after);

    /// Light Table: while drawing, every layer but the one drawn on shows
    /// faded behind it.
    void setLightTable(bool on);

    /// What renders for a compositing pass: everything, only the lines
    /// (strokes), or only the color (fills, text, bitmaps).
    enum class RenderPass
    {
        All,
        Lines,
        Color
    };
    void setRenderPass(RenderPass pass);
    bool lightTable() const { return _lightTable; }

    /// How the onion skin draws: frames as outlines, only keyframes, how
    /// strongly, and the colors of the frames before and after.
    void setOnionStyle(bool outline, bool keyframesOnly, double opacity, const QColor& before, const QColor& after);

    /// The frame of its symbol an instance in \a frame shows at the playhead,
    /// as the stage draws it: a graphic by its loop setting, counted from
    /// its keyframe; a movie clip or a button at its first frame.
    int symbolFrameShown(const fla::SymbolInstance& instance, const fla::Frame& frame);
    bool onionSkin() const { return _onionSkin; }

public:

    /// Selects a region of merge-drawn artwork with the arrow:
    /// the fill or the stroke under the cursor, not the whole shape. The
    /// region is lifted out as a shape of its own, which floats selected
    /// exactly where it was; let go untouched it goes straight back. With
    /// \p extended, a fill takes its surrounding strokes and a stroke takes
    /// the strokes joined to it, as a double-click does. Returns false when
    /// nothing under the cursor is region-selectable.
    bool selectRegionAt(const HitResult& hit, const QPointF& documentPos,
        bool extended, bool additive);

    /// Whether \p element is a region lifted out of another shape.
    bool isFloatingRegion(const fla::Element* element) const;

    /// Selects what a marquee or a lasso sweeps out: an
    /// object the area touches is selected whole, while merge-drawn artwork
    /// the area only partly covers is divided along the area's outline and
    /// the fills and strokes inside it are lifted out as the selection.
    /// Returns false when the area reached nothing.
    /// With \a only, nothing but that element is considered.
    bool selectRegionsIn(const QPainterPath& area, bool additive, const fla::Element* only = nullptr);

    /// Document-space bounds of an element, cached. Tools use this to place
    /// handles around what is selected.
    /// The element's art as drawn, edge to edge: a fill's outline, a
    /// stroke's outer edge. What snapping, the transform box and the
    /// Properties panel measure. (Culling uses bounds padded a little for
    /// antialiasing.)
    QRectF elementBounds(const fla::Element* element);

    /// The transform \a element shows with now: past the start of an object
    /// motion tween, where the motion has it; otherwise its own. Tools
    /// measure and drag this.
    fla::Transform shownTransformOf(const fla::Element* element);

    /// The element's bounds where it shows now, in document space.
    QRectF shownBoundsOf(const fla::Element* element) { return shownElementBounds(element); }
    /// A symbol's art's bounds in its own space.
    QRectF symbolBounds(const fla::Symbol* symbol) { return calculateSymbolLocalBounds(symbol); }

    /// The transform to store on \a element for it to show as \a shown now,
    /// the inverse of shownTransformOf.
    fla::Transform storedTransformFor(const fla::Element* element, const fla::Transform& shown);

    /// The command for changing \a element's stored transform from \a before
    /// to \a after. Past the start of an object motion tween, a property
    /// keyframe here holding what \a after shows instead;
    /// at its start, the start moves and the tween's later positions stay.
    fla::CommandPtr transformCommand(fla::Element* element, const fla::Transform& before,
        const fla::Transform& after, const std::string& name);

    /// The command for changing \a instance's color effect from \a before to
    /// \a after: past the start of an object motion tween, keyframes of the
    /// color properties that changed instead.
    fla::CommandPtr colorCommand(fla::SymbolInstance* instance, const fla::ColorTransform& before,
        const fla::ColorTransform& after, const std::string& name);

    /// The same for \a instance's filters: past the start of an object motion
    /// tween, a change of a filter's numbers keys them there. Adding,
    /// removing or reordering filters changes them for the whole span.
    fla::CommandPtr filtersCommand(fla::SymbolInstance* instance, const std::vector<fla::Filter>& before,
        const std::vector<fla::Filter>& after, const std::string& name);

    /// The motion paths of the selected object motion tweens, as drawn.
    struct MotionPath
    {
        fla::Element* element = nullptr;
        fla::Frame* frame = nullptr;
        /// Where the transformation point is on each frame of the span.
        std::vector<QPointF> points;
        /// The frames with keyframes, and the one shown.
        std::vector<int> keys;
        int current = -1;
        QColor color;
    };
    std::vector<MotionPath> motionPaths();

    /// Dragging a motion path, as the Selection tool does: a keyframe moves,
    /// the path between keyframes bends, the start moves the object. Begin
    /// is false when \a documentPos is not on a path; end with \a commit
    /// false puts it back.
    bool beginMotionPathDrag(const QPointF& documentPos);
    void dragMotionPath(const QPointF& documentPos);
    void endMotionPathDrag(bool commit);
    bool motionPathDragging() const { return _pathDrag.element != nullptr; }

    /// The shape tween whose hints show on the frame shown: its first
    /// keyframe, and whether that is the frame shown (its hints' starts) or
    /// its end is (their ends). Null when the playhead is on neither.
    fla::Frame* shapeHintTween(bool& start);

    /// Dragging a shape hint with the Selection tool; dropped off the stage
    /// it goes. Begin is false when no hint is under \a documentPos.
    bool beginHintDrag(const QPointF& documentPos);
    void dragHint(const QPointF& documentPos);
    void endHintDrag(bool commit);
    bool hintDragging() const { return _hintDrag.tween != nullptr; }

    /// The shape's filled regions as the stage stitches them, one path per
    /// fill style index, in the shape's own space.
    std::map<int, QPainterPath> fillRegions(const fla::Shape& shape);

    /// Top-level elements of the visible frames whose bounds meet
    /// \p documentRect. Does not descend into groups or symbols: a marquee
    /// picks whole objects at the level being edited.
    std::vector<fla::Element*> elementsIn(const QRectF& documentRect);

    /// The tool receiving stage input. Null routes everything to panning.
    void setActiveTool(Tool* tool);

    Tool* activeTool() const { return _activeTool; }

    /// The selection shown on the stage. Borrowed, not owned.
    void setSelection(fla::Selection* selection);

    /// The tool that takes over while ctrl is held.
    ///
    /// Holding ctrl reaches for the selection tool whatever is active, so
    /// something can be moved without putting the drawing tool down.
    void setModifierTool(Tool* tool) { _modifierTool = tool; }

    /// The command stack edits go through. Borrowed, and needed because the
    /// view itself commits a waiting merge when the selection moves on.
    void setCommandStack(fla::CommandStack* commandStack) { _commandStack = commandStack; }

    /// Remembers a drawing that has been placed but not yet merged into the
    /// artwork under it. Deselecting it is what commits it.
    void setPendingMerge(fla::Shape* shape, fla::Frame* frame, bool keepStrokes = false);

    /// Remembers a shape that has just been moved or reshaped.
    ///
    /// A shape that has been picked up and put down somewhere else belongs to
    /// the artwork it now sits on, exactly as a freshly drawn one does, so
    /// deselecting it commits it the same way. Finds the shape's own frame, so
    /// a tool need not know it.
    void markShapeEdited(fla::Shape* shape);

    /// Forgets any waiting drawing without merging it. For when the document it
    /// belongs to is going away.
    void clearPendingMerge();

    /// After an undo or redo: lets go of everything the view holds that the
    /// document no longer does (an undone edit owns what it took out, and
    /// frees it when the next edit is made). Steps out of edit scopes whose
    /// symbol, instance or frame is gone, forgets waiting drawings and lifted
    /// regions that went, and the highlighted outliner element.
    void pruneToDocument();

    /// Commits a waiting drawing now, whether or not it is still selected.
    ///
    /// Starting another drawing does this, so the waiting one merges into what
    /// was under it rather than into the drawing about to be laid on top.
    void flushPendingMerge();

    /// Commits a waiting drawing if the selection has moved off it.
    ///
    /// Called when the selection changes, which is the moment that counts: a
    /// drawing stays its own object while it is selected, and becomes part of
    /// the artwork when it is let go.
    void selectionChanged();

    fla::Selection* selection() const { return _selection; }

    /// The snapping settings tools should obey. Borrowed, not owned.
    void setSnapper(fla::Snapper* snapper);

    fla::Snapper* snapper() const { return _snapper; }

    /// Loads \p snapper with the edges and centers of everything on stage apart
    /// from \p exclude, so a drag can line up with its neighbors but not with
    /// itself, and with the vertices and outline pieces of their artwork, which
    /// scale handles land on.
    void gatherSnapCandidates(fla::Snapper& snapper,
        const std::vector<fla::Element*>& exclude);

    /// The frame a new object would be drawn into: the currently showing frame
    /// of the layer being edited. Null when there is nowhere to draw.
    fla::Frame* activeFrame();

    /// How much the camera enlarges art drawn on \a frame's layer: 1 with
    /// no camera, or with the camera view off.
    double cameraScaleFor(const fla::Frame* frame);

    /// The ruler guides in the space the tools work in: the vertical ones'
    /// x and the horizontal ones' y; none while they are hidden, or inside
    /// a symbol turned on the stage.
    void toolGuides(std::vector<double>& xs, std::vector<double>& ys);

    /// The keyframe at the frame being shown on \a frame's layer, which a
    /// drawing goes into. When the playhead sits inside \a frame's span
    /// rather than on its keyframe, a keyframe is inserted there first,
    /// through the command stack. Returns \a frame itself when it already is that keyframe.
    fla::Frame* keyframeForDrawing(fla::Frame* frame);

    void setShowGrid(bool show) { _showGrid = show; update(); }
    /// View > Hide Edges: the selection's highlight not drawn.
    void setHideEdges(bool hide) { _hideEdges = hide; update(); }
    /// View > Show Shape Hints.
    void setShowShapeHints(bool show) { _showShapeHints = show; update(); }
    /// The grid over the art rather than under it.
    void setGridOver(bool over) { _gridOver = over; update(); }
    bool gridOver() const { return _gridOver; }
    /// Clip Content Outside Stage: what lies off the stage is not shown.
    void setClipOutsideStage(bool clip) { _clipOutsideStage = clip; update(); }
    bool clipOutsideStage() const { return _clipOutsideStage; }
    /// The pasteboard round the stage, and what is on it; off, only the
    /// stage shows.
    void setShowPasteboard(bool show) { _showPasteboard = show; update(); }
    bool showPasteboard() const { return _showPasteboard; }
    /// Simple frame actions: on, an exported movie's movie clips stop and
    /// jump as the stop(), play() and goto calls in their frame scripts say.
    void setFrameActions(bool on) { _frameActions = on; }
    bool frameActions() const { return _frameActions; }
    /// Guides that cannot be dragged.
    void setGuidesLocked(bool locked) { _guidesLocked = locked; }
    bool guidesLocked() const { return _guidesLocked; }
    /// Brings the stage to the middle of the view at the zoom it has.
    void centerStage();

    bool showGrid() const { return _showGrid; }

    bool highQualityAntiAliasing() const { return _highQualityAntiAliasing; }

    void clearCaches();

    /// Forgets what is cached for one shape (its paths and the brushes in
    /// them, its bounds), for a tool changing it live.
    void forgetShape(const fla::Shape* shape);

    /// The pose frames of IK armatures the editable timelines show at the
    /// playhead, on visible layers, with how far into each pose span it is.
    std::vector<std::pair<fla::Frame*, int>> armatureFrames() const;

    /// A bone under the cursor, for dragging an armature into a new pose.
    struct IKBoneHit
    {
        fla::Frame* frame = nullptr;
        /// Which of the frame's extraChildren holds the IKTree.
        size_t child = 0;
        /// The joint at the bone's tail, as IKArmature numbers them.
        int joint = -1;
        /// Frames into the pose span.
        int into = 0;
        /// Where the tail joint is shown.
        QPointF tail;
        bool locked = false;

        explicit operator bool() const { return frame != nullptr; }
    };

    /// The bone of a shown armature nearest \a documentPos, within a few
    /// screen pixels.
    IKBoneHit ikBoneAt(const QPointF& documentPos) const;

    /// Drops just the cached bounds. Transforming an element changes where it
    /// sits but not its path geometry, so the expensive path cache survives a
    /// drag.
    void invalidateBounds();

    // Export the currently displayed frame to an SVG file (no animation).
    // Renders in document space using the same traversal as on-screen drawing.
    bool exportToSvg(const QString& filePath);

    /// SVG of one frame: the stage, or \a area of the document when it is
    /// valid; hidden layers drawn when \a hidden; images linked as files
    /// beside it rather than embedded when \a linked, each written once
    /// whatever frames use it (\a written keeps which, across a sequence).
    bool exportFrameSvg(const QString& filePath, int frame, bool transparent, bool hidden, bool linked,
        const QRectF& area, QStringList* written = nullptr);

    /// The frames' artwork's bounds, stage included: what Everything
    /// exports.
    QRectF framesBounds(int first, int last);

    /// While on, hidden layers are drawn too, as an export that includes
    /// them asks.
    void setDrawHidden(bool hidden) { _drawHidden = hidden; }

    /// The document at frame \a frame, drawn into an image of \a size as a
    /// published movie shows it: every layer but guides, locked ones too,
    /// and movie clips playing their own timelines. The stage color fills
    /// it unless \a transparent.
    QImage renderFrame(int frame, const QSize& size, bool transparent);
    /// The same of \a area of the document, rather than the stage.
    QImage renderFrame(int frame, const QSize& size, bool transparent, const QRectF& area);

    /// The same as vectors: what frame \a frame paints at \a size, the
    /// stage at the origin, as fills and images (no stage color), masks as
    /// clips. What SWF export writes.
    std::vector<struct RecordedItem> recordFrame(int frame, const QSize& size);

    /// While set, renderFrame draws nothing of the scene but \a layer (its
    /// mask still masking it), for exporting a sequence per layer.
    void setOnlyLayer(const fla::Layer* layer) { _onlyLayer = layer; }

    /// The document shown, or null.
    const fla::FLADocument* flaDocument() const { return _flaDocument; }

    /// Elements of the timeline being edited drawn on their own, on a
    /// transparent image fitted to them at \a scale pixels per pixel: what
    /// Copy offers other applications.
    /// The same as vectors, as an SVG document, for other applications to
    /// paste; empty when nothing is drawn.
    QByteArray svgOfElements(const std::vector<const fla::Element*>& elements);

    QImage renderElements(const std::vector<const fla::Element*>& elements, double scale);

    /// A library symbol's frame \a frame drawn on its own, \a area of its
    /// space at \a scale pixels per pixel on a clear image: what a sprite
    /// sheet of the symbol holds.
    QImage renderSymbolFrame(const fla::Symbol* symbol, int frame, const QRectF& area, double scale);
    /// What the symbol draws over frames \a first to \a last, in its space.
    QRectF symbolFramesBounds(const fla::Symbol* symbol, int first, int last);

public slots:
    void onPlayerFrameChanged(int frame);

    void onElementSelected(const fla::DOMElement* element);

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void mousePressEvent(QMouseEvent *event) override;
    /// The mouse over a movie being played: its buttons.
    void movieMouse(QMouseEvent* event);
    /// The button, or movie clip listening for the mouse, under
    /// \a widgetPos in the movie last drawn, or null.
    fla::play::Clip* movieButtonAt(const QPointF& widgetPos);
    /// Whether \a timeline's art on \a frame, drawn through \a toStage, is
    /// at \a at on the stage.
    bool movieArtAt(const fla::Timeline& timeline, int frame, const QTransform& toStage, const QPointF& at);

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    bool event(QEvent* event) override;

    void tabletEvent(QTabletEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;

    bool _highQualityAntiAliasing;

    /// Whether the view is mid-gesture, and so should render for speed rather
    /// than for looks.
    ///
    /// High quality rendering supersamples the whole stage into an offscreen
    /// image, which at a full-screen window means tens of megabytes allocated,
    /// filled and scaled down for every frame. That is affordable for a view
    /// being looked at and ruinous for one being dragged, so it is dropped while
    /// a gesture runs and the refined frame is drawn once it ends.
    bool _interacting = false;

    /// Restores quality rendering shortly after the last wheel event, which has
    /// no natural end the way a press and release do.
    QTimer* _settleTimer = nullptr;

public:
    /// Marks the start of a gesture, and returns to quality rendering after it.
    void beginInteraction();
    void endInteraction();

private:

    // Pan and zoom state
    double _zoom;
    double _panX;
    double _panY;
    double _minZoom = 0.05;
    double _maxZoom = 100.0;
    bool _isDragging;
    QPoint _lastMousePos;

    /// Space held turns any tool into the hand.
    bool _spaceHeld = false;

    /// Document-space bounds of everything on stage, united with the stage.
    QRectF contentBounds();

    /// A level of symbol editing: the instance entered and the timeline it
    /// draws.
    struct EditScope
    {
        /// The instance entered, or null when the symbol is edited on its
        /// own (from the library), centered on the stage with nothing else.
        fla::SymbolInstance* instance = nullptr;
        fla::Timeline* timeline = nullptr;
        const fla::Symbol* symbol = nullptr;
        /// A group edited in place: its frame, where its members are while it
        /// is; what else the frame held, out of reach; the command that took
        /// the group apart; and the group's own attributes, for the group it
        /// becomes again. Null for a symbol.
        fla::Group* group = nullptr;
        fla::Frame* frame = nullptr;
        std::set<const fla::Element*> outside;
        const fla::Command* entered = nullptr;
        std::vector<std::pair<std::string, std::string>> groupAttributes;
    };
    std::vector<EditScope> _scope;

    /// Maps the scope's own space to the document: the instance transforms
    /// of every level, innermost first.
    QTransform scopeToDocument() const;

    /// The pan, zoom and centering alone, without the scope.
    QTransform rootToWidget() const;

    /// The timelines the tools act on: the scoped one, or the scene's.
    std::vector<const fla::Timeline*> editableTimelines() const;

    /// Draws the stage: background, grid, the document, and over it the
    /// symbol being edited at full strength when there is one. The painter
    /// arrives mapped for document space.
    void drawStageContents(QPainter& painter, const fla::Document* document);
    QTransform viewTransform;

    /// The transform of the element being drawn, relative to the stage:
    /// the product of its own matrix and its parents', without the zoom.
    QTransform _elementTransform;

    /// True while a mask layer is being rendered: every fill and stroke is
    /// drawn opaque, since a mask is coverage and its colors and alpha
    /// mean nothing.
    bool _maskPass = false;

    static constexpr int kRulerSize = 20;
    bool _showRulers = false;
    bool _showGuides = true;
    /// The guides are the scene's, kept on its timeline in stage
    /// coordinates and saved with it; with no document there are none.
    fla::Timeline* guideTimeline() const;
    std::vector<double>& guidesX();
    std::vector<double>& guidesY();
    std::vector<double> _noGuidesX;
    std::vector<double> _noGuidesY;

    /// Sets the scene's guides as one undoable step named \a name.
    void setGuides(std::vector<double> xs, std::vector<double> ys, const char* name);
    /// A guide being dragged: 1 vertical, 2 horizontal, 0 none; the index
    /// of the guide moved, or -1 for one coming out of a ruler.
    int _guideDrag = 0;
    int _guideIndex = -1;
    double _guideValue = 0.0;

    /// Starts dragging a guide if \a widgetPos is on a ruler or a guide.
    bool beginGuideDrag(const QPointF& widgetPos);
    /// The 9-slice guides of the symbol being edited, when it has 9-slice
    /// scaling on: drawn dashed, dragged with the Selection tool, each move
    /// one step to undo.
    fla::Symbol* sliceSymbol() const;
    QTransform sliceToWidget() const;
    bool beginSliceDrag(const QPointF& widgetPos);
    void drawSliceGuides(QPainter& painter);
    /// The guide being dragged (1 left, 2 right, 3 top, 4 bottom; 0 none)
    /// and where it is, in the symbol's space.
    int _sliceDrag = 0;
    double _sliceValue = 0.0;
    void drawRulersAndGuides(QPainter& painter);
    /// Draws the safe zones and aspect-ratio frames, mapped to the device by
    /// \a toDevice from the stage's space.
    void drawSafeZones(QPainter& painter, const QTransform& toDevice, const fla::Document* document);
    /// Hands the guides to the snapper in the space the tools work in.
    void pushGuidesToSnapper();

    /// How deep the drawing is inside instances with the Layer blend mode,
    /// whose offscreen is what alpha and erase instances cut into.
    int _layerGroupDepth = 0;

    /// The frame to draw instead of the playhead's, while an onion frame is
    /// drawn; -1 otherwise.
    int _frameOverride = -1;

    /// The movie being played, the clips of what is being drawn (the
    /// root's at the bottom), and its buttons' hit areas as last drawn.
    fla::play::MovieRuntime* _movie = nullptr;
    std::vector<fla::play::Clip*> _movieClips;
    struct ButtonArea
    {
        QPolygonF area;
        fla::play::Clip* clip;
        /// What it is found by: its art on a frame, drawn through toDocument.
        const fla::Timeline* timeline = nullptr;
        int frame = 0;
        QTransform toDocument;
    };
    std::vector<ButtonArea> _buttonAreas;
    /// Each clip's art as last drawn, for the movie's exact hit tests.
    struct MovieArt
    {
        const fla::Timeline* timeline = nullptr;
        int frame = 0;
        QTransform toStage;
    };
    std::map<const fla::play::Clip*, MovieArt> _movieArt;
    /// The input text fields as last drawn, on the stage, for clicks.
    struct TextArea
    {
        QPolygonF area;
        fla::play::Clip* clip = nullptr;
        const fla::StaticText* field = nullptr;
    };
    std::vector<TextArea> _textAreas;
    /// Drawing for export (renderFrame): locked layers show and movie clips
    /// play.
    bool _exporting = false;
    int shownFrame() const;

    const fla::Layer* _onlyLayer = nullptr;
    bool _onionSkin = false;
    bool _lightTable = false;
    RenderPass _renderPass = RenderPass::All;
    /// Pegs (instances of a peg symbol) drawn as their crosshairs.
    void drawPegs(QPainter& painter);

    /// Deformers: the handles of each selected instance with one, on its
    /// keyframe, while the Selection tool is out; dragged to bend the art.
    struct DeformerTarget
    {
        fla::SymbolInstance* instance = nullptr;
        /// The instance's own space to the stage's.
        QTransform placed;
        fla::deform::Deformer deformer;
        bool locked = false;
    };
    std::vector<DeformerTarget> deformerTargets();
    void drawDeformers(QPainter& painter);
    bool beginDeformerDrag(const QPointF& widgetPos);
    struct DeformerDrag
    {
        fla::SymbolInstance* instance = nullptr;
        QTransform toLocal;
        size_t index = 0;
        QPointF grab;
        fla::deform::Deformer start;
        std::vector<std::pair<std::string, std::string>> before;
    };
    DeformerDrag _deformerDrag;
    /// An effect layer (tone, highlight, glow): its art blurred, colored and
    /// composited over \a target, clipped to it unless it glows outside.
    void drawEffectLayer(QPainter& painter, const fla::Layer* layer, const fla::Layer* target,
        fla::LoopType loopType, int firstFrame, bool editing);
    int _onionBefore = 2;
    int _onionAfter = 2;
    bool _onionOutline = false;
    bool _onionKeyframesOnly = false;
    double _onionOpacity = 0.45;
    QColor _onionBeforeColor = QColor(0, 102, 255);
    QColor _onionAfterColor = QColor(0, 170, 60);
    /// While an onion frame draws in outline mode, the color its art is
    /// outlined in.
    QColor _forcedOutline;

    /// Draws the edited timeline (or the document) at frame \a frame,
    /// faded and tinted, for onion skinning.
    void drawOnionFrame(QPainter& painter, const fla::Document* document, int frame, const QColor& tint, double opacity);

    /// While a layer in outline view is being drawn, its color: every fill
    /// and stroke is drawn as a thin line in it and nothing is filled.
    /// Invalid when drawing normally.
    QColor _outlineColor;

    /// Counts every edit that changes what the stage shows, so cached
    /// renderings can tell when they are stale.
    unsigned _revision = 0;

    /// While a gesture moves part of the stage, everything else is rendered
    /// once and reused for every frame of the gesture: what stacks below
    /// the moving elements in one image, what stacks above them in another,
    /// and only the moving elements (and anything stacked between them)
    /// drawn each time. The key says what the images were rendered for.
    struct StaticKey
    {
        double zoom = 0.0;
        double panX = 0.0;
        double panY = 0.0;
        QSize size;
        int frame = -1;
        unsigned revision = 0;
        size_t scopeDepth = 0;
        bool grid = false;
        double rotation = 0.0;
        QPointF pivot;
        std::vector<const fla::Element*> moving;

        bool operator==(const StaticKey& other) const
        {
            return zoom == other.zoom && panX == other.panX && panY == other.panY &&
                rotation == other.rotation && pivot == other.pivot &&
                size == other.size && frame == other.frame && revision == other.revision &&
                scopeDepth == other.scopeDepth && grid == other.grid && moving == other.moving;
        }
    };
    QImage _belowImage;
    QImage _aboveImage;
    StaticKey _staticKey;
    bool _staticValid = false;

    /// The top-level elements a gesture is moving: the selection's, and any
    /// drawing still floating.
    std::vector<const fla::Element*> movingElements() const;

    /// Which top-level elements drawFrame draws. Every visible element of
    /// the edited timeline's own frames is numbered in drawing order; a
    /// Scan pass draws nothing and notes where the moving elements fall,
    /// then Below draws what comes before the first of them, Live the
    /// first through the last, and Above what follows. The insides of
    /// symbols and mask coverage always draw whole.
    enum class DrawPhase { All, Scan, Below, Live, Above };
    DrawPhase _drawPhase = DrawPhase::All;
    const std::vector<const fla::Element*>* _moving = nullptr;
    int _drawOrdinal = 0;
    int _firstMoving = -1;
    int _lastMoving = -1;
    int _frameDepth = 0;

    /// How deep the walks into symbols (drawing, bounds, hit testing) go at
    /// most. Loading and editing never let a symbol contain itself
    /// (fla::symbolContains), so this only stops a loop that got past them
    /// from overflowing the stack.
    static constexpr int kMaxNesting = 64;
    mutable int _nesting = 0;
    struct NestingGuard
    {
        int& depth;
        bool ok;
        explicit NestingGuard(int& counter)
            : depth(counter)
            , ok(++counter <= kMaxNesting)
        {}
        ~NestingGuard() { --depth; }
    };

    /// Whether an element, or anything inside it, blends with what is
    /// drawn beneath it. Such an element cannot be rendered ahead into the
    /// image above the moving elements, since what it blends with moves.
    bool needsBackdrop(const fla::Element* element, int depth = 0);
    QMap<const fla::Element*, bool> _backdropCache;

    /// A filtered or color-transformed instance's rendering, kept between
    /// paints: reused while the instance's placement, apart from a shift,
    /// and the document are unchanged, and moved with a pan.
    struct FilteredImage
    {
        QTransform full;
        int frame = -1;
        /// The frame of its own symbol the instance showed: inside a graphic
        /// placed twice at different first frames, the same instance shows
        /// different frames under one playhead.
        int inner = -1;
        unsigned revision = 0;
        bool clipped = false;
        QImage image;
        QPoint origin;
    };
    QMap<const fla::SymbolInstance*, FilteredImage> _filterCache;

    /// How the classic tween of the frame being drawn turns, from its
    /// keyframe, for interpolateTransform.
    fla::TweenRotation _tweenRotation = fla::TweenRotation::Auto;
    int _tweenTurns = 0;

    /// A stroke weight in the element's own units for its scaleMode, so
    /// that a "none" stroke stays its weight on the stage however the
    /// element is scaled, and horizontal/vertical ones follow one axis.
    double scaledStrokeWeight(double weight, const std::string& scaleMode) const;

    // Visible rect in document coordinates (for culling)
    QRectF _visibleRect;

    // Debug: show bounding boxes
    bool _showBounds;

    // Dim area outside document
    bool _dimOutsideDocument = false;
    bool _showSafeZones = false;
    bool _exportSafeZones = false;

    bool _disableStaticText = false;

    double _radius = 1.0;
    QPen _overlayPen;
    QBrush _overlayBrush;

    // Selected element for overlay
    const fla::DOMElement* _selectedElement = nullptr;

    Tool* _activeTool = nullptr;
    fla::Selection* _selection = nullptr;

    /// Borrowed. Used for a gesture started with ctrl held.
    Tool* _modifierTool = nullptr;

    double _pressure = 1.0;
    double _rotation = 0.0;
    QPointF _rotationPivot;
    /// The zoom and turn zoomChanged last told of.
    double _toldZoom = -1.0;
    double _toldRotation = 0.0;
    /// Whether the pen's eraser end is the one in use.
    bool _penEraser = false;
    bool _penDown = false;
    std::vector<fla::BrushSample> _tabletSamples;

    /// The tool handling the gesture in progress, which is the active one unless
    /// ctrl took over at the press. A gesture finishes with the tool that began
    /// it, whatever happens to the modifier part way through.
    Tool* _gestureTool = nullptr;

    /// The tool a mouse gesture should go to.
    Tool* gestureTool() const { return _gestureTool ? _gestureTool : _activeTool; }
    fla::CommandStack* _commandStack = nullptr;

    /// A shape waiting to be dropped into the artwork under it, and the frame
    /// holding it. Borrowed, never owned.
    struct PendingDrop
    {
        fla::Shape* shape = nullptr;
        fla::Frame* frame = nullptr;
        /// Whether the drop paints under the artwork's strokes (the brush's
        /// fills-only modes) rather than burying them.
        bool keepStrokes = false;
    };

    /// Everything waiting. A drag can move several things at once, and each is
    /// dropped where it now sits.
    std::vector<PendingDrop> _pending;

    /// A region lifted out of a shape by selecting it. While its split is
    /// still the last thing on the history, letting go of it untouched
    /// discards the split and the source is whole again; once anything has
    /// been done to it, it is dropped back in like any moved shape.
    struct FloatingRegion
    {
        fla::Shape* source = nullptr;
        fla::Shape* piece = nullptr;
        fla::Frame* frame = nullptr;
        const fla::Command* split = nullptr;
    };
    std::vector<FloatingRegion> _floating;

    /// Puts back every floating region that is no longer selected and was
    /// never touched, and forgets the ones that were.
    void settleFloating();

    const FloatingRegion* floatingFor(const fla::Element* element) const;

    /// Guards against the merge's own selection changes re-entering the commit.
    bool _committingMerge = false;
    fla::Snapper* _snapper = nullptr;
    bool _showGrid = false;
    bool _hideEdges = false;
    bool _showShapeHints = true;
    bool _drawHidden = false;
    /// Masks clip what they mask to their area as a path, for exports that
    /// write vectors, rather than compositing pixels.
    bool _vectorMasks = false;
    bool _gridOver = false;
    bool _clipOutsideStage = false;
    bool _showPasteboard = true;
    bool _guidesLocked = false;

    struct PathCacheEntry
    {
        QBrush fillBrush;
        QPen pen;
        QPainterPath painterPath;

        /// The stroke the pen came from, for widths that depend on where
        /// the shape is drawn (scaleMode); null for a fill entry.
        const fla::StrokeStyle* strokeStyle = nullptr;

        /// The fill style index a fill entry paints, or -1 for a stroke.
        int fillIndex = -1;

        /// For a ragged, stipple or hatched stroke, the area its marks
        /// cover, filled with the pen's color in place of stroking.
        QPainterPath effectPath;

        /// A textured fill whose grain keeps its size in the frame: the
        /// art's own scale is taken out of it when it is painted.
        bool fixedGrain = false;

        /// For a fill entry, the edges it shares with a fill drawn after it,
        /// stroked a pixel wide in its own paint so no line of what is
        /// behind shows between the two.
        QPainterPath seamPath;

        /// A stroke entry's outline, filled in place of stroking it again
        /// on every paint, and the pen width it was made for. Qt's outline
        /// keeps its curves, so it serves at any zoom; a width that depends
        /// on where the shape is drawn (scaleMode) makes it again.
        mutable QPainterPath strokeOutline;
        mutable double strokeOutlineWidth = -1.0;
    };

    /// Paints one cached entry: its fill and stroke, or its procedural
    /// marks, or in outline view its outline.
    void paintEntry(QPainter& painter, const PathCacheEntry& entry);
    typedef QList<PathCacheEntry> PathCacheList;

    /// A cached stroke entry's pen with its width set for the element being drawn.
    QPen penForHere(const PathCacheEntry& entry) const;
    QMap<const fla::Shape*, PathCacheList> _pathCache;

    /// Decoded library bitmaps, by name, with the bytes each was made from
    /// (a bitmap brush, or undoing one, gives a bitmap others). Kept within
    /// a budget, the least recently shown going first: a document with many
    /// large bitmaps does not hold them all decoded.
    struct CachedBitmap
    {
        QPixmap pixmap;
        std::weak_ptr<const void> source;
    };
    static constexpr int kBitmapCacheKilobytes = 512 * 1024;
    QCache<QString, CachedBitmap> _bitmapCache{kBitmapCacheKilobytes};
    /// The library's bitmaps by name, rebuilt when a lookup misses.
    QHash<QString, const fla::Bitmap*> _bitmapsByName;
    const fla::Bitmap* findBitmap(const QString& name);

public:
    /// The library bitmap of that name, decoded once and kept.
    QPixmap libraryBitmap(const QString& name);

    /// Drops a library bitmap from the view's cache, after its pixels change.
    void forgetBitmap(const QString& name) { _bitmapCache.remove(name); }

    /// A texture's tile of coverage, built in or from the library, and a
    /// brush of \a color taking on its grain.
    QImage textureTile(const std::string& name);
    QBrush texturedBrush(const QColor& color, const fla::texture::Spec& spec);

private:

    /// Qt's spread for a gradient's spreadMethod attribute.
    static QGradient::Spread gradientSpread(const std::string& method);


    // Bounds cache for culling
    /// Keyed by element and how far into its keyframe it is measured (-1
    /// for an element that looks the same on every frame): an animated
    /// symbol's instance is as big as the frame it shows.
    using BoundsKey = QPair<const fla::Element*, int>;
    QHash<BoundsKey, QRectF> _boundsCache;
    /// Behaviors worked out, by instance and frame, for _behaviorRevision.
    QHash<QPair<const void*, int>, fla::behaviors::Copies> _behaviorCache;
    /// The copy of an instance being drawn, while drawElement draws each of
    /// the copies its behaviors make.
    const fla::Element* _copyElement = nullptr;
    const fla::behaviors::Delta* _copyDelta = nullptr;
    unsigned _behaviorRevision = ~0u;
    /// Each symbol's bounds over all its frames: its visible layers'
    /// (culling, 9-slice) and all its art's (the offscreen a filtered or
    /// blended instance draws into). Walking every frame recursively is
    /// costly, and an isolated instance needs it on every paint. Cleared
    /// with the other caches on any edit.
    QHash<const fla::Symbol*, QRectF> _symbolBoundsCache;
    /// Everything inside the symbol of the instance selected in the
    /// Outliner, worked out once per selection and edit rather than for
    /// every shape painted.
    mutable std::unordered_set<const fla::DOMElement*> _insideSelected;
    mutable bool _insideSelectedValid = false;

    /// The 2x buffer the stage is drawn into for supersampling, kept.
    QImage _supersample;
    /// Clipped bitmap fills grown by their edge pixels, by bitmap and
    /// padding, within a budget.
    QCache<QString, QPixmap> _paddedFills{256 * 1024};
    QHash<const fla::Symbol*, QRectF> _symbolReachCache;
    QRectF symbolReach(const fla::Symbol* symbol);
    /// An element's bounds in the space of what holds it, over every frame
    /// of a symbol it places: for symbol bounds, culling and offscreens.
    QRectF memberReach(const fla::Element* element);
    QHash<BoundsKey, QRectF> _artBoundsCache;
    /// How deep bounds are being worked out inside a symbol's frame, where
    /// _elapsed is that frame's playhead.
    int _boundsDepth = 0;
    /// Whether an element can look different from frame to frame: an
    /// instance of a symbol longer than a frame, or a group holding one.
    bool animates(const fla::Element* element, int depth = 0);
    /// How far into its keyframe \a element is measured: the frame being
    /// drawn or measured inside a symbol, else the playhead's distance into
    /// the keyframe that holds it. -1 when it never changes.
    int boundsElapsed(const fla::Element* element);
    /// True while art bounds are being worked out: no antialiasing margin,
    /// and the padded cache is neither read nor filled.
    bool _tightBounds = false;

    /// The stage's elements for snapping, kept between drags and dropped
    /// with the bounds cache: each one's bounds, and its artwork's vertices
    /// and outline (curves as short straight pieces) in document space.
    struct SnapEntry
    {
        fla::Element* element = nullptr;
        QRectF bounds;
        std::vector<QPointF> vertices;
        std::vector<QLineF> outline;
    };
    std::vector<SnapEntry> _snapBounds;

    /// Adds the vertices and outline of \p element's shapes to \p entry.
    void gatherSnapOutline(const fla::Element* element, SnapEntry& entry);
    bool _snapBoundsValid = false;

    /// The pen for a stroke. The width follows the stroke's scaleMode for
    /// the element being drawn unless told not to, which a cached pen is,
    /// since it is drawn under many transforms.
    QPen getPen(const fla::StrokeStyle* strokeStyle, bool scaleForElement = true);

    QBrush getFillBrush(const fla::FillStyle* fillStyle, const fla::Rect& bounds);

    void drawDocument(QPainter& painter, const fla::Document* document);

    void drawTimeline(QPainter& painter, const fla::Timeline* timeline, fla::LoopType loopType = fla::LoopType::PlayOnce, int firstFrame = 0);

    void drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame, const QPixmap* maskPixmap = nullptr);

    void drawFrame(QPainter& painter, const fla::Frame* frame, const fla::Frame* tweenFrame = nullptr, double tweenProgress = 0.0);

    void drawElement(QPainter& painter, const fla::Element* element, const fla::Element* tweenElement, double tweenProgress);

    fla::Transform interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t);

    int calculateLayerDuration(const fla::Layer* layer);

    /// The frames a symbol's timeline runs to: the end of its longest layer.
    static int symbolLength(const fla::Symbol* symbol);

    /// The frame of its symbol an instance shows, _elapsed frames into its
    /// keyframe. A graphic runs from its first frame by its loop setting,
    /// wrapping round the symbol (loop), stopping at its end (play once) or
    /// holding (single frame). On the editing stage a movie clip or a button
    /// shows its first frame; an exported movie plays its movie clips round
    /// and round from when they appear, or, with frame actions, as their
    /// frame scripts' stops and gotos lead.
    int symbolFrame(const fla::SymbolInstance& instance) const;
    bool _frameActions = false;
    /// Each movie clip's frames with frame actions, worked out once a
    /// drawing of the document.
    mutable std::map<const fla::Symbol*, fla::frameactions::TimelineActions::Sequence> _clipSequences;

    /// Which frame of a layer is showing right now, and how far a tween on it
    /// has progressed. Shared by drawing and hit-testing so the two cannot
    /// disagree about what is on screen.
    struct LayerFrame
    {
        const fla::Frame* frame = nullptr;
        const fla::Frame* tweenFrame = nullptr;
        double tweenProgress = 0.0;
        /// How many frames into the keyframe's span the shown frame is.
        int offset = 0;
    };

    LayerFrame resolveLayerFrame(const fla::Layer* layer, fla::LoopType loopType, int firstFrame);

    /// The transform an element of a resolved frame shows with: an object
    /// motion tween's (read from the frame's motionObjectXML), a classic
    /// tween's in-between, or its own.
    fla::Transform shownTransform(const LayerFrame& state, size_t elementIndex);
    /// What \a instance's behaviors make of it on frame \a at of its
    /// timeline, given its keyed transform there: a Delta for each copy
    /// drawn (one for an instance without behaviors). Kept for each
    /// instance and frame until the document changes.
    fla::behaviors::Copies behaviorCopies(const fla::SymbolInstance& instance, const fla::Transform& keyed, int at);

    /// The parsed motion of a frame with an object motion tween, kept per
    /// frame; null when it has none.
    const fla::MotionObject* motionOf(const fla::Frame* frame);
    /// Each frame's object motion, parsed once: the XML it came from, and
    /// the view's revision when it was last checked against the frame (no
    /// edit since, no need to compare the XML again).
    struct CachedMotion
    {
        std::string xml;
        std::shared_ptr<fla::MotionObject> motion;
        unsigned revision = 0;
    };
    std::map<const fla::Frame*, CachedMotion> _motionCache;
    /// Moves on with any edit that may touch a frame's motion (clearCaches,
    /// invalidateBounds), so the XML is looked at again.
    unsigned _motionRevision = 0;

    /// The armature of symbols a pose frame keeps, parsed once, with the
    /// joint each instance follows by its referenceID; null when the frame
    /// has none, or its span is a single frame, so its instances stay put.
    struct CachedArmature
    {
        std::string xml;
        std::shared_ptr<fla::IKArmature> armature;
        std::map<std::string, int> joints;
        unsigned revision = 0;
        bool parsed = false;
    };
    const CachedArmature* symbolArmatureOf(const fla::Frame* frame);
    std::map<const fla::Frame*, CachedArmature> _armatureCache;
    /// Where an instance of an armature of symbols shows \a offset frames
    /// into its pose span; false when it is not in one that moves.
    bool armatureTransform(const fla::Frame* frame, const fla::Element* element, int offset, fla::Transform& shown);
    /// The same for \a element at the playhead, on the timeline shown.
    bool armatureHere(const fla::Element* element, fla::Transform& shown);

    /// The path of the motion guide a frame's layer is guided by, from the
    /// guide layer's strokes as shown now; null when it has none.
    const fla::GuidePath* guidePathOf(const fla::Frame* frame);
    std::map<const fla::Frame*, std::pair<std::vector<const fla::Shape*>, std::shared_ptr<fla::GuidePath>>> _guideCache;

    /// A classic tween's \a tweened transform, carried along the motion
    /// guide of its layer, when it has one, from \a start's place to \a end's
    /// (and turned with it when it orients to the path).
    fla::Transform guidedTransform(const LayerFrame& state, const fla::Element& start, const fla::Element& end,
        const fla::Transform& tweened);

    /// Where an element of an object motion tween is now: its span, how
    /// far in (frames, and TimeScale units), and the motion's state at the
    /// start and now.
    struct MotionHere
    {
        fla::Frame* frame = nullptr;
        int offset = 0;
        double time = 0.0;
        fla::MotionState start;
        fla::MotionState now;
    };
    /// False when \a element is not the object of a motion tween shown now.
    bool motionHere(const fla::Element* element, MotionHere& here);

    struct PathDrag
    {
        fla::Element* element = nullptr;
        fla::Frame* frame = nullptr;
        int index = 0;
        bool key = false;
        QPointF grab;
        std::vector<std::string> children;
        fla::Transform transform;
    };
    PathDrag _pathDrag;

    struct HintDrag
    {
        fla::Frame* tween = nullptr;
        bool start = true;
        int index = -1;
        std::vector<std::pair<std::string, std::string>> before;
    };
    HintDrag _hintDrag;

    /// Brush texture tiles by name, within a budget like the bitmaps'.
    QCache<QString, QImage> _textureTiles{64 * 1024};

    bool _editMultiple = false;
    /// While the other keyframes of Edit Multiple Frames draw: only layers
    /// with a keyframe at the frame drawn, and not their current one.
    bool _multiKeysOnly = false;
    /// The keyframes of \a layer that Edit Multiple Frames adds, nearest first.
    std::vector<fla::Frame*> multipleFrames(const fla::Layer* layer) const;

    /// While drawing an instance a motion tween colors or filters: the color
    /// effect and filters it has there.
    const fla::Element* _overrideStyleElement = nullptr;
    fla::ColorTransform _overrideColor;
    std::vector<fla::Filter> _overrideFilters;

    /// Layer parenting: the transform a layer's content is placed by, its
    /// parent layer's element as shown, through its own parent in turn.
    /// Identity for a layer with no parent. A frame names its parent by the
    /// parent layer's layerRiggingIndex. The layer's parent propagation
    /// settings leave out the parent's scale, skew or flip. Drawing, hit
    /// testing and the tools all place parented content by this.
    QTransform parentTransform(const fla::Layer* layer, const fla::Frame* frame, fla::LoopType loopType,
        int firstFrame, int depth = 0);
    /// The parenting chain alone, without the camera.
    QTransform rigTransform(const fla::Layer* layer, const fla::Frame* frame, fla::LoopType loopType,
        int firstFrame, int depth);
    /// How the scene's multiplane camera shows a layer of the scene, for its
    /// depth, on the frame shown; identity with no camera or the view off.
    QTransform cameraTransform(const fla::Layer* layer);
    /// The camera's color effect on the frame shown, when the scene is seen
    /// through a camera; the identity otherwise.
    fla::ColorTransform cameraColor();
    bool _cameraView = true;

    /// An element drawn with a transform other than its own (an object
    /// motion tween's), set while that element is drawn.
    const fla::Element* _overrideElement = nullptr;
    fla::Transform _overrideTransform;
    /// The resolved frame drawFrame is drawing, for its motion.
    const LayerFrame* _drawnState = nullptr;

    /// How many frames the playhead of the timeline being drawn (or picked
    /// in, or measured) is into the keyframe whose elements are being
    /// visited: a graphic among them is that many frames on from its first
    /// frame.
    int _elapsed = 0;

    /// Sets _elapsed for a frame's elements, and puts it back after.
    struct ElapsedScope
    {
        int& slot;
        int saved;
        ElapsedScope(int& elapsed, int now)
            : slot(elapsed)
            , saved(elapsed)
        {
            slot = now;
        }
        ~ElapsedScope() { slot = saved; }
    };

    /// Asset Warp and deformers: while a puppet or a deformed instance draws
    /// its art, where the warp takes each point of its own space, and the
    /// painter's mapping of that space, so each shape drawn inside can be
    /// taken there, warped and drawn.
    bool _warping = false;
    std::function<fla::Point(const fla::Point&)> _warpMap;
    QTransform _warpBase;
    QTransform _warpBaseInverse;
    /// \a path, in a shape's space, taken by \a toPuppet into the puppet's
    /// space, flattened finely and warped by the pins.
    QPainterPath warpedPath(const QPainterPath& path, const QTransform& toPuppet) const;
    /// Draws a bitmap through the warp, a piece of mesh at a time.
    void drawWarpedPixmap(QPainter& painter, const QPixmap& pixmap) const;
    /// The graphic symbol a stroke is drawn with as a brush, or null.
    const fla::Symbol* brushOf(const fla::StrokeStyle& stroke) const;
    /// A stroke drawn with a brush: the symbol bent along its centerline,
    /// stretched end to end or repeated.
    void drawBrushStroke(QPainter& painter, const PathCacheEntry& entry, const fla::Symbol& brush);

public:
    /// A movie clip's 3D turn and depth, in perspective, as a map in the
    /// space it is placed in, to follow its 2D transform \a placed; the
    /// identity for a flat clip. Part way to \a tweenElement's at
    /// \a progress.
    QTransform threeDTransform(const fla::Element* element, const QTransform& placed,
        const fla::Element* tweenElement = nullptr, double progress = 0.0) const;

    /// A shape's strokes as drawn: each run of joined edges of one stroke
    /// style, its centerline in the shape's own space, from its start.
    std::vector<std::pair<QPainterPath, const fla::StrokeStyle*>> strokeRuns(const fla::Shape* shape);

private:

    /// Draws a shape, or, part way through a shape tween to \a tweenShape,
    /// the morph between them. \a ownTransform is the element's transform
    /// as drawn (interpolated during a tween), which the painter already
    /// carries; the morph is built on the stage and brought back through it.
    void drawShape(QPainter& painter, const fla::Shape* shape, const fla::Shape* tweenShape,
        double tweenProgress, const QTransform& ownTransform = QTransform());

    /// Builds a shape's fill and stroke paths without a painter, so drawing and
    /// hit-testing work from the same geometry.
    void buildShapePaths(const fla::Shape* shape, PathCacheList& cacheEntries);

    /// The shape's paths in its own coordinate space, building and caching them
    /// on first use.
    const PathCacheList& shapePaths(const fla::Shape* shape);

    // Hit-testing, mirroring the draw traversal so what is pickable matches what
    // is visible.
    void hitTestTimeline(const fla::Timeline& timeline, fla::LoopType loopType,
        int firstFrame, const QTransform& toDocument, const QPointF& documentPos,
        double tolerance, HitResult& result);

    void hitTestElement(fla::Element& element, fla::Frame* frame, fla::LoopType loopType,
        const QTransform& toDocument, const QPointF& documentPos, double tolerance,
        HitResult& result, const fla::Transform* shownTransform = nullptr);

    /// Where an element shows at the playhead, in the space of the timeline
    /// being edited: its content as shown now (a graphic's current frame, not
    /// its first) through the transform it shows with and its layer's
    /// parent. Falls back to its cached bounds when it is not on a frame
    /// that shows.
    QRectF shownElementBounds(const fla::Element* element);
    /// The bounds of what a symbol shows, in its own space, playing as
    /// \a loopType from \a firstFrame.
    QRectF shownSymbolBounds(const fla::Symbol* symbol, fla::LoopType loopType, int firstFrame, int depth);
    /// The bounds of an element in its own space, as it shows now.
    QRectF shownLocalBounds(const fla::Element* element, int depth);

    static bool isNearAnchor(const fla::Shape& shape, const QPointF& localPos, double tolerance);

    // Bounds calculation
    QRectF calculateElementBounds(const fla::Element* element);

    QRectF calculateShapeBounds(const fla::Shape* shape);

    QRectF getElementBounds(const fla::Element* element);

    // Helper methods
    void resetView();

    void drawDocumentBounds(QPainter& painter, const fla::Document* document);

    void drawTimelineBounds(QPainter& painter, const fla::Timeline* timeline);

    void drawElementBounds(QPainter& painter, const fla::Element* element);

    void applyColorTransform(QImage& image, const fla::ColorTransform& colorTransform);

    QRectF calculateSymbolLocalBounds(const fla::Symbol* symbol);

    QRectF calculateElementLocalBounds(const fla::Element* element);

    bool isElementSelected(const fla::DOMElement* element) const;

    bool isElementInsideSymbol(const fla::DOMElement* element, const fla::Symbol* symbol) const;

    bool isElementInsideGroup(const fla::DOMElement* element, const fla::Group* group) const;

    void drawOverlayPoints(QPainter& painter, const fla::Shape* shape);

    /// The dot screen over a selected merge-drawn shape: the fills and
    /// the strokes it is made of, screened in the highlight color.
    void drawShapeHighlight(QPainter& painter, const fla::Shape* shape, const QColor& color);

    /// The bones of the armature on a pose frame, read from the IKTree the
    /// frame keeps, drawn while its art is selected.
    void drawIKBones(QPainter& painter, const fla::Frame& frame, double scale);

    /// Draws the marks that show what is selected, plus whatever the active tool
    /// wants on top. Runs in document coordinates after the stage is drawn.
    void drawToolOverlay(QPainter& painter);

    /// Draws the document's grid under the artwork, using the spacing and color
    /// the file specifies.
    void drawGrid(QPainter& painter, const fla::Document* document);
};
