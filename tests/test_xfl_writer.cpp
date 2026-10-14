#include "test_util.h"

#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/group.h"
#include "../src/data/bitmap_fill.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/oval_primitive.h"
#include "../src/data/radial_gradient.h"
#include "../src/data/rectangle_primitive.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/edit/new_document.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/fla_parser.h"
#include "../src/parser/zip_reader.h"

#include <tinyxml2.h>
#include "../src/writer/xfl_folder_writer.h"
#include "../src/writer/xfl_writer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

using fla::XFLWriter;

namespace {

/// Collects the first few differences between two parsed trees, so a failure
/// says what diverged rather than just that something did.
class TreeComparer
{
public:
    bool equal() const { return _differences.empty(); }

    const std::vector<std::string>& differences() const { return _differences; }

    void compareDocuments(const fla::Document& a, const fla::Document& b)
    {
        check(a.width == b.width, "document width");
        check(a.height == b.height, "document height");
        check(a.frameRate == b.frameRate, "document frameRate");
        check(a.filetypeGUID == b.filetypeGUID, "document filetypeGUID");
        check(a.fileGUID == b.fileGUID, "document fileGUID");
        check(a.currentTimeline == b.currentTimeline, "document currentTimeline");
        check(a.xflVersion == b.xflVersion, "document xflVersion");
        check(a.creatorInfo == b.creatorInfo, "document creatorInfo");
        check(a.platform == b.platform, "document platform");
        check(a.versionInfo == b.versionInfo, "document versionInfo");
        check(a.majorVersion == b.majorVersion, "document majorVersion");
        check(a.buildNumber == b.buildNumber, "document buildNumber");
        check(a.nextSceneIdentifier == b.nextSceneIdentifier, "document nextSceneIdentifier");
        compareColor(a.backgroundColor, b.backgroundColor, "document backgroundColor");

        if (!check(a.timelines.size() == b.timelines.size(), "document timeline count"))
            return;

        for (size_t i = 0; i < a.timelines.size(); ++i)
            compareTimelines(*a.timelines[i], *b.timelines[i], "timeline " + std::to_string(i));

        const size_t symbolsA = a.symbolList ? a.symbolList->symbols.size() : 0;
        const size_t symbolsB = b.symbolList ? b.symbolList->symbols.size() : 0;
        if (!check(symbolsA == symbolsB, "symbol count"))
            return;

        for (size_t i = 0; i < symbolsA; ++i)
        {
            const fla::Symbol& sa = *a.symbolList->symbols[i];
            const fla::Symbol& sb = *b.symbolList->symbols[i];
            const std::string where = "symbol " + sa.name;
            check(sa.name == sb.name, where + " name");
            check(sa.itemId == sb.itemId, where + " itemId");
            if (!check(sa.timelines.size() == sb.timelines.size(), where + " timeline count"))
                continue;
            for (size_t t = 0; t < sa.timelines.size(); ++t)
                compareTimelines(*sa.timelines[t], *sb.timelines[t], where + " timeline " + std::to_string(t));
        }
    }

private:
    bool check(bool condition, const std::string& what)
    {
        if (!condition && _differences.size() < 12)
            _differences.push_back(what);
        return condition;
    }

    /// Renders a string with control characters spelled out, so a difference in
    /// invisible characters is readable in the failure output.
    static std::string visible(const std::string& value)
    {
        std::string out;
        for (char c : value)
        {
            if (c == '\r')
                out += "\\r";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\t')
                out += "\\t";
            else
                out.push_back(c);
        }
        return out;
    }

    bool checkString(const std::string& a, const std::string& b, const std::string& what)
    {
        if (a == b)
            return true;
        return check(false, what + " [" + visible(a) + "] vs [" + visible(b) + "]");
    }

    void compareColor(const uint8_t a[4], const uint8_t b[4], const std::string& where)
    {
        check(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3], where);
    }

    void compareTimelines(const fla::Timeline& a, const fla::Timeline& b, const std::string& where)
    {
        check(a.name == b.name, where + " name");
        if (!check(a.layers.size() == b.layers.size(), where + " layer count"))
            return;

        for (size_t i = 0; i < a.layers.size(); ++i)
            compareLayers(*a.layers[i], *b.layers[i], where + " layer " + std::to_string(i));
    }

    void compareLayers(const fla::Layer& a, const fla::Layer& b, const std::string& where)
    {
        checkString(a.name, b.name, where + " name");
        check(a.layerType == b.layerType, where + " layerType");
        check(a.parentLayerIndex == b.parentLayerIndex, where + " parentLayerIndex");
        check(a.locked == b.locked, where + " locked");
        check(a.outline == b.outline, where + " outline");
        check(a.visible == b.visible, where + " visible");
        check(a.autoNamed == b.autoNamed, where + " autoNamed");
        compareColor(a.color, b.color, where + " color");

        if (!check(a.frames.size() == b.frames.size(), where + " frame count"))
            return;

        for (size_t i = 0; i < a.frames.size(); ++i)
            compareFrames(*a.frames[i], *b.frames[i], where + " frame " + std::to_string(i));
    }

    void compareFrames(const fla::Frame& a, const fla::Frame& b, const std::string& where)
    {
        check(a.index == b.index, where + " index");
        check(a.duration == b.duration, where + " duration");
        check(a.keyMode == b.keyMode, where + " keyMode");
        check(a.name == b.name, where + " name");
        check(a.labelType == b.labelType, where + " labelType");
        check(a.tweenType == b.tweenType, where + " tweenType");
        check(a.acceleration == b.acceleration, where + " acceleration");
        check(a.hasCustomEase == b.hasCustomEase, where + " hasCustomEase");
        check(a.customEase.size() == b.customEase.size(), where + " customEase points");
        check(a.motionTweenRotate == b.motionTweenRotate, where + " motionTweenRotate");
        check(a.motionTweenRotateTimes == b.motionTweenRotateTimes, where + " motionTweenRotateTimes");
        check(a.motionTWeenSnap == b.motionTWeenSnap, where + " motionTweenSnap");
        check(a.motionTweenSync == b.motionTweenSync, where + " motionTweenSync");
        check((a.morphShape != nullptr) == (b.morphShape != nullptr), where + " morphShape presence");

        if (!check(a.elements.size() == b.elements.size(), where + " element count"))
            return;

        for (size_t i = 0; i < a.elements.size(); ++i)
            compareElements(*a.elements[i], *b.elements[i], where + " element " + std::to_string(i));
    }

    void compareTransform(const fla::Transform& a, const fla::Transform& b, const std::string& where)
    {
        check(a.m11 == b.m11 && a.m12 == b.m12 && a.m21 == b.m21 &&
              a.m22 == b.m22 && a.tx == b.tx && a.ty == b.ty, where + " matrix");
    }

