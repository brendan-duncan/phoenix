#include "document_parser.h"
#include "../data/raw_element.h"
#include <initializer_list>
#include "path_parser.h"
#include "../data/bitmap.h"
#include "../data/bitmap_fill.h"
#include "../data/bitmap_instance.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/morph_shape.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/swatch.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../third_party/tinyxml2/tinyxml2.h"
#include <cstdint>
#include "../edit/shape_tween.h"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <cstring>

namespace {

void describeVideo(fla::RawElement& raw);

std::string _currentFile;

bool parseGroup(const tinyxml2::XMLElement* element, fla::Group* group);

/// The XML parser drops text that is only whitespace, so a text run of a
/// single space between two words would lose it. Each such run's characters
/// become character references, which the parser keeps.
std::string keepBlankCharacters(const std::string& xml)
{
    static const std::string open = "<characters>";
    static const std::string close = "</characters>";
    std::string out;
    size_t copied = 0;
    for (size_t at = xml.find(open); at != std::string::npos; at = xml.find(open, at + open.size()))
    {
        const size_t start = at + open.size();
        const size_t end = xml.find(close, start);
        if (end == std::string::npos)
            break;
        if (end == start || xml.find_first_not_of(" \t\r\n", start) != end)
            continue;
        out.append(xml, copied, start - copied);
        for (size_t i = start; i < end; ++i)
            out += "&#" + std::to_string(static_cast<int>(xml[i])) + ";";
        copied = end;
    }
    if (copied == 0)
        return xml;
    out.append(xml, copied, std::string::npos);
    return out;
}

// Helper function to safely get attribute as string
std::string getAttribute(const tinyxml2::XMLElement* element, const char* name, const char* defaultValue = "")
{
    const char* value = element->Attribute(name);
    return value ? std::string(value) : std::string(defaultValue);
}

// Helper function to check if attribute exists
bool hasAttribute(const tinyxml2::XMLElement* element, const char* name)
{
    return element->Attribute(name) != nullptr;
}

// Helper function to get double attribute with default
double getDoubleAttribute(const tinyxml2::XMLElement* element, const char* name, double defaultValue = 0.0)
{
    double value = defaultValue;
    element->QueryDoubleAttribute(name, &value);
    return value;
}

// Helper function to get int attribute with default
int getIntAttribute(const tinyxml2::XMLElement* element, const char* name, int defaultValue = 0)
{
    int value = defaultValue;
    element->QueryIntAttribute(name, &value);
    return value;
}

// Helper function to get bool attribute with default
bool getBoolAttribute(const tinyxml2::XMLElement* element, const char* name, bool defaultValue = false)
{
    bool value = defaultValue;
    element->QueryBoolAttribute(name, &value);
    return value;
}

/// Sorts an element's attributes for keeping: the modeled ones (in
/// \a modeled) the file wrote are noted by name, so a save writes them even
/// at Phoenix's default, and the rest are kept with their values.
void keepAttributes(const tinyxml2::XMLElement* element, std::initializer_list<const char*> modeled,
    std::vector<std::pair<std::string, std::string>>& extra, std::vector<std::string>& present)
{
    for (const tinyxml2::XMLAttribute* attribute = element->FirstAttribute(); attribute;
         attribute = attribute->Next())
    {
        bool known = false;
        for (const char* name : modeled)
            known = known || std::strcmp(name, attribute->Name()) == 0;
        if (known)
            present.emplace_back(attribute->Name());
        else
            extra.emplace_back(attribute->Name(), attribute->Value());
    }
}

void keepAttributes(const tinyxml2::XMLElement* element, std::initializer_list<const char*> modeled,
    fla::DOMElement* into)
{
    keepAttributes(element, modeled, into->extraAttributes, into->presentAttributes);
}

void parseHexColor(const std::string& hexColor, uint8_t rgba[4], double alpha = 1.0)
{
    // Rounded, not truncated: files write 85/255 as 0.333333333333333, which
    // truncates to 84.
    uint8_t a = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(1.0, alpha)) * 255));
    // Remove '#' if present
    if (hexColor.size() == 0)
    {
        rgba[0] = 0;
        rgba[1] = 0;
        rgba[2] = 0;
        rgba[3] = a; // Default to opaque black
        return;
    }

    std::string color = hexColor;

    if (color[0] == '#')
    {
        color = color.substr(1);
    }

    // Handle 3-digit hex (#RGB) or 6-digit hex (#RRGGBB)
    if (color.size() == 3)
    {
        // Expand 3-digit to 6-digit (RGB -> RRGGBB)
        std::string expanded;
        for (int i = 0; i < 3; ++i)
        {
            expanded += color[i];
            expanded += color[i];
        }
        color = expanded;
    }

    // Parse 6-digit hex
    if (color.size() == 6)
    {
        // strtol, not stoi: a color that is not hex reads as 0 rather than
        // throwing out of the whole load.
        rgba[0] = static_cast<int>(std::strtol(color.substr(0, 2).c_str(), nullptr, 16));
        rgba[1] = static_cast<int>(std::strtol(color.substr(2, 2).c_str(), nullptr, 16));
        rgba[2] = static_cast<int>(std::strtol(color.substr(4, 2).c_str(), nullptr, 16));
        rgba[3] = a; // Alpha is fully opaque
    }
    else
    {
        // Default to black if invalid format
        rgba[0] = 0;
        rgba[1] = 0;
        rgba[2] = 0;
        rgba[3] = a;
    }
}

void getColorAttribute(const tinyxml2::XMLElement* element, const char* attributeName, uint8_t rgba[4])
{
    if (hasAttribute(element, attributeName))
    {
        std::string color = getAttribute(element, attributeName);
        parseHexColor(color, rgba);
    }
}

void parseFillStyle(const tinyxml2::XMLElement* element, fla::FillStyle* fillStyle)
{
    fillStyle->spreadMethod = getAttribute(element, "spreadMethod");
    fillStyle->interpolationMethod = getAttribute(element, "interpolationMethod");

    const tinyxml2::XMLElement* matrixNode = element->FirstChildElement("matrix");
    if (matrixNode)
    {
        const tinyxml2::XMLElement* matrixEl = matrixNode->FirstChildElement("Matrix");
        if (matrixEl)
        {
            fillStyle->transform.m11 = getDoubleAttribute(matrixEl, "a", 1.0);
            fillStyle->transform.m12 = getDoubleAttribute(matrixEl, "b", 0.0);
            fillStyle->transform.m21 = getDoubleAttribute(matrixEl, "c", 0.0);
            fillStyle->transform.m22 = getDoubleAttribute(matrixEl, "d", 1.0);
            fillStyle->transform.tx = getDoubleAttribute(matrixEl, "tx", 0.0);
            fillStyle->transform.ty = getDoubleAttribute(matrixEl, "ty", 0.0);
        }
        else
        {
            std::cerr << "!!!! Unhandled fillStyle matrix element: " << matrixNode->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
}

bool parseSolidColor(const tinyxml2::XMLElement* element, fla::SolidColor* solidColor)
{
    std::string color = getAttribute(element, "color");
    double alpha = getDoubleAttribute(element, "alpha", 1.0);

    // Parse CSS hex color if it starts with '#'
    parseHexColor(color, solidColor->color, alpha);
    // What else it carries (a Phoenix palette link) is kept for the save.
    keepAttributes(element, {"color", "alpha"}, solidColor);

    return true;
}

bool parseLinearGradient(const tinyxml2::XMLElement* element, fla::LinearGradient* linearGradient)
{
    parseFillStyle(element, linearGradient);

    for (const tinyxml2::XMLElement* entryElement = element->FirstChildElement("GradientEntry");
         entryElement != nullptr;
         entryElement = entryElement->NextSiblingElement("GradientEntry"))
    {
        fla::GradientEntry entry;
        entry.ratio = getDoubleAttribute(entryElement, "ratio");

        std::string color = getAttribute(entryElement, "color");
        parseHexColor(color, entry.color, getDoubleAttribute(entryElement, "alpha", 1.0));

        linearGradient->entries.push_back(entry);
    }

    std::sort(linearGradient->entries.begin(), linearGradient->entries.end(),
        [](const fla::GradientEntry& a, const fla::GradientEntry& b) {
            return a.ratio < b.ratio;
        });

    return true;
}

bool parseRadialGradient(const tinyxml2::XMLElement* element, fla::RadialGradient* radialGradient)
{
    parseFillStyle(element, radialGradient);

    if (hasAttribute(element, "focalPointRatio"))
    {
        radialGradient->focalPointRatio = getDoubleAttribute(element, "focalPointRatio");
    }

    for (const tinyxml2::XMLElement* entryElement = element->FirstChildElement("GradientEntry");
         entryElement != nullptr;
         entryElement = entryElement->NextSiblingElement("GradientEntry"))
    {
        fla::RadialEntry entry;
        entry.ratio = getDoubleAttribute(entryElement, "ratio");

        std::string color = getAttribute(entryElement, "color");
        parseHexColor(color, entry.color, getDoubleAttribute(entryElement, "alpha", 1.0));

        radialGradient->entries.push_back(entry);
    }

    std::sort(radialGradient->entries.begin(), radialGradient->entries.end(),
        [](const fla::RadialEntry& a, const fla::RadialEntry& b) {
            return a.ratio < b.ratio;
        });

    return true;
}

fla::Edge* parsePath(const tinyxml2::XMLElement* element, fla::DOMElement* parent)
{
    // Try to get edge data from either "edges" or "cubics" attribute
    std::string edgeData = getAttribute(element, "edges");
    if (edgeData.empty())
    {
        std::cerr << "Edge element missing edges/cubics attribute" << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        return nullptr;
    }

    PathParser pathParser;
    fla::Edge* edge = pathParser.parse(edgeData, parent);
    if (!edge)
    {
        std::cerr << "Failed to parse path edges: " << pathParser.errorString() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        return nullptr;
    }

    edge->data = edgeData;
    edge->fillStyle0 = getIntAttribute(element, "fillStyle0", -1);
    edge->fillStyle1 = getIntAttribute(element, "fillStyle1", -1);
    edge->strokeStyle = getIntAttribute(element, "strokeStyle", -1);

    return edge;
}

std::string elementText(const tinyxml2::XMLElement* element);

bool parseEdges(const tinyxml2::XMLElement* element, fla::Shape* shape)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "Edge") == 0)
        {
            if (hasAttribute(childElement, "edges"))
            {
                fla::Edge* edge = parsePath(childElement, shape);
                if (!edge)
                {
                    return false;
                }

                shape->edges.push_back(edge);
            }
            else if (hasAttribute(childElement, "cubics"))
            {
                // The cubic curves a pen or brush drew, which FLA files keep
                // for editing only: kept as read, in their place among the edges.
                shape->cubicEdges.emplace_back(shape->edges.size(), elementText(childElement));
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled edge element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    return true;
}

fla::FillStyle* parseFillStyle(const tinyxml2::XMLElement* element, fla::DOMElement* parent)
{
    const char* tagName = element->Name();

    if (std::strcmp(tagName, "SolidColor") == 0)
    {
        fla::SolidColor* solidFill = new fla::SolidColor(parent);
        if (!parseSolidColor(element, solidFill))
        {
            delete solidFill;
            return nullptr;
        }

        return solidFill;
    }
    else if (std::strcmp(tagName, "LinearGradient") == 0)
    {
        fla::LinearGradient* linearFill = new fla::LinearGradient(parent);
        if (!parseLinearGradient(element, linearFill))
        {
            delete linearFill;
            return nullptr;
        }
        return linearFill;
    }
    else if (std::strcmp(tagName, "RadialGradient") == 0)
    {
        fla::RadialGradient* radialFill = new fla::RadialGradient(parent);
        if (!parseRadialGradient(element, radialFill))
        {
            delete radialFill;
            return nullptr;
        }
        return radialFill;
    }
    else if (std::strcmp(tagName, "BitmapFill") == 0)
    {
        // Textured paint Phoenix baked for other apps: the solid fill it was.
        const std::string solid = getAttribute(element, "phoenixSolidColor");
        if (!solid.empty())
        {
            fla::SolidColor* solidFill = new fla::SolidColor(parent);
            parseHexColor(solid, solidFill->color, getDoubleAttribute(element, "phoenixSolidAlpha", 1.0));
            return solidFill;
        }
        fla::BitmapFill* bitmapFill = new fla::BitmapFill(parent);
        parseFillStyle(element, bitmapFill);
        bitmapFill->bitmapPath = getAttribute(element, "bitmapPath");
        bitmapFill->bitmapIsClipped = getBoolAttribute(element, "bitmapIsClipped", false);
        return bitmapFill;
    }
    std::cerr << "!!!! Unhandled fill element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
    return nullptr;
}

std::string elementText(const tinyxml2::XMLElement* element);

bool parseStroke(const tinyxml2::XMLElement* element, fla::StrokeStyle* stroke)
{
    stroke->scaleMode = getAttribute(element, "scaleMode");
    stroke->weight = getDoubleAttribute(element, "weight", 1.0);
    stroke->solidStyle = getAttribute(element, "solidStyle");
    stroke->caps = getAttribute(element, "caps");
    stroke->joints = getAttribute(element, "joints");
    stroke->miterLimit = getDoubleAttribute(element, "miterLimit", 3.0);
    stroke->pixelHinting = getBoolAttribute(element, "pixelHinting", false);
    stroke->dash1 = getDoubleAttribute(element, "dash1", 6.0);
    stroke->dash2 = getDoubleAttribute(element, "dash2", 6.0);
    stroke->dotSpace = getDoubleAttribute(element, "dotSpace", 3.0);
    stroke->paintBrushDefinition = getIntAttribute(element, "paintBrushDefinition", 0);
    for (const char* name : {"pattern", "waveHeight", "waveLength", "dotSize", "variation", "density",
             "hatchThickness", "space", "jiggle", "rotate", "curve", "length"})
    {
        if (hasAttribute(element, name))
            stroke->styleAttributes[name] = getAttribute(element, name);
    }
    // Anything else on the stroke, such as Phoenix's own brush, is kept.
    std::vector<std::string> present;
    keepAttributes(element, {"scaleMode", "weight", "solidStyle", "caps", "joints", "miterLimit", "pixelHinting",
        "dash1", "dash2", "dotSpace", "paintBrushDefinition", "pattern", "waveHeight", "waveLength", "dotSize", "variation", "density",
        "hatchThickness", "space", "jiggle", "rotate", "curve", "length"}, stroke->extraAttributes, present);

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "fill") == 0)
        {
            for (const tinyxml2::XMLElement* fillElement = childElement->FirstChildElement();
                 fillElement != nullptr;
                 fillElement = fillElement->NextSiblingElement())
            {
                stroke->fill = parseFillStyle(fillElement, stroke);
                if (!stroke->fill)
                {
                    return false;
                }
            }
        }
        else
        {
            // A variable-width profile and the like: kept and written back.
            stroke->extraChildren.push_back(elementText(childElement));
            if (std::strcmp(childElement->Name(), "VariablePointWidth") == 0)
            {
                for (const tinyxml2::XMLElement* marker = childElement->FirstChildElement("WidthMarker");
                     marker != nullptr;
                     marker = marker->NextSiblingElement("WidthMarker"))
                {
                    fla::StrokeStyle::WidthMarker point;
                    point.position = getDoubleAttribute(marker, "position", 0.0);
                    point.left = getDoubleAttribute(marker, "left", 0.5);
                    point.right = getDoubleAttribute(marker, "right", 0.5);
                    point.corner = getAttribute(marker, "type") == "corner";
                    stroke->widthProfile.push_back(point);
                }
                std::sort(stroke->widthProfile.begin(), stroke->widthProfile.end(),
                    [](const fla::StrokeStyle::WidthMarker& a, const fla::StrokeStyle::WidthMarker& b) {
                        return a.position < b.position;
                    });
            }
        }
    }
    return true;
}

