#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Document;
class Element;

/// Modify > Document: the stage size and the content with it, and the
/// units the rulers count in. Free of Qt.
namespace docsettings {

enum class Unit
{
    Pixels,
    Inches,
    Points,
    Centimeters,
    Millimeters
};

/// The document's ruler units, as its rulerUnitType attribute names them
/// (none for pixels).
Unit unitOf(const Document& document);
std::vector<std::pair<std::string, std::string>> withUnit(const Document& document, Unit unit);
/// How many stage pixels one unit is (a pixel is a point).
double pixelsPer(Unit unit);
/// The units' short label: px, in, pt, cm, mm.
const char* label(Unit unit);

/// Every element on every frame of every scene, a group's members in its
/// place, as the stage draws them.
std::vector<Element*> sceneElements(Document& document);

/// Sets the stage to \a width by \a height, undoably. With \a scaleContent
/// everything on it scales with the stage from its top left; then it all
/// moves by \a dx, \a dy (Match Contents brings the art to the corner).
CommandPtr resize(Document& document, int width, int height, bool scaleContent, double dx, double dy);

} // namespace docsettings

} // namespace fla
