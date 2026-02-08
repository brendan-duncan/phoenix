#include "document_parser.h"
#include "path_parser.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/symbol_instance.h"

#include <algorithm>
#include <QDomElement>
#include <QFile>
#include <QDomNodeList>
#include <QDebug>

namespace 
{

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

bool parseSolidColor(const QDomElement& element, SolidColor* solidColor)
{
    std::string color = element.attribute("color").toStdString();

    // Parse CSS hex color if it starts with '#'
    parseHexColor(color, solidColor->color);

    return true;
}

bool parseLinearGradient(const QDomElement& element, LinearGradient* linearGradient)
{
    QDomNodeList entryNodes = element.elementsByTagName("GradientEntry");
    for (size_t i = 0; i < entryNodes.size(); ++i)
    {
        QDomElement entryElement = entryNodes.at(i).toElement();
        GradientEntry entry;
        entry.ratio = entryElement.attribute("ratio").toDouble();

        std::string color = entryElement.attribute("color").toStdString();
        parseHexColor(color, entry.color);

        linearGradient->entries.push_back(entry);
    }

    std::sort(linearGradient->entries.begin(), linearGradient->entries.end(),
        [](const GradientEntry& a, const GradientEntry& b) {
            return a.ratio < b.ratio;
        });

    return true;
}

Path* parsePath(const QDomElement& element)
{
    std::string edgeData = element.attribute("edges").toStdString();
    PathParser pathParser;
    Path* path = pathParser.parse(edgeData);
    if (!path)
    {
        qDebug() << "Failed to parse path edges:" << QString::fromStdString(pathParser.errorString());
        return nullptr;
    }

    path->fillStyle = element.attribute("fillStyle1").toInt();
    path->strokeStyle = element.attribute("strokeStyle").toInt();

    return path;
}

bool parseEdges(const QDomElement& element, Shape* shape)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "Edge")
            {
                Path* path = parsePath(childElement);
                if (!path)
                {
                    return false;
                }
                shape->edges.push_back(path);
            }
            else
            {
                qDebug() << "!!!! Unhandled edge element:" << childElement.tagName();
            }
        }
    }
    return true;
}

FillStyle* parseFillStyle(const QDomElement& element)
{
    if (element.tagName() == "SolidColor")
    {
        SolidColor* solidFill = new SolidColor();
        if (!parseSolidColor(element, solidFill))
        {
            delete solidFill;
            return nullptr;
        }

        return solidFill;
    }
    else if (element.tagName() == "LinearGradient")
    {
        LinearGradient* linearFill = new LinearGradient();
        if (!parseLinearGradient(element, linearFill))
        {
            delete linearFill;
            return nullptr;
        }
        return linearFill;
    }
    qDebug() << "!!!! Unhandled fill element:" << element.tagName();
    return nullptr;
}