fla::StrokeStyle* parseStrokeStyle(const tinyxml2::XMLElement* element, fla::DOMElement* parent)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();
        fla::StrokeStyle* stroke = nullptr;

        if (std::strcmp(tagName, "SolidStroke") == 0)
        {
            stroke = new fla::SolidStroke(parent);
        }
        else if (std::strcmp(tagName, "DashedStroke") == 0)
        {
            stroke = new fla::DashedStroke(parent);
        }
        else if (std::strcmp(tagName, "RaggedStroke") == 0)
        {
            stroke = new fla::RaggedStroke(parent);
        }
        else if (std::strcmp(tagName, "StippleStroke") == 0)
        {
            stroke = new fla::StippleStroke(parent);
        }
        else if (std::strcmp(tagName, "DottedStroke") == 0)
        {
            stroke = new fla::DottedStroke(parent);
        }
        else if (std::strcmp(tagName, "HatchedStroke") == 0)
        {
            stroke = new fla::HatchedStroke(parent);
        }
        else if (std::strcmp(tagName, "PaintBrushStroke") == 0)
        {
            stroke = new fla::PaintBrushStroke(parent);
        }
        else
        {
            std::cerr << "!!!! Unhandled strokestyle element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
            continue;
        }

        if (!parseStroke(childElement, stroke))
        {
            delete stroke;
            return nullptr;
        }

        return stroke;
    }

    return nullptr;
}

bool parseStrokes(const tinyxml2::XMLElement* element, fla::Shape* shape)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "StrokeStyle") == 0)
        {
            int index = getIntAttribute(childElement, "index", -1);
            fla::StrokeStyle* strokeStyle = parseStrokeStyle(childElement, shape);
            if (!strokeStyle)
            {
                return false;
            }
            shape->strokes.push_back(strokeStyle);
            // Add to strokesMap for quick lookup by index
            shape->strokesMap[index] = strokeStyle;
        }
        else
        {
            std::cerr << "!!!! Unhandled strokes element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

bool parseFills(const tinyxml2::XMLElement* element, fla::Shape* shape)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "FillStyle") == 0)
        {
            int index = getIntAttribute(childElement, "index");

            const tinyxml2::XMLElement* fillElement = childElement->FirstChildElement();
            if (!fillElement)
            {
                continue;
            }

            fla::FillStyle *fillStyle = parseFillStyle(fillElement, shape);
            if (!fillStyle)
            {
                return false;
            }

            shape->fills.push_back(fillStyle);
            shape->fillsMap[index] = fillStyle;
        }
        else
        {
            std::cerr << "!!!! Unhandled fills element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

bool parseElement(const tinyxml2::XMLElement* element, fla::Element* el)
{
    el->isSelected = getBoolAttribute(element, "isSelected", false);
    el->isFloating = getBoolAttribute(element, "isFloating", false);
    el->isLocked = getBoolAttribute(element, "lockFlag", false);
    // Display > Visible switched off: the instance is left out of what is
    // shown. The attribute itself is kept as read for the save.
    el->visible = getBoolAttribute(element, "isVisible", true);

    // Parse transformation matrix
    const tinyxml2::XMLElement* matrixNode = element->FirstChildElement("matrix");
    if (matrixNode)
    {
        el->keepMatrix = true;
        const tinyxml2::XMLElement* matrixEl = matrixNode->FirstChildElement("Matrix");
        if (matrixEl)
        {
            el->transform.m11 = getDoubleAttribute(matrixEl, "a", 1.0);
            el->transform.m12 = getDoubleAttribute(matrixEl, "b", 0.0);
            el->transform.m21 = getDoubleAttribute(matrixEl, "c", 0.0);
            el->transform.m22 = getDoubleAttribute(matrixEl, "d", 1.0);
            el->transform.tx = getDoubleAttribute(matrixEl, "tx", 0.0);
            el->transform.ty = getDoubleAttribute(matrixEl, "ty", 0.0);
        }
        else
        {
            std::cerr << "!!!! Unhandled element matrix element: " << matrixNode->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    // Parse transformation point
    const tinyxml2::XMLElement* pointNode = element->FirstChildElement("transformationPoint");
    if (pointNode)
    {
        el->keepTransformationPoint = true;
        const tinyxml2::XMLElement* pointEl = pointNode->FirstChildElement("Point");
        if (pointEl)
        {
            double x = getDoubleAttribute(pointEl, "x", 0.0);
            double y = getDoubleAttribute(pointEl, "y", 0.0);
            el->transformationPoint = fla::Point(x, y);
        }
        else
        {
            std::cerr << "!!!! Unhandled element transformation point element: " << pointNode->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    return true;
}

bool parseShape(const tinyxml2::XMLElement* element, fla::Shape* shape)
{
    if (!parseElement(element, shape))
    {
        return false;
    }

    shape->isDrawingObject = getBoolAttribute(element, "isDrawingObject", false);
    keepAttributes(element, {"isSelected", "isFloating", "lockFlag", "isDrawingObject"}, shape);

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "fills") == 0)
        {
            if (!parseFills(childElement, shape))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "strokes") == 0)
        {
            if (!parseStrokes(childElement, shape))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "edges") == 0)
        {
            if (!parseEdges(childElement, shape))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "matrix") == 0)
        {
            // from parseElement
        }
        else if (std::strcmp(tagName, "transformationPoint") == 0)
        {
            // from parseElement
        }
        else
        {
            shape->extraChildren.push_back(elementText(childElement));
        }
    }

    shape->readEdgeCount = shape->edges.size();
    shape->bounds.reset();

    for (fla::Edge* edge : shape->edges)
    {
        for (fla::Path* path : edge->paths)
        {
            for (fla::PathSegment* segment : path->segments)
            {
                for (fla::Point& point : segment->points)
                {
                    shape->bounds.topLeft.x = std::min(shape->bounds.topLeft.x, point.x);
                    shape->bounds.topLeft.y = std::min(shape->bounds.topLeft.y, point.y);
                    shape->bounds.bottomRight.x = std::max(shape->bounds.bottomRight.x, point.x);
                    shape->bounds.bottomRight.y = std::max(shape->bounds.bottomRight.y, point.y);
                }
            }
        }
    }
    shape->localBounds = shape->bounds;
    shape->bounds.transform(shape->transform);

    return true;
}