    void compareElements(const fla::Element& a, const fla::Element& b, const std::string& where)
    {
        if (!check(a.elementType() == b.elementType(), where + " type"))
            return;

        compareTransform(a.transform, b.transform, where);
        check(a.transformationPoint.x == b.transformationPoint.x &&
              a.transformationPoint.y == b.transformationPoint.y, where + " transformationPoint");
        check(a.isLocked == b.isLocked, where + " lockFlag");

        switch (a.elementType())
        {
        case fla::Element::Type::Shape:
            compareShapes(static_cast<const fla::Shape&>(a), static_cast<const fla::Shape&>(b), where);
            break;

        case fla::Element::Type::SymbolInstance:
        {
            const auto& sa = static_cast<const fla::SymbolInstance&>(a);
            const auto& sb = static_cast<const fla::SymbolInstance&>(b);
            check(sa.libraryItemName == sb.libraryItemName, where + " libraryItemName");
            check(sa.name == sb.name, where + " name");
            check(sa.firstFrame == sb.firstFrame, where + " firstFrame");
            check(sa.symbolType == sb.symbolType, where + " symbolType");
            check(sa.loopType == sb.loopType, where + " loopType");
            check(sa.colorTransform.alphaMultiplier == sb.colorTransform.alphaMultiplier,
                where + " alphaMultiplier");
            check(sa.colorTransform.brightness == sb.colorTransform.brightness, where + " brightness");
            check(sa.colorTransform.tintMultiplier == sb.colorTransform.tintMultiplier,
                where + " tintMultiplier");
            compareColor(sa.colorTransform.tintColor, sb.colorTransform.tintColor, where + " tintColor");
            check(sa.blendMode == sb.blendMode, where + " blendMode");
            if (check(sa.filters.size() == sb.filters.size(), where + " filter count"))
            {
                for (size_t i = 0; i < sa.filters.size(); ++i)
                {
                    const fla::Filter& fa = sa.filters[i];
                    const fla::Filter& fb = sb.filters[i];
                    const std::string at = where + " filter " + std::to_string(i);
                    check(fa.type == fb.type, at + " type");
                    check(fa.enabled == fb.enabled, at + " enabled");
                    check(fa.blurX == fb.blurX && fa.blurY == fb.blurY, at + " blur");
                    check(fa.quality == fb.quality, at + " quality");
                    check(fa.angle == fb.angle && fa.distance == fb.distance, at + " offset");
                    check(fa.strength == fb.strength, at + " strength");
                    compareColor(fa.color, fb.color, at + " color");
                    compareColor(fa.highlightColor, fb.highlightColor, at + " highlightColor");
                    compareColor(fa.shadowColor, fb.shadowColor, at + " shadowColor");
                    check(fa.inner == fb.inner && fa.knockout == fb.knockout && fa.hideObject == fb.hideObject, at + " flags");
                    check(fa.bevelType == fb.bevelType, at + " bevel type");
                    check(fa.brightness == fb.brightness && fa.contrast == fb.contrast &&
                        fa.saturation == fb.saturation && fa.hue == fb.hue, at + " adjust");
                    check(fa.entries.size() == fb.entries.size(), at + " entries");
                }
            }
            break;
        }

        case fla::Element::Type::Group:
        {
            const auto& ga = static_cast<const fla::Group&>(a);
            const auto& gb = static_cast<const fla::Group&>(b);
            if (!check(ga.members.size() == gb.members.size(), where + " member count"))
                break;
            for (size_t i = 0; i < ga.members.size(); ++i)
                compareElements(*ga.members[i], *gb.members[i], where + " member " + std::to_string(i));
            break;
        }

        case fla::Element::Type::BitmapInstance:
        {
            const auto& ba = static_cast<const fla::BitmapInstance&>(a);
            const auto& bb = static_cast<const fla::BitmapInstance&>(b);
            check(ba.libraryItemName == bb.libraryItemName, where + " libraryItemName");
            break;
        }

        case fla::Element::Type::StaticText:
        {
            const auto& ta = static_cast<const fla::StaticText&>(a);
            const auto& tb = static_cast<const fla::StaticText&>(b);
            check(ta.left == tb.left && ta.top == tb.top, where + " text origin");
            check(ta.width == tb.width && ta.height == tb.height, where + " text size");
            check(ta.textType == tb.textType, where + " text type");
            check(ta.instanceName == tb.instanceName && ta.lineType == tb.lineType, where + " text field attributes");
            if (!check(ta.runs.size() == tb.runs.size(), where + " text run count"))
                break;
            for (size_t i = 0; i < ta.runs.size(); ++i)
            {
                const std::string runWhere = where + " run " + std::to_string(i);
                checkString(ta.runs[i].text, tb.runs[i].text, runWhere + " text");
                checkString(ta.runs[i].face, tb.runs[i].face, runWhere + " face");
                check(ta.runs[i].size == tb.runs[i].size, runWhere + " size");
                check(ta.runs[i].alignment == tb.runs[i].alignment, runWhere + " alignment");
                compareColor(ta.runs[i].fillColor, tb.runs[i].fillColor, runWhere + " fillColor");
            }
            break;
        }

        case fla::Element::Type::Rectangle:
        {
            const auto& ra = static_cast<const fla::RectanglePrimitive&>(a);
            const auto& rb = static_cast<const fla::RectanglePrimitive&>(b);
            check(ra.rect.topLeft.x == rb.rect.topLeft.x &&
                  ra.rect.topLeft.y == rb.rect.topLeft.y &&
                  ra.rect.width() == rb.rect.width() &&
                  ra.rect.height() == rb.rect.height(), where + " rect");
            check(ra.topLeftRadius == rb.topLeftRadius &&
                  ra.topRightRadius == rb.topRightRadius &&
                  ra.bottomLeftRadius == rb.bottomLeftRadius &&
                  ra.bottomRightRadius == rb.bottomRightRadius, where + " radii");
            break;
        }

        case fla::Element::Type::Oval:
        {
            const auto& oa = static_cast<const fla::OvalPrimitive&>(a);
            const auto& ob = static_cast<const fla::OvalPrimitive&>(b);
            check(oa.rect.topLeft.x == ob.rect.topLeft.x &&
                  oa.rect.topLeft.y == ob.rect.topLeft.y &&
                  oa.rect.width() == ob.rect.width() &&
                  oa.rect.height() == ob.rect.height(), where + " rect");
            check(oa.startAngle == ob.startAngle, where + " startAngle");
            check(oa.endAngle == ob.endAngle, where + " endAngle");
            check(oa.innerRadius == ob.innerRadius, where + " innerRadius");
            break;
        }
        }
    }

    void compareFills(const fla::FillStyle& a, const fla::FillStyle& b, const std::string& where)
    {
        if (!check(a.type() == b.type(), where + " fill type"))
            return;

        switch (a.type())
        {
        case fla::FillStyle::Type::SolidColor:
            compareColor(static_cast<const fla::SolidColor&>(a).color,
                         static_cast<const fla::SolidColor&>(b).color, where + " fill color");
            break;

        case fla::FillStyle::Type::LinearGradient:
        {
            const auto& ga = static_cast<const fla::LinearGradient&>(a);
            const auto& gb = static_cast<const fla::LinearGradient&>(b);
            compareTransform(ga.transform, gb.transform, where + " gradient");
            if (!check(ga.entries.size() == gb.entries.size(), where + " gradient stop count"))
                break;
            for (size_t i = 0; i < ga.entries.size(); ++i)
            {
                check(ga.entries[i].ratio == gb.entries[i].ratio, where + " gradient stop ratio");
                compareColor(ga.entries[i].color, gb.entries[i].color, where + " gradient stop color");
            }
            break;
        }

        case fla::FillStyle::Type::RadialGradient:
        {
            const auto& ga = static_cast<const fla::RadialGradient&>(a);
            const auto& gb = static_cast<const fla::RadialGradient&>(b);
            compareTransform(ga.transform, gb.transform, where + " gradient");
            check(ga.focalPointRatio == gb.focalPointRatio, where + " focalPointRatio");
            if (!check(ga.entries.size() == gb.entries.size(), where + " gradient stop count"))
                break;
            for (size_t i = 0; i < ga.entries.size(); ++i)
            {
                check(ga.entries[i].ratio == gb.entries[i].ratio, where + " gradient stop ratio");
                compareColor(ga.entries[i].color, gb.entries[i].color, where + " gradient stop color");
            }
            break;
        }

        case fla::FillStyle::Type::BitmapFill:
        {
            const auto& ba = static_cast<const fla::BitmapFill&>(a);
            const auto& bb = static_cast<const fla::BitmapFill&>(b);
            compareTransform(ba.transform, bb.transform, where + " bitmap fill");
            check(ba.bitmapPath == bb.bitmapPath, where + " bitmapPath");
            check(ba.bitmapIsClipped == bb.bitmapIsClipped, where + " bitmapIsClipped");
            break;
        }
        }
        check(a.spreadMethod == b.spreadMethod, where + " spreadMethod");
        check(a.interpolationMethod == b.interpolationMethod, where + " interpolationMethod");
    }