bool parseStroke(const QDomElement& element, Stroke* stroke)
{
    stroke->scaleMode = element.attribute("scaleMode").toStdString();
    bool ok;
    stroke->weight = element.attribute("weight").toDouble(&ok);
    if (!ok)
    {
        stroke->weight = 1.0; // Default weight
    }

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "fill")
            {
                QDomNodeList fillNodes = childElement.childNodes();
                for (int j = 0; j < fillNodes.size(); ++j)
                {
                    QDomNode fillNode = fillNodes.at(j);
                    if (fillNode.isElement())
                    {
                        QDomElement fillElement = fillNode.toElement();
                        stroke->fill = parseFillStyle(fillElement);
                        if (!stroke->fill)
                        {
                            return false;
                        
                        }
                    }
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled stroke element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseStrokeStyle(const QDomElement& element, StrokeStyle* strokeStyle)
{
    strokeStyle->index = element.attribute("index").toInt();

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "SolidStroke" ||
                childElement.tagName() == "DashedStroke" ||
                childElement.tagName() == "RaggedStroke" ||
                childElement.tagName() == "StippleStroke")
            {
                strokeStyle->stroke = new Stroke();
                strokeStyle->stroke->style = childElement.tagName().toStdString();
                if (!parseStroke(childElement, strokeStyle->stroke))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled stroke element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseStrokes(const QDomElement& element, Shape* shape)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "StrokeStyle")
            {
                StrokeStyle* strokeStyle = new StrokeStyle();
                if (!parseStrokeStyle(childElement, strokeStyle))
                {
                    return false;
                }
                shape->strokes.push_back(strokeStyle);
                // Add to strokesMap for quick lookup by index
                shape->strokesMap[strokeStyle->index] = strokeStyle;
            }
            else
            {
                qDebug() << "!!!! Unhandled strokes element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseFills(const QDomElement& element, Shape* shape)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "FillStyle")
            {
                int index = childElement.attribute("index").toInt();

                QDomNodeList fillNodes = childElement.childNodes();
                if (fillNodes.size() == 0)
                {
                    continue;
                }

                QDomElement fillElement = fillNodes.at(0).toElement();
                FillStyle *fillStyle = parseFillStyle(fillElement);
                if (!fillStyle)
                {
                    return false;
                }

                shape->fills.push_back(fillStyle);
                shape->fillsMap[index] = fillStyle;
            }
            else
            {
                qDebug() << "!!!! Unhandled fills element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseElement(const QDomElement& element, Element* el)
{
    if (element.hasAttribute("isSelected"))
        el ->isSelected = (element.attribute("isSelected") == "true");

    if (element.hasAttribute("isFloating"))
        el->isFloating = (element.attribute("isFloating") == "true");

    // Parse transformation matrix
    QDomNodeList matrixNodes = element.elementsByTagName("matrix");
    if (!matrixNodes.isEmpty())
    {
        QDomElement matrixElement = matrixNodes.at(0).toElement();
        QDomNodeList matrixList = matrixElement.elementsByTagName("Matrix");
        if (!matrixList.isEmpty())
        {
            QDomElement matrixEl = matrixList.at(0).toElement();
            el->transform.m11 = matrixEl.hasAttribute("a") ? matrixEl.attribute("a").toDouble() : 1.0;
            el->transform.m12 = matrixEl.hasAttribute("b") ? matrixEl.attribute("b").toDouble() : 0.0;
            el->transform.m21 = matrixEl.hasAttribute("c") ? matrixEl.attribute("c").toDouble() : 0.0;
            el->transform.m22 = matrixEl.hasAttribute("d") ? matrixEl.attribute("d").toDouble() : 1.0;
            el->transform.tx = matrixEl.hasAttribute("tx") ? matrixEl.attribute("tx").toDouble() : 0.0;
            el->transform.ty = matrixEl.hasAttribute("ty") ? matrixEl.attribute("ty").toDouble() : 0.0;
        }
        else
        {
            qDebug() << "!!!! Unhandled element matrix element:" << matrixElement.tagName();
        }
    }

    // Parse transformation point
    QDomNodeList pointNodes = element.elementsByTagName("transformationPoint");
    if (!pointNodes.isEmpty())
    {
        QDomElement pointElement = pointNodes.at(0).toElement();
        QDomNodeList pointList = pointElement.elementsByTagName("Point");
        if (!pointList.isEmpty())
        {
            QDomElement pointEl = pointList.at(0).toElement();
            double x = pointEl.hasAttribute("x") ? pointEl.attribute("x").toDouble() : 0.0;
            double y = pointEl.hasAttribute("y") ? pointEl.attribute("y").toDouble() : 0.0;
            el->transformationPoint = Point(x, y);
        }
        else
        {
            qDebug() << "!!!! Unhandled element transformation point element:" << pointElement.tagName();
        }
    }

    return true;
}

bool parseShape(const QDomElement& element, Shape* shape)
{
    if (!parseElement(element, shape))
    {
        return false;
    }

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "fills")
            {
                if (!parseFills(childElement, shape))
                {
                    return false;
                }
            }
            else if (childElement.tagName() == "strokes")
            {
                if (!parseStrokes(childElement, shape))
                {
                    return false;
                }
            }
            else if (childElement.tagName() == "edges")
            {
                if (!parseEdges(childElement, shape))
                {
                    return false;
                }
            }
            else if (childElement.tagName() == "matrix")
            {
                // from parseElement
            }
            else if (childElement.tagName() == "transformationPoint")
            {
                // from parseElement
            }
            else
            {
                qDebug() << "!!!! Unhandled shape element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseSymbolInstance(const QDomElement& element, SymbolInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    instance->libraryItemName = element.attribute("libraryItemName").toStdString();
    
    return true;
}

bool parseGroup(const QDomElement& element, Group* group)
{
    if (!parseElement(element, group))
    {
        return false;
    }

    QDomNodeList memberNodes = element.elementsByTagName("members");
    if (!memberNodes.isEmpty())
    {
        QDomElement memberElement = memberNodes.at(0).toElement();

        for (int i = 0; i < memberElement.childNodes().size(); ++i)
        {
            QDomNode node = memberElement.childNodes().at(i);
            if (node.isElement())
            {
                QDomElement childElement = node.toElement();
                if (childElement.tagName() == "DOMShape")
                {
                    Shape* shape = new Shape();
                    if (!parseShape(childElement, shape))
                    {
                        return false;
                    }
                    group->members.push_back(shape);
                }
                else if (childElement.tagName() == "DOMSymbolInstance")
                {
                    SymbolInstance* instance = new SymbolInstance();
                    if (!parseSymbolInstance(childElement, instance))
                    {
                        delete instance;
                        return false;
                    }
                    group->members.push_back(instance);
                }
                else if (childElement.tagName() == "DOMGroup")
                {
                    Group* subgroup = new Group();
                    if (!parseGroup(childElement, subgroup))
                    {
                        delete subgroup;
                        return false;
                    }
                    group->members.push_back(subgroup);
                }
                else if (childElement.tagName() == "matrix")
                {
                    // from parseElement
                }
                else if (childElement.tagName() == "transformationPoint")
                {
                    // from parseElement
                }
                else
                {
                    qDebug() << "!!!! Unhandled group member element:" << childElement.tagName();
                }
            }
        }
    }

    return true;
}

bool parseElements(const QDomElement& element, Frame* frame)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "DOMShape")
            {
                Shape* shape = new Shape();
                if (!parseShape(childElement, shape))
                {
                    delete shape;
                    return false;
                }
                frame->elements.push_back(shape);
            }
            else if (childElement.tagName() == "DOMSymbolInstance")
            {
                SymbolInstance* instance = new SymbolInstance();
                if (!parseSymbolInstance(childElement, instance))
                {
                    delete instance;
                    return false;
                }
                frame->elements.push_back(instance);
            }
            else if (childElement.tagName() == "DOMGroup")
            {
                Group* group = new Group();
                if (!parseGroup(childElement, group))
                {
                    delete group;
                    return false;
                }
                frame->elements.push_back(group);
            }
            else
            {
                qDebug() << "!!!! Unhandled elements element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseFrame(const QDomElement& element, Frame* frame)
{
    frame->index = element.attribute("index").toInt();
    frame->keyMode = element.attribute("keyMode").toStdString();

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "elements") 
            {
                if (!parseElements(childElement, frame))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled frame element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseFrames(const QDomElement& element, Layer* layer)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "DOMFrame")
            {
                Frame* frame = new Frame();
                if (!parseFrame(childElement, frame))
                {
                    return false;
                }
                layer->frames.push_back(frame);
            }
            else
            {
                qDebug() << "!!!! Unhandled frames element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseLayer(const QDomElement& element, Layer* layer)
{
    layer->name = element.attribute("name").toStdString();

    std::string color = element.attribute("color").toStdString();
    parseHexColor(color, layer->color);

    layer->current = element.attribute("current").toStdString();
    layer->isSelected = element.attribute("isSelected") == "true";
    layer->autoNamed = element.attribute("autoNamed") == "true";

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "frames")
            {
                if (!parseFrames(childElement, layer))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled layer element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseLayers(const QDomElement& element, Timeline* timeline)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "DOMLayer")
            {
                Layer* layer = new Layer();
                if (!parseLayer(childElement, layer))
                {
                    return false;
                }
                timeline->layers.push_back(layer);
            }
            else
            {
                qDebug() << "!!!! Unhandled layer element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseTimeline(const QDomElement& element, Timeline* timeline)
{
    timeline->name = element.attribute("name").toStdString();
    timeline->layerDepthEnabled = element.attribute("layerDepthEnabled") == "true";

    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "layers")
            {
                if (!parseLayers(childElement, timeline))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled timeline element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseTimelines(const QDomElement& element, std::vector<Timeline*>& timelines)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "DOMTimeline")
            {
                Timeline* timeline = new Timeline();
                if (!parseTimeline(childElement, timeline))
                {
                    return false;
                }
                timelines.push_back(timeline);
            }
            else
            {
                qDebug() << "!!!! Unhandled timeline element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseSymbolInclude(const QDomElement& element, Document* document, ZipReader* zipReader)
{
    std::string href = element.attribute("href").toStdString();
    if (href.empty())
    {
        qDebug() << "Symbol include missing href attribute";
        return false;
    }

    std::string libraryPath = "LIBRARY/" + href;

    if (!zipReader->containsFile(libraryPath))
    {
        qDebug() << "Symbol include not found in fla";
        return false;
    }

    std::string content = zipReader->readTextFile(libraryPath);

    // Parse the included symbol XML content
    QDomDocument symbolDoc;
    if (!symbolDoc.setContent(QString::fromStdString(content)))
    {
        qDebug() << "Failed to parse symbol XML content from:" << QString::fromStdString(href);
        return false;
    }

    QDomElement root = symbolDoc.documentElement();
    if (root.tagName() != "DOMSymbolItem")
    {
        qDebug() << "Root element of symbol file is not DOMSymbolItem:" << QString::fromStdString(href);
        return false;
    }

    Symbol* symbol = new Symbol();
    symbol->name = root.attribute("name").toStdString();
    symbol->itemId = root.attribute("itemId").toStdString();
    symbol->lastModified = root.attribute("lastModified").toStdString();

    // Parse timelines within the symbol
    QDomNodeList childNodes = root.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "timeline")
            {
                if (!parseTimelines(childElement, symbol->timelines))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled symbol timeline element:" << childElement.tagName();
            }
        }
    }

    document->symbols.push_back(symbol);
    document->symbolMap[symbol->name] = symbol;

    return true;
}

bool parseSymbols(const QDomElement& element, Document* document, ZipReader* zipReader)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "Include")
            {
                if (!parseSymbolInclude(childElement, document, zipReader))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled symbols element:" << childElement.tagName();
            }
        }
    }

    return true;
}

