#include "document_parser.h"
#include "path_parser.h"
#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/oval_primitive.h"
#include "../data/radial_gradient.h"
#include "../data/rectangle_primitive.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/symbol_instance.h"
#include "../third_party/tinyxml2/tinyxml2.h"
#include <algorithm>
#include <iostream>
#include <cstring>

namespace {

std::string _currentFile;

bool parseGroup(const tinyxml2::XMLElement* element, fla::Group* group);

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

void parseHexColor(const std::string& hexColor, uint8_t rgba[4])
{
    // Remove '#' if present
    if (hexColor.size() == 0)
    {
        rgba[0] = 0;
        rgba[1] = 0;
        rgba[2] = 0;
        rgba[3] = 255; // Default to opaque black
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
        rgba[0] = std::stoi(color.substr(0, 2), nullptr, 16);
        rgba[1] = std::stoi(color.substr(2, 2), nullptr, 16);
        rgba[2] = std::stoi(color.substr(4, 2), nullptr, 16);
        rgba[3] = 255; // Alpha is fully opaque
    }
    else
    {
        // Default to black if invalid format
        rgba[0] = 0;
        rgba[1] = 0;
        rgba[2] = 0;
        rgba[3] = 255;
    }
}

bool parseSolidColor(const tinyxml2::XMLElement* element, fla::SolidColor* solidColor)
{
    std::string color = getAttribute(element, "color");

    // Parse CSS hex color if it starts with '#'
    parseHexColor(color, solidColor->color);

    return true;
}

bool parseLinearGradient(const tinyxml2::XMLElement* element, fla::LinearGradient* linearGradient)
{
    for (const tinyxml2::XMLElement* entryElement = element->FirstChildElement("GradientEntry");
         entryElement != nullptr;
         entryElement = entryElement->NextSiblingElement("GradientEntry"))
    {
        fla::GradientEntry entry;
        entry.ratio = getDoubleAttribute(entryElement, "ratio");

        std::string color = getAttribute(entryElement, "color");
        parseHexColor(color, entry.color);

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
    for (const tinyxml2::XMLElement* entryElement = element->FirstChildElement("GradientEntry");
         entryElement != nullptr;
         entryElement = entryElement->NextSiblingElement("GradientEntry"))
    {
        fla::RadialEntry entry;
        entry.ratio = getDoubleAttribute(entryElement, "ratio");

        std::string color = getAttribute(entryElement, "color");
        parseHexColor(color, entry.color);

        radialGradient->entries.push_back(entry);
    }

    std::sort(radialGradient->entries.begin(), radialGradient->entries.end(),
        [](const fla::RadialEntry& a, const fla::RadialEntry& b) {
            return a.ratio < b.ratio;
        });

    return true;
}

fla::Edge* parsePath(const tinyxml2::XMLElement* element)
{
    // Try to get edge data from either "edges" or "cubics" attribute
    std::string edgeData = getAttribute(element, "edges");
    if (edgeData.empty())
    {
        edgeData = getAttribute(element, "cubics");
    }

    if (edgeData.empty())
    {
        std::cerr << "Edge element missing edges/cubics attribute" << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        return nullptr;
    }

    PathParser pathParser;
    fla::Edge* edge = pathParser.parse(edgeData);

    if (!edge)
    {
        std::cerr << "Failed to parse path edges: " << pathParser.errorString() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        return nullptr;
    }

    edge->fillStyle0 = hasAttribute(element, "fillStyle0") ? getIntAttribute(element, "fillStyle0") : -1;
    edge->fillStyle1 = hasAttribute(element, "fillStyle1") ? getIntAttribute(element, "fillStyle1") : -1;

    edge->strokeStyle = hasAttribute(element, "strokeStyle") ? getIntAttribute(element, "strokeStyle") : -1;

    return edge;
}

bool parseEdges(const tinyxml2::XMLElement* element, fla::Shape* shape)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "Edge") == 0)
        {
            fla::Edge* edge = parsePath(childElement);
            if (!edge)
            {
                return false;
            }

            shape->edges.push_back(edge);
        }
        else
        {
            std::cerr << "!!!! Unhandled edge element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

fla::FillStyle* parseFillStyle(const tinyxml2::XMLElement* element)
{
    const char* tagName = element->Name();

    if (std::strcmp(tagName, "SolidColor") == 0)
    {
        fla::SolidColor* solidFill = new fla::SolidColor();
        if (!parseSolidColor(element, solidFill))
        {
            delete solidFill;
            return nullptr;
        }

        return solidFill;
    }
    else if (std::strcmp(tagName, "LinearGradient") == 0)
    {
        fla::LinearGradient* linearFill = new fla::LinearGradient();
        if (!parseLinearGradient(element, linearFill))
        {
            delete linearFill;
            return nullptr;
        }
        return linearFill;
    }
    else if (std::strcmp(tagName, "RadialGradient") == 0)
    {
        fla::RadialGradient* radialFill = new fla::RadialGradient();
        if (!parseRadialGradient(element, radialFill))
        {
            delete radialFill;
            return nullptr;
        }
        return radialFill;
    }
    std::cerr << "!!!! Unhandled fill element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
    return nullptr;
}

bool parseStroke(const tinyxml2::XMLElement* element, fla::StrokeStyle* stroke)
{
    stroke->scaleMode = getAttribute(element, "scaleMode");
    stroke->weight = getDoubleAttribute(element, "weight", 1.0);

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
                stroke->fill = parseFillStyle(fillElement);
                if (!stroke->fill)
                {
                    return false;
                }
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled stroke element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

fla::StrokeStyle* parseStrokeStyle(const tinyxml2::XMLElement* element)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();
        fla::StrokeStyle* stroke = nullptr;

        if (std::strcmp(tagName, "SolidStroke") == 0)
        {
            stroke = new fla::SolidStroke();
        }
        else if (std::strcmp(tagName, "DashedStroke") == 0)
        {
            stroke = new fla::DashedStroke();
        }
        else if (std::strcmp(tagName, "RaggedStroke") == 0)
        {
            stroke = new fla::RaggedStroke();
        }
        else if (std::strcmp(tagName, "StippleStroke") == 0)
        {
            stroke = new fla::StippleStroke();
        }
        else if (std::strcmp(tagName, "DottedStroke") == 0)
        {
            stroke = new fla::DottedStroke();
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
            int index = hasAttribute(childElement, "index") ? getIntAttribute(childElement, "index") : -1;
            fla::StrokeStyle* strokeStyle = parseStrokeStyle(childElement);
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

            fla::FillStyle *fillStyle = parseFillStyle(fillElement);
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
    if (hasAttribute(element, "isSelected"))
        el->isSelected = (getAttribute(element, "isSelected") == "true");

    if (hasAttribute(element, "isFloating"))
        el->isFloating = (getAttribute(element, "isFloating") == "true");

    if (hasAttribute(element, "lockFlag"))
        el->isLocked = (getAttribute(element, "lockFlag") == "true");

    // Parse transformation matrix
    const tinyxml2::XMLElement* matrixNode = element->FirstChildElement("matrix");
    if (matrixNode)
    {
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
            std::cerr << "!!!! Unhandled shape element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }

    shape->bounds.reset();

    for (fla::Edge* edge : shape->edges)
    {
        for (const fla::Path& path : edge->paths)
        {
            for (const fla::PathSegment& segment : path.segments)
            {
                for (const fla::Point& point : segment.points)
                {
                    shape->bounds.topLeft.x = std::min(shape->bounds.topLeft.x, point.x);
                    shape->bounds.topLeft.y = std::min(shape->bounds.topLeft.y, point.y);
                    shape->bounds.bottomRight.x = std::max(shape->bounds.bottomRight.x, point.x);
                    shape->bounds.bottomRight.y = std::max(shape->bounds.bottomRight.y, point.y);
                }
            }
        }
    }

    shape->bounds.translate(shape->transformationPoint.x, shape->transformationPoint.y);
    shape->bounds.transform(shape->transform);
    shape->bounds.translate(-shape->transformationPoint.x, -shape->transformationPoint.y);

    return true;
}

bool parseSymbolInstance(const tinyxml2::XMLElement* element, fla::SymbolInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    instance->libraryItemName = getAttribute(element, "libraryItemName");

    return true;
}

bool parseBitmapInstance(const tinyxml2::XMLElement* element, fla::BitmapInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    instance->libraryItemName = getAttribute(element, "libraryItemName");

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
                rectangle->fillStyle = parseFillStyle(fillElement);
            }
        }
        else if (std::strcmp(tagName, "stroke") == 0)
        {
            rectangle->strokeStyle = parseStrokeStyle(childElement);
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
            std::cerr << "!!!! Unhandled rectangle primitive child element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
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

    oval->bounds = oval->rect;

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
                oval->fillStyle = parseFillStyle(fillElement);
            }
        }
        else if (std::strcmp(tagName, "stroke") == 0)
        {
            oval->strokeStyle = parseStrokeStyle(childElement);
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
            std::cerr << "!!!! Unhandled oval primitive child element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
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
                                if (hasAttribute(attrElement, "aliasText"))
                                {
                                    textRun.aliasText = (getAttribute(attrElement, "aliasText") == "true");
                                }
                                if (hasAttribute(attrElement, "lineHeight"))
                                {
                                    textRun.lineHeight = getDoubleAttribute(attrElement, "lineHeight");
                                }
                                if (hasAttribute(attrElement, "size"))
                                {
                                    textRun.size = getDoubleAttribute(attrElement, "size");
                                }
                                if (hasAttribute(attrElement, "bitmapSize"))
                                {
                                    textRun.bitmapSize = getDoubleAttribute(attrElement, "bitmapSize");
                                }
                                if (hasAttribute(attrElement, "face"))
                                {
                                    textRun.face = getAttribute(attrElement, "face");
                                }
                                if (hasAttribute(attrElement, "fillColor"))
                                {
                                    std::string color = getAttribute(attrElement, "fillColor");
                                    parseHexColor(color, textRun.fillColor);
                                }
                            }
                        }
                    }
                    else
                    {
                        std::cerr << "!!!! Unhandled text run child element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
                    }
                }

                if (!textRun.text.empty())
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
    staticText->bounds.translate(staticText->transformationPoint.x, staticText->transformationPoint.y);
    staticText->bounds.transform(staticText->transform);
    staticText->bounds.translate(-staticText->transformationPoint.x, -staticText->transformationPoint.y);

    return true;
}