/// Reads one filter element. The attributes a type does not have are left
/// at their defaults. False for an element that is not a filter.
bool parseFilter(const tinyxml2::XMLElement* element, fla::Filter& filter)
{
    const char* name = element->Name();
    if (std::strcmp(name, "BlurFilter") == 0)
        filter.type = fla::Filter::Type::Blur;
    else if (std::strcmp(name, "DropShadowFilter") == 0)
        filter.type = fla::Filter::Type::DropShadow;
    else if (std::strcmp(name, "GlowFilter") == 0)
        filter.type = fla::Filter::Type::Glow;
    else if (std::strcmp(name, "BevelFilter") == 0)
        filter.type = fla::Filter::Type::Bevel;
    else if (std::strcmp(name, "GradientGlowFilter") == 0)
        filter.type = fla::Filter::Type::GradientGlow;
    else if (std::strcmp(name, "GradientBevelFilter") == 0)
        filter.type = fla::Filter::Type::GradientBevel;
    else if (std::strcmp(name, "AdjustColorFilter") == 0)
        filter.type = fla::Filter::Type::AdjustColor;
    else
        return false;

    filter.enabled = getBoolAttribute(element, "enabled", true);
    filter.blurX = getDoubleAttribute(element, "blurX", 5.0);
    filter.blurY = getDoubleAttribute(element, "blurY", 5.0);
    filter.quality = getIntAttribute(element, "quality", 1);
    filter.angle = getDoubleAttribute(element, "angle", 45.0);
    filter.distance = getDoubleAttribute(element, "distance", 5.0);
    filter.strength = getDoubleAttribute(element, "strength", 1.0);
    filter.inner = getBoolAttribute(element, "inner", false);
    filter.knockout = getBoolAttribute(element, "knockout", false);
    filter.hideObject = getBoolAttribute(element, "hideObject", false);
    filter.bevelType = getAttribute(element, "type");
    filter.brightness = getDoubleAttribute(element, "brightness", 0.0);
    filter.contrast = getDoubleAttribute(element, "contrast", 0.0);
    filter.saturation = getDoubleAttribute(element, "saturation", 0.0);
    filter.hue = getDoubleAttribute(element, "hue", 0.0);
    keepAttributes(element, {"enabled", "blurX", "blurY", "quality", "angle", "distance", "strength", "inner",
        "knockout", "hideObject", "type", "brightness", "contrast", "saturation", "hue", "color", "alpha",
        "highlightColor", "highlightAlpha", "shadowColor", "shadowAlpha", "phoenixColorMatrix", "phoenixConvolution",
        "phoenixMatrixX", "phoenixMatrixY", "phoenixDivisor", "phoenixBias", "phoenixPreserveAlpha", "phoenixClamp", "phoenixEdgeColor",
        "phoenixEdgeAlpha"},
        filter.extraAttributes, filter.presentAttributes);

    // Phoenix's own filters, on an Adjust Color and a Blur that do nothing
    // elsewhere.
    const auto numbers = [](const std::string& text) {
        std::vector<double> values;
        std::stringstream stream(text);
        std::string part;
        while (std::getline(stream, part, ','))
            values.push_back(std::atof(part.c_str()));
        return values;
    };
    const std::string colorMatrix = getAttribute(element, "phoenixColorMatrix");
    const std::string convolution = getAttribute(element, "phoenixConvolution");
    if (filter.type == fla::Filter::Type::AdjustColor && !colorMatrix.empty())
    {
        filter.type = fla::Filter::Type::ColorMatrix;
        filter.matrix = numbers(colorMatrix);
        filter.matrix.resize(20, 0.0);
    }
    else if (filter.type == fla::Filter::Type::Blur && !convolution.empty())
    {
        filter.type = fla::Filter::Type::Convolution;
        filter.matrix = numbers(convolution);
        filter.matrixX = std::clamp(getIntAttribute(element, "phoenixMatrixX", 0), 0, 15);
        filter.matrixY = std::clamp(getIntAttribute(element, "phoenixMatrixY", 0), 0, 15);
        filter.matrix.resize(static_cast<size_t>(filter.matrixX * filter.matrixY), 0.0);
        filter.divisor = getDoubleAttribute(element, "phoenixDivisor", 1.0);
        filter.bias = getDoubleAttribute(element, "phoenixBias", 0.0);
        filter.preserveAlpha = getBoolAttribute(element, "phoenixPreserveAlpha", true);
        filter.clamp = getBoolAttribute(element, "phoenixClamp", true);
        filter.blurX = filter.blurY = 0.0;
    }

    const std::string color = getAttribute(element, "color");
    if (!color.empty())
        parseHexColor(color, filter.color, getDoubleAttribute(element, "alpha", 1.0));
    const std::string pastEdge = getAttribute(element, "phoenixEdgeColor");
    if (filter.type == fla::Filter::Type::Convolution)
    {
        filter.color[0] = filter.color[1] = filter.color[2] = filter.color[3] = 0;
        if (!pastEdge.empty())
            parseHexColor(pastEdge, filter.color, getDoubleAttribute(element, "phoenixEdgeAlpha", 0.0));
    }
    const std::string highlight = getAttribute(element, "highlightColor");
    if (!highlight.empty())
        parseHexColor(highlight, filter.highlightColor, getDoubleAttribute(element, "highlightAlpha", 1.0));
    const std::string shadow = getAttribute(element, "shadowColor");
    if (!shadow.empty())
        parseHexColor(shadow, filter.shadowColor, getDoubleAttribute(element, "shadowAlpha", 1.0));

    for (const tinyxml2::XMLElement* entryElement = element->FirstChildElement("GradientEntry");
         entryElement != nullptr;
         entryElement = entryElement->NextSiblingElement("GradientEntry"))
    {
        fla::GradientEntry entry;
        entry.ratio = getDoubleAttribute(entryElement, "ratio");
        parseHexColor(getAttribute(entryElement, "color"), entry.color,
            getDoubleAttribute(entryElement, "alpha", 1.0));
        filter.entries.push_back(entry);
    }
    return true;
}

bool parseSymbolInstance(const tinyxml2::XMLElement* element, fla::SymbolInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "color") == 0)
        {
            instance->keepColor = true;
            for (const tinyxml2::XMLElement* colorElement = childElement->FirstChildElement();
                 colorElement != nullptr;
                 colorElement = colorElement->NextSiblingElement())
            {
                if (std::strcmp(colorElement->Name(), "Color") == 0)
                {
                    instance->colorTransform.tintMultiplier = getDoubleAttribute(colorElement, "tintMultiplier", 0.0);
                    instance->colorTransform.alphaMultiplier = getDoubleAttribute(colorElement, "alphaMultiplier", 1.0);
                    instance->colorTransform.redMultiplier = getDoubleAttribute(colorElement, "redMultiplier", 1.0);
                    instance->colorTransform.greenMultiplier = getDoubleAttribute(colorElement, "greenMultiplier", 1.0);
                    instance->colorTransform.blueMultiplier = getDoubleAttribute(colorElement, "blueMultiplier", 1.0);
                    instance->colorTransform.alphaOffset = getDoubleAttribute(colorElement, "alphaOffset", 0.0);
                    instance->colorTransform.redOffset = getDoubleAttribute(colorElement, "redOffset", 0.0);
                    instance->colorTransform.greenOffset = getDoubleAttribute(colorElement, "greenOffset", 0.0);
                    instance->colorTransform.blueOffset = getDoubleAttribute(colorElement, "blueOffset", 0.0);
                    instance->colorTransform.brightness = getDoubleAttribute(colorElement, "brightness", 0.0);
                    std::string tintColor = getAttribute(colorElement, "tintColor", "");
                    if (!tintColor.empty())
                    {
                        parseHexColor(tintColor, instance->colorTransform.tintColor);
                    }
                }
                else
                {
                    std::cerr << "!!!! Unhandled symbol instance color element: " << colorElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
                }
            }
        }
        else if (std::strcmp(tagName, "matrix") == 0)
        {
            // from parseElement
        }
        else if (std::strcmp(tagName, "transformationPoint") == 0)
        {
            // from parseElement
        }
        else if (std::strcmp(tagName, "filters") == 0)
        {
            for (const tinyxml2::XMLElement* filterElement = childElement->FirstChildElement();
                 filterElement != nullptr;
                 filterElement = filterElement->NextSiblingElement())
            {
                fla::Filter filter;
                if (!parseFilter(filterElement, filter))
                {
                    // A filter Phoenix does not know, kept as read.
                    instance->extraFilters.push_back(elementText(filterElement));
                    continue;
                }
                instance->filters.push_back(filter);
            }
        }
        else
        {
            // Kept as written, so a save puts it back: a button's script.
            instance->extraChildren.push_back(elementText(childElement));
        }
    }

    instance->libraryItemName = getAttribute(element, "libraryItemName");
    instance->name = getAttribute(element, "name");
    keepAttributes(element, {"libraryItemName", "name", "blendMode", "firstFrame", "lastFrame", "symbolType", "loop",
        "loopType", "isSelected", "isFloating", "lockFlag"}, instance);
    instance->blendMode = getAttribute(element, "blendMode");
    // "normal" is what no blend mode means; FLA files leave it out.
    if (instance->blendMode == "normal")
        instance->blendMode.clear();
    instance->firstFrame = fla::clampFrameIndex(getIntAttribute(element, "firstFrame", 0));
    instance->lastFrame = std::min(getIntAttribute(element, "lastFrame", -1), fla::kMaxFrames);
    // Moved in 3D, the instance is placed by a 3D matrix and keeps its
    // transformation point in its parent's space.
    double values[16];
    if (!instance->keepMatrix && instance->matrix3D(values))
    {
        instance->transform = fla::SymbolInstance::transformOf3D(values);
        const fla::Transform& t = instance->transform;
        const double det = t.m11 * t.m22 - t.m12 * t.m21;
        if (std::fabs(det) > 1.0e-12)
        {
            const double x = instance->transformationPoint.x - t.tx;
            const double y = instance->transformationPoint.y - t.ty;
            instance->transformationPoint = fla::Point((t.m22 * x - t.m21 * y) / det, (-t.m12 * x + t.m11 * y) / det);
        }
    }

    // FLA files leave the type out for a movie clip and carry it for
    // everything else, a graphic always with its loop.
    std::string symbolType = getAttribute(element, "symbolType", "movie clip");
    if (symbolType == "graphic")
    {
        instance->symbolType = fla::SymbolType::Graphic;
    }
    else if (symbolType == "button")
    {
        instance->symbolType = fla::SymbolType::Button;
    }
    else if (symbolType == "movie clip")
    {
        instance->symbolType = fla::SymbolType::MovieClip;
    }
    else
    {
        // Drawn as a graphic, the type kept as spelled for the save.
        instance->symbolType = fla::SymbolType::Graphic;
        instance->otherSymbolType = symbolType;
    }

    // XFL may use "loop" or "loopType" for the loop mode
    std::string loopType = getAttribute(element, "loopType", "");
    if (loopType.empty())
        loopType = getAttribute(element, "loop", "play once");
    if (loopType == "single frame")
    {
        instance->loopType = fla::LoopType::SingleFrame;
    }
    else if (loopType == "loop")
    {
        instance->loopType = fla::LoopType::Loop;
    }
    else if (loopType == "play once")
    {
        instance->loopType = fla::LoopType::PlayOnce;
    }
    else if (loopType == "ping pong")
    {
        instance->loopType = fla::LoopType::PingPong;
    }
    else if (loopType == "loop reverse")
    {
        instance->loopType = fla::LoopType::LoopReverse;
    }
    else if (loopType == "play once reverse")
    {
        instance->loopType = fla::LoopType::PlayOnceReverse;
    }
    else
    {
        std::cerr << "Unknown loop type: " << loopType << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        instance->loopType = fla::LoopType::PlayOnce; // Default to play once
    }

    return true;
}

bool parseBitmapInstance(const tinyxml2::XMLElement* element, fla::BitmapInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    instance->libraryItemName = getAttribute(element, "libraryItemName");
    keepAttributes(element, {"libraryItemName", "isSelected", "isFloating", "lockFlag"}, instance);
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), "matrix") != 0 && std::strcmp(child->Name(), "transformationPoint") != 0)
            instance->extraChildren.push_back(elementText(child));
    }

    return true;
}

