#pragma once

#include "../data/fla_document.h"
#include "../data/shape.h"

#include <QList>
#include <QMap>
#include <QRectF>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>

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

    void clearCaches();

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onPlayerFrameChanged(int frame);

private:
    const fla::FLADocument* _flaDocument;
    Player* _player;

    bool _highQualityAntiAliasing;

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

    bool _disableStaticText = false;

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

    QBrush getFillBrush(const fla::FillStyle* fillStyle);

    void drawDocument(QPainter& painter, const fla::Document* document);

    void drawTimeline(QPainter& painter, const fla::Timeline* timeline, fla::LoopType loopType = fla::LoopType::PlayOnce, int firstFrame = 0);

    void drawLayer(QPainter& painter, const fla::Layer* layer, fla::LoopType loopType, int firstFrame);

    void drawFrame(QPainter& painter, const fla::Frame* frame);

    void drawElement(QPainter& painter, const fla::Element* element);

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

    fla::Symbol* findSymbolByName(const fla::Document* document, const std::string& name);
};
