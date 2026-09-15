#include "test_util.h"

#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/group.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/oval_primitive.h"
#include "../src/data/radial_gradient.h"
#include "../src/data/rectangle_primitive.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/fla_parser.h"
#include "../src/writer/xfl_folder_writer.h"
#include "../src/writer/xfl_writer.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
        check(a.tweenType == b.tweenType, where + " tweenType");
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
            check(sa.firstFrame == sb.firstFrame, where + " firstFrame");
            check(sa.symbolType == sb.symbolType, where + " symbolType");
            check(sa.loopType == sb.loopType, where + " loopType");
            check(sa.colorTransform.alphaMultiplier == sb.colorTransform.alphaMultiplier,
                where + " alphaMultiplier");
            check(sa.colorTransform.brightness == sb.colorTransform.brightness, where + " brightness");
            check(sa.colorTransform.tintMultiplier == sb.colorTransform.tintMultiplier,
                where + " tintMultiplier");
            compareColor(sa.colorTransform.tintColor, sb.colorTransform.tintColor, where + " tintColor");
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
            break;
        }
    }

    void compareShapes(const fla::Shape& a, const fla::Shape& b, const std::string& where)
    {
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

TEST(xfl_round_trips_symbol_instances_and_groups)
{
    checkRoundTrip(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines><DOMTimeline name='Scene 1'><layers>"
        "<DOMLayer name='Layer 1'><frames><DOMFrame index='0'><elements>"
        "<DOMSymbolInstance libraryItemName='Hero' symbolType='movie clip' "
        "loopType='loop' firstFrame='3'>"
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
        "<DOMRectangleObject x='10' y='20' objectWidth='30' objectHeight='40'>"
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
    // The writer must not drop things in silence. Swatch lists are read but not
    // yet written, so saving a document with them has to say so.
    std::unique_ptr<fla::Document> document = parseDocumentText(
        "<DOMDocument xmlns='http://ns.adobe.com/xfl/2008/'>"
        "<timelines/>"
        "<swatchLists><swatchList><swatchListInfo name='Default'/>"
        "<swatches><DOMSwatchItem color='#FF0000'/></swatches></swatchList></swatchLists>"
        "</DOMDocument>");
    CHECK(document != nullptr);
    if (!document)
        return;

    XFLWriter writer;
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
