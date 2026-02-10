#pragma once

#include "data/fla_document.h"
#include "data/shape.h"

#include <QList>
#include <QMap>
#include <QRectF>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>

class PhoenixView : public QWidget
{
    Q_OBJECT
public:
    PhoenixView(QWidget *parent = nullptr);

    ~PhoenixView();

    void setDocument(const FLADocument* document);

    void setShowBounds(bool show) { _showBounds = show; update(); }
    bool showBounds() const { return _showBounds; }

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

private:
    const FLADocument* _flaDocument;

    // Pan and zoom state
    double _zoom;
    double _panX;
    double _panY;
    bool _isDragging;
    QPoint _lastMousePos;

    // Visible rect in document coordinates (for culling)
    QRectF _visibleRect;

    // Debug: show bounding boxes
    bool _showBounds;

    struct PathCacheEntry
    {
        QBrush fillBrush;
        QPen pen;
        QPainterPath painterPath;
    };
    typedef QList<PathCacheEntry> PathCacheList;
    QMap<const Shape*, PathCacheList> _pathCache;

    QMap<QString, QPixmap> _bitmapCache;

    // Bounds cache for culling
    QMap<const Element*, QRectF> _boundsCache;

    void drawDocument(QPainter& painter, const Document* document);

    void drawTimeline(QPainter& painter, const Timeline* timeline);

    void drawLayer(QPainter& painter, const Layer* layer);

    void drawFrame(QPainter& painter, const Frame* frame);

    void drawElement(QPainter& painter, const Element* element);

    void drawShape(QPainter& painter, const Shape* shape);

    // Bounds calculation
    QRectF calculateElementBounds(const Element* element);
    QRectF calculateShapeBounds(const Shape* shape);
    QRectF getElementBounds(const Element* element);

    // Helper methods
    void resetView();

    QPointF screenToScene(const QPointF& screenPos) const;

    QPointF sceneToScreen(const QPointF& scenePos) const;
};
