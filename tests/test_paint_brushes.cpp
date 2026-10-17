#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/edge.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/stroke_style.h"
#include "../src/data/timeline.h"
#include "../src/edit/paint_brushes.h"
#include "../src/parser/fla_parser.h"
#include "../src/phx/phx_document.h"
#include "../src/writer/fla_writer.h"
#include "../src/writer/xfl_folder_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;

using P = fla::PaintBrushArt::Point;

// A black bar 100 by 10, the art of an art brush; and a tile 20 by 10 whose
// left half is a blue square, a pattern brush's.
const char* const kArtJson =
    R"({"FlashArtBrushMinorVersion":"0","FlashArtBrushVersion":"1","FlashPaintBrushType":"ArtBrush",)"
    R"("artDirection":"0","diameter":"78.71","flipArt":"false","name":"Bar","rightEdgePercentage":"100.00",)"
    R"("leftEdgePercentage":"0.00","scaleOptionm":"1","svgData":"<svg xmlns=\"http://www.w3.org/2000/svg\" )"
    R"(viewBox=\"0 0 100 10\">\r\n<rect width=\"100\" height=\"10\"/></svg>"})";
const char* const kPatternJson =
    R"({"FlashPaintBrushType":"PatternBrush","FlipAcross":"0","FlipAlong":"0","adjustment":"0","cornerValue":"2",)"
    R"("name":"Squares","spacing":"0","svgData":"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 20 10\">)"
    R"(<rect width=\"10\" height=\"10\" style=\"fill:#0000FF;\"/><rect width=\"20\" height=\"10\" style=\"fill:none;\"/></svg>"})";

/// \a json as an XML attribute value in single quotes, as the file has it.
std::string attribute(const std::string& json)
{
    std::string out;
    for (const char c : json)
    {
        if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '&')
            out += "&amp;";
        else
            out += c;
    }
    return out;
}

std::string definitionsXml()
{
    return "<paintbrushdefinitions xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
           "xmlns=\"http://ns.adobe.com/xfl/2008/\">\n"
           "  <paintbrushdefinition index=\"1\" brushParam='" + attribute(kArtJson) + "'/>\n"
           "  <paintbrushdefinition index=\"2\" brushParam='" + attribute(kPatternJson) + "'/>\n"
           "</paintbrushdefinitions>\n";
}

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// An XFL document with two strokes, each drawn with one of the brushes.
fs::path writeSample(const char* name)
{
    const fs::path folder = fs::temp_directory_path() / "phoenix_paint_brush_tests" / name;
    fs::remove_all(folder);
    writeText(folder / "PaintBrushDefinitions.xml", definitionsXml());
    writeText(folder / "DOMDocument.xml",
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"550\" height=\"400\">"
        "<brushdefinitions><Include href=\"PaintBrushDefinitions.xml\"/></brushdefinitions>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers><DOMLayer name=\"Layer_1\"><frames>"
        "<DOMFrame index=\"0\"><elements>"
        "<DOMShape isDrawingObject=\"true\"><strokes><StrokeStyle index=\"1\">"
        "<PaintBrushStroke scaleMode=\"normal\" weight=\"30\" paintBrushDefinition=\"1\">"
        "<fill><SolidColor color=\"#FF9966\"/></fill></PaintBrushStroke></StrokeStyle></strokes>"
        "<edges><Edge strokeStyle=\"1\" edges=\"!0 0|4000 0\"/></edges></DOMShape>"
        "<DOMShape isDrawingObject=\"true\"><strokes><StrokeStyle index=\"1\">"
        "<PaintBrushStroke scaleMode=\"normal\" weight=\"10\" paintBrushDefinition=\"2\">"
        "<fill><SolidColor color=\"#000000\"/></fill></PaintBrushStroke></StrokeStyle></strokes>"
        "<edges><Edge strokeStyle=\"1\" edges=\"!0 2000|4000 2000\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timelines></DOMDocument>");
    writeText(folder / (std::string(name) + ".xfl"), "PROXY-CS5");
    return folder;
}