bool parseRectanglePrimitive(const tinyxml2::XMLElement* element, fla::RectanglePrimitive* rectangle)
{
    if (!parseElement(element, rectangle))
    {
        return false;
    }

    rectangle->rect.topLeft.x = getDoubleAttribute(element, "x");
    rectangle->rect.topLeft.y = getDoubleAttribute(element, "y");
    rectangle->rect.bottomRight.x = rectangle->rect.topLeft.x + getDoubleAttribute(element, "objectWidth");
    rectangle->rect.bottomRight.y = rectangle->rect.topLeft.y + getDoubleAttribute(element, "objectHeight");

    rectangle->bounds = rectangle->rect;
    rectangle->localBounds = rectangle->rect;

    rectangle->topLeftRadius = getDoubleAttribute(element, "topLeftRadius", 0.0);
    rectangle->topRightRadius = getDoubleAttribute(element, "topRightRadius", 0.0);
    rectangle->bottomLeftRadius = getDoubleAttribute(element, "bottomLeftRadius", 0.0);
    rectangle->bottomRightRadius = getDoubleAttribute(element, "bottomRightRadius", 0.0);
    keepAttributes(element, {"x", "y", "objectWidth", "objectHeight", "topLeftRadius", "topRightRadius",
        "bottomLeftRadius", "bottomRightRadius", "isSelected", "isFloating", "lockFlag"}, rectangle);

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "fill") == 0)
        {
            for (const tinyxml2::XMLElement* fillElement = childElement->FirstChildElement();
                 fillElement != nullptr;
                 fillElement = fillElement->NextSiblingElement())
            {
                rectangle->fillStyle = parseFillStyle(fillElement, rectangle);
            }
        }
        else if (std::strcmp(tagName, "stroke") == 0)
        {
            rectangle->strokeStyle = parseStrokeStyle(childElement, rectangle);
        }
        else if (std::strcmp(tagName, "matrix") == 0)
        {
            // from parseElement
        }
        else if (std::strcmp(tagName, "transformationPoint") == 0)
        {
            // from parseElement
        }
        else
        {
            rectangle->extraChildren.push_back(elementText(childElement));
        }
    }

    return true;
}

bool parseOvalPrimitive(const tinyxml2::XMLElement* element, fla::OvalPrimitive* oval)
{
    if (!parseElement(element, oval))
    {
        return false;
    }

    oval->rect.topLeft.x = getDoubleAttribute(element, "x");
    oval->rect.topLeft.y = getDoubleAttribute(element, "y");
    oval->rect.bottomRight.x = oval->rect.topLeft.x + getDoubleAttribute(element, "objectWidth");
    oval->rect.bottomRight.y = oval->rect.topLeft.y + getDoubleAttribute(element, "objectHeight");

    oval->startAngle = getDoubleAttribute(element, "startAngle", 0.0);
    oval->endAngle = getDoubleAttribute(element, "endAngle", 0.0);
    oval->innerRadius = getDoubleAttribute(element, "innerRadius", 0.0);
    keepAttributes(element, {"x", "y", "objectWidth", "objectHeight", "startAngle", "endAngle", "innerRadius",
        "isSelected", "isFloating", "lockFlag"}, oval);

    oval->bounds = oval->rect;
    oval->localBounds = oval->rect;

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "fill") == 0)
        {
            for (const tinyxml2::XMLElement* fillElement = childElement->FirstChildElement();
                 fillElement != nullptr;
                 fillElement = fillElement->NextSiblingElement())
            {
                oval->fillStyle = parseFillStyle(fillElement, oval);
            }
        }
        else if (std::strcmp(tagName, "stroke") == 0)
        {
            oval->strokeStyle = parseStrokeStyle(childElement, oval);
        }
        else if (std::strcmp(tagName, "matrix") == 0)
        {
            // from parseElement
        }
        else if (std::strcmp(tagName, "transformationPoint") == 0)
        {
            // from parseElement
        }
        else
        {
            oval->extraChildren.push_back(elementText(childElement));
        }
    }

    return true;
}

bool parseStaticText(const tinyxml2::XMLElement* element, fla::StaticText* staticText)
{
    if (!parseElement(element, staticText))
    {
        return false;
    }

    staticText->left = getDoubleAttribute(element, "left", 0.0);
    staticText->top = getDoubleAttribute(element, "top", 0.0);
    staticText->width = getDoubleAttribute(element, "width", 0.0);
    staticText->height = getDoubleAttribute(element, "height", 0.0);
    staticText->autoExpand = getBoolAttribute(element, "autoExpand", false);
    staticText->instanceName = getAttribute(element, "instanceName");
    staticText->lineType = getAttribute(element, "lineType");
    keepAttributes(element, {"left", "top", "width", "height", "autoExpand", "instanceName", "lineType",
        "isSelected", "isFloating", "lockFlag"}, staticText);
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
    {
        const char* name = child->Name();
        if (std::strcmp(name, "textRuns") != 0 && std::strcmp(name, "matrix") != 0 &&
            std::strcmp(name, "transformationPoint") != 0)
            staticText->extraChildren.push_back(elementText(child));
    }

    for (const tinyxml2::XMLElement* textRunsNode = element->FirstChildElement("textRuns");
         textRunsNode != nullptr;
         textRunsNode = textRunsNode->NextSiblingElement("textRuns"))
    {
        for (const tinyxml2::XMLElement* childElement = textRunsNode->FirstChildElement();
             childElement != nullptr;
             childElement = childElement->NextSiblingElement())
        {
            if (std::strcmp(childElement->Name(), "DOMTextRun") == 0)
            {
                fla::TextRun textRun;

                for (const tinyxml2::XMLElement* textRunChildElement = childElement->FirstChildElement();
                     textRunChildElement != nullptr;
                     textRunChildElement = textRunChildElement->NextSiblingElement())
                {
                    const char* tagName = textRunChildElement->Name();

                    if (std::strcmp(tagName, "characters") == 0)
                    {
                        const char* text = textRunChildElement->GetText();
                        if (text)
                        {
                            textRun.text = text;
                        }
                    }
                    else if (std::strcmp(tagName, "textAttrs") == 0)
                    {
                        for (const tinyxml2::XMLElement* attrElement = textRunChildElement->FirstChildElement();
                             attrElement != nullptr;
                             attrElement = attrElement->NextSiblingElement())
                        {
                            if (std::strcmp(attrElement->Name(), "DOMTextAttrs") == 0)
                            {
                                std::string alignment = getAttribute(attrElement, "alignment");
                                if (!alignment.empty())
                                {
                                    if (alignment == "left")
                                    {
                                        textRun.alignment = fla::TextRun::Alignment::Left;
                                    }
                                    else if (alignment == "center")
                                    {
                                        textRun.alignment = fla::TextRun::Alignment::Center;
                                    }
                                    else if (alignment == "right")
                                    {
                                        textRun.alignment = fla::TextRun::Alignment::Right;
                                    }
                                    else if (alignment == "justify")
                                    {
                                        textRun.alignment = fla::TextRun::Alignment::Justify;
                                    }
                                    else
                                    {
                                        std::cerr << "!!!! Unhandled text alignment value: " << alignment << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
                                    }
                                }
                                textRun.aliasText = getBoolAttribute(attrElement, "aliasText", false);
                                textRun.autoKern = getBoolAttribute(attrElement, "autoKern", true);
                                textRun.letterSpacing = getDoubleAttribute(attrElement, "letterSpacing", 0.0);
                                textRun.lineHeight = getDoubleAttribute(attrElement, "lineHeight", 0.0);
                                textRun.size = getDoubleAttribute(attrElement, "size", 0.0);
                                textRun.bitmapSize = getDoubleAttribute(attrElement, "bitmapSize", 0.0);
                                textRun.face = getAttribute(attrElement, "face");
                                getColorAttribute(attrElement, "fillColor", textRun.fillColor);
                                keepAttributes(attrElement, {"alignment", "aliasText", "autoKern", "letterSpacing",
                                    "lineHeight", "size", "bitmapSize", "face", "fillColor"},
                                    textRun.extraAttributes, textRun.presentAttributes);
                            }
                        }
                    }
                    else
                    {
                        std::cerr << "!!!! Unhandled text run child element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
                    }
                }

                // An empty run is kept too: it carries a paragraph's
                // formatting.
                staticText->runs.push_back(textRun);
            }
            else
            {
                std::cerr << "!!!! Unhandled static text run element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
            }
        }
    }

    staticText->bounds.reset();
    for (const fla::TextRun& run : staticText->runs)
    {
        // For simplicity, we can estimate the bounds based on the number of characters and font size
        double width = run.text.length() * run.size * 0.6; // Approximate character width
        double height = run.lineHeight > 0 ? run.lineHeight : run.size; // Use lineHeight if specified, otherwise use size

        staticText->bounds.topLeft.x = std::min(staticText->bounds.topLeft.x, 0.0);
        staticText->bounds.topLeft.y = std::min(staticText->bounds.topLeft.y, -height);
        staticText->bounds.bottomRight.x = std::max(staticText->bounds.bottomRight.x, width);
        staticText->bounds.bottomRight.y = std::max(staticText->bounds.bottomRight.y, 0.0);
    }
    staticText->localBounds = staticText->bounds;
    staticText->bounds.translate(staticText->transformationPoint.x, staticText->transformationPoint.y);
    staticText->bounds.transform(staticText->transform);
    staticText->bounds.translate(-staticText->transformationPoint.x, -staticText->transformationPoint.y);

    return true;
}

std::string elementText(const tinyxml2::XMLElement* element);

