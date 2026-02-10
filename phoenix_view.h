#pragma once

#include "data/fla_document.h"
#include "data/shape.h"

#include <QList>
#include <QMap>
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

    struct PathCacheEntry
    {
        QBrush fillBrush;
        QPen pen;
        QPainterPath painterPath;
    };
    typedef QList<PathCacheEntry> PathCacheList;
    QMap<const Shape*, PathCacheList> _pathCache;

    QMap<QString, QPixmap> _bitmapCache;

    void drawDocument(QPainter& painter, const Document* document);

    void drawTimeline(QPainter& painter, const Timeline* timeline);

    void drawLayer(QPainter& painter, const Layer* layer);

    void drawFrame(QPainter& painter, const Frame* frame);

    void drawElement(QPainter& painter, const Element* element);

    void drawShape(QPainter& painter, const Shape* shape);

    // Helper methods
    void resetView();

    QPointF screenToScene(const QPointF& screenPos) const;

    QPointF sceneToScreen(const QPointF& scenePos) const;
};
