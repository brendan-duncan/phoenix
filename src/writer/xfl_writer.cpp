#include "xfl_writer.h"

#include "edge_writer.h"

#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
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

#include "../third_party/tinyxml2/tinyxml2.h"

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
    if (value == static_cast<long long>(value) &&
        std::fabs(value) < 1.0e15)
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

/// Writes a colour as #RRGGBB, with alpha split out the way the format stores
/// it. Both are omitted when they match the reader's default of opaque black.
void setColor(XMLElement* node, const char* colorName, const char* alphaName,
    const uint8_t rgba[4], bool forceWrite = false)
{
    const bool defaultColor = rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0;
    if (forceWrite || !defaultColor)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0], rgba[1], rgba[2]);
        node->SetAttribute(colorName, buffer);
    }

    if (alphaName && rgba[3] != 255)
        node->SetAttribute(alphaName, formatDouble(rgba[3] / 255.0).c_str());
}

void setString(XMLElement* node, const char* name, const std::string& value)
{
    if (!value.empty())
        node->SetAttribute(name, value.c_str());
}

void setInt(XMLElement* node, const char* name, int value, int defaultValue)
{
    if (value != defaultValue)
        node->SetAttribute(name, value);
}

void setDouble(XMLElement* node, const char* name, double value, double defaultValue)
{
    if (value != defaultValue)
        node->SetAttribute(name, formatDouble(value).c_str());
}

void setBool(XMLElement* node, const char* name, bool value, bool defaultValue)
{
    if (value != defaultValue)
        node->SetAttribute(name, value);
}

bool isIdentity(const Transform& t)
{
    return t.m11 == 1.0 && t.m12 == 0.0 && t.m21 == 0.0 &&
           t.m22 == 1.0 && t.tx == 0.0 && t.ty == 0.0;
}

/// Writes <matrix><Matrix .../></matrix>, skipping it entirely for an identity
/// transform since the reader defaults to identity anyway.
void writeMatrix(const Transform& transform, XMLElement* parent, const char* wrapperName = "matrix")
{
    if (isIdentity(transform))
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

void writeTransformationPoint(const Point& point, XMLElement* parent)
{
    if (point.x == 0.0 && point.y == 0.0)
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
    return EdgeWriter::formatCoordinate(point.x) + ", " +
           EdgeWriter::formatCoordinate(point.y);
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
        // A solid fill always states its colour: black is a real choice here,
        // not an absent one.
        setColor(node, "color", "alpha", solid.color, true);
        parent->InsertEndChild(node);
        break;
    }

    case FillStyle::Type::LinearGradient:
    {
        const LinearGradient& gradient = static_cast<const LinearGradient&>(fill);
        XMLElement* node = doc->NewElement("LinearGradient");
        writeMatrix(gradient.transform, node);
        for (const GradientEntry& entry : gradient.entries)
        {
            XMLElement* entryNode = doc->NewElement("GradientEntry");
            setDouble(entryNode, "ratio", entry.ratio, 0.0);
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
        writeMatrix(gradient.transform, node);
        for (const RadialEntry& entry : gradient.entries)
        {
            XMLElement* entryNode = doc->NewElement("GradientEntry");
            setDouble(entryNode, "ratio", entry.ratio, 0.0);
            setColor(entryNode, "color", "alpha", entry.color, true);
            node->InsertEndChild(entryNode);
        }
        parent->InsertEndChild(node);
        break;
    }

    case FillStyle::Type::BitmapFill:
        // The reader does not build these yet either, so nothing can reach here
        // from a parsed file.
        note("BitmapFill fill style");
        break;
    }
}

void XFLWriter::writeStrokeStyle(const StrokeStyle& stroke, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();

    XMLElement* node = doc->NewElement(strokeTagName(stroke.style()));
    setDouble(node, "weight", stroke.weight, 1.0);
    setString(node, "scaleMode", stroke.scaleMode);

    if (stroke.fill)
    {
        XMLElement* fillNode = doc->NewElement("fill");
        writeFillStyle(*stroke.fill, fillNode);
        node->InsertEndChild(fillNode);
    }

    parent->InsertEndChild(node);
}

void XFLWriter::writeElementCommon(const Element& element, XMLElement* node)
{
    setBool(node, "isSelected", element.isSelected, false);
    setBool(node, "isFloating", element.isFloating, false);
    setBool(node, "lockFlag", element.isLocked, false);

    writeMatrix(element.transform, node);
    writeTransformationPoint(element.transformationPoint, node);
}

