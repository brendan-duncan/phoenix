#pragma once

#include <QColor>
#include <QRectF>

#include <string>

class QPainter;

/// How the node graph's kinds of node look, shared by the graph and its
/// menu of nodes: a color for each category and a small icon drawn for it.
namespace nodestyle {

/// The header color of a category's nodes.
QColor categoryColor(const std::string& category);
/// The category's icon, drawn in \a color within \a box.
void drawIcon(QPainter& painter, const QRectF& box, const std::string& category, const QColor& color);

} // namespace nodestyle