fla::Element* parseElementByType(const tinyxml2::XMLElement* element)
{
    const char* tagName = element->Name();

    if (std::strcmp(tagName, "DOMShape") == 0)
    {
        fla::Shape* shape = new fla::Shape();
        if (!parseShape(element, shape))
        {
            delete shape;
            return nullptr;
        }
        return shape;
    }
    else if (std::strcmp(tagName, "DOMSymbolInstance") == 0)
    {
        fla::SymbolInstance* instance = new fla::SymbolInstance();
        if (!parseSymbolInstance(element, instance))
        {
            delete instance;
            return nullptr;
        }
        return instance;
    }
    else if (std::strcmp(tagName, "DOMGroup") == 0)
    {
        fla::Group* group = new fla::Group();
        if (!parseGroup(element, group))
        {
            delete group;
            return nullptr;
        }
        return group;
    }
    else if (std::strcmp(tagName, "DOMStaticText") == 0)
    {
        fla::StaticText* staticText = new fla::StaticText();
        if (!parseStaticText(element, staticText))
        {
            delete staticText;
            return nullptr;
        }
        return staticText;
    }
    else if (std::strcmp(tagName, "DOMBitmapInstance") == 0)
    {
        fla::BitmapInstance* bitmapInstance = new fla::BitmapInstance();
        if (!parseBitmapInstance(element, bitmapInstance))
        {
            delete bitmapInstance;
            return nullptr;
        }
        return bitmapInstance;
    }
    else if (std::strcmp(tagName, "DOMRectangleObject") == 0)
    {
        fla::RectanglePrimitive* rectangle = new fla::RectanglePrimitive();
        if (!parseRectanglePrimitive(element, rectangle))
        {
            delete rectangle;
            return nullptr;
        }
        return rectangle;
    }
    else if (std::strcmp(tagName, "DOMOvalObject") == 0)
    {
        fla::OvalPrimitive* oval = new fla::OvalPrimitive();
        if (!parseOvalPrimitive(element, oval))
        {
            delete oval;
            return nullptr;
        }
        return oval;
    }

    std::cerr << "!!!! Unhandled element type: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
    return nullptr;
}