void XFLWriter::writeShape(const Shape& shape, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMShape");

    writeElementCommon(shape, node);

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

    if (!shape.edges.empty())
    {
        XMLElement* edges = doc->NewElement("edges");
        for (const Edge* edge : shape.edges)
        {
            if (!edge)
                continue;

            XMLElement* edgeNode = doc->NewElement("Edge");
            setInt(edgeNode, "fillStyle0", edge->fillStyle0, -1);
            setInt(edgeNode, "fillStyle1", edge->fillStyle1, -1);
            setInt(edgeNode, "strokeStyle", edge->strokeStyle, -1);

            // Prefer the text the file was read with: it round-trips exactly,
            // including anything the reader normalised away. Regenerating is for
            // edges that have actually been edited.
            const std::string data = edge->data.empty()
                ? EdgeWriter::writeEdge(*edge)
                : edge->data;
            edgeNode->SetAttribute("edges", data.c_str());

            edges->InsertEndChild(edgeNode);
        }
        node->InsertEndChild(edges);
    }

    parent->InsertEndChild(node);
}

void XFLWriter::writeSymbolInstance(const SymbolInstance& instance, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMSymbolInstance");

    setString(node, "libraryItemName", instance.libraryItemName);
    setInt(node, "firstFrame", instance.firstFrame, 0);

    if (instance.symbolType != SymbolType::Graphic)
        node->SetAttribute("symbolType", symbolTypeName(instance.symbolType));
    if (instance.loopType != LoopType::PlayOnce)
        node->SetAttribute("loopType", loopTypeName(instance.loopType));

    writeElementCommon(instance, node);

    const ColorTransform& color = instance.colorTransform;
    const bool hasTintColor =
        color.tintColor[0] != 0 || color.tintColor[1] != 0 || color.tintColor[2] != 0;

    if (!color.isIdentity() || hasTintColor)
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

    parent->InsertEndChild(node);
}

void XFLWriter::writeGroup(const Group& group, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMGroup");

    writeElementCommon(group, node);

    XMLElement* members = doc->NewElement("members");
    for (const Element* member : group.members)
    {
        if (member)
            writeElement(*member, members);
    }
    node->InsertEndChild(members);

    parent->InsertEndChild(node);
}

void XFLWriter::writeBitmapInstance(const BitmapInstance& instance, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMBitmapInstance");

    setString(node, "libraryItemName", instance.libraryItemName);
    writeElementCommon(instance, node);

    parent->InsertEndChild(node);
}

void XFLWriter::writeStaticText(const StaticText& text, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMStaticText");

    setDouble(node, "left", text.left, 0.0);
    setDouble(node, "top", text.top, 0.0);
    setDouble(node, "width", text.width, 0.0);
    setDouble(node, "height", text.height, 0.0);
    setBool(node, "autoExpand", text.autoExpand, false);

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
            if (run.alignment != TextRun::Alignment::Left)
                attrNode->SetAttribute("alignment", alignmentName(run.alignment));
            setBool(attrNode, "aliasText", run.aliasText, false);
            setBool(attrNode, "autoKern", run.autoKern, false);
            setDouble(attrNode, "letterSpacing", run.letterSpacing, 0.0);
            setDouble(attrNode, "lineHeight", run.lineHeight, 0.0);
            setDouble(attrNode, "size", run.size, 0.0);
            setDouble(attrNode, "bitmapSize", run.bitmapSize, 0.0);
            setString(attrNode, "face", run.face);
            setColor(attrNode, "fillColor", nullptr, run.fillColor);
            attrs->InsertEndChild(attrNode);
            runNode->InsertEndChild(attrs);

            runs->InsertEndChild(runNode);
        }
        node->InsertEndChild(runs);
    }

    parent->InsertEndChild(node);
}

void XFLWriter::writeRectanglePrimitive(const RectanglePrimitive& rectangle, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMRectangleObject");

    setDouble(node, "x", rectangle.rect.topLeft.x, 0.0);
    setDouble(node, "y", rectangle.rect.topLeft.y, 0.0);
    setDouble(node, "objectWidth", rectangle.rect.width(), 0.0);
    setDouble(node, "objectHeight", rectangle.rect.height(), 0.0);

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

    parent->InsertEndChild(node);
}

void XFLWriter::writeOvalPrimitive(const OvalPrimitive& oval, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMOvalObject");

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
            segmentNode->InsertEndChild(curvesNode);
        }

        segments->InsertEndChild(segmentNode);
    }
    node->InsertEndChild(segments);

    parent->InsertEndChild(node);
}

void XFLWriter::writeFrame(const Frame& frame, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMFrame");

    node->SetAttribute("index", frame.index);
    setInt(node, "duration", frame.duration, 1);
    setString(node, "keyMode", frame.keyMode);
    setBool(node, "motionTWeenSnap", frame.motionTWeenSnap, false);

    if (frame.tweenType == TweenType::Motion)
        node->SetAttribute("tweenType", "motion");
    else if (frame.tweenType == TweenType::Shape)
        node->SetAttribute("tweenType", "shape");

    if (frame.actionScript)
    {
        XMLElement* actionScript = doc->NewElement("Actionscript");
        XMLElement* script = doc->NewElement("script");
        script->SetText(frame.actionScript->code.c_str());
        actionScript->InsertEndChild(script);
        node->InsertEndChild(actionScript);
    }

    if (frame.morphShape)
        writeMorphShape(*frame.morphShape, node);

    XMLElement* elements = doc->NewElement("elements");
    for (const Element* element : frame.elements)
    {
        if (element)
            writeElement(*element, elements);
    }
    node->InsertEndChild(elements);

    parent->InsertEndChild(node);
}

