#include "document_parser.h"
#include "path_parser.h"
#include "../data/bitmap.h"
#include "../data/bitmap_instance.h"
#include "../data/group.h"
#include "../data/linear_gradient.h"
#include "../data/radial_gradient.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/static_text.h"
#include "../data/symbol_instance.h"
#include <algorithm>

#include <QDomElement>
#include <QFile>
#include <QDomNodeList>
#include <QDebug>

namespace 
{

std::string _currentFile;

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

bool parseRadialGradient(const QDomElement& element, RadialGradient* radialGradient)
{
    QDomNodeList entryNodes = element.elementsByTagName("GradientEntry");
    for (size_t i = 0; i < entryNodes.size(); ++i)
    {
        QDomElement entryElement = entryNodes.at(i).toElement();
        RadialEntry entry;
        entry.ratio = entryElement.attribute("ratio").toDouble();

        std::string color = entryElement.attribute("color").toStdString();
        parseHexColor(color, entry.color);

        radialGradient->entries.push_back(entry);
    }

    std::sort(radialGradient->entries.begin(), radialGradient->entries.end(),
        [](const RadialEntry& a, const RadialEntry& b) {
            return a.ratio < b.ratio;
        });

    return true;
}

Edge* parsePath(const QDomElement& element)
{
    std::string edgeData =element.attribute("edges").toStdString();
    if (edgeData.empty())
    {
        qDebug() << "Edge element missing edges attribute" << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
        return nullptr;
    }

    PathParser pathParser;
    Edge* edge = pathParser.parse(edgeData);

    if (!edge)
    {
        qDebug() << "Failed to parse path edges:" << QString::fromStdString(pathParser.errorString()) << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
        return nullptr;
    }

    edge->fillStyle0 = element.hasAttribute("fillStyle0") ? element.attribute("fillStyle0").toInt() : -1;
    edge->fillStyle1 = element.hasAttribute("fillStyle1") ? element.attribute("fillStyle1").toInt() : -1;

    edge->strokeStyle = element.hasAttribute("strokeStyle") ? element.attribute("strokeStyle").toInt() : -1;

    return edge;
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
                if (childElement.hasAttribute("cubics"))
                {
                    // Cubics are used for the editor, not for rendering, so they can be skipped for now
                    continue;
                }

                Edge* edge = parsePath(childElement);
                if (!edge)
                {
                    return false;
                }

                shape->edges.push_back(edge);
            }
            else
            {
                qDebug() << "!!!! Unhandled edge element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
    else if (element.tagName() == "RadialGradient")
    {
        RadialGradient* radialFill = new RadialGradient();
        if (!parseRadialGradient(element, radialFill))
        {
            delete radialFill;
            return nullptr;
        }
        return radialFill;
    }
    qDebug() << "!!!! Unhandled fill element:" << element.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled stroke element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
            Stroke* stroke = nullptr;
            if (childElement.tagName() == "SolidStroke")
            {
                stroke = new SolidStroke();
            }
            else if (childElement.tagName() == "DashedStroke")
            {
                stroke = new DashedStroke();
            }
            else if (childElement.tagName() == "RaggedStroke")
            {
                stroke = new RaggedStroke();
            }
            else if (childElement.tagName() == "StippleStroke")
            {
                stroke = new StippleStroke();
            }
            else if (childElement.tagName() == "DottedStroke")
            {
                stroke = new DottedStroke();
            }
            else
            {
                qDebug() << "!!!! Unhandled stroke element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
                continue;
            }

             if (!parseStroke(childElement, stroke))
             {
                 delete stroke;
                 return false;
             }

             stroke->style = childElement.tagName().toStdString();
             strokeStyle->stroke = stroke;
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
                qDebug() << "!!!! Unhandled strokes element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled fills element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
            qDebug() << "!!!! Unhandled element matrix element:" << matrixElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
            qDebug() << "!!!! Unhandled element transformation point element:" << pointElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled shape element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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

bool parseBitmapInstance(const QDomElement& element, BitmapInstance* instance)
{
    if (!parseElement(element, instance))
    {
        return false;
    }

    instance->libraryItemName = element.attribute("libraryItemName").toStdString();

    return true;
}

bool parseStaticText(const QDomElement& element, StaticText* staticText)
{
    if (!parseElement(element, staticText))
    {
        return false;
    }

    QDomNodeList textRunsNodes = element.elementsByTagName("textRuns");
    for (QDomNode textRunNode : textRunsNodes)
    {
        QDomElement textRunElement = textRunNode.toElement();

        for (int i = 0; i < textRunElement.childNodes().size(); ++i)
        {
            QDomNode node = textRunElement.childNodes().at(i);
            if (node.isElement())
            {
                QDomElement childElement = node.toElement();
                if (childElement.tagName() == "DOMTextRun")
                {
                    TextRun textRun;

                    QDomNodeList textRunChildNodes = childElement.childNodes();
                    for (int j = 0; j < textRunChildNodes.size(); ++j)
                    {
                        QDomNode textRunChildNode = textRunChildNodes.at(j);
                        if (textRunChildNode.isElement())
                        {
                            QDomElement textRunChildElement = textRunChildNode.toElement();
                            if (textRunChildElement.tagName() == "characters")
                            {
                                textRun.text = textRunChildElement.text().toStdString();
                            }
                            else if (textRunChildElement.tagName() == "textAttrs")
                            {
                                for (int k = 0; k < textRunChildElement.childNodes().size(); ++k)
                                {
                                    QDomNode attrNode = textRunChildElement.childNodes().at(k);
                                    if (attrNode.isElement())
                                    {
                                        QDomElement attrElement = attrNode.toElement();

                                        if (attrElement.tagName() == "DOMTextAttrs")
                                        {
                                            QDomNamedNodeMap attributes = attrElement.attributes();
                                            for (int i = 0; i < attributes.length(); ++i)
                                            {
                                                QDomAttr attr = attributes.item(i).toAttr();
                                                if (attr.name() == "aliasText")
                                                {
                                                    textRun.aliasText = attr.value() == "true";
                                                }
                                                else if (attr.name() == "lineHeight")
                                                {
                                                    textRun.lineHeight = attr.value().toDouble();
                                                }
                                                else if (attr.name() == "size")
                                                {
                                                    textRun.size = attr.value().toDouble();
                                                }
                                                else if (attr.name() == "bitmapSize")
                                                {
                                                    textRun.bitmapSize = attr.value().toDouble();
                                                }
                                                else if (attr.name() == "face")
                                                {
                                                    textRun.face = attr.value().toStdString();
                                                }
                                                else if (attr.name() == "fillColor")
                                                {
                                                    std::string color = attr.value().toStdString();
                                                    parseHexColor(color, textRun.fillColor);
                                                }
                                               /*else
                                                {
                                                    qDebug() << "!!!! Unhandled text attribute:" << attr.name() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
                                                }*/
                                            }
                                        }
                                        /*else
                                        {
                                            qDebug() << "!!!! Unhandled text attribute element:" << attrElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
                                        }*/
                                    }
                                }
                            }
                            else
                            {
                                qDebug() << "!!!! Unhandled text run child element:" << textRunChildElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
                            }
                        }
                    }

                    if (!textRun.text.empty())
                        staticText->runs.push_back(textRun);
                }
                else
                {
                    qDebug() << "!!!! Unhandled static text run element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
                }
            }
        }
    }

    return true;
}

bool parseGroup(const QDomElement& element, Group* group)
{
    if (!parseElement(element, group))
    {
        return false;
    }

    QDomNodeList memberNodes = element.elementsByTagName("members");
    for (QDomNode memberNode : memberNodes)
    {
        QDomElement memberElement = memberNode.toElement();

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
                else if (childElement.tagName() == "DOMStaticText")
                {
                    StaticText* staticText = new StaticText();
                    if (!parseStaticText(childElement, staticText))
                    {
                        delete staticText;
                        return false;
                    }
                    group->members.push_back(staticText);
                }
                else if (childElement.tagName() == "DOMBitmapInstance")
                {
                    BitmapInstance* bitmapInstance = new BitmapInstance();
                    if (!parseBitmapInstance(childElement, bitmapInstance))
                    {
                        delete bitmapInstance;
                        return false;
                    }
                    group->members.push_back(bitmapInstance);
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
                    qDebug() << "!!!! Unhandled group member element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
            else if (childElement.tagName() == "DOMStaticText")
            {
                StaticText* staticText = new StaticText();
                if (!parseStaticText(childElement, staticText))
                {
                    delete staticText;
                    return false;
                }
                frame->elements.push_back(staticText);
            }
            else if (childElement.tagName() == "DOMBitmapInstance")
            {
                BitmapInstance* bitmapInstance = new BitmapInstance();
                if (!parseBitmapInstance(childElement, bitmapInstance))
                {
                    delete bitmapInstance;
                    return false;
                }
                frame->elements.push_back(bitmapInstance);
            }
            else
            {
                qDebug() << "!!!! Unhandled elements element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
            }
        }
    }
    return true;
}

bool parseActionScript(const QDomElement& element, ActionScript* actionScript)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "script")
            {
                actionScript->code = element.text().toStdString();
            }
            else
            {
                qDebug() << "!!!! Unhandled ActionScript element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
            if (childElement.tagName() == "Actionscript")
            {
                ActionScript* actionScript = new ActionScript();
                if (!parseActionScript(childElement, actionScript))
                {
                    delete actionScript;
                    return false;
                }
                frame->actionScript = actionScript;
            }
            else if (childElement.tagName() == "elements") 
            {
                if (!parseElements(childElement, frame))
                {
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled frame element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled frames element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
    layer->selected = element.attribute("isSelected") == "true";
    layer->autoNamed = element.attribute("autoNamed") == "true";
    layer->locked = element.attribute("locked") == "true";
    layer->visible = element.attribute("visible") != "false";

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
                qDebug() << "!!!! Unhandled layer element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled layer element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled timeline element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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
                qDebug() << "!!!! Unhandled timeline element:" << childElement.tagName() << (_currentFile.empty() ? "" : " from") << QString::fromStdString(_currentFile);
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

    _currentFile = href;

    // Parse the included symbol XML content
    QDomDocument symbolDoc;
    if (!symbolDoc.setContent(QString::fromStdString(content)))
    {
        qDebug() << "Failed to parse symbol XML content from:" << QString::fromStdString(href);
        _currentFile.clear();
        return false;
    }

    QDomElement root = symbolDoc.documentElement();
    if (root.tagName() != "DOMSymbolItem")
    {
        qDebug() << "Root element of symbol file is not DOMSymbolItem:" << QString::fromStdString(href);
        _currentFile.clear();
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
                    _currentFile.clear();
                    return false;
                }
            }
            else
            {
                qDebug() << "!!!! Unhandled symbol timeline element:" << childElement.tagName() << " from" << QString::fromStdString(href);
            }
        }
    }