bool parseGroup(const tinyxml2::XMLElement* element, fla::Group* group)
{
    if (!parseElement(element, group))
    {
        return false;
    }

    for (const tinyxml2::XMLElement* memberNode = element->FirstChildElement("members");
         memberNode != nullptr;
         memberNode = memberNode->NextSiblingElement("members"))
    {
        for (const tinyxml2::XMLElement* childElement = memberNode->FirstChildElement();
             childElement != nullptr;
             childElement = childElement->NextSiblingElement())
        {
            fla::Element* member = parseElementByType(childElement);
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
    group->bounds.translate(group->transformationPoint.x, group->transformationPoint.y);
    group->bounds.transform(group->transform);
    group->bounds.translate(-group->transformationPoint.x, -group->transformationPoint.y);

    return true;
}

bool parseElements(const tinyxml2::XMLElement* element, fla::Frame* frame)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        fla::Element* el = parseElementByType(childElement);
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
            const char* text = element->GetText();
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

bool parseFrame(const tinyxml2::XMLElement* element, fla::Frame* frame)
{
    frame->index = getIntAttribute(element, "index");
    frame->keyMode = getAttribute(element, "keyMode");

    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "Actionscript") == 0)
        {
            fla::ActionScript* actionScript = new fla::ActionScript();
            if (!parseActionScript(childElement, actionScript))
            {
                delete actionScript;
                return false;
            }
            frame->actionScript = actionScript;
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
            std::cerr << "!!!! Unhandled frame element: " << tagName << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
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
            fla::Frame* frame = new fla::Frame();
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
    return true;
}

bool parseLayer(const tinyxml2::XMLElement* element, fla::Layer* layer)
{
    layer->name = getAttribute(element, "name");

    std::string color = getAttribute(element, "color");
    parseHexColor(color, layer->color);

    layer->current = getAttribute(element, "current");
    layer->selected = getAttribute(element, "isSelected") == "true";
    layer->autoNamed = getAttribute(element, "autoNamed") == "true";
    layer->locked = getAttribute(element, "locked") == "true";
    layer->visible = getAttribute(element, "visible") != "false";

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
            std::cerr << "!!!! Unhandled layer element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
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
            fla::Layer* layer = new fla::Layer();
            if (!parseLayer(childElement, layer))
            {
                return false;
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

bool parseTimeline(const tinyxml2::XMLElement* element, fla::Timeline* timeline)
{
    timeline->name = getAttribute(element, "name");
    timeline->layerDepthEnabled = getAttribute(element, "layerDepthEnabled") == "true";

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
            std::cerr << "!!!! Unhandled timeline element: " << childElement->Name() << (_currentFile.empty() ? "" : " from ") << _currentFile << std::endl;
        }
    }
    return true;
}

bool parseTimelines(const tinyxml2::XMLElement* element, std::vector<fla::Timeline*>& timelines)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "DOMTimeline") == 0)
        {
            fla::Timeline* timeline = new fla::Timeline();
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

bool parseSymbolInclude(const tinyxml2::XMLElement* element, fla::Document* document, ZipReader* zipReader)
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

    std::string content = zipReader->readTextFile(libraryPath);

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
    if (!root || std::strcmp(root->Name(), "DOMSymbolItem") != 0)
    {
        std::cerr << "Root element of symbol file is not DOMSymbolItem: " << href << std::endl;
        _currentFile.clear();
        return false;
    }

    fla::Symbol* symbol = new fla::Symbol();
    symbol->name = getAttribute(root, "name");
    symbol->itemId = getAttribute(root, "itemId");
    symbol->lastModified = getAttribute(root, "lastModified");

    // Parse timelines within the symbol
    for (const tinyxml2::XMLElement* childElement = root->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "timeline") == 0)
        {
            if (!parseTimelines(childElement, symbol->timelines))
            {
                _currentFile.clear();
                return false;
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled symbol timeline element: " << childElement->Name() << " from " << href << std::endl;
        }
    }

    document->symbols.push_back(symbol);
    document->symbolMap[symbol->name] = symbol;
    _currentFile.clear();

    return true;
}