bool parseDocument(Document* document, const QDomElement& element, ZipReader* zipReader)
{
    // Parse attributes
    bool ok;
    double w = element.attribute("width").toDouble(&ok);
    document->width = ok ? w : 0.0;
    double h = element.attribute("height").toDouble(&ok);
    document->height = ok ? h : 0.0;
    double fr = element.attribute("frameRate").toDouble(&ok);
    document->frameRate = ok ? fr : 0.0;
    document->currentTimeline = element.attribute("currentTimeline").toStdString();
    document->xflVersion = element.attribute("xflVersion").toStdString();
    document->creatorInfo = element.attribute("creatorInfo").toStdString();
    document->platform = element.attribute("platform").toStdString();
    document->versionInfo = element.attribute("versionInfo").toStdString();
    document->majorVersion = element.attribute("majorVersion").toStdString();
    document->buildNumber = element.attribute("buildNumber").toStdString();
    document->viewAngle3D = element.attribute("viewAngle3D").toStdString();
    document->vanishingPoint3DX = element.attribute("vanishingPoint3DX").toStdString();
    document->vanishingPoint3DY = element.attribute("vanishingPoint3DY").toStdString();
    document->nextSceneIdentifier = element.attribute("nextSceneIdentifier").toStdString();
    document->playOptionsPlayLoop = element.attribute("playOptionsPlayLoop").toStdString();
    document->playOptionsPlayPages = element.attribute("playOptionsPlayPages").toStdString();
    document->playOptionsPlayFrameActions = element.attribute("playOptionsPlayFrameActions").toStdString();
    document->filetypeGUID = element.attribute("filetypeGUID").toStdString();
    document->fileGUID = element.attribute("fileGUID").toStdString();

    // Parse child elements
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "timelines")
            {
                if (!parseTimelines(childElement, document->timelines))
                {
                    return false;
                }
            }
            else if (childElement.tagName() == "symbols")
            {
                if (!parseSymbols(childElement, document, zipReader))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled document element:" << childElement.tagName();
            }
        }
    }

    return true;
}

} // namespace

DocumentParser::DocumentParser(ZipReader* zipReader)
    : _zipReader(zipReader)
{}

Document* DocumentParser::parse(const std::string& xmlContent)
{
    QDomDocument doc;
    if (!doc.setContent(xmlContent))
    {
        _errorString = "Failed to parse XML content";
        return nullptr;
    }

    QDomElement root = doc.documentElement();
    if (root.tagName() != "DOMDocument")
    {
        _errorString = "Root element is not Document";
        return nullptr;
    }

    Document* document = new Document();
    if (!parseDocument(document, root, _zipReader))
    {
        delete document;
        _errorString = "Failed to parse document content";
        return nullptr;
    }

    document->source = xmlContent;

    return document;
}