namespace {

/// The character formatting a TLF element passes down to what it holds.
struct TlfStyle
{
    std::string family = "Arial";
    double size = 12.0;
    uint8_t color[4] = {0, 0, 0, 255};
    bool bold = false;
    bool italic = false;
    std::string tracking;
    fla::TextRun::Alignment alignment = fla::TextRun::Alignment::Left;
};

/// \a style with what \a element sets of TLF's formatting attributes.
TlfStyle tlfStyleOf(const tinyxml2::XMLElement* element, TlfStyle style)
{
    if (const char* family = element->Attribute("fontFamily"))
    {
        // A list of fallbacks: the first is the font meant.
        std::string first = family;
        first = first.substr(0, first.find(','));
        first.erase(std::remove(first.begin(), first.end(), '\''), first.end());
        if (!first.empty())
            style.family = first;
    }
    if (const char* size = element->Attribute("fontSize"))
        if (std::strtod(size, nullptr) > 0.0)
            style.size = std::strtod(size, nullptr);
    if (const char* color = element->Attribute("color"))
    {
        std::string digits = color;
        if (!digits.empty() && digits[0] == '#')
            digits = digits.substr(1);
        else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
            digits = digits.substr(2);
        const unsigned long rgb = std::strtoul(digits.c_str(), nullptr, 16);
        style.color[0] = static_cast<uint8_t>((rgb >> 16) & 0xFF);
        style.color[1] = static_cast<uint8_t>((rgb >> 8) & 0xFF);
        style.color[2] = static_cast<uint8_t>(rgb & 0xFF);
    }
    if (const char* alpha = element->Attribute("textAlpha"))
        style.color[3] = static_cast<uint8_t>(std::clamp(std::strtod(alpha, nullptr), 0.0, 1.0) * 255.0 + 0.5);
    if (const char* weight = element->Attribute("fontWeight"))
        style.bold = std::strcmp(weight, "bold") == 0;
    if (const char* posture = element->Attribute("fontStyle"))
        style.italic = std::strcmp(posture, "italic") == 0;
    if (const char* tracking = element->Attribute("trackingRight"))
        style.tracking = tracking;
    if (const char* align = element->Attribute("textAlign"))
    {
        const std::string value = align;
        if (value == "center")
            style.alignment = fla::TextRun::Alignment::Center;
        else if (value == "right" || value == "end")
            style.alignment = fla::TextRun::Alignment::Right;
        else if (value == "justify")
            style.alignment = fla::TextRun::Alignment::Justify;
        else
            style.alignment = fla::TextRun::Alignment::Left;
    }
    return style;
}

/// A classic run with \a style's formatting.
fla::TextRun tlfRun(const TlfStyle& style)
{
    fla::TextRun run;
    run.face = style.family;
    if (style.bold || style.italic)
        run.face += std::string("-") + (style.bold ? "Bold" : "") + (style.italic ? "Italic" : "");
    run.size = style.size;
    std::copy(std::begin(style.color), std::end(style.color), run.fillColor);
    run.alignment = style.alignment;
    // Tracking is in pixels, or a percentage of the font's size.
    if (!style.tracking.empty())
    {
        const double amount = std::strtod(style.tracking.c_str(), nullptr);
        run.letterSpacing = style.tracking.back() == '%' ? style.size * amount / 100.0 : amount;
    }
    return run;
}

/// Adds the text inside \a element (a paragraph, span, link...) to \a runs
/// as runs of its formatting; a line break or tab inside it becomes the
/// character classic text holds for it.
void addTlfRuns(const tinyxml2::XMLElement* element, const TlfStyle& outer, std::vector<fla::TextRun>& runs)
{
    const TlfStyle style = tlfStyleOf(element, outer);
    for (const tinyxml2::XMLNode* node = element->FirstChild(); node; node = node->NextSibling())
    {
        std::string characters;
        if (const tinyxml2::XMLText* text = node->ToText())
            characters = text->Value() ? text->Value() : "";
        else if (const tinyxml2::XMLElement* child = node->ToElement())
        {
            if (std::strcmp(child->Name(), "br") == 0)
                characters = "\r";
            else if (std::strcmp(child->Name(), "tab") == 0)
                characters = "\t";
            else
            {
                // A span, link, or other holder of text (an image has none).
                addTlfRuns(child, style, runs);
                continue;
            }
        }
        if (characters.empty())
            continue;
        fla::TextRun run = tlfRun(style);
        if (!runs.empty() && runs.back().face == run.face && runs.back().size == run.size &&
            std::equal(std::begin(run.fillColor), std::end(run.fillColor), runs.back().fillColor) &&
            runs.back().letterSpacing == run.letterSpacing && runs.back().alignment == run.alignment)
            runs.back().text += characters;
        else
        {
            run.text = characters;
            runs.push_back(run);
        }
    }
}

/// Adds the paragraphs under \a element (the flow, or a division of it),
/// each ended by a paragraph break but the first's start.
void addTlfParagraphs(const tinyxml2::XMLElement* element, const TlfStyle& outer, std::vector<fla::TextRun>& runs)
{
    const TlfStyle style = tlfStyleOf(element, outer);
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child;
         child = child->NextSiblingElement())
    {
        if (std::strcmp(child->Name(), "div") == 0)
        {
            addTlfParagraphs(child, style, runs);
            continue;
        }
        if (std::strcmp(child->Name(), "p") != 0)
            continue;
        if (!runs.empty())
            runs.back().text += "\r";
        const size_t before = runs.size();
        addTlfRuns(child, style, runs);
        // An empty paragraph still has its formatting, as classic text keeps it.
        if (runs.size() == before)
            runs.push_back(tlfRun(tlfStyleOf(child, style)));
    }
}

} // namespace

bool convertTlfText(const tinyxml2::XMLElement* element, fla::StaticText* text)
{
    const tinyxml2::XMLElement* markup = element->FirstChildElement("markup");
    const char* source = markup ? markup->GetText() : nullptr;
    if (!source)
        return false;
    tinyxml2::XMLDocument xml;
    if (xml.Parse(source) != tinyxml2::XML_SUCCESS || !xml.RootElement())
        return false;
    const tinyxml2::XMLElement* flow = xml.RootElement();
    if (std::strcmp(flow->Name(), "TextFlow") != 0)
        flow = flow->FirstChildElement("TextFlow");
    if (!flow || !parseElement(element, text))
        return false;

    // The box is in twips, as the file's other frames are.
    text->left = getDoubleAttribute(element, "left", 0.0) / 20.0;
    text->top = getDoubleAttribute(element, "top", 0.0) / 20.0;
    text->width = (getDoubleAttribute(element, "right", 0.0) - getDoubleAttribute(element, "left", 0.0)) / 20.0;
    text->height = (getDoubleAttribute(element, "bottom", 0.0) - getDoubleAttribute(element, "top", 0.0)) / 20.0;
    // Without a width it grows with its text.
    text->autoExpand = text->width <= 0.0;
    if (text->autoExpand)
        text->width = 0.0;
    // A named field is one a script reaches: dynamic text keeps it reachable.
    text->instanceName = getAttribute(element, "name");
    if (!text->instanceName.empty())
        text->textType = fla::StaticText::TextType::Dynamic;
    // What the object holding the flow (or the flow, or the element) says
    // of the frame.
    const tinyxml2::XMLElement* holder = xml.RootElement();
    const auto setting = [&](const char* name) -> const char* {
        const char* value = flow->Attribute(name);
        if (!value && holder != flow)
            value = holder->Attribute(name);
        return value ? value : element->Attribute(name);
    };
    // Text a reader may edit or select is a field to type in or select.
    const char* policy = setting("editPolicy");
    if (policy && std::strcmp(policy, "readWrite") == 0)
        text->textType = fla::StaticText::TextType::Input;
    else if (policy && std::strcmp(policy, "readSelect") == 0)
        text->textType = fla::StaticText::TextType::Dynamic;
    if (text->textType != fla::StaticText::TextType::Static)
    {
        const char* kind = setting("type");
        text->lineType = kind && std::strcmp(kind, "Point") == 0 ? "single line" : "multiline";
    }
    // The frame's padding insets the text. Its columns are not kept: the
    // text runs across the whole frame.
    if (!text->autoExpand)
    {
        const auto padding = [&](const char* name) {
            const char* value = setting(name);
            return value ? std::max(0.0, std::strtod(value, nullptr)) : 0.0;
        };
        const double left = padding("paddingLeft"), top = padding("paddingTop");
        text->left += left;
        text->top += top;
        text->width = std::max(1.0, text->width - left - padding("paddingRight"));
        text->height = std::max(1.0, text->height - top - padding("paddingBottom"));
    }

    addTlfParagraphs(flow, TlfStyle(), text->runs);
    if (text->runs.empty())
        text->runs.push_back(tlfRun(TlfStyle()));
    return true;
}

fla::Element* parseElementByType(const tinyxml2::XMLElement* element, fla::DOMElement* parent)
{
    const char* tagName = element->Name();

    if (std::strcmp(tagName, "DOMShape") == 0)
    {
        fla::Shape* shape = new fla::Shape(parent);
        if (!parseShape(element, shape))
        {
            delete shape;
            return nullptr;
        }
        return shape;
    }
    else if (std::strcmp(tagName, "DOMSymbolInstance") == 0)
    {
        fla::SymbolInstance* instance = new fla::SymbolInstance(parent);
        if (!parseSymbolInstance(element, instance))
        {
            delete instance;
            return nullptr;
        }
        fla::Document* document = parent->findAncestorOfType<fla::Document>();
        if (document)
        {
            document->symbolInstances.push_back(instance);
        }
        return instance;
    }
    else if (std::strcmp(tagName, "DOMGroup") == 0)
    {
        fla::Group* group = new fla::Group(parent);
        if (!parseGroup(element, group))
        {
            delete group;
            return nullptr;
        }
        return group;
    }
    else if (std::strcmp(tagName, "DOMStaticText") == 0 ||
        std::strcmp(tagName, "DOMDynamicText") == 0 ||
        std::strcmp(tagName, "DOMInputText") == 0)
    {
        fla::StaticText* staticText = new fla::StaticText(parent);
        if (!parseStaticText(element, staticText))
        {
            delete staticText;
            return nullptr;
        }
        if (std::strcmp(tagName, "DOMDynamicText") == 0)
            staticText->textType = fla::StaticText::TextType::Dynamic;
        else if (std::strcmp(tagName, "DOMInputText") == 0)
            staticText->textType = fla::StaticText::TextType::Input;
        return staticText;
    }
    else if (std::strcmp(tagName, "DOMBitmapInstance") == 0)
    {
        fla::BitmapInstance* bitmapInstance = new fla::BitmapInstance(parent);
        if (!parseBitmapInstance(element, bitmapInstance))
        {
            delete bitmapInstance;
            return nullptr;
        }
        return bitmapInstance;
    }
    else if (std::strcmp(tagName, "DOMRectangleObject") == 0)
    {
        fla::RectanglePrimitive* rectangle = new fla::RectanglePrimitive(parent);
        if (!parseRectanglePrimitive(element, rectangle))
        {
            delete rectangle;
            return nullptr;
        }
        return rectangle;
    }
    else if (std::strcmp(tagName, "DOMOvalObject") == 0)
    {
        fla::OvalPrimitive* oval = new fla::OvalPrimitive(parent);
        if (!parseOvalPrimitive(element, oval))
        {
            delete oval;
            return nullptr;
        }
        return oval;
    }

    // TLF text, an older kind of FLA text, opens as classic text.
    if (std::strcmp(tagName, "DOMTLFText") == 0)
    {
        fla::StaticText* text = new fla::StaticText(parent);
        if (convertTlfText(element, text))
            return text;
        delete text;
    }

    // A kind Phoenix does not model (a component, a video, TLF text it
    // could not read): kept as read, in its place, and written back.
    fla::RawElement* raw = new fla::RawElement(parent);
    raw->xml = elementText(element);
    describeVideo(*raw);
    return raw;
}

