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

    case Cursors::Kind::ArrowDistort:
        painter.setBrush(Qt::white);
        painter.drawPolygon(QPolygonF({
            QPointF(origin.x(), origin.y() + 2.0), QPointF(origin.x() + 7.0, origin.y()),
            QPointF(origin.x() + 8.0, origin.y() + 8.0), QPointF(origin.x() + 2.0, origin.y() + 6.0)
        }));
        break;

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

/// A double-headed arrow along the x axis, centered on the origin.
QPainterPath doubleArrowPath()
{
    QPainterPath path;
    path.addPolygon(QPolygonF({
        QPointF(-11.0, 0.0), QPointF(-5.0, -5.0), QPointF(-5.0, -2.0),
        QPointF(5.0, -2.0), QPointF(5.0, -5.0), QPointF(11.0, 0.0),
        QPointF(5.0, 5.0), QPointF(5.0, 2.0), QPointF(-5.0, 2.0),
        QPointF(-5.0, 5.0)
    }));
    path.closeSubpath();
    return path;
}

/// Two half arrows, one above the axis pulling right and one below pulling
/// left: what a skew does to the two sides of a box.
QPainterPath skewArrowsPath()
{
    QPainterPath path;
    path.addPolygon(QPolygonF({
        QPointF(-9.0, -5.0), QPointF(3.0, -5.0), QPointF(3.0, -8.0),
        QPointF(9.0, -3.5), QPointF(3.0, -1.0), QPointF(3.0, -3.0),
        QPointF(-9.0, -3.0)
    }));
    path.closeSubpath();
    path.addPolygon(QPolygonF({
        QPointF(9.0, 5.0), QPointF(-3.0, 5.0), QPointF(-3.0, 8.0),
        QPointF(-9.0, 3.5), QPointF(-3.0, 1.0), QPointF(-3.0, 3.0),
        QPointF(9.0, 3.0)
    }));
    path.closeSubpath();
    return path;
}

QCursor build(Cursors::Kind kind, int eighthTurns)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPoint hotspot(1, 1);

    switch (kind)
    {
    case Cursors::Kind::Scale:
    case Cursors::Kind::Skew:
    {
        painter.translate(kSize / 2.0, kSize / 2.0);
        painter.rotate(45.0 * eighthTurns);
        outlined(painter, kind == Cursors::Kind::Scale ? doubleArrowPath() : skewArrowsPath());
        hotspot = QPoint(kSize / 2, kSize / 2);
        break;
    }

    case Cursors::Kind::Faucet:
    {
        // A tap seen from the side: a horizontal pipe with a handle on top
        // and the spout turning down to the drop, which is the hotspot.
        QPainterPath pipe;
        pipe.addRoundedRect(QRectF(4.0, 9.0, 16.0, 6.0), 2.0, 2.0);
        outlined(painter, pipe);
        QPainterPath spout;
        spout.addRect(QRectF(16.0, 14.0, 5.0, 7.0));
        outlined(painter, spout);
        QPainterPath handle;
        handle.addRect(QRectF(9.0, 4.0, 3.0, 6.0));
        handle.addRect(QRectF(6.0, 2.0, 9.0, 3.0));
        outlined(painter, handle);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.setBrush(Qt::black);
        painter.drawEllipse(QRectF(16.5, 23.0, 4.0, 5.0));
        hotspot = QPoint(18, 25);
        break;
    }

    case Cursors::Kind::Rotate:
    {
        // An arc most of the way round the hotspot, ending in an arrowhead.
        painter.translate(kSize / 2.0, kSize / 2.0);
        QPainterPath arc;
        arc.arcMoveTo(QRectF(-8.0, -8.0, 16.0, 16.0), 300.0);
        arc.arcTo(QRectF(-8.0, -8.0, 16.0, 16.0), 300.0, -270.0);
        QPen under(Qt::white, 4.5);
        under.setCapStyle(Qt::RoundCap);
        painter.setPen(under);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arc);
        QPen over(Qt::black, 2.0);
        over.setCapStyle(Qt::RoundCap);
        painter.setPen(over);
        painter.drawPath(arc);

        // The head sits at the arc's end, 30 degrees past the top on the right.
        const QPointF end = arc.currentPosition();
        QPainterPath head;
        head.addPolygon(QPolygonF({
            end + QPointF(-4.5, -3.5), end + QPointF(3.5, -4.5), end + QPointF(1.0, 4.0)
        }));
        head.closeSubpath();
        outlined(painter, head);
        painter.setBrush(Qt::black);
        painter.setPen(Qt::NoPen);
        painter.drawPath(head);
        hotspot = QPoint(kSize / 2, kSize / 2);
        break;
    }

    case Cursors::Kind::Arrow:
    case Cursors::Kind::ArrowMove:
    case Cursors::Kind::ArrowCorner:
    case Cursors::Kind::ArrowCurve:
    case Cursors::Kind::ArrowDistort:
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

    case Cursors::Kind::Bucket:
    {
        // A tilted bucket with paint running from its lip at the hotspot.
        QPainterPath bucket;
        bucket.addPolygon(QPolygonF({
            QPointF(8.0, 6.0), QPointF(22.0, 10.0), QPointF(19.0, 22.0), QPointF(6.0, 18.0)
        }));
        bucket.closeSubpath();
        outlined(painter, bucket);
        painter.setPen(QPen(Qt::black, 1.2));
        painter.drawLine(QPointF(8.0, 6.0), QPointF(6.0, 18.0));
        painter.setBrush(Qt::black);
        painter.drawPolygon(QPolygonF({
            QPointF(7.0, 7.0), QPointF(4.0, 3.0), QPointF(1.0, 2.0), QPointF(5.0, 8.0)
        }));
        hotspot = QPoint(1, 2);
        break;
    }

    case Cursors::Kind::InkBottle:
    {
        // A bottle on its side with ink at the mouth, the hotspot.
        QPainterPath bottle;
        bottle.addRoundedRect(QRectF(9.0, 8.0, 13.0, 14.0), 2.0, 2.0);
        outlined(painter, bottle);
        QPainterPath neck;
        neck.addRect(QRectF(5.0, 4.0, 7.0, 6.0));
        outlined(painter, neck);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(QRectF(0.0, 0.0, 6.0, 6.0));
        hotspot = QPoint(2, 2);
        break;
    }

    case Cursors::Kind::Eyedropper:
    {
        // A dropper from top right down to its tip at the hotspot.
        QPainterPath tube;
        tube.addPolygon(QPolygonF({
            QPointF(4.0, 22.0), QPointF(2.0, 20.0), QPointF(14.0, 8.0), QPointF(16.0, 10.0)
        }));
        tube.closeSubpath();
        outlined(painter, tube);
        QPainterPath bulb;
        bulb.addEllipse(QRectF(14.0, 2.0, 8.0, 8.0));
        painter.setPen(QPen(Qt::black, 1.0));
        painter.setBrush(Qt::black);
        painter.drawPath(bulb);
        painter.drawLine(QPointF(12.0, 6.0), QPointF(18.0, 12.0));
        hotspot = QPoint(2, 21);
        break;
    }

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
    return cursor(kind, 0);
}

QCursor Cursors::cursor(Kind kind, int eighthTurns)
{
    static QMap<int, QCursor> cache;

    const int turns = ((eighthTurns % 8) + 8) % 8;
    const int key = static_cast<int>(kind) * 8 + turns;
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, build(kind, turns));
    return it.value();
}