/// The strokes of the first frame's shapes, in order.
std::vector<const fla::StrokeStyle*> strokesOf(const fla::Document& document)
{
    std::vector<const fla::StrokeStyle*> strokes;
    for (const fla::Element* element : document.timelines.front()->layers.front()->frames.front()->elements)
    {
        if (element->elementType() == fla::Element::Type::Shape)
        {
            for (const fla::StrokeStyle* stroke : static_cast<const fla::Shape*>(element)->strokes)
                strokes.push_back(stroke);
        }
    }
    return strokes;
}

/// Both strokes read as paint brush strokes, and the brushes found.
void checkSample(const fla::Document& document)
{
    const std::vector<const fla::StrokeStyle*> strokes = strokesOf(document);
    CHECK(strokes.size() == 2);
    if (strokes.size() != 2)
        return;
    CHECK(strokes[0]->style() == fla::StrokeStyle::Style::PaintBrush && strokes[0]->paintBrushDefinition == 1);
    CHECK(strokes[1]->style() == fla::StrokeStyle::Style::PaintBrush && strokes[1]->paintBrushDefinition == 2);
    CHECK(strokes[0]->weight == 30.0 && strokes[0]->scaleMode == "normal");
    // Nothing about the brush is left over as an attribute of its own.
    CHECK(strokes[0]->extraAttributes.empty());
    const auto library = fla::paintBrushesOf(document);
    CHECK(library && library->definitions.size() == 2);
    if (library)
        CHECK(library->find(1) && library->find(2) && library->find(2)->pattern && !library->find(3));
}

struct Box
{
    double left = 1.0e300, top = 1.0e300, right = -1.0e300, bottom = -1.0e300;
};

Box boxOf(const std::vector<fla::PaintBrushArt::Piece>& pieces)
{
    Box box;
    for (const auto& piece : pieces)
        for (const auto& ring : piece.rings)
            for (const P& point : ring)
            {
                box.left = std::min(box.left, point.x);
                box.top = std::min(box.top, point.y);
                box.right = std::max(box.right, point.x);
                box.bottom = std::max(box.bottom, point.y);
            }
    return box;
}

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST(paint_brush_definitions_are_read)
{
    const std::vector<fla::PaintBrushDefinition> definitions = fla::readPaintBrushDefinitions(definitionsXml());
    CHECK(definitions.size() == 2);
    if (definitions.size() != 2)
        return;
    const fla::PaintBrushDefinition& bar = definitions[0];
    CHECK(bar.index == 1 && bar.name == "Bar" && !bar.pattern);
    CHECK(bar.scale == fla::PaintBrushDefinition::Scale::Stretch);
    CHECK(bar.direction == fla::PaintBrushDefinition::Direction::LeftToRight && !bar.flipAcross);
    CHECK(near(bar.leftGuide, 0.0) && near(bar.rightGuide, 1.0));
    // The SVG unescaped, line break and all.
    CHECK(bar.params.at("svgData").find("viewBox=\"0 0 100 10\">\r\n<rect") != std::string::npos);
    CHECK(bar.params.at("diameter") == "78.71");
    const fla::PaintBrushDefinition& squares = definitions[1];
    CHECK(squares.index == 2 && squares.pattern && squares.fit == fla::PaintBrushDefinition::Fit::Stretch);

    const fla::PaintBrushArt art = fla::paintBrushArt(bar);
    CHECK(near(art.width, 100.0) && near(art.height, 10.0));
    CHECK(art.pieces.size() == 1 && art.tinted);
    // The pattern's empty box paints nothing; its square keeps its blue.
    const fla::PaintBrushArt tile = fla::paintBrushArt(squares);
    CHECK(near(tile.width, 20.0) && tile.pieces.size() == 1 && !tile.tinted);
}

