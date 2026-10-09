#include "cursors.h"

#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace {

constexpr int kSize = 32;

/// A white glyph with a black outline reads on any artwork, which is why
/// every system arrow is drawn that way.
void outlined(QPainter& painter, const QPainterPath& path)
{
    painter.setPen(QPen(Qt::black, 1.0));
    painter.setBrush(Qt::white);
    painter.drawPath(path);
}

/// The selection arrow, with its tip at the origin.
QPainterPath arrowPath()
{
    QPainterPath path;
    path.addPolygon(QPolygonF({
        QPointF(1.0, 1.0), QPointF(1.0, 16.5), QPointF(5.0, 12.5),
        QPointF(8.0, 19.0), QPointF(10.5, 18.0), QPointF(7.5, 11.5),
        QPointF(12.5, 11.5)
    }));
    path.closeSubpath();
    return path;
}

/// A pen nib, with its tip at the origin.
QPainterPath penPath()
{
    QPainterPath path;
    path.addPolygon(QPolygonF({
        QPointF(1.0, 1.0), QPointF(6.0, 3.0), QPointF(15.0, 12.0),
        QPointF(13.5, 16.5), QPointF(9.0, 18.0), QPointF(3.0, 6.0)
    }));
    path.closeSubpath();
    // The slit down the nib.
    path.moveTo(4.0, 4.0);
    path.lineTo(11.0, 12.0);
    return path;
}

/// The small marks that sit at the bottom right of a glyph.
void drawMark(QPainter& painter, Cursors::Kind kind)
{
    painter.setBrush(Qt::NoBrush);
    QPen pen(Qt::black, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const QPointF origin(17.0, 17.0);

    switch (kind)
    {
    case Cursors::Kind::ArrowMove:
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(origin.x(), origin.y(), 7.0, 7.0));
        break;

    case Cursors::Kind::ArrowCorner:
        painter.drawPolyline(QPolygonF({
            QPointF(origin.x(), origin.y() + 8.0), QPointF(origin.x(), origin.y()),
            QPointF(origin.x() + 8.0, origin.y())
        }));
        break;

    case Cursors::Kind::ArrowCurve:
    {
        QPainterPath arc;
        arc.moveTo(origin.x(), origin.y() + 8.0);
        arc.quadTo(origin.x(), origin.y(), origin.x() + 8.0, origin.y());
        painter.drawPath(arc);
        break;
    }

    case Cursors::Kind::PenStart:
        painter.drawLine(QPointF(origin.x(), origin.y()), QPointF(origin.x() + 6.0, origin.y() + 6.0));
        painter.drawLine(QPointF(origin.x() + 6.0, origin.y()), QPointF(origin.x(), origin.y() + 6.0));
        break;

    case Cursors::Kind::PenClose:
        painter.drawEllipse(QRectF(origin.x(), origin.y(), 7.0, 7.0));
        break;

    case Cursors::Kind::PenAdd:
        painter.drawLine(QPointF(origin.x() + 3.5, origin.y()), QPointF(origin.x() + 3.5, origin.y() + 7.0));
        painter.drawLine(QPointF(origin.x(), origin.y() + 3.5), QPointF(origin.x() + 7.0, origin.y() + 3.5));
        break;

    case Cursors::Kind::PenRemove:
        painter.drawLine(QPointF(origin.x(), origin.y() + 3.5), QPointF(origin.x() + 7.0, origin.y() + 3.5));
        break;

    case Cursors::Kind::PenConvert:
        painter.drawPolyline(QPolygonF({
            QPointF(origin.x(), origin.y() + 7.0), QPointF(origin.x() + 3.5, origin.y()),
            QPointF(origin.x() + 7.0, origin.y() + 7.0)
        }));
        break;

    default:
        break;
    }
}

QCursor build(Cursors::Kind kind)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPoint hotspot(1, 1);

    switch (kind)
    {
    case Cursors::Kind::Arrow:
    case Cursors::Kind::ArrowMove:
    case Cursors::Kind::ArrowCorner:
    case Cursors::Kind::ArrowCurve:
        outlined(painter, arrowPath());
        drawMark(painter, kind);
        break;

    case Cursors::Kind::PenStart:
    case Cursors::Kind::Pen:
    case Cursors::Kind::PenClose:
    case Cursors::Kind::PenAdd:
    case Cursors::Kind::PenRemove:
    case Cursors::Kind::PenConvert:
        outlined(painter, penPath());
        drawMark(painter, kind);
        break;

    case Cursors::Kind::ZoomIn:
    case Cursors::Kind::ZoomOut:
    {
        // A magnifier: the hotspot is the middle of the lens.
        QPainterPath lens;
        lens.addEllipse(QRectF(3.0, 3.0, 14.0, 14.0));
        outlined(painter, lens);

        QPen pen(Qt::black, 2.5);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(15.5, 15.5), QPointF(23.0, 23.0));

        painter.setPen(QPen(Qt::black, 1.5));
        painter.drawLine(QPointF(6.5, 10.0), QPointF(13.5, 10.0));
        if (kind == Cursors::Kind::ZoomIn)
            painter.drawLine(QPointF(10.0, 6.5), QPointF(10.0, 13.5));

        hotspot = QPoint(10, 10);
        break;
    }
    }

    painter.end();
    return QCursor(pixmap, hotspot.x(), hotspot.y());
}

} // namespace

QCursor Cursors::cursor(Kind kind)
{
    static QMap<int, QCursor> cache;

    const int key = static_cast<int>(kind);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, build(kind));
    return it.value();
}
