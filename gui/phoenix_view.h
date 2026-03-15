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

    void setHighQualityAntiAliasing(bool on);

    bool highQualityAntiAliasing() const { return _highQualityAntiAliasing; }

    void clearCaches();

public slots:
    void onPlayerFrameChanged(int frame);

    void onElementSelected(const fla::DOMElement* element);

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

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

    // Visible rect in document coordinates (for culling)
    QRectF _visibleRect;

    // Debug: show bounding boxes
    bool _showBounds;

    bool _disableStaticText = false;

    double _radius = 1.0;
    QPen _overlayPen;
    QBrush _overlayBrush;

    // Selected element for overlay
    const fla::DOMElement* _selectedElement = nullptr;

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

    void drawFrame(QPainter& painter, const fla::Frame* frame, const fla::Frame* nextTweenFrame = nullptr, double tweenProgress = 0.0);

    void drawElement(QPainter& painter, const fla::Element* element, const fla::Transform* transformOverride = nullptr);

    fla::Transform interpolateTransform(const fla::Transform& a, const fla::Transform& b, double t);

    void drawShape(QPainter& painter, const fla::Shape* shape);

    // Bounds calculation
    QRectF calculateElementBounds(const fla::Element* element);

    QRectF calculateShapeBounds(const fla::Shape* shape);

    QRectF getElementBounds(const fla::Element* element);

    // Helper methods
    void resetView();

    QPointF screenToScene(const QPointF& screenPos) const;

    QPointF sceneToScreen(const QPointF& scenePos) const;

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
};