TEST(paint_brush_art_stretches_along_a_straight_stroke)
{
    const fla::PaintBrushDefinition bar = fla::paintBrushDefinition(1, kArtJson);
    const fla::PaintBrushArt art = fla::paintBrushArt(bar);
    const auto laid = fla::layPaintBrush(bar, art, {{0, 0}, {200, 0}}, false, 30.0, {0xFF, 0x99, 0x66, 0xFF});
    CHECK(laid.size() == 1);
    // End to end along the stroke, the weight across it.
    const Box box = boxOf(laid);
    CHECK(near(box.left, 0.0, 1e-6) && near(box.right, 200.0, 1e-6));
    CHECK(near(box.top, -15.0, 1e-6) && near(box.bottom, 15.0, 1e-6));
    // Black art takes the stroke's color.
    if (!laid.empty())
        CHECK(laid[0].color == (fla::PaintBrushArt::Color{0xFF, 0x99, 0x66, 0xFF}));

    // Running the other way, or flipped across the stroke, covers the same.
    fla::PaintBrushDefinition turned = bar;
    turned.direction = fla::PaintBrushDefinition::Direction::RightToLeft;
    turned.flipAcross = true;
    const Box same = boxOf(fla::layPaintBrush(turned, art, {{0, 0}, {200, 0}}, false, 30.0, {0, 0, 0, 255}));
    CHECK(near(same.left, 0.0, 1e-6) && near(same.right, 200.0, 1e-6) && near(same.top, -15.0, 1e-6));

    // Scaled proportionately the art's height grows with its length.
    fla::PaintBrushDefinition whole = bar;
    whole.scale = fla::PaintBrushDefinition::Scale::Proportional;
    const Box tall = boxOf(fla::layPaintBrush(whole, art, {{0, 0}, {200, 0}}, false, 30.0, {0, 0, 0, 255}));
    CHECK(near(tall.top, -10.0, 1e-6) && near(tall.bottom, 10.0, 1e-6));
}

TEST(paint_brush_art_bends_along_a_curved_stroke)
{
    const fla::PaintBrushDefinition bar = fla::paintBrushDefinition(1, kArtJson);
    const fla::PaintBrushArt art = fla::paintBrushArt(bar);
    // A quarter circle of radius 100 round the origin, from (100, 0) to
    // (0, 100).
    std::vector<P> arc;
    for (int i = 0; i <= 90; ++i)
    {
        const double angle = i * 3.14159265358979323846 / 180.0;
        arc.push_back({100.0 * std::cos(angle), 100.0 * std::sin(angle)});
    }
    const auto laid = fla::layPaintBrush(bar, art, arc, false, 20.0, {0, 0, 0, 255});
    CHECK(laid.size() == 1);
    // Every point of the art is within the band 10 either side of the arc.
    double nearest = 1.0e300, farthest = 0.0;
    for (const auto& piece : laid)
        for (const auto& ring : piece.rings)
            for (const P& point : ring)
            {
                const double radius = std::hypot(point.x, point.y);
                nearest = std::min(nearest, radius);
                farthest = std::max(farthest, radius);
            }
    CHECK(nearest > 89.9 && nearest < 90.1);
    CHECK(farthest > 109.9 && farthest < 110.1);
    const Box box = boxOf(laid);
    // The ends square to the polyline's last pieces, a half degree off.
    CHECK(near(box.left, 0.0, 0.1) && near(box.right, 110.0, 0.01));
    CHECK(near(box.top, 0.0, 0.1) && near(box.bottom, 110.0, 0.01));
}