bool parseSymbols(const tinyxml2::XMLElement* element, fla::Document* document, ZipReader* zipReader)
{
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        if (std::strcmp(childElement->Name(), "Include") == 0)
        {
            if (!parseSymbolInclude(childElement, document, zipReader))
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
            fla::Bitmap* bitmap = new fla::Bitmap();

            bitmap->name = getAttribute(childElement, "name");
            bitmap->itemId = getAttribute(childElement, "itemId");
            bitmap->href = getAttribute(childElement, "href");

            std::string mediaPath = "LIBRARY/" + bitmap->href;
            if (zipReader->containsFile(mediaPath))
            {
                bitmap->imageData = zipReader->readFile(mediaPath);
                document->resources.push_back(bitmap);
            }
            else
            {
                std::cerr << "Bitmap media not found: " << bitmap->href << std::endl;
                delete bitmap;
            }
        }
        else
        {
            std::cerr << "!!!! Unhandled media element: " << childElement->Name() << std::endl;
        }
    }
    return true;
}

bool parseDocument(fla::Document* document, const tinyxml2::XMLElement* element, ZipReader* zipReader)
{
    // Parse attributes
    if (hasAttribute(element, "width"))
        document->width = getIntAttribute(element, "width");
    if (hasAttribute(element, "height"))
        document->height = getIntAttribute(element, "height");
    if (hasAttribute(element, "frameRate"))
        document->frameRate = getDoubleAttribute(element, "frameRate");

    document->currentTimeline = getAttribute(element, "currentTimeline");
    document->xflVersion = getAttribute(element, "xflVersion");
    document->creatorInfo = getAttribute(element, "creatorInfo");
    document->platform = getAttribute(element, "platform");
    document->versionInfo = getAttribute(element, "versionInfo");
    document->majorVersion = getAttribute(element, "majorVersion");
    document->buildNumber = getAttribute(element, "buildNumber");
    document->viewAngle3D = getAttribute(element, "viewAngle3D");
    document->vanishingPoint3DX = getAttribute(element, "vanishingPoint3DX");
    document->vanishingPoint3DY = getAttribute(element, "vanishingPoint3DY");
    document->nextSceneIdentifier = getAttribute(element, "nextSceneIdentifier");
    document->playOptionsPlayLoop = getAttribute(element, "playOptionsPlayLoop");
    document->playOptionsPlayPages = getAttribute(element, "playOptionsPlayPages");
    document->playOptionsPlayFrameActions = getAttribute(element, "playOptionsPlayFrameActions");
    document->filetypeGUID = getAttribute(element, "filetypeGUID");
    document->fileGUID = getAttribute(element, "fileGUID");

    // Parse child elements
    for (const tinyxml2::XMLElement* childElement = element->FirstChildElement();
         childElement != nullptr;
         childElement = childElement->NextSiblingElement())
    {
        const char* tagName = childElement->Name();

        if (std::strcmp(tagName, "timelines") == 0)
        {
            if (!parseTimelines(childElement, document->timelines))
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
            fla::PublishHistory* publishHistory = new fla::PublishHistory();
            /*if (!parsePublishHistory(childElement, publishHistory))
            {
                delete publishHistory;
                return false;
            }*/
            document->publishHistory = publishHistory;
        }
        else if (std::strcmp(tagName, "PrinterSettings") == 0)
        {
            fla::PrinterSettings* printerSettings = new fla::PrinterSettings();
            /*if (!parsePrinterSettings(childElement, printerSettings))
            {
                delete printerSettings;
                return false;
            }*/
            document->printerSettings = printerSettings;
        }
        else if (std::strcmp(tagName, "scripts") == 0)
        {
            fla::Scripts* scripts = new fla::Scripts();
            /*if (!parseScripts(childElement, scripts))
            {
                delete scripts;
                return false;
            }*/
            document->scripts = scripts;
        }
        else
        {
            std::cerr << "!!!! Unhandled document element: " << tagName << std::endl;
        }
    }

    return true;
}

} // namespace

DocumentParser::DocumentParser(ZipReader* zipReader)
    : _zipReader(zipReader)
{}

fla::Document* DocumentParser::parse(const std::string& xmlContent)
{
    _currentFile.clear();

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xmlContent.c_str()) != tinyxml2::XML_SUCCESS)
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

    fla::Document* document = new fla::Document();
    if (!parseDocument(document, root, _zipReader))
    {
        delete document;
        _errorString = "Failed to parse document content";
        return nullptr;
    }

    document->source = xmlContent;

    return document;
}
