#include "xfl_writer.h"
#include "../data/raw_element.h"

#include "edge_writer.h"

#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/filter.h"
#include "../data/document.h"
#include "../data/folder.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/morph_shape.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/symbol.h"
#include "../data/symbol_list.h"
#include "../edit/shape_tween.h"

#include "../third_party/tinyxml2/tinyxml2.h"

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fla {

namespace {

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
using tinyxml2::XMLPrinter;

/// Formats a double compactly but without losing the value: the shortest of the
/// usual precisions that still reads back as the same number.
///
/// tinyxml2 would write %.17g, which is exact but turns 0.1 into
/// 0.10000000000000001 all through the file.
std::string formatDouble(double value)
{
    // A file cannot hold "nan" or "inf", which no reader takes for a number;
    // zero is the least harm.
    if (!std::isfinite(value))
        return "0";

    // In range before the cast, which is undefined past a long long.
    if (std::fabs(value) < 1.0e15 &&
        value == static_cast<long long>(value))
    {
        return std::to_string(static_cast<long long>(value));
    }

    char buffer[64];
    for (int precision = 6; precision <= 17; ++precision)
    {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value)
            break;
    }
    return std::string(buffer);
}

/// The node being written and the modeled attributes its file wrote: those
/// are written even at Phoenix's default (see DOMElement::presentAttributes).
const XMLElement* g_presentNode = nullptr;
const std::vector<std::string>* g_present = nullptr;

bool wasPresent(const XMLElement* node, const char* name)
{
    if (node != g_presentNode || !g_present)
        return false;
    for (const std::string& present : *g_present)
    {
        if (present == name)
            return true;
    }
    return false;
}

/// Makes the attributes the file wrote on \a node be written back, for as
/// long as it lives.
class PresentScope
{
public:
    PresentScope(const XMLElement* node, const std::vector<std::string>& present)
        : _node(g_presentNode)
        , _present(g_present)
    {
        g_presentNode = node;
        g_present = &present;
    }

    ~PresentScope()
    {
        g_presentNode = _node;
        g_present = _present;
    }

private:
    const XMLElement* _node;
    const std::vector<std::string>* _present;
};

/// Writes back the attributes kept as read, after the modeled ones: one the
/// writer has set already stays as set.
void writeKept(XMLElement* node, const std::vector<std::pair<std::string, std::string>>& kept)
{
    for (const auto& attribute : kept)
    {
        if (!node->Attribute(attribute.first.c_str()))
            node->SetAttribute(attribute.first.c_str(), attribute.second.c_str());
    }
}

/// Writes a color as #RRGGBB, with alpha split out the way the format stores
/// it. Both are omitted when they match the reader's default of opaque black.
void setColor(XMLElement* node, const char* colorName, const char* alphaName,
    const uint8_t rgba[4], bool forceWrite = false)
{
    const bool defaultColor = rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0;
    if (forceWrite || !defaultColor || wasPresent(node, colorName))
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0], rgba[1], rgba[2]);
        node->SetAttribute(colorName, buffer);
    }

    if (alphaName && (rgba[3] != 255 || wasPresent(node, alphaName)))
        node->SetAttribute(alphaName, formatDouble(rgba[3] / 255.0).c_str());
}

void setString(XMLElement* node, const char* name, const std::string& value)
{
    if (!value.empty() || wasPresent(node, name))
        node->SetAttribute(name, value.c_str());
}

void setInt(XMLElement* node, const char* name, int value, int defaultValue)
{
    if (value != defaultValue || wasPresent(node, name))
        node->SetAttribute(name, value);
}

void setDouble(XMLElement* node, const char* name, double value, double defaultValue)
{
    if (value != defaultValue || wasPresent(node, name))
        node->SetAttribute(name, formatDouble(value).c_str());
}

void setBool(XMLElement* node, const char* name, bool value, bool defaultValue)
{
    if (value != defaultValue || wasPresent(node, name))
        node->SetAttribute(name, value);
}

bool isIdentity(const Transform& t)
{
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 &&
           t.m22 == 1.0 && t.tx == 0.0 && t.ty == 0.0;
}

/// Writes <matrix><Matrix .../></matrix>, skipping it entirely for an identity
/// transform since the reader defaults to identity anyway.
void writeMatrix(const Transform& transform, XMLElement* parent, const char* wrapperName = "matrix",
    bool keep = false)
{
    if (isIdentity(transform) && !keep)
        return;

    XMLDocument* doc = parent->GetDocument();
    XMLElement* wrapper = doc->NewElement(wrapperName);
    XMLElement* matrix = doc->NewElement("Matrix");

    setDouble(matrix, "a", transform.m11, 1.0);
    setDouble(matrix, "b", transform.m12, 0.0);
    setDouble(matrix, "c", transform.m21, 0.0);
    setDouble(matrix, "d", transform.m22, 1.0);
    setDouble(matrix, "tx", transform.tx, 0.0);
    setDouble(matrix, "ty", transform.ty, 0.0);

    wrapper->InsertEndChild(matrix);
    parent->InsertEndChild(wrapper);
}

void writeTransformationPoint(const Point& point, XMLElement* parent, bool keep = false)
{
    if (point.x == 0.0 && point.y == 0.0 && !keep)
        return;

    XMLDocument* doc = parent->GetDocument();
    XMLElement* wrapper = doc->NewElement("transformationPoint");
    XMLElement* node = doc->NewElement("Point");

    setDouble(node, "x", point.x, 0.0);
    setDouble(node, "y", point.y, 0.0);

    wrapper->InsertEndChild(node);
    parent->InsertEndChild(wrapper);
}

/// Formats a morph point, which shares the edge format's twip encoding but is
/// written as a comma-separated pair.
std::string formatMorphPoint(const Point& point)
{
    return EdgeWriter::formatMorphCoordinate(point.x) + ", " +
           EdgeWriter::formatMorphCoordinate(point.y);
}