void describeVideo(fla::RawElement& raw)
{
    if (raw.xml.find("DOMVideoInstance") == std::string::npos)
        return;
    tinyxml2::XMLDocument xml;
    if (xml.Parse(raw.xml.c_str(), raw.xml.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement() ||
        std::strcmp(xml.RootElement()->Name(), "DOMVideoInstance") != 0)
        return;
    const tinyxml2::XMLElement* element = xml.RootElement();
    // Where it sits and how big its frame is, to draw it by.
    parseElement(element, &raw);
    raw.videoItem = getAttribute(element, "libraryItemName");
    const double right = getDoubleAttribute(element, "frameRight", 0.0) / 20.0;
    const double bottom = getDoubleAttribute(element, "frameBottom", 0.0) / 20.0;
    raw.localBounds = fla::Rect(fla::Point(0.0, 0.0), fla::Point(right, bottom));
    raw.bounds = raw.localBounds;
}

void collectVideoItems(fla::Document& document)
{
    document.videos.clear();
    for (const std::string& text : document.extraMedia)
    {
        if (text.find("DOMVideoItem") == std::string::npos)
            continue;
        tinyxml2::XMLDocument xml;
        if (xml.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS || !xml.RootElement() ||
            std::strcmp(xml.RootElement()->Name(), "DOMVideoItem") != 0)
            continue;
        const tinyxml2::XMLElement* item = xml.RootElement();
        fla::Document::VideoInfo video;
        video.name = getAttribute(item, "name");
        video.width = getIntAttribute(item, "width", 0);
        video.height = getIntAttribute(item, "height", 0);
        video.fps = getDoubleAttribute(item, "fps", 0.0);
        video.source = getAttribute(item, "sourceExternalFilepath");
        video.embedded = !getAttribute(item, "videoDataHRef").empty();
        document.videos.push_back(video);
    }
}

bool parseGroup(const tinyxml2::XMLElement* element, fla::Group* group)
{
    if (!parseElement(element, group))
    {
        return false;
    }
    keepAttributes(element, {"isSelected", "isFloating", "lockFlag"}, group);
    for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
    {
        const char* name = child->Name();
        if (std::strcmp(name, "members") != 0 && std::strcmp(name, "matrix") != 0 &&
            std::strcmp(name, "transformationPoint") != 0)
            group->extraChildren.push_back(elementText(child));
    }

    for (const tinyxml2::XMLElement* memberNode = element->FirstChildElement("members");
         memberNode != nullptr;
         memberNode = memberNode->NextSiblingElement("members"))
    {
        for (const tinyxml2::XMLElement* childElement = memberNode->FirstChildElement();
             childElement != nullptr;
             childElement = childElement->NextSiblingElement())
        {
            fla::Element* member = parseElementByType(childElement, group);
            if (member)
            {
                group->members.push_back(member);
            }
        }
    }

    group->bounds.reset();
    for (fla::Element* member : group->members)
    {
        group->bounds.expandToInclude(member->bounds);
    }
    group->localBounds = group->bounds;

    return true;
}

bool parseElements(const tinyxml2::XMLElement* element, fla::Frame* frame)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        fla::Element* el = parseElementByType(childElement, frame);
        if (el)
        {
            frame->elements.push_back(el);
        }
    }
    return true;
}

bool parseActionScript(const tinyxml2::XMLElement* element, fla::ActionScript* actionScript)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "script") == 0)
        {
            // Read the <script> element's own text. This used to read the
            // enclosing Actionscript element, which never carries the code.
            const char* text = childElement->GetText();
            if (text)
            {
                actionScript->code = text;
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled ActionScript element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    return true;
}

bool parseMorphCurves(const tinyxml2::XMLElement* element, fla::MorphCurves* curves)
{
    curves->controlPointA = PathParser::parseMorphPoint(getAttribute(element, "controlPointA"));
    curves->anchorPointA = PathParser::parseMorphPoint(getAttribute(element, "anchorPointA"));
    curves->controlPointB = PathParser::parseMorphPoint(getAttribute(element, "controlPointB"));
    curves->anchorPointB = PathParser::parseMorphPoint(getAttribute(element, "anchorPointB"));
    curves->isLine = getBoolAttribute(element, "isLine", false);
    keepAttributes(element, {"controlPointA", "anchorPointA", "controlPointB", "anchorPointB", "isLine"}, curves);

    return true;
}

bool parseMorphSegment(const tinyxml2::XMLElement* element, fla::MorphSegment* segment)
{
    segment->startPointA = PathParser::parseMorphPoint(getAttribute(element, "startPointA"));
    segment->startPointB = PathParser::parseMorphPoint(getAttribute(element, "startPointB"));
    segment->strokeIndex1 = getIntAttribute(element, "strokeIndex1", 0);
    segment->strokeIndex2 = getIntAttribute(element, "strokeIndex2", 0);
    segment->fillIndex1 = getIntAttribute(element, "fillIndex1", 0);
    segment->fillIndex2 = getIntAttribute(element, "fillIndex2", 0);
    keepAttributes(element, {"startPointA", "startPointB", "strokeIndex1", "strokeIndex2", "fillIndex1",
        "fillIndex2"}, segment);

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "MorphCurves") == 0)
        {
            fla::MorphCurves* curves = new fla::MorphCurves(segment);
            if (!parseMorphCurves(childElement, curves))
            {
                delete curves;
                return false;
            }
            segment->curves.push_back(curves);
        }
    }

    return true;
}

bool parseMorphShape(const tinyxml2::XMLElement* element, fla::MorphShape* morphShape)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "morphSegments") == 0)
        {
            for (const tinyxml2::XMLElement* segmentElement = childElement->FirstChildElement("MorphSegment");
                 segmentElement != nullptr;
                 segmentElement = segmentElement->NextSiblingElement("MorphSegment"))
            {
                fla::MorphSegment* segment = new fla::MorphSegment(morphShape);
                if (!parseMorphSegment(segmentElement, segment))
                {
                    delete segment;
                    return false;
                }
                morphShape->segments.push_back(segment);
            }
        }
        else if (std::strcmp(tagName, "morphHintsList") == 0)
        {
            // Shape hints, kept as read until Phoenix uses them.
            morphShape->hintsXml = elementText(childElement);
        }
        else
        {
            std::cerr << "!!!! Unhandled morph shape element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    return true;
}

std::string elementText(const tinyxml2::XMLElement* element);

bool parseFrame(const tinyxml2::XMLElement* element, fla::Frame* frame)
{
    keepAttributes(element, {"index", "duration", "keyMode", "name", "labelType", "tweenType",
        "motionTweenSnap", "motionTWeenSnap", "motionTweenSync", "acceleration", "hasCustomEase",
        "motionTweenRotate", "motionTweenRotateTimes"}, frame);

    frame->index = fla::clampFrameIndex(getIntAttribute(element, "index"));
    frame->duration = fla::clampFrameDuration(getIntAttribute(element, "duration", 1));
    frame->keyMode = getAttribute(element, "keyMode");
    frame->name = getAttribute(element, "name");
    frame->labelType = getAttribute(element, "labelType");
    // The FLA spells it motionTweenSnap; the other spelling was ours.
    frame->motionTWeenSnap = getBoolAttribute(element, "motionTweenSnap",
        getBoolAttribute(element, "motionTWeenSnap", false));
    frame->motionTweenSync = getBoolAttribute(element, "motionTweenSync", false);
    frame->acceleration = getIntAttribute(element, "acceleration", 0);
    frame->hasCustomEase = getBoolAttribute(element, "hasCustomEase", false);
    frame->motionTweenRotate = getAttribute(element, "motionTweenRotate");
    frame->motionTweenRotateTimes = getIntAttribute(element, "motionTweenRotateTimes", 0);

    std::string tweenType = getAttribute(element, "tweenType");
    if (tweenType == "motion")
    {
        frame->tweenType = fla::TweenType::Motion;
    }
    else if (tweenType == "shape")
    {
        frame->tweenType = fla::TweenType::Shape;
    }
    else
    {
        frame->tweenType = fla::TweenType::None;
        if (!tweenType.empty() && tweenType != "none")
            frame->otherTweenType = tweenType;
    }

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "Actionscript") == 0)
        {
            fla::ActionScript* actionScript = new fla::ActionScript(frame);
            if (!parseActionScript(childElement, actionScript))
            {
                delete actionScript;
                return false;
            }
            frame->actionScript = actionScript;
        }
        else if (std::strcmp(tagName, "MorphShape") == 0)
        {
            fla::MorphShape* morphShape = new fla::MorphShape(frame);
            if (!parseMorphShape(childElement, morphShape))
            {
                delete morphShape;
                return false;
            }
            frame->morphShape = morphShape;
        }
        else if (std::strcmp(tagName, "tweens") == 0)
        {
            // An Ease repeats the acceleration attribute, or names one of
            // the FLA's ease presets (method); a CustomEase is the curve
            // itself. Eases for one property (a target other than all) are
            // kept as read.
            for (const tinyxml2::XMLElement* tween = childElement->FirstChildElement();
                 tween != nullptr;
                 tween = tween->NextSiblingElement())
            {
                const std::string target = getAttribute(tween, "target", "all");
                const bool ease = std::strcmp(tween->Name(), "Ease") == 0;
                const bool custom = std::strcmp(tween->Name(), "CustomEase") == 0;
                if (target != "all" || (!ease && !custom))
                {
                    frame->otherEases.push_back(elementText(tween));
                    continue;
                }
                if (ease)
                {
                    frame->hasEase = true;
                    frame->easeMethod = getAttribute(tween, "method");
                    if (frame->acceleration == 0)
                        frame->acceleration = getIntAttribute(tween, "intensity", 0);
                }
                else
                {
                    frame->customEaseName = getAttribute(tween, "name");
                    frame->hasCustomEase = true;
                    frame->customEase.clear();
                    for (const tinyxml2::XMLElement* point = tween->FirstChildElement("Point");
                         point != nullptr;
                         point = point->NextSiblingElement("Point"))
                    {
                        fla::Point p;
                        p.x = getDoubleAttribute(point, "x", 0.0);
                        p.y = getDoubleAttribute(point, "y", 0.0);
                        frame->customEase.push_back(p);
                    }
                }
            }
        }
        else if (std::strcmp(tagName, "elements") == 0)
        {
            if (!parseElements(childElement, frame))
            {
                return false;
            }
        }
        else
        {
            // Kept as read (a sound's SoundEnvelope, say) and written back.
            frame->extraChildren.push_back(elementText(childElement));
        }
    }
    return true;
}

bool parseFrames(const tinyxml2::XMLElement* element, fla::Layer* layer)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMFrame") == 0)
        {
            fla::Frame* frame = new fla::Frame(layer);
            if (!parseFrame(childElement, frame))
            {
                return false;
            }
            layer->frames.push_back(frame);
        }
        else
        {
            std::cerr << "!!!! Unhandled frames element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    if (!layer->frames.empty())
    {
        layer->firstFrame = layer->frames[0]->index;
        layer->lastFrame = layer->frames[0]->index + layer->frames[0]->duration;
        for (const fla::Frame* frame : layer->frames)
        {
            if (frame->index < layer->firstFrame)
                layer->firstFrame = frame->index;
            int frameEnd = frame->index + frame->duration;
            if (frameEnd > layer->lastFrame)
                layer->lastFrame = frameEnd;
        }
    }

    return true;
}

bool parseLayer(const tinyxml2::XMLElement* element, fla::Layer* layer)
{
    layer->name = getAttribute(element, "name");

    std::string color = getAttribute(element, "color");
    parseHexColor(color, layer->color);

    layer->current = getAttribute(element, "current");

    std::string layerType = getAttribute(element, "layerType", "normal");
    if (layerType == "normal")
    {
        layer->layerType = fla::Layer::Type::Normal;
    }
    else if (layerType == "folder")
    {
        layer->layerType = fla::Layer::Type::Folder;
    }
    else if (layerType == "mask")
    {
        layer->layerType = fla::Layer::Type::Mask;
    }
    else if (layerType == "masked")
    {
        layer->layerType = fla::Layer::Type::Masked;
    }
    else if (layerType == "guide")
    {
        layer->layerType = fla::Layer::Type::Guide;
    }
    else
    {
        // Kept as spelled, and drawn as a normal layer.
        layer->layerType = fla::Layer::Type::Normal;
        layer->otherLayerType = layerType;
    }

    keepAttributes(element, {"name", "color", "current", "layerType", "parentLayerIndex", "isSelected",
        "autoNamed", "locked", "outline", "visible"}, layer);

    layer->parentLayerIndex = getIntAttribute(element, "parentLayerIndex", -1);
    layer->selected = getBoolAttribute(element, "isSelected", false);
    layer->autoNamed = getBoolAttribute(element, "autoNamed", true);
    layer->locked = getBoolAttribute(element, "locked", false);
    layer->outline = getBoolAttribute(element, "outline", false);
    layer->visible = getBoolAttribute(element, "visible", true);

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "frames") == 0)
        {
            if (!parseFrames(childElement, layer))
            {
                return false;
            }
        }
        else
        {
            layer->extraChildren.push_back(elementText(childElement));
        }
    }

    // The morphs the file stored are taken as they are, until an edit to
    // either keyframe's shape calls for a new one.
    for (fla::Frame* frame : layer->frames)
    {
        if (!frame || frame->tweenType != fla::TweenType::Shape)
            continue;
        for (const fla::Frame* end : layer->frames)
        {
            if (end && end->index == frame->index + frame->duration)
                fla::ensureMorph(*frame, *end);
        }
    }
    return true;
}

