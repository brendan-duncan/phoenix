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