TEST(paint_brush_patterns_repeat_along_the_stroke)
{
    const fla::PaintBrushDefinition squares = fla::paintBrushDefinition(2, kPatternJson);
    const fla::PaintBrushArt tile = fla::paintBrushArt(squares);
    // Tiles 20 long at weight 10: ten of them along 200.
    const auto laid = fla::layPaintBrush(squares, tile, {{0, 0}, {200, 0}}, false, 10.0, {0, 0, 0, 128});
    CHECK(laid.size() == 1);
    if (laid.empty())
        return;
    CHECK(laid[0].rings.size() == 10);
    // Its own blue, faded with the stroke.
    CHECK(laid[0].color == (fla::PaintBrushArt::Color{0, 0, 255, 128}));
    const Box box = boxOf(laid);
    CHECK(near(box.left, 0.0, 1e-6) && near(box.right, 190.0, 1e-6));
    CHECK(near(box.top, -5.0, 1e-6) && near(box.bottom, 5.0, 1e-6));
    // A length that is not a whole number of tiles stretches them to fit.
    const auto stretched = fla::layPaintBrush(squares, tile, {{0, 0}, {205, 0}}, false, 10.0, {0, 0, 0, 255});
    CHECK(!stretched.empty() && stretched[0].rings.size() == 10 && near(boxOf(stretched).right, 194.75, 1e-6));
}

TEST(paint_brush_strokes_round_trip_through_xfl_fla_and_phx)
{
    const fs::path folder = writeSample("sample");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> original(parser.parse(folder.string()));
    CHECK(original && original->document);
    if (!original || !original->document)
        return;
    checkSample(*original->document);

    // An XFL folder: the definitions file as it was.
    const fs::path out = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "written";
    fs::remove_all(out);
    fla::XFLFolderWriter folderWriter;
    CHECK(folderWriter.write(*original->document, out.string()));
    CHECK(readText(out / "PaintBrushDefinitions.xml") == definitionsXml());
    const std::string dom = readText(out / "DOMDocument.xml");
    CHECK(dom.find("<Include href=\"PaintBrushDefinitions.xml\"/>") != std::string::npos);
    CHECK(dom.find("<PaintBrushStroke weight=\"30\" scaleMode=\"normal\" paintBrushDefinition=\"1\">") !=
        std::string::npos);
    {
        FLAParser again;
        std::unique_ptr<fla::FLADocument> back(again.parse(out.string()));
        CHECK(back && back->document);
        if (back && back->document)
            checkSample(*back->document);
    }

    // An FLA.
    const fs::path flaPath = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "written.fla";
    fla::FLAWriter flaWriter;
    CHECK(flaWriter.write(*original->document, flaPath.string()));
    {
        FLAParser again;
        std::unique_ptr<fla::FLADocument> back(again.parse(flaPath.string()));
        CHECK(back && back->document);
        if (back && back->document)
            checkSample(*back->document);
    }

    // A Phoenix document.
    const fs::path phxPath = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "written.phx";
    std::string error, warning;
    bool readOnly = false;
    CHECK(phx::save(*original->document, phxPath.string(), phx::SaveOptions(), error));
    std::unique_ptr<fla::FLADocument> back(phx::load(phxPath.string(), error, warning, readOnly));
    CHECK(back && back->document);
    if (back && back->document)
        checkSample(*back->document);
}

TEST(paint_brush_library_of_a_real_document_draws_every_brush)
{
    // Opt-in: an FLA whose strokes use paint brushes, every one of which
    // must have art to lay along a line.
    const char* path = std::getenv("PHOENIX_PAINT_BRUSH_FLA");
    if (!path || !fs::exists(path))
    {
        std::printf("    skipped: set PHOENIX_PAINT_BRUSH_FLA to an FLA drawn with paint brushes\n");
        return;
    }
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> document(parser.parse(path));
    CHECK(document && document->document);
    if (!document || !document->document)
        return;
    const auto library = fla::paintBrushesOf(*document->document);
    CHECK(library && !library->definitions.empty());
    if (!library)
        return;
    int empty = 0;
    for (const fla::PaintBrushDefinition& definition : library->definitions)
    {
        const auto art = library->art(definition.index);
        const auto laid =
            fla::layPaintBrush(definition, *art, {{0, 0}, {150, 20}, {300, 0}}, false, 30.0, {0, 0, 0, 255});
        if (laid.empty())
        {
            std::printf("    %d %s draws nothing\n", definition.index, definition.name.c_str());
            ++empty;
        }
    }
    CHECK(empty == 0);
    std::printf("    %zu brushes\n", library->definitions.size());
}