    void compareShapes(const fla::Shape& a, const fla::Shape& b, const std::string& where)
    {
        check(a.isDrawingObject == b.isDrawingObject, where + " isDrawingObject");
        if (check(a.fillsMap.size() == b.fillsMap.size(), where + " fill count"))
        {
            for (const auto& entry : a.fillsMap)
            {
                const auto it = b.fillsMap.find(entry.first);
                if (!check(it != b.fillsMap.end(), where + " fill index " + std::to_string(entry.first)))
                    continue;
                if (entry.second && it->second)
                    compareFills(*entry.second, *it->second, where + " fill " + std::to_string(entry.first));
            }
        }

        if (check(a.strokesMap.size() == b.strokesMap.size(), where + " stroke count"))
        {
            for (const auto& entry : a.strokesMap)
            {
                const auto it = b.strokesMap.find(entry.first);
                if (!check(it != b.strokesMap.end(), where + " stroke index " + std::to_string(entry.first)))
                    continue;
                if (!entry.second || !it->second)
                    continue;
                const std::string strokeWhere = where + " stroke " + std::to_string(entry.first);
                check(entry.second->style() == it->second->style(), strokeWhere + " style");
                check(entry.second->weight == it->second->weight, strokeWhere + " weight");
                check(entry.second->scaleMode == it->second->scaleMode, strokeWhere + " scaleMode");
                check(entry.second->solidStyle == it->second->solidStyle, strokeWhere + " solidStyle");
                check(entry.second->caps == it->second->caps, strokeWhere + " caps");
                check(entry.second->joints == it->second->joints, strokeWhere + " joints");
                check(entry.second->miterLimit == it->second->miterLimit, strokeWhere + " miterLimit");
                check(entry.second->pixelHinting == it->second->pixelHinting, strokeWhere + " pixelHinting");
                check(entry.second->dash1 == it->second->dash1, strokeWhere + " dash1");
                check(entry.second->dash2 == it->second->dash2, strokeWhere + " dash2");
                check(entry.second->dotSpace == it->second->dotSpace, strokeWhere + " dotSpace");
                if (entry.second->fill && it->second->fill)
                    compareFills(*entry.second->fill, *it->second->fill, strokeWhere);
            }
        }

        if (!check(a.edges.size() == b.edges.size(), where + " edge count"))
            return;

        for (size_t i = 0; i < a.edges.size(); ++i)
        {
            const std::string edgeWhere = where + " edge " + std::to_string(i);
            check(a.edges[i]->fillStyle0 == b.edges[i]->fillStyle0, edgeWhere + " fillStyle0");
            check(a.edges[i]->fillStyle1 == b.edges[i]->fillStyle1, edgeWhere + " fillStyle1");
            check(a.edges[i]->strokeStyle == b.edges[i]->strokeStyle, edgeWhere + " strokeStyle");

            if (!check(a.edges[i]->paths.size() == b.edges[i]->paths.size(), edgeWhere + " path count"))
                continue;

            for (size_t p = 0; p < a.edges[i]->paths.size(); ++p)
            {
                const fla::Path* pa = a.edges[i]->paths[p];
                const fla::Path* pb = b.edges[i]->paths[p];
                const std::string pathWhere = edgeWhere + " path " + std::to_string(p);

                check(pa->styleIndex == pb->styleIndex, pathWhere + " styleIndex");
                check(pa->fillStyleIndex == pb->fillStyleIndex, pathWhere + " fillStyleIndex");
                check(pa->lineStyleIndex == pb->lineStyleIndex, pathWhere + " lineStyleIndex");

                if (!check(pa->segments.size() == pb->segments.size(), pathWhere + " segment count"))
                    continue;

                for (size_t s = 0; s < pa->segments.size(); ++s)
                {
                    if (!check(pa->segments[s]->command == pb->segments[s]->command,
                            pathWhere + " segment command"))
                        continue;
                    if (!check(pa->segments[s]->points.size() == pb->segments[s]->points.size(),
                            pathWhere + " segment point count"))
                        continue;
                    for (size_t n = 0; n < pa->segments[s]->points.size(); ++n)
                    {
                        check(pa->segments[s]->points[n].x == pb->segments[s]->points[n].x &&
                              pa->segments[s]->points[n].y == pb->segments[s]->points[n].y,
                            pathWhere + " segment point");
                    }
                }
            }
        }
    }

    std::vector<std::string> _differences;
};

/// Parses standalone DOMDocument.xml text. Safe with a null zip reader as long
/// as the text has no <symbols> section, which would need library files.
std::unique_ptr<fla::Document> parseDocumentText(const std::string& xml)
{
    DocumentParser parser(nullptr);
    return std::unique_ptr<fla::Document>(parser.parse(xml, nullptr));
}

/// Parses text, writes it back out, and re-parses, reporting what changed.
void checkRoundTrip(const std::string& xml)
{
    std::unique_ptr<fla::Document> first = parseDocumentText(xml);
    CHECK(first != nullptr);
    if (!first)
        return;

    XFLWriter writer;
    const std::string written = writer.writeDocument(*first);

    std::unique_ptr<fla::Document> second = parseDocumentText(written);
    CHECK(second != nullptr);
    if (!second)
    {
        std::printf("    re-parse failed, written was:\n%s\n", written.c_str());
        return;
    }

    TreeComparer comparer;
    comparer.compareDocuments(*first, *second);
    if (!comparer.equal())
    {
        for (const std::string& difference : comparer.differences())
            std::printf("    differs: %s\n", difference.c_str());
        std::printf("    written:\n%s\n", written.c_str());
    }
    CHECK(comparer.equal());
}

const char* kMinimalDocument =
    "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='640' height='480' "
    "frameRate='30' backgroundColor='#336699' currentTimeline='1' xflVersion='2.971'>"
    "<timelines><DOMTimeline name='Scene 1'><layers>"
    "<DOMLayer name='Layer 1' color='#4FFF4F'><frames>"
    "<DOMFrame index='0' duration='5' keyMode='9728'><elements/></DOMFrame>"
    "</frames></DOMLayer>"
    "</layers></DOMTimeline></timelines></DOMDocument>";

} // namespace

TEST(xfl_round_trips_a_minimal_document)
{
    checkRoundTrip(kMinimalDocument);
}

TEST(xfl_round_trips_frame_labels_and_instance_names)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames>"
        "<DOMFrame index='0' duration='3' name='start' labelType='anchor'><elements>"
        "<DOMSymbolInstance libraryItemName='Sym' name='hero' symbolType='movie clip'>"
        "<matrix><Matrix tx='5' ty='6'/></matrix></DOMSymbolInstance>"
        "</elements></DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document || document->timelines.empty() || document->timelines[0]->layers.empty())
        return;
    const fla::Frame* frame = document->timelines[0]->layers[0]->frames[0];
    CHECK(frame->name == "start");
    CHECK(frame->labelType == "anchor");
    CHECK(frame->elements.size() == 1);
    if (frame->elements.size() == 1)
    {
        const auto* instance = dynamic_cast<const fla::SymbolInstance*>(frame->elements[0]);
        CHECK(instance && instance->name == "hero");
    }

    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("name=\"start\"") != std::string::npos);
    CHECK(written.find("labelType=\"anchor\"") != std::string::npos);
    CHECK(written.find("name=\"hero\"") != std::string::npos);
}

