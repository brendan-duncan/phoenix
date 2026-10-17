#pragma once

#include <cstdint>
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
/// Attributes matching the reader's default are left out, the way FLA files
/// carry them; the reader restores the same value either way.
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

    /// Parses \a xml, a whole element as text, and appends a copy of it to
    /// \a parent. False when it does not parse.
    static bool appendRawXml(const std::string& xml, tinyxml2::XMLElement* parent);

    /// Serializes \a symbol as the text of its file under LIBRARY/.
    std::string writeSymbol(const Symbol& symbol);

    /// Path of a symbol's file relative to LIBRARY/. Uses the href the symbol
    /// was read with, falling back to its name plus ".xml" for symbols created
    /// in this session. Characters no file name can hold are made safe
    /// (fileSafeHref); the symbol keeps its own name in its file.
    static std::string symbolHref(const Symbol& symbol);

    /// \a href with what Windows forbids in a file name (<>:"\|?*, control
    /// characters, and a dot or space ending a folder or file name) as
    /// underscores. Folders stay folders.
    static std::string fileSafeHref(const std::string& href);

    /// Descriptions of content this writer could not represent, accumulated
    /// across calls since the last clearUnsupported().
    const std::vector<std::string>& unsupported() const { return _unsupported; }

    void clearUnsupported() { _unsupported.clear(); }

    /// Textured paint (a brush texture's grain on a solid fill) written so
    /// other apps show it: each fill a repeating bitmap fill of a small tile,
    /// the paint's color with the grain in its alpha, placed as the texture
    /// is. The fill keeps its color as Phoenix attributes, so Phoenix reads
    /// it back as the solid fill with its live texture. Off unless set:
    /// only files written for other apps want it.
    void setBakeTextures(bool on) { _bakeTextures = on; }

    /// A tile made for textured paint: its library item, its data file
    /// under bin and the data.
    struct BakedTexture
    {
        std::string item;
        std::string dataFile;
        int size = 0;
        std::vector<uint8_t> data;
    };
    const std::vector<BakedTexture>& bakedTextures() const { return _baked; }

    /// \a documentXml (DOMDocument.xml as written) with a library bitmap for
    /// each baked tile; unchanged when there are none.
    std::string withBakedMedia(const std::string& documentXml) const;

    /// What marks a baked tile's library bitmap and data file, which a
    /// reader drops: Phoenix makes them again when it writes.
    static constexpr const char* kBakedAttribute = "phoenixTextureBake";
    static constexpr const char* kBakedDataPrefix = "phoenix_texture_";

private:
    /// The library bitmap a solid fill of \a rgba on \a shape, textured as
    /// the shape says, is drawn with in other apps; empty when the texture's
    /// tile cannot be made (a texture from a JPEG bitmap).
    std::string bakeTexture(const Shape& shape, const uint8_t rgba[4]);
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

    /// The attributes and children of an element kept as read, written back
    /// once the modeled ones are.
    void writeElementKept(const Element& element, tinyxml2::XMLElement* node);

    std::vector<std::string> _unsupported;
    bool _bakeTextures = false;
    /// The shape whose fills are being written, when it is textured.
    const Shape* _texturedShape = nullptr;
    std::vector<BakedTexture> _baked;
};

} // namespace fla