    document->symbols.push_back(symbol);
    document->symbolMap[symbol->name] = symbol;
    _currentFile.clear();

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

bool parseMedia(const QDomElement& element, Document* document, ZipReader* zipReader)
{
    QDomNodeList childNodes = element.childNodes();
    for (int i = 0; i < childNodes.size(); ++i)
    {
        QDomNode node = childNodes.at(i);
        if (node.isElement())
        {
            QDomElement childElement = node.toElement();
            if (childElement.tagName() == "DOMBitmapItem")
            {
                Bitmap* bitmap = new Bitmap();

                bitmap->name = childElement.attribute("name").toStdString();
                bitmap->itemId = childElement.attribute("itemId").toStdString();
                bitmap->href = childElement.attribute("href").toStdString();

                std::string bitmapDataHRef = childElement.attribute("bitmapDataHRef").toStdString();
                if (bitmapDataHRef.empty())
                {
                    qDebug() << "Bitmap item missing bitmapDataHRef attribute";
                    delete bitmap;
                    return false;
                }

                std::string mediaPath = "bin/" + bitmapDataHRef;

                if (!zipReader->containsFile(mediaPath))
                {
                    qDebug() << "Bitmap media not found in fla:" << QString::fromStdString(bitmapDataHRef);
                    delete bitmap;
                    return false;
                }

                bitmap->imageData = zipReader->readFile(mediaPath);

                document->resources.push_back(bitmap);
            }
            else
            {
                qDebug() << "!!!! Unhandled media element:" << childElement.tagName();
            }
        }
    }
    return true;
}

bool parseDocument(Document* document, const QDomElement& element, ZipReader* zipReader)
{
    // Parse attributes
    bool ok;
    if (element.hasAttribute("width"))
        document->width = element.attribute("width").toInt();
    if (element.hasAttribute("height"))
        document->height = element.attribute("height").toInt();
    if (element.hasAttribute("frameRate"))
        document->frameRate = element.attribute("frameRate").toDouble();

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
            else if (childElement.tagName() == "media")
            {
                if (!parseMedia(childElement, document, zipReader))
                {
                    return false;
                }
            }
            else if (childElement.tagName() == "publishHistory")
            {
                PublishHistory* publishHistory = new PublishHistory();
                /*if (!parsePublishHistory(childElement, publishHistory))
                {
                    delete publishHistory;
                    return false;
                }*/
                document->publishHistory = publishHistory;
            }
            else if (childElement.tagName() == "PrinterSettings")
            {
                PrinterSettings* printerSettings = new PrinterSettings();
                /*if (!parsePrinterSettings(childElement, printerSettings))
                {
                    delete printerSettings;
                    return false;
                }*/
                document->printerSettings = printerSettings;
            }
            else if (childElement.tagName() == "scripts")
            {
                Scripts* scripts = new Scripts();
                /*if (!parseScripts(childElement, scripts))
                {
                    delete scripts;
                    return false;
                }*/
                document->scripts = scripts;
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
    _currentFile.clear();

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