TEST(xfl_writes_document_metadata_back_as_read)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements/></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines>"
        "<PrinterSettings width='612' height='792'/>"
        "<publishHistory><PublishItem publishSize='1234' publishTime='1'/></publishHistory>"
        "<swatchLists><swatchList><swatchListInfo name='Swatches'/><swatches>"
        "<SolidSwatchItem color='#FF0000'/><SolidSwatchItem color='#00FF00' alpha='0.5'/>"
        "</swatches></swatchList></swatchLists>"
        "<scripts><Script name='Document'><script>trace(1);</script></Script></scripts>"
        "</DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;

    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(writer.unsupported().empty());
    CHECK(written.find("<PrinterSettings width=\"612\"") != std::string::npos);
    CHECK(written.find("publishSize=\"1234\"") != std::string::npos);
    CHECK(written.find("<SolidSwatchItem color=\"00FF00\"") != std::string::npos ||
          written.find("<SolidSwatchItem color=\"#00FF00\"") != std::string::npos);
    CHECK(written.find("trace(1);") != std::string::npos);
    // Everything lands in the order Animate writes it.
    CHECK(written.find("<PrinterSettings") < written.find("<publishHistory"));
    CHECK(written.find("<publishHistory") < written.find("<swatchLists"));
    CHECK(written.find("<swatchLists") < written.find("<scripts"));
}

TEST(xfl_round_trips_procedural_stroke_settings)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMShape><strokes>"
        "<StrokeStyle index='1'><HatchedStroke weight='8' hatchThickness='thick' space='close'>"
        "<fill><SolidColor color='#000000'/></fill></HatchedStroke></StrokeStyle>"
        "<StrokeStyle index='2'><RaggedStroke weight='3' waveHeight='wild'>"
        "<fill><SolidColor color='#000000'/></fill></RaggedStroke></StrokeStyle>"
        "</strokes><edges><Edge strokeStyle='1' edges='!0 0|1000 0'/><Edge strokeStyle='2' edges='!0 200|1000 200'/></edges>"
        "</DOMShape></elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("<HatchedStroke") != std::string::npos);
    CHECK(written.find("hatchThickness=\"thick\"") != std::string::npos);
    CHECK(written.find("space=\"close\"") != std::string::npos);
    CHECK(written.find("waveHeight=\"wild\"") != std::string::npos);
}

TEST(xfl_keeps_what_it_does_not_model)
{
    // Sound in the library and on a frame, extended swatches and brush
    // definitions: none of it modeled, all of it written back as read.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<brushdefinitions><Include href='PaintBrushDefinitions.xml'/></brushdefinitions>"
        "<media><DOMSoundItem name='beat.mp3' href='beat.mp3' soundDataHRef='Media 2.dat' format='44kHz 16bit Stereo'/></media>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Sound'><frames>"
        "<DOMFrame index='0' duration='40' keyMode='9728' soundName='beat.mp3' soundSync='stream' inPoint44='100'>"
        "<SoundEnvelope><SoundEnvelopePoint level0='32768' level1='32768'/></SoundEnvelope>"
        "<elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines>"
        "<extendedSwatchLists><swatchList><swatchListInfo name='Default Swatches'/></swatchList></extendedSwatchLists>"
        "</DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("<DOMSoundItem name=\"beat.mp3\"") != std::string::npos);
    CHECK(written.find("soundName=\"beat.mp3\"") != std::string::npos);
    CHECK(written.find("soundSync=\"stream\"") != std::string::npos);
    CHECK(written.find("inPoint44=\"100\"") != std::string::npos);
    CHECK(written.find("<SoundEnvelopePoint level0=\"32768\"") != std::string::npos);
    CHECK(written.find("<extendedSwatchLists>") != std::string::npos);
    CHECK(written.find("<brushdefinitions>") != std::string::npos);
    // Each where Animate puts it: brushes before the timelines, the
    // extended swatches after, the envelope before the frame's elements.
    CHECK(written.find("<brushdefinitions>") < written.find("<timelines>"));
    CHECK(written.find("<extendedSwatchLists>") > written.find("</timelines>"));
    CHECK(written.find("<SoundEnvelope>") < written.find("<elements"));
}

TEST(xfl_keeps_a_strokes_own_attributes)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'><timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements><DOMShape><strokes>"
        "<StrokeStyle index='1'><SolidStroke weight='2' phoenixBrush='Leaf' phoenixBrushPattern='true'>"
        "<fill><SolidColor/></fill></SolidStroke></StrokeStyle></strokes>"
        "<edges><Edge strokeStyle='1' edges='!0 0|2000 0'/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("phoenixBrush=\"Leaf\"") != std::string::npos);
    CHECK(written.find("phoenixBrushPattern=\"true\"") != std::string::npos);
}

TEST(xfl_writes_an_armature_pose_after_the_frame_elements)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Armature_1' animationType='IK pose'><frames>"
        "<DOMFrame index='0' tweenType='IK pose' isIKPose='true' poseLocations='0'>"
        "<elements><DOMShape/></elements>"
        "<betweenFrameMatrixList><Matrix/></betweenFrameMatrixList>"
        "<IKTree name='Armature_1'><states><IKState duration='1'/></states></IKTree>"
        "</DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines>"
        "</DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("<elements") != std::string::npos);
    CHECK(written.find("<elements") < written.find("<betweenFrameMatrixList"));
    CHECK(written.find("<betweenFrameMatrixList") < written.find("<IKTree"));
}

TEST(xfl_keeps_timeline_guides_and_attributes)
{
    // Animate keeps ruler guides as XML inside the DOMTimeline's guides
    // attribute; other timeline attributes Phoenix does not model come back
    // as they were.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1' currentFrame='3' "
        "guides='&lt;guidelines&gt;&lt;guideline direction=\"v\"&gt;120&lt;/guideline&gt;"
        "&lt;guideline direction=\"h\"&gt;-40.5&lt;/guideline&gt;&lt;/guidelines&gt;'>"
        "<layers><DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements/></DOMFrame></frames>"
        "</DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document || document->timelines.empty())
        return;
    const fla::Timeline* timeline = document->timelines.front();
    CHECK(timeline->guidesX.size() == 1 && timeline->guidesX[0] == 120.0);
    CHECK(timeline->guidesY.size() == 1 && timeline->guidesY[0] == -40.5);

    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("currentFrame=\"3\"") != std::string::npos);
    std::unique_ptr<fla::Document> again = parseDocumentText(written);
    CHECK(again != nullptr);
    if (!again || again->timelines.empty())
        return;
    CHECK(again->timelines.front()->guidesX == timeline->guidesX);
    CHECK(again->timelines.front()->guidesY == timeline->guidesY);

    // A guides attribute that is not what Phoenix reads is kept as text.
    document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1' guides='something else'><layers/></DOMTimeline>"
        "</timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (document)
        CHECK(writer.writeDocument(*document).find("guides=\"something else\"") != std::string::npos);
}