bool parseLayers(const tinyxml2::XMLElement* element, fla::Timeline* timeline)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMLayer") == 0)
        {
            fla::Layer* layer = new fla::Layer(timeline);
            if (!parseLayer(childElement, layer))
            {
                return false;
            }

            if (layer->parentLayerIndex >= 0 && layer->parentLayerIndex < timeline->layers.size())
            {
                layer->parentLayer = timeline->layers[layer->parentLayerIndex];

                // For whatever reason, FLA files don't set the layer type of masked layers to "masked", it just sets the parent layer to "mask".
                if (layer->parentLayer->layerType == fla::Layer::Type::Mask)
                {
                    layer->layerType = fla::Layer::Type::Masked;
                }
            }

            timeline->layers.push_back(layer);
        }
        else
        {
            std::cerr << "!!!! Unhandled layer element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

/// Reads the ruler guides from a DOMTimeline's guides attribute, which
/// holds XML of its own: <guidelines><guideline direction="v">x</guideline>
/// ... </guidelines>, a vertical guide by its x and a horizontal one ("h")
/// by its y. False when the text is not that.
bool parseGuides(const std::string& text, fla::Timeline* timeline)
{
    tinyxml2::XMLDocument guides;
    if (guides.Parse(text.c_str()) != tinyxml2::XML_SUCCESS)
        return false;
    const tinyxml2::XMLElement* root = guides.FirstChildElement("guidelines");
    if (!root)
        return false;
    std::vector<double> xs;
    std::vector<double> ys;
    for (const tinyxml2::XMLElement* guide = root->FirstChildElement("guideline"); guide;
         guide = guide->NextSiblingElement("guideline"))
    {
        const char* direction = guide->Attribute("direction");
        const char* value = guide->GetText();
        if (!direction || !value)
            return false;
        char* end = nullptr;
        const double position = std::strtod(value, &end);
        if (end == value)
            return false;
        if (std::strcmp(direction, "v") == 0)
            xs.push_back(position);
        else if (std::strcmp(direction, "h") == 0)
            ys.push_back(position);
        else
            return false;
    }
    timeline->guidesX = std::move(xs);
    timeline->guidesY = std::move(ys);
    return true;
}

bool parseTimeline(const tinyxml2::XMLElement* element, fla::Timeline* timeline)
{
    timeline->name = getAttribute(element, "name");
    timeline->layerDepthEnabled = getBoolAttribute(element, "layerDepthEnabled");
    // The rest are kept as they came; guides are read when they can be, and
    // kept as text when they cannot.
    for (const tinyxml2::XMLAttribute* attribute = element->FirstAttribute(); attribute;
         attribute = attribute->Next())
    {
        const std::string name = attribute->Name();
        if (name == "name" || name == "layerDepthEnabled")
            continue;
        if (name == "guides" && parseGuides(attribute->Value(), timeline))
            continue;
        timeline->extraAttributes.emplace_back(name, attribute->Value());
    }

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "layers") == 0)
        {
            if (!parseLayers(childElement, timeline))
            {
                return false;
            }
        }
        else
        {
            timeline->extraChildren.push_back(elementText(childElement));
        }
    }
    return true;
}

bool parseTimelines(const tinyxml2::XMLElement* element, std::vector<fla::Timeline*>& timelines, fla::DOMElement* parent)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMTimeline") == 0)
        {
            fla::Timeline* timeline = new fla::Timeline(parent);
            if (!parseTimeline(childElement, timeline))
            {
                return false;
            }
            timelines.push_back(timeline);
        }
        else
        {
            std::cerr << "!!!! Unhandled timeline element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

bool parseSymbolInclude(const tinyxml2::XMLElement* element, fla::SymbolList* symbolList, ZipReader* zipReader)
{
    std::string href = getAttribute(element, "href");
    if (href.empty())
    {
        std::cerr << "Symbol include missing href attribute" << std::endl;
        return false;
    }

    std::string libraryPath = "LIBRARY/" + href;

    if (!zipReader->containsFile(libraryPath))
    {
        std::cerr << "Symbol include not found: " << href << std::endl;
        return false;
    }

    std::string content = keepBlankCharacters(zipReader->readTextFile(libraryPath));

    _currentFile = href;

    // Parse the included symbol XML content
    tinyxml2::XMLDocument symbolDoc;
    if (symbolDoc.Parse(content.c_str()) != tinyxml2::XML_SUCCESS)
    {
        std::cerr << "Failed to parse symbol XML content from: " << href << std::endl;
        _currentFile.clear();
        return false;
    }

    const tinyxml2::XMLElement* root = symbolDoc.RootElement();
    // A component (from the Components panel) is a symbol too, with its
    // parameters and class definitions besides.
    const bool component = root && std::strcmp(root->Name(), "DOMComponentItem") == 0;
    if (!root || (!component && std::strcmp(root->Name(), "DOMSymbolItem") != 0))
    {
        std::cerr << "Root element of symbol file is not DOMSymbolItem: " << href << std::endl;
        _currentFile.clear();
        return false;
    }

    fla::Symbol* symbol = new fla::Symbol(symbolList);
    if (component)
        symbol->itemElement = root->Name();
    symbol->name = getAttribute(root, "name");
    symbol->itemId = getAttribute(root, "itemID");
    symbol->lastModified = getAttribute(root, "lastModified");
    symbol->href = href;
    {
        // As for instances, no type is a movie clip.
        const std::string type = getAttribute(root, "symbolType", "movie clip");
        symbol->symbolType = type == "movie clip" ? fla::SymbolType::MovieClip
            : type == "button" ? fla::SymbolType::Button : fla::SymbolType::Graphic;
        if (type != "movie clip" && type != "button" && type != "graphic")
            symbol->otherSymbolType = type;
    }
    keepAttributes(root, {"xmlns", "xmlns:xsi", "name", "itemID", "lastModified", "symbolType", "scaleGridLeft",
        "scaleGridTop", "scaleGridRight", "scaleGridBottom"}, symbol);
    for (const tinyxml2::XMLAttribute* attribute = element->FirstAttribute(); attribute; attribute = attribute->Next())
    {
        if (std::strcmp(attribute->Name(), "href") != 0)
            symbol->includeAttributes.emplace_back(attribute->Name(), attribute->Value());
    }
    if (hasAttribute(root, "scaleGridLeft"))
    {
        symbol->scale9 = true;
        symbol->scaleGridLeft = getDoubleAttribute(root, "scaleGridLeft", 0.0);
        symbol->scaleGridTop = getDoubleAttribute(root, "scaleGridTop", 0.0);
        symbol->scaleGridRight = getDoubleAttribute(root, "scaleGridRight", 0.0);
        symbol->scaleGridBottom = getDoubleAttribute(root, "scaleGridBottom", 0.0);
    }

    // Parse timelines within the symbol
    for (const tinyxml2::XMLElement* childElement = root->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "timeline") == 0)
        {
            if (!parseTimelines(childElement, symbol->timelines, symbol))
            {
                _currentFile.clear();
                return false;
            }
        }
        else
        {
            // Kept as read (the persistentData scripts keep on an item) and
            // written back.
            symbol->extraChildren.push_back(elementText(childElement));
        }
    }

    symbolList->symbols.push_back(symbol);
    symbolList->symbolMap[symbol->name] = symbol;
    _currentFile.clear();

    return true;
}

bool parseSymbols(const tinyxml2::XMLElement* element, fla::Document* document, ZipReader* zipReader)
{
    fla::SymbolList* symbolList = new fla::SymbolList(document);
    document->symbolList = symbolList;

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "Include") == 0)
        {
            if (!parseSymbolInclude(childElement, symbolList, zipReader))
            {
                return false;
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled symbols element: " << childElement->Name() << std::endl;
        }
    }

    return true;
}

bool parseMedia(const tinyxml2::XMLElement* element, fla::Document* document, ZipReader* zipReader)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMBitmapItem") == 0)
        {
            // A texture tile Phoenix baked for other apps, made again on save.
            if (getAttribute(childElement, "phoenixTextureBake") == "true")
                continue;
            fla::Bitmap* bitmap = new fla::Bitmap(document);

            bitmap->name = getAttribute(childElement, "name");
            bitmap->itemId = getAttribute(childElement, "itemID");
            bitmap->href = getAttribute(childElement, "href");
            bitmap->bitmapDataHRef = getAttribute(childElement, "bitmapDataHRef");
            bitmap->sourceExternalFilepath = getAttribute(childElement, "sourceExternalFilepath");
            bitmap->sourceLastImported = static_cast<uint32_t>(
                getDoubleAttribute(childElement, "sourceLastImported", 0.0));
            bitmap->allowSmoothing = getBoolAttribute(childElement, "allowSmoothing", false);
            bitmap->quality = getIntAttribute(childElement, "quality", 0);
            bitmap->frameRight = getIntAttribute(childElement, "frameRight", 0);
            bitmap->frameBottom = getIntAttribute(childElement, "frameBottom", 0);
            keepAttributes(childElement, {"name", "itemID", "href", "bitmapDataHRef", "sourceExternalFilepath",
                "sourceLastImported", "allowSmoothing", "quality", "frameRight", "frameBottom"}, bitmap);
            for (const tinyxml2::XMLElement* child = childElement->FirstChildElement(); child; child = child->NextSiblingElement())
                bitmap->extraChildren.push_back(elementText(child));

            // The image as imported sits under LIBRARY when the FLA kept a
            // copy; the pixels in its own encoding sit under bin. Either is
            // enough to draw from.
            const std::string mediaPath = "LIBRARY/" + bitmap->href;
            if (!bitmap->href.empty() && zipReader->containsFile(mediaPath))
                bitmap->imageData = zipReader->lazyFile(mediaPath);
            const std::string dataPath = "bin/" + bitmap->bitmapDataHRef;
            if (!bitmap->bitmapDataHRef.empty() && zipReader->containsFile(dataPath))
                bitmap->datData = zipReader->lazyFile(dataPath);

            if (!bitmap->imageData.empty() || !bitmap->datData.empty())
            {
                document->resources.push_back(bitmap);
            }
            else
            {
                // Nothing to draw from, but the item is kept as read.
                std::cerr << "Bitmap media not found: " << bitmap->href << std::endl;
                document->extraMedia.push_back(elementText(childElement));
                delete bitmap;
            }
        }
        else
        {
            // A sound or video item: kept as read, with the data files it
            // names, so a save writes them back.
            document->extraMedia.push_back(elementText(childElement));
            if (std::strcmp(childElement->Name(), "DOMSoundItem") == 0)
            {
                fla::Document::SoundInfo sound;
                sound.name = getAttribute(childElement, "name");
                sound.format = getAttribute(childElement, "format");
                const std::string data = getAttribute(childElement, "soundDataHRef");
                if (!data.empty())
                    sound.dataPath = "bin/" + data;
                document->sounds.push_back(sound);
            }
            for (const tinyxml2::XMLAttribute* attribute = childElement->FirstAttribute(); attribute;
                 attribute = attribute->Next())
            {
                const std::string name = attribute->Name();
                std::string path;
                if (name.size() > 8 && name.compare(name.size() - 8, 8, "DataHRef") == 0)
                    path = std::string("bin/") + attribute->Value();
                else if (name == "href")
                    path = std::string("LIBRARY/") + attribute->Value();
                if (!path.empty() && zipReader && zipReader->containsFile(path))
                    document->extraFiles.emplace_back(path, zipReader->lazyFile(path));
            }
        }
    }
    return true;
}

