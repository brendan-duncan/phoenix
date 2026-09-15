#pragma once

#include <string>
#include <vector>

namespace tinyxml2 {
class XMLElement;
class XMLDocument;
}

namespace fla {

class BitmapInstance;
class Document;
class Element;
class FillStyle;
class Frame;
class Group;
class Layer;
class MorphShape;
class OvalPrimitive;
class RectanglePrimitive;
class Shape;
class StaticText;
class StrokeStyle;
class Symbol;
class SymbolInstance;
class Timeline;
class Transform;
class Point;
class Rect;

/// Writes the parsed document model back out as XFL XML, the inverse of
/// DocumentParser.
///
/// Attributes matching the reader's default are left out, the way Animate writes
/// them; the reader restores the same value either way.
///
/// Anything the writer cannot yet represent is recorded rather than dropped in
/// silence -- see unsupported(). Callers saving a file should treat a non-empty
/// list as a reason to warn, because the output will not be a faithful copy.
///
/// Free of Qt.
class XFLWriter
{
public:
    XFLWriter() = default;

    XFLWriter(const XFLWriter&) = delete;
    XFLWriter& operator=(const XFLWriter&) = delete;

    /// Serializes \a document as the text of DOMDocument.xml.
    std::string writeDocument(const Document& document);

    /// Serializes \a symbol as the text of its file under LIBRARY/.
    std::string writeSymbol(const Symbol& symbol);

    /// Path of a symbol's file relative to LIBRARY/. Uses the href the symbol
    /// was read with, falling back to its name plus ".xml" for symbols created
    /// in this session.
    static std::string symbolHref(const Symbol& symbol);

    /// Descriptions of content this writer could not represent, accumulated
    /// across calls since the last clearUnsupported().
    const std::vector<std::string>& unsupported() const { return _unsupported; }

    void clearUnsupported() { _unsupported.clear(); }

private:
    void note(const std::string& description);

    void writeTimeline(const Timeline& timeline, tinyxml2::XMLElement* parent);
    void writeLayer(const Layer& layer, tinyxml2::XMLElement* parent);
    void writeFrame(const Frame& frame, tinyxml2::XMLElement* parent);
    void writeElement(const Element& element, tinyxml2::XMLElement* parent);
    void writeShape(const Shape& shape, tinyxml2::XMLElement* parent);
    void writeSymbolInstance(const SymbolInstance& instance, tinyxml2::XMLElement* parent);
    void writeGroup(const Group& group, tinyxml2::XMLElement* parent);
    void writeBitmapInstance(const BitmapInstance& instance, tinyxml2::XMLElement* parent);
    void writeStaticText(const StaticText& text, tinyxml2::XMLElement* parent);
    void writeRectanglePrimitive(const RectanglePrimitive& rectangle, tinyxml2::XMLElement* parent);
    void writeOvalPrimitive(const OvalPrimitive& oval, tinyxml2::XMLElement* parent);
    void writeMorphShape(const MorphShape& morphShape, tinyxml2::XMLElement* parent);

    void writeFillStyle(const FillStyle& fill, tinyxml2::XMLElement* parent);
    void writeStrokeStyle(const StrokeStyle& stroke, tinyxml2::XMLElement* parent);

    /// Writes the attributes every Element shares, plus its matrix and
    /// transformation point.
    void writeElementCommon(const Element& element, tinyxml2::XMLElement* node);

    std::vector<std::string> _unsupported;
};

} // namespace fla