TEST(xfl_round_trips_shapes)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMShape>"
        "<fills>"
        "<FillStyle index='1'><SolidColor color='#FF0000' alpha='0.5'/></FillStyle>"
        "<FillStyle index='2'><LinearGradient>"
        "<matrix><Matrix a='2' b='0' c='0' d='2' tx='10' ty='20'/></matrix>"
        "<GradientEntry color='#000000' ratio='0'/>"
        "<GradientEntry color='#FFFFFF' ratio='1'/>"
        "</LinearGradient></FillStyle>"
        "</fills>"
        "<strokes>"
        "<StrokeStyle index='1'><SolidStroke weight='2.5' scaleMode='normal'>"
        "<fill><SolidColor color='#00FF00'/></fill></SolidStroke></StrokeStyle>"
        "</strokes>"
        "<edges>"
        "<Edge fillStyle1='1' strokeStyle='1' edges='!0 0|1000 0|1000 1000|0 1000|0 0'/>"
        "<Edge fillStyle0='2' edges='!0 0[100 200 300 0'/>"
        "</edges>"
        "</DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_stroke_attributes_and_drawing_objects)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMShape isDrawingObject='true'>"
        "<strokes>"
        "<StrokeStyle index='1'><SolidStroke scaleMode='normal' solidStyle='hairline'>"
        "<fill><SolidColor color='#00FF00'/></fill></SolidStroke></StrokeStyle>"
        "<StrokeStyle index='2'><SolidStroke weight='4' caps='none' joints='miter' "
        "miterLimit='5' pixelHinting='true'>"
        "<fill><SolidColor/></fill></SolidStroke></StrokeStyle>"
        "<StrokeStyle index='3'><DashedStroke weight='2' dash1='10' dash2='4'>"
        "<fill><SolidColor/></fill></DashedStroke></StrokeStyle>"
        "<StrokeStyle index='4'><DottedStroke weight='3' dotSpace='7'>"
        "<fill><SolidColor/></fill></DottedStroke></StrokeStyle>"
        "</strokes>"
        "<edges>"
        "<Edge strokeStyle='1' edges='!0 0|1000 0'/>"
        "<Edge strokeStyle='2' edges='!0 0|0 1000'/>"
        "<Edge strokeStyle='3' edges='!0 0|1000 1000'/>"
        "<Edge strokeStyle='4' edges='!1000 0|0 1000'/>"
        "</edges>"
        "</DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_symbol_instances_and_groups)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMSymbolInstance libraryItemName='Hero' symbolType='graphic' "
        "loop='loop' firstFrame='3'>"
        "<matrix><Matrix a='1' b='0' c='0' d='1' tx='100' ty='50'/></matrix>"
        "<transformationPoint><Point x='5' y='7'/></transformationPoint>"
        "<color><Color alphaMultiplier='0.25' brightness='0.5'/></color>"
        "</DOMSymbolInstance>"
        "<DOMGroup><members>"
        "<DOMShape><edges><Edge edges='!0 0|100 0'/></edges></DOMShape>"
        "<DOMBitmapInstance libraryItemName='sky.png'/>"
        "</members></DOMGroup>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_primitives_and_text)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMRectangleObject x='10' y='20' objectWidth='30' objectHeight='40' "
        "topLeftRadius='5' topRightRadius='6' bottomLeftRadius='7' bottomRightRadius='-8'>"
        "<fill><SolidColor color='#123456'/></fill>"
        "<stroke><SolidStroke weight='3'/></stroke>"
        "</DOMRectangleObject>"
        "<DOMOvalObject x='1' y='2' objectWidth='3' objectHeight='4' "
        "startAngle='10' endAngle='200' innerRadius='25'>"
        "<fill><SolidColor color='#654321'/></fill>"
        "</DOMOvalObject>"
        "<DOMStaticText left='5' top='6' width='200' height='30'>"
        "<textRuns><DOMTextRun>"
        "<characters>Hello &amp; goodbye</characters>"
        "<textAttrs><DOMTextAttrs face='Arial' size='24' alignment='center' "
        "fillColor='#FF00FF' lineHeight='28'/></textAttrs>"
        "</DOMTextRun></textRuns>"
        "</DOMStaticText>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_dynamic_and_input_text)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMDynamicText left='5' top='6' width='200' height='30' instanceName='score_txt' lineType='multiline'>"
        "<textRuns><DOMTextRun><characters>0</characters>"
        "<textAttrs><DOMTextAttrs face='ArialMT' size='24'/></textAttrs>"
        "</DOMTextRun></textRuns>"
        "</DOMDynamicText>"
        "<DOMInputText left='5' top='40' width='200' height='30' instanceName='name_txt'>"
        "<textRuns><DOMTextRun><characters>type here</characters>"
        "<textAttrs><DOMTextAttrs face='ArialMT' size='12'/></textAttrs>"
        "</DOMTextRun></textRuns>"
        "</DOMInputText>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");

    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMInputText left='5' top='40' width='200' height='30' instanceName='name_txt'>"
        "<textRuns><DOMTextRun><characters>x</characters></DOMTextRun></textRuns>"
        "</DOMInputText>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (document)
    {
        XFLWriter writer;
        const std::string written = writer.writeDocument(*document);
        CHECK(written.find("<DOMInputText") != std::string::npos);
        CHECK(written.find("instanceName=\"name_txt\"") != std::string::npos);
    }
}

TEST(xfl_round_trips_bitmap_fills_and_gradient_spread)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMShape><fills>"
        "<FillStyle index='1'><BitmapFill bitmapPath='photo.png' bitmapIsClipped='true'>"
        "<matrix><Matrix a='20' d='20' tx='5' ty='6'/></matrix></BitmapFill></FillStyle>"
        "<FillStyle index='2'><LinearGradient spreadMethod='reflect' interpolationMethod='linearRGB'>"
        "<matrix><Matrix a='0.1' d='0.1'/></matrix>"
        "<GradientEntry color='#FF0000' ratio='0'/><GradientEntry color='#0000FF' ratio='1'/>"
        "</LinearGradient></FillStyle>"
        "</fills><edges>"
        "<Edge fillStyle1='1' edges='!0 0|2000 0|2000 2000|0 2000|0 0'/>"
        "<Edge fillStyle1='2' edges='!3000 0|5000 0|5000 2000|3000 2000|3000 0'/>"
        "</edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_filters_blend_and_easing)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames>"
        "<DOMFrame index='0' duration='5' tweenType='motion' motionTweenSnap='true' "
        "motionTweenRotate='clockwise' motionTweenRotateTimes='2' acceleration='-60'>"
        "<tweens><Ease target='all' intensity='-60'/></tweens>"
        "<elements>"
        "<DOMSymbolInstance libraryItemName='Hero' blendMode='multiply'>"
        "<filters>"
        "<BlurFilter blurX='8' blurY='2' quality='3'/>"
        "<DropShadowFilter blurX='4' blurY='4' angle='90' distance='10' color='#FF0000' alpha='0.5' strength='2' knockout='true'/>"
        "<GlowFilter color='#00FF00' inner='true' enabled='false'/>"
        "<BevelFilter highlightColor='#FFFF00' shadowColor='#0000FF' shadowAlpha='0.25' type='full'/>"
        "<AdjustColorFilter brightness='10' contrast='-20' saturation='30' hue='-40'/>"
        "<GradientGlowFilter blurX='3' blurY='3'><GradientEntry color='#FFFFFF' alpha='0' ratio='0'/><GradientEntry color='#FF00FF' ratio='1'/></GradientGlowFilter>"
        "</filters>"
        "</DOMSymbolInstance>"
        "</elements></DOMFrame>"
        "<DOMFrame index='5' duration='5' tweenType='motion' hasCustomEase='true'>"
        "<tweens><CustomEase target='all'><Point/><Point x='0.3889'/><Point x='0.5488' y='1'/><Point x='1' y='1'/></CustomEase></tweens>"
        "<elements><DOMSymbolInstance libraryItemName='Hero'/></elements></DOMFrame>"
        "<DOMFrame index='10'><elements><DOMSymbolInstance libraryItemName='Hero'/></elements></DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_round_trips_layer_structure)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Folder' layerType='folder' autoNamed='false'>"
        "<frames/></DOMLayer>"
        "<DOMLayer name='Mask' layerType='mask' locked='true' visible='false' "
        "parentLayerIndex='0'><frames/></DOMLayer>"
        "<DOMLayer name='Masked' layerType='masked' parentLayerIndex='1'>"
        "<frames><DOMFrame index='0' duration='10' tweenType='motion'><elements/></DOMFrame>"
        "<DOMFrame index='10' tweenType='shape'><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
}

TEST(xfl_reports_unsupported_content)
{
    // The writer must not drop things in silence. A swatch list is written
    // back from the text it was read with; one that has none (made in code)
    // cannot be written, and saving a document with it has to say so.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines/>"
        "<swatchLists><swatchList><swatchListInfo name='Default'/>"
        "<swatches><DOMSwatchItem color='#FF0000'/></swatches></swatchList></swatchLists>"
        "</DOMDocument>");
    CHECK(document != nullptr);
    if (!document || document->swatchLists.empty())
        return;

    XFLWriter writer;
    writer.writeDocument(*document);
    CHECK(writer.unsupported().empty());

    document->swatchLists[0]->xml.clear();
    writer.clearUnsupported();
    writer.writeDocument(*document);
    CHECK(!writer.unsupported().empty());
}