void XFLWriter::writeLayer(const Layer& layer, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMLayer");

    setString(node, "name", layer.name);
    setColor(node, "color", nullptr, layer.color);
    setString(node, "current", layer.current);

    if (layer.layerType != Layer::Type::Normal)
        node->SetAttribute("layerType", layerTypeName(layer.layerType));

    setInt(node, "parentLayerIndex", layer.parentLayerIndex, -1);
    setBool(node, "isSelected", layer.selected, false);
    setBool(node, "autoNamed", layer.autoNamed, true);
    setBool(node, "locked", layer.locked, false);
    setBool(node, "visible", layer.visible, true);

    XMLElement* frames = doc->NewElement("frames");
    for (const Frame* frame : layer.frames)
    {
        if (frame)
            writeFrame(*frame, frames);
    }
    node->InsertEndChild(frames);

    parent->InsertEndChild(node);
}

void XFLWriter::writeTimeline(const Timeline& timeline, XMLElement* parent)
{
    XMLDocument* doc = parent->GetDocument();
    XMLElement* node = doc->NewElement("DOMTimeline");

    setString(node, "name", timeline.name);
    setBool(node, "layerDepthEnabled", timeline.layerDepthEnabled, false);

    XMLElement* layers = doc->NewElement("layers");
    for (const Layer* layer : timeline.layers)
    {
        if (layer)
            writeLayer(*layer, layers);
    }
    node->InsertEndChild(layers);

    parent->InsertEndChild(node);
}

std::string XFLWriter::writeDocument(const Document& document)
{
    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());

    XMLElement* root = doc.NewElement("DOMDocument");
    root->SetAttribute("xmlns", "http://ns.adobe.com/xfl/2008/");
    root->SetAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");

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
    setInt(root, "viewAngle3D", document.viewAngle3D, 0);
    setColor(root, "backgroundColor", nullptr, document.backgroundColor);
    setColor(root, "gridColor", nullptr, document.gridColor);
    setInt(root, "gridSpacingX", document.gridSpacingX, 18);
    setInt(root, "gridSpacingY", document.gridSpacingY, 18);
    setBool(root, "objectsSnapTo", document.objectsSnapTo, false);
    setInt(root, "snapAlignBorderSpacing", document.snapAlignBorderSpacing, 18);
    setColor(root, "guidesColor", nullptr, document.guidesColor);
    setInt(root, "vanishingPoint3DX", document.vanishingPoint3DX, 0);
    setInt(root, "vanishingPoint3DY", document.vanishingPoint3DY, 0);
    setInt(root, "nextSceneIdentifier", document.nextSceneIdentifier, 0);
    setBool(root, "playOptionsPlayLoop", document.playOptionsPlayLoop, false);
    setBool(root, "playOptionsPlayPages", document.playOptionsPlayPages, false);
    setBool(root, "playOptionsPlayFrameActions", document.playOptionsPlayFrameActions, false);

    if (!document.folders.empty())
    {
        XMLElement* folders = doc.NewElement("folders");
        for (const Folder* folder : document.folders)
        {
            if (!folder)
                continue;
            XMLElement* node = doc.NewElement("DOMFolderItem");
            setString(node, "name", folder->name);
            setString(node, "itemId", folder->itemId);
            setBool(node, "isExpanded", folder->isExpanded, false);
            folders->InsertEndChild(node);
        }
        root->InsertEndChild(folders);
    }

    if (!document.resources.empty())
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
            setString(node, "name", bitmap->name);
            setString(node, "itemId", bitmap->itemId);
            setString(node, "href", bitmap->href);
            media->InsertEndChild(node);
        }
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
            symbols->InsertEndChild(include);
        }
        root->InsertEndChild(symbols);
    }

    XMLElement* timelines = doc.NewElement("timelines");
    for (const Timeline* timeline : document.timelines)
    {
        if (timeline)
            writeTimeline(*timeline, timelines);
    }
    root->InsertEndChild(timelines);

    // Read but not yet modelled well enough to write back.
    if (document.scriptList)
        note("document scripts");
    if (document.publishHistory)
        note("publish history");
    if (document.printerSettings)
        note("printer settings");
    if (!document.swatchLists.empty())
        note("swatch lists");

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

    setString(root, "name", symbol.name);
    setString(root, "itemId", symbol.itemId);
    setString(root, "lastModified", symbol.lastModified);

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
