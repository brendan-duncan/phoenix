#include "tool_icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>

namespace {

/// Glyphs are drawn on this grid and scaled to whatever size is asked for, so
/// one set of coordinates serves every icon size.
constexpr double kGrid = 24.0;

constexpr double kPi = 3.14159265358979323846;

QColor strokeColour()
{
    // Light enough to read on the dark toolbar, short of pure white so the
    // icons do not shout over the artwork.
    return QColor(220, 220, 225);
}

/// The arrow shared by the selection and subselection tools.
QPolygonF arrowShape()
{
    return QPolygonF({
        QPointF(6.0, 3.0), QPointF(6.0, 18.0), QPointF(10.0, 14.5),
        QPointF(12.5, 20.0), QPointF(15.0, 19.0), QPointF(12.5, 13.5),
        QPointF(17.5, 13.0)
    });
}

void drawSelection(QPainter& painter)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    painter.drawPolygon(arrowShape());
}

void drawSubselection(QPainter& painter)
{
    // The same arrow hollowed out, which is how every editor distinguishes the
    // two.
    painter.setPen(QPen(strokeColour(), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(arrowShape());
}

void drawFreeTransform(QPainter& painter)
{
    const QRectF box(5.0, 5.0, 14.0, 14.0);

    painter.setPen(QPen(strokeColour(), 1.2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);

    // The corner grips are what make it read as a transform box rather than a
    // rectangle.
    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    const QPointF corners[4] = {
        box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()
    };
    for (const QPointF& corner : corners)
        painter.drawRect(QRectF(corner.x() - 2.0, corner.y() - 2.0, 4.0, 4.0));
}

void drawRectangle(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(4.0, 6.0, 16.0, 12.0));
}

void drawOval(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(3.5, 6.0, 17.0, 12.0));
}

void drawLine(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.8, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(4.5, 19.0), QPointF(19.5, 5.0));
}

