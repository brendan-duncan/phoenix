#include "node_graph_style.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <map>

namespace nodestyle {

QColor categoryColor(const std::string& category)
{
    static const std::map<std::string, QColor> colors = {
        {"Input", QColor(0x3E, 0x8E, 0x66)},
        {"Combine", QColor(0x4A, 0x6F, 0xA8)},
        {"Transform", QColor(0x8E, 0x5C, 0xA8)},
        {"Time", QColor(0xB0, 0x5C, 0x86)},
        {"Blur", QColor(0x3D, 0x84, 0xC6)},
        {"Color", QColor(0xC0, 0x5E, 0x3E)},
        {"Distort", QColor(0xA8, 0x7A, 0x34)},
        {"Generate", QColor(0x34, 0x98, 0x8E)},
        {"Transition", QColor(0x6E, 0x5C, 0xD6)},
        {"Utility", QColor(0x5A, 0x66, 0x75)},
        {"Output", QColor(0xD8, 0x84, 0x30)},
    };
    const auto found = colors.find(category);
    return found == colors.end() ? QColor(0x5A, 0x66, 0x75) : found->second;
}

void drawIcon(QPainter& painter, const QRectF& box, const std::string& category, const QColor& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = std::min(box.width(), box.height());
    const QPointF c = box.center();
    const double u = s / 12.0;
    QPen pen(color, std::max(1.0, 1.3 * u), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const auto at = [&](double x, double y) { return QPointF(c.x() + x * u, c.y() + y * u); };

    if (category == "Input")
    {
        // Two layers, one over the other.
        painter.drawRoundedRect(QRectF(at(-5, -2.5), at(2.5, 5)), u, u);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(at(-2.5, -5), at(5, 2.5)), u, u);
    }
    else if (category == "Combine")
    {
        painter.drawEllipse(at(-1.8, 0), 3.6 * u, 3.6 * u);
        painter.setBrush(color);
        painter.drawEllipse(at(1.8, 0), 3.6 * u, 3.6 * u);
    }
    else if (category == "Transform")
    {
        // Arrows out four ways.
        painter.drawLine(at(-5, 0), at(5, 0));
        painter.drawLine(at(0, -5), at(0, 5));
        for (const auto& [dx, dy] : {std::pair<double, double>{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        {
            const QPointF tip = at(5 * dx, 5 * dy);
            painter.drawLine(tip, at(5 * dx - 2 * dx - 1.6 * dy, 5 * dy - 2 * dy - 1.6 * dx));
            painter.drawLine(tip, at(5 * dx - 2 * dx + 1.6 * dy, 5 * dy - 2 * dy + 1.6 * dx));
        }
    }
    else if (category == "Blur")
    {
        // A drop, softly ringed.
        QPainterPath drop;
        drop.moveTo(at(0, -5.5));
        drop.cubicTo(at(3, -1.5), at(4, 0.5), at(4, 2));
        drop.cubicTo(at(4, 4.5), at(2, 5.5), at(0, 5.5));
        drop.cubicTo(at(-2, 5.5), at(-4, 4.5), at(-4, 2));
        drop.cubicTo(at(-4, 0.5), at(-3, -1.5), at(0, -5.5));
        QColor soft = color;
        soft.setAlphaF(color.alphaF() * 0.35);
        painter.setBrush(soft);
        painter.drawPath(drop);
    }
    else if (category == "Color")
    {
        // A circle half filled.
        painter.drawEllipse(c, 5 * u, 5 * u);
        QPainterPath half;
        half.moveTo(at(0, -5));
        half.arcTo(QRectF(at(-5, -5), at(5, 5)), 90, 180);
        half.closeSubpath();
        painter.setBrush(color);
        painter.drawPath(half);
    }
    else if (category == "Distort")
    {
        QPainterPath wave;
        wave.moveTo(at(-5.5, 0));
        for (int i = 0; i < 4; ++i)
        {
            const double x0 = -5.5 + i * 2.75;
            wave.quadTo(at(x0 + 1.375, i % 2 ? 3.5 : -3.5), at(x0 + 2.75, 0));
        }
        painter.drawPath(wave);
    }
    else if (category == "Generate")
    {
        // A four-pointed spark.
        QPainterPath star;
        star.moveTo(at(0, -5.5));
        star.quadTo(at(0.6, -0.6), at(5.5, 0));
        star.quadTo(at(0.6, 0.6), at(0, 5.5));
        star.quadTo(at(-0.6, 0.6), at(-5.5, 0));
        star.quadTo(at(-0.6, -0.6), at(0, -5.5));
        painter.setBrush(color);
        painter.drawPath(star);
    }
    else if (category == "Transition")
    {
        // A square split on the diagonal.
        painter.drawRoundedRect(QRectF(at(-5, -5), at(5, 5)), u, u);
        QPainterPath half;
        half.moveTo(at(-5, 5));
        half.lineTo(at(5, -5));
        half.lineTo(at(5, 5));
        half.closeSubpath();
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPath(half);
    }
    else if (category == "Time")
    {
        // A clock.
        painter.drawEllipse(c, 5.2 * u, 5.2 * u);
        painter.drawLine(c, at(0, -3.2));
        painter.drawLine(c, at(2.4, 1.2));
    }
    else if (category == "Output")
    {
        // A screen with its stand.
        painter.drawRoundedRect(QRectF(at(-5.5, -4.5), at(5.5, 2.5)), u, u);
        painter.drawLine(at(0, 2.5), at(0, 5));
        painter.drawLine(at(-3, 5), at(3, 5));
    }
    else
    {
        painter.setBrush(color);
        painter.drawEllipse(c, 2.5 * u, 2.5 * u);
    }
    painter.restore();
}

} // namespace nodestyle