TEST(xfl_symbol_href_falls_back_to_the_name)
{
    fla::Symbol symbol(nullptr);
    symbol.name = "Hero";
    CHECK(XFLWriter::symbolHref(symbol) == "Hero.xml");

    // A symbol read from a file keeps the href the Include used, which need not
    // match the name.
    symbol.href = "Characters/Hero v2.xml";
    CHECK(XFLWriter::symbolHref(symbol) == "Characters/Hero v2.xml");
}

TEST(xfl_round_trips_real_documents)
{
    // Opt-in corpus check. Point PHOENIX_FLA_CORPUS at a directory of .fla files
    // or extracted XFL folders to exercise the writer against real content.
    //
    // The document is written as a whole XFL folder rather than just
    // DOMDocument.xml, because symbols live in separate library files and the
    // reader needs them to resolve the Include entries on the way back in.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "phoenix_roundtrip";
    std::error_code ignored;
    std::filesystem::remove_all(scratch, ignored);

    int checked = 0;
    int failed = 0;
    std::vector<std::string> unsupported;

    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        std::string path;
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "DOMDocument.xml"))
            path = entry.path().string();
        else if (entry.is_regular_file() && entry.path().extension() == ".fla")
            path = entry.path().string();
        else
            continue;

        FLAParser parser;
        std::unique_ptr<fla::FLADocument> first(parser.parse(path));
        if (!first || !first->document)
        {
            std::printf("    could not read %s, skipping\n", path.c_str());
            continue;
        }

        const std::filesystem::path out = scratch / entry.path().stem();
        fla::XFLFolderWriter folderWriter;
        if (!folderWriter.write(*first->document, out.string()))
        {
            std::printf("    %s: write failed: %s\n", path.c_str(),
                folderWriter.errorString().c_str());
            CHECK(false);
            ++failed;
            continue;
        }

        FLAParser reparser;
        std::unique_ptr<fla::FLADocument> second(reparser.parse(out.string()));
        if (!second || !second->document)
        {
            std::printf("    %s: re-read failed: %s\n", path.c_str(),
                reparser.errorString().c_str());
            CHECK(false);
            ++failed;
            continue;
        }

        for (const std::string& description : folderWriter.unsupported())
        {
            bool seen = false;
            for (const std::string& existing : unsupported)
                seen = seen || existing == description;
            if (!seen)
                unsupported.push_back(description);
        }

        TreeComparer comparer;
        comparer.compareDocuments(*first->document, *second->document);
        if (!comparer.equal())
        {
            std::printf("    %s\n", path.c_str());
            for (const std::string& difference : comparer.differences())
                std::printf("      differs: %s\n", difference.c_str());
            ++failed;
        }
        CHECK(comparer.equal());
        ++checked;
    }

    std::printf("    round-tripped %d real documents, %d with differences\n", checked, failed);
    for (const std::string& description : unsupported)
        std::printf("    not written back: %s\n", description.c_str());
    CHECK(checked > 0);
}

/// A document Phoenix built itself, rather than one it read, still has to come
/// out of the writer as valid XFL. Everything else here starts from parsed text,
/// so nothing would otherwise catch a new document that cannot be saved.
TEST(xfl_writes_a_new_document)
{
    std::unique_ptr<fla::FLADocument> created(fla::createEmptyDocument());
    CHECK(created != nullptr && created->document != nullptr);
    if (!created || !created->document)
        return;

    XFLWriter writer;
    const std::string written = writer.writeDocument(*created->document);

    std::unique_ptr<fla::Document> reparsed = parseDocumentText(written);
    CHECK(reparsed != nullptr);
    if (!reparsed)
    {
        std::printf("    re-parse failed, written was:\n%s\n", written.c_str());
        return;
    }

    TreeComparer comparer;
    comparer.compareDocuments(*created->document, *reparsed);
    if (!comparer.equal())
    {
        for (const std::string& difference : comparer.differences())
            std::printf("    differs: %s\n", difference.c_str());
        std::printf("    written:\n%s\n", written.c_str());
    }
    CHECK(comparer.equal());
}

TEST(ik_bones_are_kept_on_save)
{
    // Opt-in: the corpus's ik_bones.fla, a shape with four bones saved by
    // Animate 24. Phoenix does not model IK yet, so everything about the
    // armature has to come back out as it went in.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    const std::filesystem::path source = corpus ? std::filesystem::path(corpus) / "ik_bones.fla"
                                                : std::filesystem::path();
    if (!corpus || !std::filesystem::exists(source))
    {
        std::printf("    skipped: no ik_bones.fla in PHOENIX_FLA_CORPUS\n");
        return;
    }
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> fla(parser.parse(source.string()));
    CHECK(fla && fla->document);
    if (!fla || !fla->document)
        return;

    XFLWriter writer;
    const std::string document = writer.writeDocument(*fla->document);
    for (const char* kept : {"animationType=\"IK pose\"", "tweenType=\"IK pose\"", "isIKPose=\"true\"",
             "poseLocations=\"0\"", "<betweenFrameMatrixList>", "<IKTree name=\"Armature_11\"",
             "connectionIndices=\"9.000000,0.000000", "physicsEnabled=\"true\"", "<IKNode name=\"ikNode_11\"",
             "name=\"ikNode_17\" boneName=\"ikBoneName4\"", "angle=\"-1.6424436004084\"", "<IKState duration=\"1\"",
             "<IKContour interior=\"true\"", "<DataPoint integerPoint=\"#20.37, #10.A3\"",
             "<PD n=\"ikBoneCount\" t=\"i\" v=\"4\"/>"})
    {
        const bool found = document.find(kept) != std::string::npos;
        if (!found)
            std::printf("    lost on save: %s\n", kept);
        CHECK(found);
    }
    // The boned shape's symbol keeps its kind.
    CHECK(fla->document->symbolList && !fla->document->symbolList->symbols.empty());
    if (fla->document->symbolList && !fla->document->symbolList->symbols.empty())
    {
        const std::string symbol = writer.writeSymbol(*fla->document->symbolList->symbols.front());
        const bool kind = symbol.find("symbolType=\"ik container\"") != std::string::npos;
        if (!kind)
            std::printf("    lost on save: the symbol's ik container type\n");
        CHECK(kind);
    }
}

TEST(ik_symbol_armatures_and_poses_are_kept_on_save)
{
    // Opt-in, with the corpus's ik_symbols.fla (bones joining three movie
    // clips) and ik_poses.fla (a shape's bones posed twice, eased).
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus)
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS\n");
        return;
    }
    const auto check = [corpus](const char* file, std::vector<const char*> kept) {
        const std::filesystem::path source = std::filesystem::path(corpus) / file;
        if (!std::filesystem::exists(source))
        {
            std::printf("    skipped: no %s\n", file);
            return;
        }
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> fla(parser.parse(source.string()));
        CHECK(fla && fla->document);
        if (!fla || !fla->document)
            return;
        XFLWriter writer;
        const std::string document = writer.writeDocument(*fla->document);
        for (const char* text : kept)
        {
            const bool found = document.find(text) != std::string::npos;
            if (!found)
                std::printf("    %s lost on save: %s\n", file, text);
            CHECK(found);
        }
    };
    // The bones name the instances they move by referenceID, which the
    // instances carry too.
    check("ik_symbols.fla", {"referenceID=\"5AAD26F1-D8F0-F145-A300-F35454B87A46\" selected",
        "name=\"ikNode_2\"", "animationSymbolMode=\"2\"", "animationFrameOffset=\"0\"",
        "<IKTree name=\"Armature_1\"", "<Matrix a=\"0.999954223632812\" d=\"0.999954223632812\" tx=\"0.05\"/>"});
    check("ik_poses.fla", {"poseLocations=\"0,29\"", "easeType=\"simple quadratic\"", "easeValue=\"50\"",
        "<State duration=\"29\"", "xArray=\"15212.505720,15317.849479"});
}