bool parseFolder(const tinyxml2::XMLElement* element, fla::Folder* folder)
{
    folder->name = getAttribute(element, "name");
    folder->itemId = getAttribute(element, "itemID");
    folder->isExpanded = getBoolAttribute(element, "isExpanded", false);
    keepAttributes(element, {"name", "itemID", "isExpanded"}, folder);
    return true;
}

bool parseFolders(const tinyxml2::XMLElement* element, fla::Document* document)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMFolderItem") == 0)
        {
            fla::Folder* folder = new fla::Folder(document);
            if (!parseFolder(childElement, folder))
            {
                return false;
            }
            document->folders.push_back(folder);
        }
        else
        {
            std::cerr << "!!!! Unhandled folder element: " << childElement->Name() << std::endl;
        }
    }
    return true;
}

bool parseSwatchList(const tinyxml2::XMLElement* element, fla::SwatchList* swatchList)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "swatchListInfo") == 0)
        {
            swatchList->name = getAttribute(childElement, "name");
        }
        else if (std::strcmp(childElement->Name(), "swatches") == 0)
        {
            for (const tinyxml2::XMLElement* swatchElement = childElement->FirstChildElement();
                 swatchElement != nullptr;
                 swatchElement = swatchElement->NextSiblingElement())
            {
                if (std::strcmp(swatchElement->Name(), "swatchListInfo") == 0)
                {
                    // Don't know why this is repeated here
                }
                else if (std::strcmp(swatchElement->Name(), "SolidSwatchItem") == 0)
                {
                    fla::SolidSwatch* solidSwatch = new fla::SolidSwatch(swatchList);
                    if (hasAttribute(swatchElement, "color"))
                    {
                        std::string color = getAttribute(swatchElement, "color");
                        parseHexColor(color, solidSwatch->color);
                    }
                    swatchList->swatches.push_back(solidSwatch);
                }
                else
                {
                    // Other swatch kinds are not modeled; the whole list is kept
                    // as read and written back, so nothing is lost.
                }
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled swatch list element: " << childElement->Name() << std::endl;
        }
    }

    return true;
}

/// The element and everything under it, as text, for parts of the file
/// that are read but not modeled: they are written back as they were.
std::string elementText(const tinyxml2::XMLElement* element)
{
    tinyxml2::XMLPrinter printer(nullptr, true);
    element->Accept(&printer);
    return printer.CStr();
}

bool parseSwatchLists(const tinyxml2::XMLElement* element, fla::Document* document)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "swatchList") == 0)
        {
            fla::SwatchList* swatchList = new fla::SwatchList(document);
            if (!parseSwatchList(childElement, swatchList))
            {
                delete swatchList;
                return false;
            }
            swatchList->xml = elementText(childElement);
            document->swatchLists.push_back(swatchList);
        }
        else
        {
            std::cout << "!!!! Unhandled swatch lists element: " << childElement->Name() << std::endl;
        }
    }
    return true;
}

bool parseDocument(fla::Document* document, const tinyxml2::XMLElement* element, ZipReader* zipReader)
{
    // Parse attributes
    document->filetypeGUID = getAttribute(element, "filetypeGUID");
    document->fileGUID = getAttribute(element, "fileGUID");
    document->width = getIntAttribute(element, "width", 550);
    document->height = getIntAttribute(element, "height", 400);
    document->frameRate = getDoubleAttribute(element, "frameRate", 24.0);
    document->currentTimeline = getAttribute(element, "currentTimeline");
    document->xflVersion = getAttribute(element, "xflVersion");
    document->creatorInfo = getAttribute(element, "creatorInfo");
    document->platform = getAttribute(element, "platform");
    document->versionInfo = getAttribute(element, "versionInfo");
    document->majorVersion = getIntAttribute(element, "majorVersion", 0);
    document->buildNumber = getIntAttribute(element, "buildNumber", 0);
    document->viewAngle3D = getDoubleAttribute(element, "viewAngle3D", 0.0);
    getColorAttribute(element, "backgroundColor", document->backgroundColor);
    // A see-through stage is Phoenix's: the FLA keeps only the color.
    if (const char* alpha = element->Attribute("phoenixStageAlpha"))
        document->backgroundColor[3] = static_cast<uint8_t>(std::clamp(std::lround(std::atof(alpha) * 255.0), 0L, 255L));
    getColorAttribute(element, "gridColor", document->gridColor);
    document->gridSpacingX = getIntAttribute(element, "gridSpacingX", 18);
    document->gridSpacingY = getIntAttribute(element, "gridSpacingY", 18);
    document->objectsSnapTo = getBoolAttribute(element, "objectsSnapTo", true);
    document->guidesSnapTo = getBoolAttribute(element, "guidesSnapTo", true);
    document->guidesLocked = getBoolAttribute(element, "guidesLocked", false);
    // Phoenix's drawing guides and symmetry axis.
    if (const char* guides = element->Attribute("phoenixDrawingGuides"))
        document->drawingGuides = guides;
    if (const char* symmetry = element->Attribute("phoenixSymmetry"))
        document->symmetry = symmetry;
    document->snapAlignBorderSpacing = getIntAttribute(element, "snapAlignBorderSpacing", 18);
    getColorAttribute(element, "guidesColor", document->guidesColor);
    document->vanishingPoint3DX = getDoubleAttribute(element, "vanishingPoint3DX", 0.0);
    document->vanishingPoint3DY = getDoubleAttribute(element, "vanishingPoint3DY", 0.0);
    document->nextSceneIdentifier = getIntAttribute(element, "nextSceneIdentifier", 0);
    // FLA files carry these only when they are off.
    document->playOptionsPlayLoop = getBoolAttribute(element, "playOptionsPlayLoop", true);
    document->playOptionsPlayPages = getBoolAttribute(element, "playOptionsPlayPages", true);
    document->playOptionsPlayFrameActions = getBoolAttribute(element, "playOptionsPlayFrameActions", true);
    keepAttributes(element, {"xmlns", "xmlns:xsi", "filetypeGUID", "fileGUID", "width", "height", "frameRate",
        "currentTimeline", "xflVersion", "creatorInfo", "platform", "versionInfo", "majorVersion", "buildNumber",
        "viewAngle3D", "backgroundColor", "phoenixStageAlpha", "gridColor", "gridSpacingX", "gridSpacingY", "objectsSnapTo", "guidesSnapTo",
        "guidesLocked", "phoenixDrawingGuides", "phoenixSymmetry", "snapAlignBorderSpacing", "guidesColor", "vanishingPoint3DX", "vanishingPoint3DY", "nextSceneIdentifier",
        "playOptionsPlayLoop", "playOptionsPlayPages", "playOptionsPlayFrameActions"}, document);

    // Parse child elements
    // Whether the timelines have been read yet, so an unmodeled section
    // goes back in the same place.
    bool sawTimelines = false;
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "timelines") == 0)
            sawTimelines = true;
        if (std::strcmp(tagName, "timelines") == 0)
        {
            if (!parseTimelines(childElement, document->timelines, document))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "symbols") == 0)
        {
            if (!parseSymbols(childElement, document, zipReader))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "media") == 0)
        {
            if (!parseMedia(childElement, document, zipReader))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "publishHistory") == 0)
        {
            fla::PublishHistory* publishHistory = new fla::PublishHistory(document);
            publishHistory->xml = elementText(childElement);
            document->publishHistory = publishHistory;
        }
        else if (std::strcmp(tagName, "PrinterSettings") == 0)
        {
            fla::PrinterSettings* printerSettings = new fla::PrinterSettings(document);
            printerSettings->xml = elementText(childElement);
            document->printerSettings = printerSettings;
        }
        else if (std::strcmp(tagName, "scripts") == 0)
        {
            fla::ScriptList* scripts = new fla::ScriptList(document);
            scripts->xml = elementText(childElement);
            document->scriptList = scripts;
        }
        else if (std::strcmp(tagName, "folders") == 0)
        {
            if (!parseFolders(childElement, document))
            {
                return false;
            }
        }
        else if (std::strcmp(tagName, "swatchLists") == 0)
        {
            if (!parseSwatchLists(childElement, document))
            {
                return false;
            }
        }
        else
        {
            // Kept as read (extended swatch lists, brush definitions, fonts)
            // and written back, with any package file an Include in it names.
            document->extraSections.push_back({elementText(childElement), !sawTimelines});
            std::function<void(const tinyxml2::XMLElement*)> includes = [&](const tinyxml2::XMLElement* node) {
                for (const tinyxml2::XMLElement* child = node->FirstChildElement(); child; child = child->NextSiblingElement())
                {
                    if (std::strcmp(child->Name(), "Include") == 0)
                    {
                        const std::string href = getAttribute(child, "href");
                        if (!href.empty() && zipReader && zipReader->containsFile(href))
                            document->extraFiles.emplace_back(href, zipReader->lazyFile(href));
                    }
                    includes(child);
                }
            };
            includes(childElement);
        }
    }

    return true;
}

} // namespace

void describeVideoInstance(fla::RawElement& raw)
{
    describeVideo(raw);
}

void collectVideos(fla::Document& document)
{
    collectVideoItems(document);
}

DocumentParser::DocumentParser(ZipReader* zipReader)
    : _zipReader(zipReader)
{}

fla::Document* DocumentParser::parse(const std::string& xmlContent, fla::DOMElement* parent)
{
    _currentFile.clear();

    tinyxml2::XMLDocument doc;
    if (doc.Parse(keepBlankCharacters(xmlContent).c_str()) != tinyxml2::XML_SUCCESS)
    {
        _errorString = "Failed to parse XML content";
        return nullptr;
    }

    const tinyxml2::XMLElement* root = doc.RootElement();
    if (!root || std::strcmp(root->Name(), "DOMDocument") != 0)
    {
        _errorString = "Root element is not Document";
        return nullptr;
    }

    fla::Document* document = new fla::Document(parent);
    if (!parseDocument(document, root, _zipReader))
    {
        delete document;
        _errorString = "Failed to parse document content";
        return nullptr;
    }

    if (document->symbolList)
    {
        // Resolve symbol references in symbol instances
        for (fla::SymbolInstance* instance : document->symbolInstances)
        {
            auto it = document->symbolList->symbolMap.find(instance->libraryItemName);
            if (it != document->symbolList->symbolMap.end())
            {
                instance->symbol = it->second;
                document->symbolInstancesMap[it->second].push_back(instance);
            }
            else
            {
                std::cerr << "Symbol instance references unknown symbol: " << instance->libraryItemName << std::endl;
            }
        }
    }

    // A symbol inside itself cannot be drawn: the loop is cut, and the
    // instance closing it shows as a missing symbol.
    fla::noteCenterPoints(*document);
    if (int cut = fla::breakSymbolCycles(*document))
        std::cerr << cut << " symbol instance(s) placed a symbol inside itself and were cut" << std::endl;

    collectVideoItems(*document);
    return document;
}