const char* symbolTypeName(SymbolType type)
{
    switch (type)
    {
    case SymbolType::Button: return "button";
    case SymbolType::MovieClip: return "movie clip";
    case SymbolType::Graphic: break;
    }
    return "graphic";
}

const char* loopTypeName(LoopType type)
{
    switch (type)
    {
    case LoopType::SingleFrame: return "single frame";
    case LoopType::Loop: return "loop";
    case LoopType::PingPong: return "ping pong";
    case LoopType::LoopReverse: return "loop reverse";
    case LoopType::PlayOnceReverse: return "play once reverse";
    case LoopType::PlayOnce: break;
    }
    return "play once";
}

const char* layerTypeName(Layer::Type type)
{
    switch (type)
    {
    case Layer::Type::Mask: return "mask";
    case Layer::Type::Masked: return "masked";
    case Layer::Type::Folder: return "folder";
    case Layer::Type::Guide: return "guide";
    case Layer::Type::Normal: break;
    }
    return "normal";
}

const char* alignmentName(TextRun::Alignment alignment)
{
    switch (alignment)
    {
    case TextRun::Alignment::Center: return "center";
    case TextRun::Alignment::Right: return "right";
    case TextRun::Alignment::Justify: return "justify";
    case TextRun::Alignment::Left: break;
    }
    return "left";
}

const char* strokeTagName(StrokeStyle::Style style)
{
    switch (style)
    {
    case StrokeStyle::Style::Dashed: return "DashedStroke";
    case StrokeStyle::Style::Ragged: return "RaggedStroke";
    case StrokeStyle::Style::Stipple: return "StippleStroke";
    case StrokeStyle::Style::Dotted: return "DottedStroke";
    case StrokeStyle::Style::Hatched: return "HatchedStroke";
    case StrokeStyle::Style::Solid: break;
    }
    return "SolidStroke";
}

std::string toText(const XMLDocument& doc)
{
    XMLPrinter printer;
    doc.Print(&printer);
    const char* text = printer.CStr();

    // XML turns a literal carriage return into a line feed when it is read back.
    // Flash uses CR for the line breaks inside text runs, so those have to go out
    // as character references or they come back changed. The printer only ever
    // emits LF for its own formatting, so every CR reaching here is data.
    std::string out;
    for (const char* c = text; *c; ++c)
    {
        if (*c == '\r')
            out += "&#13;";
        else
            out.push_back(*c);
    }
    return out;
}

} // namespace

void XFLWriter::note(const std::string& description)
{
    for (const std::string& existing : _unsupported)
    {
        if (existing == description)
            return;
    }
    _unsupported.push_back(description);
}

std::string XFLWriter::symbolHref(const Symbol& symbol)
{
    if (!symbol.href.empty())
        return symbol.href;
    return symbol.name + ".xml";
}

void XFLWriter::writeFillStyle(const FillStyle& fill, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();

    switch (fill.type())
    {
    case FillStyle::Type::SolidColor:
    {
        const SolidColor& solid = static_cast<const SolidColor&>(fill);
        XMLElement* node = doc->NewElement("SolidColor");
        // A solid fill always states its color: black is a real choice here,
        // not an absent one.
        setColor(node, "color", "alpha", solid.color, true);
        writeKept(node, solid.extraAttributes);
        parent->InsertEndChild(node);
        break;
    }

    case FillStyle::Type::LinearGradient:
    {
        const LinearGradient& gradient = static_cast<const LinearGradient&>(fill);
        XMLElement* node = doc->NewElement("LinearGradient");
        setString(node, "spreadMethod", gradient.spreadMethod);
        setString(node, "interpolationMethod", gradient.interpolationMethod);
        writeMatrix(gradient.transform, node);
        for (const GradientEntry& entry : gradient.entries)
        {
            XMLElement* entryNode = doc->NewElement("GradientEntry");
            entryNode->SetAttribute("ratio", formatDouble(entry.ratio).c_str());
            setColor(entryNode, "color", "alpha", entry.color, true);
            node->InsertEndChild(entryNode);
        }
        parent->InsertEndChild(node);
        break;
    }

    case FillStyle::Type::RadialGradient:
    {
        const RadialGradient& gradient = static_cast<const RadialGradient&>(fill);
        XMLElement* node = doc->NewElement("RadialGradient");
        setDouble(node, "focalPointRatio", gradient.focalPointRatio, 0.0);
        setString(node, "spreadMethod", gradient.spreadMethod);
        setString(node, "interpolationMethod", gradient.interpolationMethod);
        writeMatrix(gradient.transform, node);
        for (const RadialEntry& entry : gradient.entries)
        {
            XMLElement* entryNode = doc->NewElement("GradientEntry");
            entryNode->SetAttribute("ratio", formatDouble(entry.ratio).c_str());
            setColor(entryNode, "color", "alpha", entry.color, true);
            node->InsertEndChild(entryNode);
        }
        parent->InsertEndChild(node);
        break;
    }

    case FillStyle::Type::BitmapFill:
    {
        const BitmapFill& bitmap = static_cast<const BitmapFill&>(fill);
        XMLElement* node = doc->NewElement("BitmapFill");
        setString(node, "bitmapPath", bitmap.bitmapPath);
        setBool(node, "bitmapIsClipped", bitmap.bitmapIsClipped, false);
        writeMatrix(bitmap.transform, node);
        parent->InsertEndChild(node);
        break;
    }
    }
}