TEST(symbol_instances_keep_their_attributes_and_write_loop_as_animate_does)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers><DOMLayer name='L'><frames><DOMFrame index='0'><elements>"
        "<DOMSymbolInstance libraryItemName='A' symbolType='graphic' referenceID='ABC-1' centerPoint3DX='12.5' cacheAsBitmap='true' loop='single frame'/>"
        "<DOMSymbolInstance libraryItemName='B' symbolType='button' trackAsMenu='true'/>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    for (const char* kept : {"referenceID=\"ABC-1\"", "centerPoint3DX=\"12.5\"", "cacheAsBitmap=\"true\"",
             "trackAsMenu=\"true\"", "loop=\"single frame\""})
        CHECK(written.find(kept) != std::string::npos);
    // Animate's attribute is loop; a button has none.
    CHECK(written.find("loopType") == std::string::npos);
    CHECK(written.find("libraryItemName=\"B\" symbolType=\"button\" loop") == std::string::npos);
}

TEST(xfl_keeps_an_instance_script)
{
    // A button's script lives on its instance; it goes back on save.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Buttons'><frames><DOMFrame index='0' keyMode='9728'><elements>"
        "<DOMSymbolInstance libraryItemName='play' symbolType='button'>"
        "<Actionscript><script><![CDATA[on (release) { play(); }]]></script></Actionscript>"
        "</DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("on (release) { play(); }") != std::string::npos);
    CHECK(written.find("<Actionscript>") < written.find("</DOMSymbolInstance>"));
}

namespace {

/// How often each element, and each attribute on each element, occurs in a
/// piece of XML: "Edge" and "Edge@fillStyle1". What a save loses shows up as
/// a count that went down.
void countXml(const tinyxml2::XMLElement* element, std::map<std::string, int>& counts,
    std::map<std::string, std::string>& values)
{
    for (; element; element = element->NextSiblingElement())
    {
        const std::string name = element->Name();
        // An empty list carries nothing: Animate writes some, Phoenix leaves
        // them out.
        const bool emptyList = (name == "media" || name == "symbols" || name == "folders") &&
            !element->FirstChildElement() && !element->FirstAttribute();
        if (!emptyList)
            ++counts[name];
        for (const tinyxml2::XMLAttribute* attribute = element->FirstAttribute(); attribute;
             attribute = attribute->Next())
        {
            // A matrix entry at its identity value says nothing; Phoenix
            // leaves those out as Animate does.
            if (name == "Matrix")
            {
                const std::string entry = attribute->Name();
                const double value = attribute->DoubleValue();
                if (((entry == "a" || entry == "d") && value == 1.0) ||
                    ((entry == "b" || entry == "c" || entry == "tx" || entry == "ty") && value == 0.0))
                    continue;
            }
            const std::string key = name + "@" + attribute->Name();
            ++counts[key];
            values.emplace(key, attribute->Value());
        }
        countXml(element->FirstChildElement(), counts, values);
    }
}

std::map<std::string, int> countXml(const std::string& text, std::map<std::string, std::string>& values)
{
    std::map<std::string, int> counts;
    tinyxml2::XMLDocument xml;
    if (xml.Parse(text.c_str(), text.size()) == tinyxml2::XML_SUCCESS)
        countXml(xml.FirstChildElement(), counts, values);
    return counts;
}

} // namespace

TEST(xfl_save_keeps_every_element_and_attribute_of_real_documents)
{
    // Opt-in, with PHOENIX_FLA_CORPUS. The round trip above compares what
    // Phoenix parses, so it cannot see what the parser never kept. This
    // compares the files themselves: every file of the package comes back,
    // and in every XML file each element and attribute occurs as often as it
    // did. Losses are added up over the corpus and reported together.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "phoenix_keeps";
    std::error_code ignored;
    std::filesystem::remove_all(scratch, ignored);

    struct Loss
    {
        int count = 0;
        std::string example;
    };
    std::map<std::string, Loss> losses;
    int checked = 0;

    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        const bool folder = entry.is_directory() && std::filesystem::exists(entry.path() / "DOMDocument.xml");
        if (!folder && !(entry.is_regular_file() && entry.path().extension() == ".fla"))
            continue;
        ZipReader source;
        if (!source.open(entry.path().string()) || !source.containsFile("DOMDocument.xml"))
            continue; // an old binary FLA, or an archive holding a folder
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> fla(parser.parse(entry.path().string()));
        if (!fla || !fla->document)
            continue;
        const std::filesystem::path out = scratch / entry.path().stem();
        fla::XFLFolderWriter writer;
        if (!writer.write(*fla->document, out.string()))
            continue;
        ZipReader written;
        CHECK(written.open(out.string()));
        ++checked;
        const std::string name = entry.path().filename().string();
        for (const std::string& file : source.getFileList())
        {
            if (file.empty() || file.back() == '/' || file == "bin/SymDepend.cache")
                continue;
            if (file.size() > 4 && file.substr(file.size() - 4) == ".xfl")
                continue; // the marker is named after the document
            // A folder written on Windows makes a backslash in a library path
            // a folder of its own, where a zip would keep the name as it is.
            std::string inFolder = file;
            std::replace(inFolder.begin(), inFolder.end(), '\\', '/');
            if (!written.containsFile(file) && !written.containsFile(inFolder))
            {
                Loss& loss = losses["file " + file.substr(0, file.find('/') + 1)];
                ++loss.count;
                loss.example = name + ": " + file;
                continue;
            }
            if (file.size() < 4 || file.substr(file.size() - 4) != ".xml")
                continue;
            std::map<std::string, std::string> values;
            std::map<std::string, std::string> writtenValues;
            const std::map<std::string, int> before = countXml(source.readTextFile(file), values);
            const std::map<std::string, int> after = countXml(written.readTextFile(file), writtenValues);
            for (const auto& [key, count] : before)
            {
                const auto found = after.find(key);
                const int now = found == after.end() ? 0 : found->second;
                if (now < count)
                {
                    Loss& loss = losses[key];
                    loss.count += count - now;
                    loss.example = name + ": " + file;
                    const auto value = values.find(key);
                    if (value != values.end())
                        loss.example += " (" + value->second.substr(0, 40) + ")";
                }
            }
        }
    }

    std::printf("    checked %d documents\n", checked);
    for (const auto& [key, loss] : losses)
        std::printf("    lost on save: %s x%d (e.g. %s)\n", key.c_str(), loss.count, loss.example.c_str());
    CHECK(checked > 0);
    CHECK(losses.empty());
}