void drawPolyStar(QPainter& painter)
{
    const QPointF centre(12.0, 12.5);
    const double outer = 8.5;
    const double inner = 3.6;

    QPolygonF star;
    for (int i = 0; i < 10; ++i)
    {
        // Start at the top so the star points upward.
        const double angle = -kPi / 2.0 + (kPi * i) / 5.0;
        const double radius = (i % 2 == 0) ? outer : inner;
        star << QPointF(centre.x() + radius * std::cos(angle),
                        centre.y() + radius * std::sin(angle));
    }

    painter.setPen(QPen(strokeColour(), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(star);
}

void drawPen(QPainter& painter)
{
    // A nib: a tapering body with a slit and a point.
    QPolygonF body({
        QPointF(8.0, 3.5), QPointF(16.0, 3.5), QPointF(14.0, 15.0),
        QPointF(12.0, 20.5), QPointF(10.0, 15.0)
    });

    painter.setPen(QPen(strokeColour(), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(body);

    painter.drawLine(QPointF(12.0, 8.0), QPointF(12.0, 15.0));
    painter.setBrush(strokeColour());
    painter.drawEllipse(QPointF(12.0, 15.5), 1.3, 1.3);
}

void drawPencil(QPainter& painter)
{
    painter.save();
    painter.translate(12.0, 12.0);
    painter.rotate(45.0);
    painter.translate(-12.0, -12.0);

    painter.setPen(QPen(strokeColour(), 1.2));
    painter.setBrush(Qt::NoBrush);

    // The barrel, the collar, and the sharpened tip.
    painter.drawRect(QRectF(9.5, 3.5, 5.0, 12.0));
    painter.drawLine(QPointF(9.5, 15.5), QPointF(14.5, 15.5));
    painter.drawPolygon(QPolygonF({
        QPointF(9.5, 15.5), QPointF(14.5, 15.5), QPointF(12.0, 20.5)
    }));

    painter.restore();
}

void drawHand(QPainter& painter)
{
    // A palm with four fingers up and a thumb out to the side.
    QPainterPath hand;
    hand.moveTo(8.0, 20.5);
    hand.lineTo(6.0, 13.0);
    hand.lineTo(5.0, 9.0);
    hand.quadTo(5.0, 7.5, 6.8, 8.0);
    hand.lineTo(8.5, 11.5);
    hand.lineTo(8.5, 4.5);
    hand.quadTo(8.5, 3.0, 10.2, 3.0);
    hand.quadTo(11.9, 3.0, 11.9, 4.5);
    hand.lineTo(11.9, 10.0);
    hand.lineTo(12.4, 4.0);
    hand.quadTo(12.4, 2.5, 14.1, 2.5);
    hand.quadTo(15.8, 2.5, 15.8, 4.0);
    hand.lineTo(15.8, 10.5);
    hand.lineTo(16.4, 6.0);
    hand.quadTo(16.4, 4.5, 18.1, 4.5);
    hand.quadTo(19.8, 4.5, 19.8, 6.0);
    hand.lineTo(19.8, 12.0);
    hand.lineTo(19.8, 16.0);
    hand.quadTo(19.8, 20.5, 15.0, 20.5);
    hand.closeSubpath();

    painter.setPen(QPen(strokeColour(), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(hand);
}

void drawZoom(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(4.0, 4.0, 11.0, 11.0));

    QPen handle(strokeColour(), 2.4);
    handle.setCapStyle(Qt::RoundCap);
    painter.setPen(handle);
    painter.drawLine(QPointF(14.0, 14.0), QPointF(20.0, 20.0));

    painter.setPen(QPen(strokeColour(), 1.4));
    painter.drawLine(QPointF(7.0, 9.5), QPointF(12.0, 9.5));
    painter.drawLine(QPointF(9.5, 7.0), QPointF(9.5, 12.0));
}

void drawRectanglePrimitive(QPainter& painter)
{
    // The rectangle with the corner grips a primitive keeps for its radii.
    painter.setPen(QPen(strokeColour(), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(5.0, 6.5, 14.0, 11.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    for (const QPointF& corner : {QPointF(5.0, 6.5), QPointF(19.0, 6.5),
                                  QPointF(19.0, 17.5), QPointF(5.0, 17.5)})
    {
        painter.drawRect(QRectF(corner.x() - 1.8, corner.y() - 1.8, 3.6, 3.6));
    }
}

void drawOvalPrimitive(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(4.0, 6.5, 16.0, 11.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    for (const QPointF& grip : {QPointF(12.0, 6.5), QPointF(20.0, 12.0),
                                QPointF(12.0, 17.5), QPointF(4.0, 12.0)})
    {
        painter.drawRect(QRectF(grip.x() - 1.8, grip.y() - 1.8, 3.6, 3.6));
    }
}

void drawPaintBucket(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF({
        QPointF(8.5, 7.0), QPointF(20.0, 10.5), QPointF(17.5, 20.0), QPointF(6.0, 16.5)
    }));
    painter.drawLine(QPointF(8.5, 7.0), QPointF(6.0, 16.5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    painter.drawPolygon(QPolygonF({
        QPointF(7.5, 8.0), QPointF(5.0, 4.5), QPointF(3.0, 5.5), QPointF(5.5, 9.5)
    }));
}

void drawInkBottle(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(9.0, 8.0, 10.0, 12.0), 1.5, 1.5);
    painter.drawRect(QRectF(6.0, 4.5, 6.0, 5.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    painter.drawEllipse(QRectF(3.0, 2.5, 4.0, 4.0));
}

void drawEyedropper(QPainter& painter)
{
    painter.setPen(QPen(strokeColour(), 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF({
        QPointF(5.5, 19.5), QPointF(4.0, 18.0), QPointF(13.5, 8.5), QPointF(15.0, 10.0)
    }));
    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    painter.drawEllipse(QRectF(14.0, 4.0, 6.0, 6.0));
}

void drawObjectDrawing(QPainter& painter)
{
    // A shape inside its own boundary: what the mode does is keep the two
    // separate rather than letting them merge.
    painter.setPen(QPen(strokeColour(), 1.1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(3.5, 5.5, 17.0, 13.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(strokeColour());
    painter.drawEllipse(QRectF(7.0, 8.0, 10.0, 8.0));
}

QPixmap render(ToolIcons::Tool tool, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double scale = size / kGrid;
    painter.scale(scale, scale);

    switch (tool)
    {
    case ToolIcons::Tool::Selection:     drawSelection(painter); break;
    case ToolIcons::Tool::Subselection:  drawSubselection(painter); break;
    case ToolIcons::Tool::FreeTransform: drawFreeTransform(painter); break;
    case ToolIcons::Tool::Rectangle:     drawRectangle(painter); break;
    case ToolIcons::Tool::Oval:          drawOval(painter); break;
    case ToolIcons::Tool::Line:          drawLine(painter); break;
    case ToolIcons::Tool::PolyStar:      drawPolyStar(painter); break;
    case ToolIcons::Tool::Pen:           drawPen(painter); break;
    case ToolIcons::Tool::Pencil:        drawPencil(painter); break;
    case ToolIcons::Tool::Hand:          drawHand(painter); break;
    case ToolIcons::Tool::Zoom:          drawZoom(painter); break;
    case ToolIcons::Tool::RectanglePrimitive: drawRectanglePrimitive(painter); break;
    case ToolIcons::Tool::OvalPrimitive: drawOvalPrimitive(painter); break;
    case ToolIcons::Tool::PaintBucket:   drawPaintBucket(painter); break;
    case ToolIcons::Tool::InkBottle:     drawInkBottle(painter); break;
    case ToolIcons::Tool::Eyedropper:    drawEyedropper(painter); break;
    case ToolIcons::Tool::ObjectDrawing: drawObjectDrawing(painter); break;
    }

    return pixmap;
}

} // namespace

QIcon ToolIcons::icon(Tool tool)
{
    QIcon result;
    for (int size : {16, 20, 24, 32, 48})
        result.addPixmap(render(tool, size));
    return result;
}
