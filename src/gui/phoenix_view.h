#pragma once

#include "../data/color_transform.h"
#include "../data/edge.h"
#include "../data/fla_document.h"
#include "../data/group.h"
#include "../data/rect.h"
#include "../data/shape.h"
#include "../data/symbol.h"

#include <QList>
#include <QMap>
#include <QRectF>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>

#include "tool.h"

#include <vector>

namespace fla {
class Selection;
}

class Player;

class PhoenixView : public QWidget
{
    Q_OBJECT
public:
    PhoenixView(Player* player, QWidget *parent = nullptr);

    ~PhoenixView();

    void setDocument(const fla::FLADocument* document);

    void setShowBounds(bool show) { _showBounds = show; update(); }

    bool showBounds() const { return _showBounds; }

    void setDimOutsideDocument(bool dim) { _dimOutsideDocument = dim; update(); }

    bool dimOutsideDocument() const { return _dimOutsideDocument; }

    void setHighQualityAntiAliasing(bool on);

    /// Maps document coordinates to widget coordinates: the pan, the zoom, and
    /// the centring that keeps the stage in the middle of the view.
    ///
    /// The single source of truth for painting, hit-testing and the zoom anchor,
    /// so they cannot drift apart.
    QTransform documentToWidget() const;

    QPointF mapToDocument(const QPointF& widgetPos) const;

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

    double zoom() const { return _zoom; }

    /// Document-space bounds of an element, cached. Tools use this to place
    /// handles around what is selected.
    QRectF elementBounds(const fla::Element* element) { return getElementBounds(element); }

    /// Top-level elements of the visible frames whose bounds meet
    /// \p documentRect. Does not descend into groups or symbols: a marquee
    /// picks whole objects at the level being edited, the way Animate does.
    std::vector<fla::Element*> elementsIn(const QRectF& documentRect);

    /// The tool receiving stage input. Null routes everything to panning.
    void setActiveTool(Tool* tool);

    Tool* activeTool() const { return _activeTool; }

    /// The selection shown on the stage. Borrowed, not owned.
    void setSelection(fla::Selection* selection);

    fla::Selection* selection() const { return _selection; }

    bool highQualityAntiAliasing() const { return _highQualityAntiAliasing; }

    void clearCaches();

    /// Drops just the cached bounds. Transforming an element changes where it
    /// sits but not its path geometry, so the expensive path cache survives a
    /// drag.
    void invalidateBounds();

    // Export the currently displayed frame to an SVG file (no animation).
    // Renders in document space using the same traversal as on-screen drawing.
    bool exportToSvg(const QString& filePath);

public slots:
    void onPlayerFrameChanged(int frame);

    void onElementSelected(const fla::DOMElement* element);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;

    bool _highQualityAntiAliasing;

    // Pan and zoom state
    double _zoom;
    double _panX;
    double _panY;
    double _minZoom = 0.05;
    double _maxZoom = 100.0;
    bool _isDragging;
    QPoint _lastMousePos;
    QTransform viewTransform;

    // Visible rect in document coordinates (for culling)
    QRectF _visibleRect;

    // Debug: show bounding boxes
    bool _showBounds;

    // Dim area outside document
    bool _dimOutsideDocument = false;

    bool _disableStaticText = false;

    double _radius = 1.0;
    QPen _overlayPen;
    QBrush _overlayBrush;

    // Selected element for overlay
    const fla::DOMElement* _selectedElement = nullptr;

    Tool* _activeTool = nullptr;
    fla::Selection* _selection = nullptr;

    struct PathCacheEntry
    {
        QBrush fillBrush;
        QPen pen;
        QPainterPath painterPath;
    };
    typedef QList<PathCacheEntry> PathCacheList;
    QMap<const fla::Shape*, PathCacheList> _pathCache;

    QMap<QString, QPixmap> _bitmapCache;

    QMap<QString, QFont> _fontCache;

    // Bounds cache for culling
    QMap<const fla::Element*, QRectF> _boundsCache;

    QPen getPen(const fla::StrokeStyle* strokeStyle);

    QBrush getFillBrush(const fla::FillStyle* fillStyle, const fla::Rect& bounds);

    void drawDocument(QPainter& painter, const fla::Document* document);

    void drawTimeline(QPainter& painter, const fla::Timeline* timeline, fla::LoopType loopType = fla::LoopType::PlayOnce, int firstFrame = 0);

    void drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame, const QPixmap* maskPixmap = nullptr);

    void drawFrame(QPainter& painter, const fla::Frame* frame, const fla::Frame* tweenFrame = nullptr, double tweenProgress = 0.0);

    void drawElement(QPainter& painter, const fla::Element* element, const fla::Element* tweenElement, double tweenProgress);

    fla::Transform interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t);

    int calculateLayerDuration(const fla::Layer* layer);

    /// Which frame of a layer is showing right now, and how far a tween on it
    /// has progressed. Shared by drawing and hit-testing so the two cannot
    /// disagree about what is on screen.
    struct LayerFrame
    {
        const fla::Frame* frame = nullptr;
        const fla::Frame* tweenFrame = nullptr;
        double tweenProgress = 0.0;
    };

    LayerFrame resolveLayerFrame(const fla::Layer* layer, fla::LoopType loopType, int firstFrame);

    void drawShape(QPainter& painter, const fla::Shape* shape, const fla::Shape* tweenShape, double tweenProgress);

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
        const QTransform& toDocument, const QPointF& documentPos,
        double tolerance, HitResult& result);

    /// Converts a document-space tolerance into the element's own space, so the
    /// grab distance stays constant on screen however the element is scaled.
    static double toleranceInLocalSpace(const QTransform& elementToDocument, double tolerance);

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

    /// Draws the marks that show what is selected, plus whatever the active tool
    /// wants on top. Runs in document coordinates after the stage is drawn.
    void drawToolOverlay(QPainter& painter);
};