TEST(xfl_save_keeps_animates_defaults_and_values_as_meant)
{
    // Animate leaves out only its own defaults, which are not always the
    // ones Phoenix would pick: what comes back has to mean the same to it.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/' width='550' height='400' "
        "viewAngle3D='92.3090013472118' playOptionsPlayLoop='false' objectsSnapTo='false' gridSpacingX='18' "
        "rulerUnitType='inches'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1' color='#000000'><frames>"
        "<DOMFrame index='0' duration='10' tweenType='motion' keyMode='22017'>"
        "<tweens><Ease target='all' method='quadInOut'/><Ease target='position' intensity='50'/></tweens>"
        "<elements>"
        "<DOMStaticText selected='true' isSelectable='false'><textRuns>"
        "<DOMTextRun><characters>kerned</characters><textAttrs><DOMTextAttrs aliasText='false' bold='true'/></textAttrs></DOMTextRun>"
        "<DOMTextRun><characters>not</characters><textAttrs><DOMTextAttrs autoKern='false' alignment='justify'/></textAttrs></DOMTextRun>"
        "</textRuns></DOMStaticText>"
        "<DOMTLFText left='1'><tlfTextObject/></DOMTLFText>"
        "<DOMShape selected='true'><transformationPoint><Point/></transformationPoint></DOMShape>"
        "</elements></DOMFrame>"
        "<DOMFrame index='10' keyMode='9728'><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    CHECK_NEAR(document->viewAngle3D, 92.3090013472118);
    CHECK(!document->playOptionsPlayLoop);
    CHECK(document->playOptionsPlayPages);
    const fla::Frame* frame = document->timelines[0]->layers[0]->frames[0];
    CHECK(frame->easeMethod == "quadInOut");
    CHECK(frame->elements.size() == 3);
    if (frame->elements.size() == 3)
    {
        const fla::StaticText* text = static_cast<const fla::StaticText*>(frame->elements[0]);
        CHECK(text->runs.size() == 2 && text->runs[0].autoKern && !text->runs[1].autoKern);
        CHECK(text->runs.size() == 2 && text->runs[1].alignment == fla::TextRun::Alignment::Justify);
        CHECK(frame->elements[1]->elementType() == fla::Element::Type::Raw);
    }

    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    for (const char* kept : {"viewAngle3D=\"92.3090013472118\"", "playOptionsPlayLoop=\"false\"",
             "objectsSnapTo=\"false\"", "gridSpacingX=\"18\"", "rulerUnitType=\"inches\"", "color=\"#000000\"",
             "method=\"quadInOut\"", "<Ease target=\"position\" intensity=\"50\"/>", "isSelectable=\"false\"",
             "bold=\"true\"", "autoKern=\"false\"", "alignment=\"justify\"", "<DOMTLFText left=\"1\">",
             "<transformationPoint>"})
    {
        const bool found = written.find(kept) != std::string::npos;
        if (!found)
            std::printf("    lost on save: %s\n", kept);
        CHECK(found);
    }
    // The first run kerns, Animate's default, so it says nothing about it.
    const size_t first = written.find("kerned");
    CHECK(first != std::string::npos && written.find("autoKern", first) > written.find("</DOMTextRun>", first));
    // The unknown element stays between the text and the shape.
    CHECK(written.find("<DOMTLFText") > written.find("DOMStaticText") &&
          written.find("<DOMTLFText") < written.find("<DOMShape"));
}

TEST(variable_width_profiles_are_read_and_kept)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'><timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements><DOMShape><strokes>"
        "<StrokeStyle index='1'><SolidStroke weight='2'><fill><SolidColor color='#FFFFCC'/></fill>"
        "<VariablePointWidth><WidthMarker position='1' left='0' right='0' type='corner'/>"
        "<WidthMarker position='0' left='0' right='0' type='corner'/><WidthMarker position='0.5' left='0.5' right='0.5'/>"
        "</VariablePointWidth></SolidStroke></StrokeStyle></strokes>"
        "<edges><Edge strokeStyle='1' edges='!0 0|2000 0'/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    const fla::Shape* shape = static_cast<const fla::Shape*>(document->timelines[0]->layers[0]->frames[0]->elements[0]);
    CHECK(shape->strokes.size() == 1);
    if (shape->strokes.size() != 1)
        return;
    const auto& profile = shape->strokes[0]->widthProfile;
    // In order along the stroke, whatever order the file gave.
    CHECK(profile.size() == 3);
    if (profile.size() == 3)
    {
        CHECK(profile[0].position == 0.0 && profile[0].corner);
        CHECK(profile[1].position == 0.5 && profile[1].left == 0.5 && !profile[1].corner);
        CHECK(profile[2].position == 1.0);
    }
    XFLWriter writer;
    CHECK(writer.writeDocument(*document).find("<WidthMarker position=\"0.5\" left=\"0.5\" right=\"0.5\"/>") !=
        std::string::npos);
}

TEST(an_instance_with_visible_off_is_hidden_and_kept_so)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'><timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMSymbolInstance libraryItemName='Ball' isVisible='false'/><DOMSymbolInstance libraryItemName='Ball'/>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;
    const auto& elements = document->timelines[0]->layers[0]->frames[0]->elements;
    CHECK(elements.size() == 2 && !elements[0]->visible && elements[1]->visible);
    XFLWriter writer;
    CHECK(writer.writeDocument(*document).find("isVisible=\"false\"") != std::string::npos);
}

TEST(xfl_moves_an_instances_3d_center_point_with_it)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers><DOMLayer name='L'><frames><DOMFrame index='0'><elements>"
        "<DOMSymbolInstance libraryItemName='A' centerPoint3DX='1366.8' centerPoint3DY='862.95'>"
        "<matrix><Matrix a='2' d='2' tx='1230.75' ty='754.55'/></matrix></DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document || document->timelines.empty())
        return;
    auto* instance = dynamic_cast<fla::SymbolInstance*>(document->timelines[0]->layers[0]->frames[0]->elements[0]);
    CHECK(instance && instance->hasReadMatrix);
    if (!instance)
        return;
    XFLWriter writer;
    // Unmoved, the point is written as read.
    std::string written = writer.writeDocument(*document);
    CHECK(written.find("centerPoint3DX=\"1366.8\"") != std::string::npos);
    CHECK(written.find("centerPoint3DY=\"862.95\"") != std::string::npos);
    // Moved 10 to the right and squeezed to half its width, the point follows.
    instance->transform.m11 = 1.0;
    instance->transform.tx += 10.0;
    written = writer.writeDocument(*document);
    CHECK(written.find("centerPoint3DX=\"1308.775\"") != std::string::npos);
    CHECK(written.find("centerPoint3DY=\"862.95\"") != std::string::npos);
}

TEST(tlf_text_opens_as_classic_text)
{
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers><DOMLayer name='L'><frames><DOMFrame index='0'><elements>"
        "<DOMTLFText name='score' right='2000' bottom='800'>"
        "<matrix><Matrix tx='30' ty='40'/></matrix>"
        "<markup><![CDATA[<tlfTextObject><TextFlow fontFamily='Verdana' fontSize='20' "
        "xmlns='http://ns.adobe.com/textLayout/2008'>"
        "<p textAlign='center'><span color='#FF0000'>Hello </span><span fontWeight='bold'>world</span></p>"
        "<p><span fontSize='10' trackingRight='50%'>Second</span></p>"
        "</TextFlow></tlfTextObject>]]></markup>"
        "</DOMTLFText>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    CHECK(document != nullptr);
    if (!document || document->timelines.empty())
        return;
    const auto* text =
        dynamic_cast<const fla::StaticText*>(document->timelines[0]->layers[0]->frames[0]->elements[0]);
    CHECK(text != nullptr);
    if (!text)
        return;
    CHECK(text->transform.tx == 30.0 && text->transform.ty == 40.0);
    CHECK(text->width == 100.0 && text->height == 40.0 && !text->autoExpand);
    CHECK(text->instanceName == "score");
    CHECK(text->textType == fla::StaticText::TextType::Dynamic);
    CHECK(text->runs.size() == 3);
    if (text->runs.size() != 3)
        return;
    CHECK(text->runs[0].text == "Hello ");
    CHECK(text->runs[0].face == "Verdana" && text->runs[0].size == 20.0);
    CHECK(text->runs[0].fillColor[0] == 255 && text->runs[0].fillColor[1] == 0);
    CHECK(text->runs[0].alignment == fla::TextRun::Alignment::Center);
    CHECK(text->runs[1].text == "world\r");
    CHECK(text->runs[1].face == "Verdana-Bold");
    CHECK(text->runs[2].text == "Second");
    CHECK(text->runs[2].size == 10.0 && text->runs[2].letterSpacing == 5.0);
    CHECK(text->runs[2].alignment == fla::TextRun::Alignment::Left);

    // Saved as classic text.
    XFLWriter writer;
    const std::string written = writer.writeDocument(*document);
    CHECK(written.find("DOMTLFText") == std::string::npos);
    CHECK(written.find("DOMDynamicText") != std::string::npos);
}