void XFLWriter::writeStrokeStyle(const StrokeStyle& stroke, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();

    XMLElement* node = doc->NewElement(strokeTagName(stroke.style()));
    setDouble(node, "weight", stroke.weight, 1.0);
    setString(node, "scaleMode", stroke.scaleMode);
    setString(node, "solidStyle", stroke.solidStyle);
    setString(node, "caps", stroke.caps);
    setString(node, "joints", stroke.joints);
    setDouble(node, "miterLimit", stroke.miterLimit, 3.0);
    setBool(node, "pixelHinting", stroke.pixelHinting, false);
    if (stroke.style() == StrokeStyle::Style::Dashed)
    {
        setDouble(node, "dash1", stroke.dash1, 6.0);
        setDouble(node, "dash2", stroke.dash2, 6.0);
    }
    if (stroke.style() == StrokeStyle::Style::Dotted)
        node->SetAttribute("dotSpace", formatDouble(stroke.dotSpace).c_str());
    for (const auto& entry : stroke.styleAttributes)
        node->SetAttribute(entry.first.c_str(), entry.second.c_str());
    for (const auto& entry : stroke.extraAttributes)
        node->SetAttribute(entry.first.c_str(), entry.second.c_str());

    if (stroke.fill)
    {
        XMLElement* fillNode = doc->NewElement("fill");
        writeFillStyle(*stroke.fill, fillNode);
        node->InsertEndChild(fillNode);
    }
    for (const std::string& xml : stroke.extraChildren)
        appendRawXml(xml, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeElementCommon(const Element& element, XMLElement* node)
{
    setBool(node, "isSelected", element.isSelected, false);
    setBool(node, "isFloating", element.isFloating, false);
    setBool(node, "lockFlag", element.isLocked, false);

    writeMatrix(element.transform, node, "matrix", element.keepMatrix);
    writeTransformationPoint(element.transformationPoint, node, element.keepTransformationPoint);
}

void XFLWriter::writeElementKept(const Element& element, XMLElement* node)
{
    writeKept(node, element.extraAttributes);
    for (const std::string& xml : element.extraChildren)
        appendRawXml(xml, node);
}

void XFLWriter::writeShape(const Shape& shape, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMShape");
    PresentScope present(node, shape.presentAttributes);

    writeElementCommon(shape, node);
    setBool(node, "isDrawingObject", shape.isDrawingObject, false);

    if (!shape.fillsMap.empty())
    {
        XMLElement* fills = doc->NewElement("fills");
        for (const auto& entry : shape.fillsMap)
        {
            XMLElement* fillStyle = doc->NewElement("FillStyle");
            fillStyle->SetAttribute("index", entry.first);
            if (entry.second)
                writeFillStyle(*entry.second, fillStyle);
            fills->InsertEndChild(fillStyle);
        }
        node->InsertEndChild(fills);
    }

    if (!shape.strokesMap.empty())
    {
        XMLElement* strokes = doc->NewElement("strokes");
        for (const auto& entry : shape.strokesMap)
        {
            XMLElement* strokeStyle = doc->NewElement("StrokeStyle");
            strokeStyle->SetAttribute("index", entry.first);
            if (entry.second)
                writeStrokeStyle(*entry.second, strokeStyle);
            strokes->InsertEndChild(strokeStyle);
        }
        node->InsertEndChild(strokes);
    }

    // The cubics kept beside the edges describe them as read; once the
    // edges change they would describe something else, so they go.
    const bool cubics = shape.cubicsStillMatch();
    if (!shape.edges.empty())
    {
        XMLElement* edges = doc->NewElement("edges");
        size_t cubic = 0;
        for (size_t i = 0; i < shape.edges.size(); ++i)
        {
            while (cubics && cubic < shape.cubicEdges.size() && shape.cubicEdges[cubic].first == i)
                appendRawXml(shape.cubicEdges[cubic++].second, edges);
            const Edge* edge = shape.edges[i];
            if (!edge)
                continue;

            XMLElement* edgeNode = doc->NewElement("Edge");
            setInt(edgeNode, "fillStyle0", edge->fillStyle0, -1);
            setInt(edgeNode, "fillStyle1", edge->fillStyle1, -1);
            setInt(edgeNode, "strokeStyle", edge->strokeStyle, -1);

            // Prefer the text the file was read with: it round-trips exactly,
            // including anything the reader normalized away. Regenerating is for
            // edges that have actually been edited, and writes their cubics
            // as Animate does, quadratics here and the cubics after.
            std::vector<std::string> cubicRecords;
            const std::string data = edge->data.empty()
                ? EdgeWriter::writeEdgeAsAnimate(*edge, cubicRecords)
                : edge->data;
            edgeNode->SetAttribute("edges", data.c_str());

            edges->InsertEndChild(edgeNode);
            for (const std::string& record : cubicRecords)
            {
                XMLElement* cubicNode = doc->NewElement("Edge");
                cubicNode->SetAttribute("cubics", record.c_str());
                edges->InsertEndChild(cubicNode);
            }
        }
        while (cubics && cubic < shape.cubicEdges.size())
            appendRawXml(shape.cubicEdges[cubic++].second, edges);
        node->InsertEndChild(edges);
    }

    writeElementKept(shape, node);
    parent->InsertEndChild(node);
}

void XFLWriter::writeSymbolInstance(const SymbolInstance& instance, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMSymbolInstance");
    PresentScope present(node, instance.presentAttributes);

    setString(node, "libraryItemName", instance.libraryItemName);
    setString(node, "name", instance.name);
    setInt(node, "firstFrame", instance.firstFrame, 0);
    setInt(node, "lastFrame", instance.lastFrame, -1);
    if (instance.blendMode != "normal")
        setString(node, "blendMode", instance.blendMode);

    // A movie clip goes without a type, as Animate writes it.
    if (!instance.otherSymbolType.empty())
        node->SetAttribute("symbolType", instance.otherSymbolType.c_str());
    else if (instance.symbolType != SymbolType::MovieClip || wasPresent(node, "symbolType"))
        node->SetAttribute("symbolType", symbolTypeName(instance.symbolType));
    // Animate's name for it is loop, and it writes it on every graphic
    // instance: an instance without one is not read the same way.
    if (instance.symbolType == SymbolType::Graphic && instance.otherSymbolType.empty())
        node->SetAttribute("loop", loopTypeName(instance.loopType));

    writeElementCommon(instance, node);
    writeKept(node, instance.extraAttributes);

    const ColorTransform& color = instance.colorTransform;
    const bool hasTintColor =
        color.tintColor[0] != 0 || color.tintColor[1] != 0 || color.tintColor[2] != 0;

    if (!color.isIdentity() || hasTintColor || instance.keepColor)
    {
        XMLElement* wrapper = doc->NewElement("color");
        XMLElement* colorNode = doc->NewElement("Color");

        setDouble(colorNode, "tintMultiplier", color.tintMultiplier, 0.0);
        setDouble(colorNode, "alphaMultiplier", color.alphaMultiplier, 1.0);
        setDouble(colorNode, "redMultiplier", color.redMultiplier, 1.0);
        setDouble(colorNode, "greenMultiplier", color.greenMultiplier, 1.0);
        setDouble(colorNode, "blueMultiplier", color.blueMultiplier, 1.0);
        setInt(colorNode, "alphaOffset", color.alphaOffset, 0);
        setInt(colorNode, "redOffset", color.redOffset, 0);
        setInt(colorNode, "greenOffset", color.greenOffset, 0);
        setInt(colorNode, "blueOffset", color.blueOffset, 0);
        setDouble(colorNode, "brightness", color.brightness, 0.0);
        setColor(colorNode, "tintColor", nullptr, color.tintColor);

        wrapper->InsertEndChild(colorNode);
        node->InsertEndChild(wrapper);
    }

    if (!instance.filters.empty() || !instance.extraFilters.empty())
    {
        XMLElement* filters = doc->NewElement("filters");
        for (const Filter& filter : instance.filters)
        {
            XMLElement* filterNode = doc->NewElement(filter.elementName());
            PresentScope filterPresent(filterNode, filter.presentAttributes);
            setBool(filterNode, "enabled", filter.enabled, true);
            switch (filter.type)
            {
            case Filter::Type::Blur:
                setDouble(filterNode, "blurX", filter.blurX, 5.0);
                setDouble(filterNode, "blurY", filter.blurY, 5.0);
                setInt(filterNode, "quality", filter.quality, 1);
                break;
            case Filter::Type::DropShadow:
            case Filter::Type::Glow:
            case Filter::Type::GradientGlow:
                setDouble(filterNode, "blurX", filter.blurX, 5.0);
                setDouble(filterNode, "blurY", filter.blurY, 5.0);
                setInt(filterNode, "quality", filter.quality, 1);
                if (filter.type != Filter::Type::Glow)
                {
                    setDouble(filterNode, "angle", filter.angle, 45.0);
                    setDouble(filterNode, "distance", filter.distance, 5.0);
                }
                setDouble(filterNode, "strength", filter.strength, 1.0);
                if (filter.type != Filter::Type::GradientGlow)
                    setColor(filterNode, "color", "alpha", filter.color, true);
                setBool(filterNode, "inner", filter.inner, false);
                setBool(filterNode, "knockout", filter.knockout, false);
                setBool(filterNode, "hideObject", filter.hideObject, false);
                break;
            case Filter::Type::Bevel:
            case Filter::Type::GradientBevel:
                setDouble(filterNode, "blurX", filter.blurX, 5.0);
                setDouble(filterNode, "blurY", filter.blurY, 5.0);
                setInt(filterNode, "quality", filter.quality, 1);
                setDouble(filterNode, "angle", filter.angle, 45.0);
                setDouble(filterNode, "distance", filter.distance, 5.0);
                setDouble(filterNode, "strength", filter.strength, 1.0);
                if (filter.type == Filter::Type::Bevel)
                {
                    setColor(filterNode, "highlightColor", "highlightAlpha", filter.highlightColor, true);
                    setColor(filterNode, "shadowColor", "shadowAlpha", filter.shadowColor, true);
                }
                setString(filterNode, "type", filter.bevelType);
                setBool(filterNode, "knockout", filter.knockout, false);
                break;
            case Filter::Type::AdjustColor:
                setDouble(filterNode, "brightness", filter.brightness, 0.0);
                setDouble(filterNode, "contrast", filter.contrast, 0.0);
                setDouble(filterNode, "saturation", filter.saturation, 0.0);
                setDouble(filterNode, "hue", filter.hue, 0.0);
                break;
            }
            for (const GradientEntry& entry : filter.entries)
            {
                XMLElement* entryNode = doc->NewElement("GradientEntry");
                setColor(entryNode, "color", "alpha", entry.color, true);
                entryNode->SetAttribute("ratio", formatDouble(entry.ratio).c_str());
                filterNode->InsertEndChild(entryNode);
            }
            writeKept(filterNode, filter.extraAttributes);
            filters->InsertEndChild(filterNode);
        }
        for (const std::string& xml : instance.extraFilters)
            appendRawXml(xml, filters);
        node->InsertEndChild(filters);
    }

    for (const std::string& xml : instance.extraChildren)
        appendRawXml(xml, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeGroup(const Group& group, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMGroup");
    PresentScope present(node, group.presentAttributes);

    writeElementCommon(group, node);

    XMLElement* members = doc->NewElement("members");
    for (const Element* member : group.members)
    {
        if (member)
            writeElement(*member, members);
    }
    node->InsertEndChild(members);

    writeElementKept(group, node);
    parent->InsertEndChild(node);
}

void XFLWriter::writeBitmapInstance(const BitmapInstance& instance, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMBitmapInstance");
    PresentScope present(node, instance.presentAttributes);

    setString(node, "libraryItemName", instance.libraryItemName);
    writeElementCommon(instance, node);
    writeElementKept(instance, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeStaticText(const StaticText& text, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    const char* tag = "DOMStaticText";
    if (text.textType == StaticText::TextType::Dynamic)
        tag = "DOMDynamicText";
    else if (text.textType == StaticText::TextType::Input)
        tag = "DOMInputText";
    XMLElement* node = doc->NewElement(tag);
    PresentScope present(node, text.presentAttributes);

    setDouble(node, "left", text.left, 0.0);
    setDouble(node, "top", text.top, 0.0);
    setDouble(node, "width", text.width, 0.0);
    setDouble(node, "height", text.height, 0.0);
    setBool(node, "autoExpand", text.autoExpand, false);
    setString(node, "instanceName", text.instanceName);
    setString(node, "lineType", text.lineType);

    writeElementCommon(text, node);

    if (!text.runs.empty())
    {
        XMLElement* runs = doc->NewElement("textRuns");
        for (const TextRun& run : text.runs)
        {
            XMLElement* runNode = doc->NewElement("DOMTextRun");

            XMLElement* characters = doc->NewElement("characters");
            characters->SetText(run.text.c_str());
            runNode->InsertEndChild(characters);

            XMLElement* attrs = doc->NewElement("textAttrs");
            XMLElement* attrNode = doc->NewElement("DOMTextAttrs");
            PresentScope runPresent(attrNode, run.presentAttributes);
            if (run.alignment != TextRun::Alignment::Left || wasPresent(attrNode, "alignment"))
                attrNode->SetAttribute("alignment", alignmentName(run.alignment));
            // Animate writes aliasText on every run.
            attrNode->SetAttribute("aliasText", run.aliasText ? "true" : "false");
            setBool(attrNode, "autoKern", run.autoKern, true);
            setDouble(attrNode, "letterSpacing", run.letterSpacing, 0.0);
            setDouble(attrNode, "lineHeight", run.lineHeight, 0.0);
            setDouble(attrNode, "size", run.size, 0.0);
            setDouble(attrNode, "bitmapSize", run.bitmapSize, 0.0);
            setString(attrNode, "face", run.face);
            setColor(attrNode, "fillColor", nullptr, run.fillColor);
            writeKept(attrNode, run.extraAttributes);
            attrs->InsertEndChild(attrNode);
            runNode->InsertEndChild(attrs);

            runs->InsertEndChild(runNode);
        }
        node->InsertEndChild(runs);
    }

    writeElementKept(text, node);
    parent->InsertEndChild(node);
}

void XFLWriter::writeRectanglePrimitive(const RectanglePrimitive& rectangle, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMRectangleObject");
    PresentScope present(node, rectangle.presentAttributes);

    setDouble(node, "x", rectangle.rect.topLeft.x, 0.0);
    setDouble(node, "y", rectangle.rect.topLeft.y, 0.0);
    setDouble(node, "objectWidth", rectangle.rect.width(), 0.0);
    setDouble(node, "objectHeight", rectangle.rect.height(), 0.0);
    setDouble(node, "topLeftRadius", rectangle.topLeftRadius, 0.0);
    setDouble(node, "topRightRadius", rectangle.topRightRadius, 0.0);
    setDouble(node, "bottomLeftRadius", rectangle.bottomLeftRadius, 0.0);
    setDouble(node, "bottomRightRadius", rectangle.bottomRightRadius, 0.0);

    writeElementCommon(rectangle, node);

    if (rectangle.fillStyle)
    {
        XMLElement* fill = doc->NewElement("fill");
        writeFillStyle(*rectangle.fillStyle, fill);
        node->InsertEndChild(fill);
    }

    if (rectangle.strokeStyle)
    {
        XMLElement* stroke = doc->NewElement("stroke");
        writeStrokeStyle(*rectangle.strokeStyle, stroke);
        node->InsertEndChild(stroke);
    }

    writeElementKept(rectangle, node);
    parent->InsertEndChild(node);
}

void XFLWriter::writeOvalPrimitive(const OvalPrimitive& oval, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMOvalObject");
    PresentScope present(node, oval.presentAttributes);

    setDouble(node, "x", oval.rect.topLeft.x, 0.0);
    setDouble(node, "y", oval.rect.topLeft.y, 0.0);
    setDouble(node, "objectWidth", oval.rect.width(), 0.0);
    setDouble(node, "objectHeight", oval.rect.height(), 0.0);
    setDouble(node, "startAngle", oval.startAngle, 0.0);
    setDouble(node, "endAngle", oval.endAngle, 0.0);
    setDouble(node, "innerRadius", oval.innerRadius, 0.0);

    writeElementCommon(oval, node);

    if (oval.fillStyle)
    {
        XMLElement* fill = doc->NewElement("fill");
        writeFillStyle(*oval.fillStyle, fill);
        node->InsertEndChild(fill);
    }

    if (oval.strokeStyle)
    {
        XMLElement* stroke = doc->NewElement("stroke");
        writeStrokeStyle(*oval.strokeStyle, stroke);
        node->InsertEndChild(stroke);
    }

    writeElementKept(oval, node);
    parent->InsertEndChild(node);
}

void XFLWriter::writeElement(const Element& element, XMLElement* parent)
{
    switch (element.elementType())
    {
    case Element::Type::Shape:
        writeShape(static_cast<const Shape&>(element), parent);
        break;
    case Element::Type::SymbolInstance:
        writeSymbolInstance(static_cast<const SymbolInstance&>(element), parent);
        break;
    case Element::Type::Group:
        writeGroup(static_cast<const Group&>(element), parent);
        break;
    case Element::Type::StaticText:
        writeStaticText(static_cast<const StaticText&>(element), parent);
        break;
    case Element::Type::BitmapInstance:
        writeBitmapInstance(static_cast<const BitmapInstance&>(element), parent);
        break;
    case Element::Type::Rectangle:
        writeRectanglePrimitive(static_cast<const RectanglePrimitive&>(element), parent);
        break;
    case Element::Type::Oval:
        writeOvalPrimitive(static_cast<const OvalPrimitive&>(element), parent);
        break;
    case Element::Type::Raw:
        appendRawXml(static_cast<const RawElement&>(element).xml, parent);
        break;
    }
}

void XFLWriter::writeMorphShape(const MorphShape& morphShape, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("MorphShape");

    XMLElement* segments = doc->NewElement("morphSegments");
    for (const MorphSegment* segment : morphShape.segments)
    {
        if (!segment)
            continue;

        XMLElement* segmentNode = doc->NewElement("MorphSegment");
        PresentScope present(segmentNode, segment->presentAttributes);
        segmentNode->SetAttribute("startPointA", formatMorphPoint(segment->startPointA).c_str());
        segmentNode->SetAttribute("startPointB", formatMorphPoint(segment->startPointB).c_str());
        setInt(segmentNode, "strokeIndex1", segment->strokeIndex1, 0);
        setInt(segmentNode, "strokeIndex2", segment->strokeIndex2, 0);
        setInt(segmentNode, "fillIndex1", segment->fillIndex1, 0);
        setInt(segmentNode, "fillIndex2", segment->fillIndex2, 0);

        for (const MorphCurves* curves : segment->curves)
        {
            if (!curves)
                continue;

            XMLElement* curvesNode = doc->NewElement("MorphCurves");
            curvesNode->SetAttribute("controlPointA", formatMorphPoint(curves->controlPointA).c_str());
            curvesNode->SetAttribute("anchorPointA", formatMorphPoint(curves->anchorPointA).c_str());
            curvesNode->SetAttribute("controlPointB", formatMorphPoint(curves->controlPointB).c_str());
            curvesNode->SetAttribute("anchorPointB", formatMorphPoint(curves->anchorPointB).c_str());
            if (curves->isLine)
                curvesNode->SetAttribute("isLine", "true");
            writeKept(curvesNode, curves->extraAttributes);
            segmentNode->InsertEndChild(curvesNode);
        }

        writeKept(segmentNode, segment->extraAttributes);
        segments->InsertEndChild(segmentNode);
    }
    node->InsertEndChild(segments);
    if (!morphShape.hintsXml.empty())
        appendRawXml(morphShape.hintsXml, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeFrame(const Frame& frame, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMFrame");
    PresentScope present(node, frame.presentAttributes);

    node->SetAttribute("index", frame.index);
    setInt(node, "duration", frame.duration, 1);
    setString(node, "keyMode", frame.keyMode);
    setString(node, "name", frame.name);
    setString(node, "labelType", frame.labelType);

    if (frame.tweenType == TweenType::Motion)
        node->SetAttribute("tweenType", "motion");
    else if (frame.tweenType == TweenType::Shape)
        node->SetAttribute("tweenType", "shape");
    else if (!frame.otherTweenType.empty())
        node->SetAttribute("tweenType", frame.otherTweenType.c_str());
    setBool(node, "motionTweenSync", frame.motionTweenSync, false);
    setBool(node, "motionTweenSnap", frame.motionTWeenSnap, false);
    setBool(node, "hasCustomEase", frame.hasCustomEase, false);
    setString(node, "motionTweenRotate", frame.motionTweenRotate);
    setInt(node, "motionTweenRotateTimes", frame.motionTweenRotateTimes, 0);
    setInt(node, "acceleration", frame.acceleration, 0);

    // The eases: an Ease for all properties (its preset, or the Ease value
    // it repeats), the custom curve, and any kept for single properties.
    const bool customEase = frame.hasCustomEase && !frame.customEase.empty();
    const bool ease = !customEase && (frame.hasEase || !frame.easeMethod.empty() ||
        (frame.acceleration != 0 && frame.tweenType != TweenType::None));
    if (ease || customEase || !frame.otherEases.empty())
    {
        XMLElement* tweens = doc->NewElement("tweens");
        if (ease)
        {
            XMLElement* node = doc->NewElement("Ease");
            node->SetAttribute("target", "all");
            setString(node, "method", frame.easeMethod);
            setInt(node, "intensity", frame.acceleration, 0);
            tweens->InsertEndChild(node);
        }
        if (customEase)
        {
            XMLElement* node = doc->NewElement("CustomEase");
            node->SetAttribute("target", "all");
            setString(node, "name", frame.customEaseName);
            for (const Point& point : frame.customEase)
            {
                XMLElement* pointNode = doc->NewElement("Point");
                setDouble(pointNode, "x", point.x, 0.0);
                setDouble(pointNode, "y", point.y, 0.0);
                node->InsertEndChild(pointNode);
            }
            tweens->InsertEndChild(node);
        }
        for (const std::string& xml : frame.otherEases)
            appendRawXml(xml, tweens);
        node->InsertEndChild(tweens);
    }

    if (frame.actionScript)
    {
        XMLElement* actionScript = doc->NewElement("Actionscript");
        XMLElement* script = doc->NewElement("script");
        script->SetText(frame.actionScript->code.c_str());
        actionScript->InsertEndChild(script);
        node->InsertEndChild(actionScript);
    }

    if (frame.morphShape && frame.tweenType == TweenType::Shape)
        writeMorphShape(*frame.morphShape, node);

    // What the reader kept without modeling, back where it was.
    // An armature's pose (its IKTree and the matrices kept between frames)
    // follows the elements, as Animate writes it; the rest goes before.
    writeKept(node, frame.extraAttributes);
    const auto afterElements = [](const std::string& xml) {
        const size_t at = xml.find('<');
        return at != std::string::npos &&
            (xml.compare(at, 7, "<IKTree") == 0 || xml.compare(at, 23, "<betweenFrameMatrixList") == 0);
    };
    for (const std::string& xml : frame.extraChildren)
    {
        if (!afterElements(xml))
            appendRawXml(xml, node);
    }

    XMLElement* elements = doc->NewElement("elements");
    for (const Element* element : frame.elements)
    {
        if (element)
            writeElement(*element, elements);
    }
    node->InsertEndChild(elements);
    for (const std::string& xml : frame.extraChildren)
    {
        if (afterElements(xml))
            appendRawXml(xml, node);
    }

    parent->InsertEndChild(node);
}

bool XFLWriter::appendRawXml(const std::string& xml, XMLElement* parent)
{
    if (xml.empty() || !parent)
        return false;
    XMLDocument fragment;
    if (fragment.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !fragment.RootElement())
        return false;
    tinyxml2::XMLNode* copy = fragment.RootElement()->DeepClone(parent->GetDocument());
    if (!copy)
        return false;
    parent->InsertEndChild(copy);
    return true;
}

void XFLWriter::writeLayer(const Layer& layer, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMLayer");
    PresentScope present(node, layer.presentAttributes);

    setString(node, "name", layer.name);
    // Animate writes every layer's color, black included.
    setColor(node, "color", nullptr, layer.color, true);
    setString(node, "current", layer.current);

    if (layer.layerType != Layer::Type::Normal)
        node->SetAttribute("layerType", layerTypeName(layer.layerType));
    else if (!layer.otherLayerType.empty())
        node->SetAttribute("layerType", layer.otherLayerType.c_str());

    setInt(node, "parentLayerIndex", layer.parentLayerIndex, -1);
    setBool(node, "isSelected", layer.selected, false);
    setBool(node, "autoNamed", layer.autoNamed, true);
    setBool(node, "locked", layer.locked, false);
    setBool(node, "outline", layer.outline, false);
    setBool(node, "visible", layer.visible, true);
    writeKept(node, layer.extraAttributes);

    XMLElement* frames = doc->NewElement("frames");
    for (const Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        // A shape tween saves the morph its keyframes call for now.
        if (frame->tweenType == TweenType::Shape)
        {
            for (const Frame* end : layer.frames)
            {
                if (end && end->index == frame->index + frame->duration)
                    ensureMorph(const_cast<Frame&>(*frame), *end);
            }
        }
        writeFrame(*frame, frames);
    }
    node->InsertEndChild(frames);
    for (const std::string& xml : layer.extraChildren)
        appendRawXml(xml, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeTimeline(const Timeline& timeline, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMTimeline");

    setString(node, "name", timeline.name);
    setBool(node, "layerDepthEnabled", timeline.layerDepthEnabled, false);
    writeKept(node, timeline.extraAttributes);
    if (!timeline.guidesX.empty() || !timeline.guidesY.empty())
    {
        // Animate's own form: XML held in the attribute, a guideline per
        // guide, "v" for a vertical one at its x and "h" for a horizontal
        // one at its y.
        std::string guides = "<guidelines>";
        const auto add = [&guides](const char* direction, double position) {
            char number[64];
            std::snprintf(number, sizeof(number), "%.2f", position);
            std::string text = number;
            while (!text.empty() && text.back() == '0')
                text.pop_back();
            if (!text.empty() && text.back() == '.')
                text.pop_back();
            if (text == "-0")
                text = "0";
            guides += std::string("<guideline direction=\"") + direction + "\">" + text + "</guideline>";
        };
        for (double x : timeline.guidesX)
            add("v", x);
        for (double y : timeline.guidesY)
            add("h", y);
        guides += "</guidelines>";
        node->SetAttribute("guides", guides.c_str());
    }

    XMLElement* layers = doc->NewElement("layers");
    for (const Layer* layer : timeline.layers)
    {
        if (layer)
            writeLayer(*layer, layers);
    }
    node->InsertEndChild(layers);
    for (const std::string& xml : timeline.extraChildren)
        appendRawXml(xml, node);

    parent->InsertEndChild(node);
}

std::string XFLWriter::writeDocument(const Document& document)
{
    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());

    XMLElement* root = doc.NewElement("DOMDocument");
    root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
    root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    PresentScope present(root, document.presentAttributes);

    setString(root, "filetypeGUID", document.filetypeGUID);
    setString(root, "fileGUID", document.fileGUID);
    setInt(root, "width", document.width, 550);
    setInt(root, "height", document.height, 400);
    setDouble(root, "frameRate", document.frameRate, 24.0);
    setString(root, "currentTimeline", document.currentTimeline);
    setString(root, "xflVersion", document.xflVersion);
    setString(root, "creatorInfo", document.creatorInfo);
    setString(root, "platform", document.platform);
    setString(root, "versionInfo", document.versionInfo);
    setInt(root, "majorVersion", document.majorVersion, 0);
    setInt(root, "buildNumber", document.buildNumber, 0);
    setDouble(root, "viewAngle3D", document.viewAngle3D, 0.0);
    setColor(root, "backgroundColor", "phoenixStageAlpha", document.backgroundColor);
    setColor(root, "gridColor", nullptr, document.gridColor);
    setInt(root, "gridSpacingX", document.gridSpacingX, 18);
    setInt(root, "gridSpacingY", document.gridSpacingY, 18);
    setBool(root, "objectsSnapTo", document.objectsSnapTo, true);
    setBool(root, "guidesSnapTo", document.guidesSnapTo, true);
    setInt(root, "snapAlignBorderSpacing", document.snapAlignBorderSpacing, 18);
    setColor(root, "guidesColor", nullptr, document.guidesColor);
    setDouble(root, "vanishingPoint3DX", document.vanishingPoint3DX, 0.0);
    setDouble(root, "vanishingPoint3DY", document.vanishingPoint3DY, 0.0);
    setInt(root, "nextSceneIdentifier", document.nextSceneIdentifier, 0);
    setBool(root, "playOptionsPlayLoop", document.playOptionsPlayLoop, true);
    setBool(root, "playOptionsPlayPages", document.playOptionsPlayPages, true);
    setBool(root, "playOptionsPlayFrameActions", document.playOptionsPlayFrameActions, true);
    writeKept(root, document.extraAttributes);

    if (!document.folders.empty())
    {
        XMLElement* folders = doc.NewElement("folders");
        for (const Folder* folder : document.folders)
        {
            if (!folder)
                continue;
            XMLElement* node = doc.NewElement("DOMFolderItem");
            PresentScope folderPresent(node, folder->presentAttributes);
            setString(node, "name", folder->name);
            setString(node, "itemID", folder->itemId);
            setBool(node, "isExpanded", folder->isExpanded, false);
            writeKept(node, folder->extraAttributes);
            folders->InsertEndChild(node);
        }
        root->InsertEndChild(folders);
    }

    // Font items go between the folders and the media, where Animate puts
    // them.
    const auto isFonts = [](const std::string& xml) {
        const size_t start = xml.find_first_not_of(" \t\r\n");
        return start != std::string::npos && xml.compare(start, 6, "<fonts") == 0;
    };
    for (const Document::RawSection& section : document.extraSections)
    {
        if (isFonts(section.xml))
            appendRawXml(section.xml, root);
    }

    if (!document.resources.empty() || !document.extraMedia.empty())
    {
        XMLElement* media = doc.NewElement("media");
        for (const Resource* resource : document.resources)
        {
            if (!resource || resource->resourceType() != Resource::Type::Bitmap)
            {
                if (resource)
                    note("non-bitmap library resource");
                continue;
            }

            const Bitmap* bitmap = static_cast<const Bitmap*>(resource);
            XMLElement* node = doc.NewElement("DOMBitmapItem");
            PresentScope bitmapPresent(node, bitmap->presentAttributes);
            setString(node, "name", bitmap->name);
            setString(node, "itemID", bitmap->itemId);
            setString(node, "sourceExternalFilepath", bitmap->sourceExternalFilepath);
            if (bitmap->sourceLastImported != 0)
                setString(node, "sourceLastImported", std::to_string(bitmap->sourceLastImported));
            setBool(node, "allowSmoothing", bitmap->allowSmoothing, false);
            setInt(node, "quality", bitmap->quality, 0);
            setString(node, "href", bitmap->href);
            setString(node, "bitmapDataHRef", bitmap->bitmapDataHRef);
            setInt(node, "frameRight", bitmap->frameRight, 0);
            setInt(node, "frameBottom", bitmap->frameBottom, 0);
            writeKept(node, bitmap->extraAttributes);
            media->InsertEndChild(node);
        }
        for (const std::string& xml : document.extraMedia)
            appendRawXml(xml, media);
        root->InsertEndChild(media);
    }

    if (document.symbolList && !document.symbolList->symbols.empty())
    {
        XMLElement* symbols = doc.NewElement("symbols");
        for (const Symbol* symbol : document.symbolList->symbols)
        {
            if (!symbol)
                continue;
            XMLElement* include = doc.NewElement("Include");
            include->SetAttribute("href", symbolHref(*symbol).c_str());
            writeKept(include, symbol->includeAttributes);
            symbols->InsertEndChild(include);
        }
        root->InsertEndChild(symbols);
    }

    for (const Document::RawSection& section : document.extraSections)
    {
        if (section.beforeTimelines && !isFonts(section.xml))
            appendRawXml(section.xml, root);
    }

    XMLElement* timelines = doc.NewElement("timelines");
    for (const Timeline* timeline : document.timelines)
    {
        if (timeline)
            writeTimeline(*timeline, timelines);
    }
    root->InsertEndChild(timelines);

    // Document metadata is read whole and written back as it was, in the
    // order Animate writes it. Anything that came without its text (a
    // document made here) is simply left out.
    const auto appendRaw = [&doc, root](const std::string& xml, XMLElement* under) {
        if (xml.empty())
            return false;
        XMLDocument fragment;
        if (fragment.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !fragment.RootElement())
            return false;
        tinyxml2::XMLNode* copy = fragment.RootElement()->DeepClone(&doc);
        if (!copy)
            return false;
        (under ? under : root)->InsertEndChild(copy);
        return true;
    };
    if (document.printerSettings && !appendRaw(document.printerSettings->xml, nullptr))
        note("printer settings");
    if (document.publishHistory && !appendRaw(document.publishHistory->xml, nullptr))
        note("publish history");
    if (!document.swatchLists.empty())
    {
        XMLElement* lists = doc.NewElement("swatchLists");
        bool any = false;
        for (const SwatchList* list : document.swatchLists)
        {
            if (list && appendRaw(list->xml, lists))
                any = true;
            else
                note("swatch lists");
        }
        if (any)
            root->InsertEndChild(lists);
    }
    if (document.scriptList && !appendRaw(document.scriptList->xml, nullptr))
        note("document scripts");
    for (const Document::RawSection& section : document.extraSections)
    {
        if (!section.beforeTimelines)
            appendRawXml(section.xml, root);
    }

    doc.InsertEndChild(root);
    return toText(doc);
}

std::string XFLWriter::writeSymbol(const Symbol& symbol)
{
    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());

    XMLElement* root = doc.NewElement("DOMSymbolItem");
    root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
    root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    PresentScope present(root, symbol.presentAttributes);

    setString(root, "name", symbol.name);
    setString(root, "itemID", symbol.itemId);
    setString(root, "lastModified", symbol.lastModified);
    if (!symbol.otherSymbolType.empty())
        root->SetAttribute("symbolType", symbol.otherSymbolType.c_str());
    else if (symbol.symbolType != SymbolType::MovieClip || wasPresent(root, "symbolType"))
        root->SetAttribute("symbolType", symbolTypeName(symbol.symbolType));
    writeKept(root, symbol.extraAttributes);
    if (symbol.scale9)
    {
        root->SetAttribute("scaleGridLeft", symbol.scaleGridLeft);
        root->SetAttribute("scaleGridTop", symbol.scaleGridTop);
        root->SetAttribute("scaleGridRight", symbol.scaleGridRight);
        root->SetAttribute("scaleGridBottom", symbol.scaleGridBottom);
    }

    // The reader expects a <timeline> wrapper holding DOMTimeline children.
    XMLElement* timeline = doc.NewElement("timeline");
    for (const Timeline* t : symbol.timelines)
    {
        if (t)
            writeTimeline(*t, timeline);
    }
    root->InsertEndChild(timeline);

    doc.InsertEndChild(root);
    return toText(doc);
}

} // namespace fla
