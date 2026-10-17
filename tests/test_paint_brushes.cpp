#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/edge.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/stroke_style.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/brush_library.h"
#include "../src/edit/library_transfer.h"
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
#include <map>
#include <memory>
#include <set>
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

    // Written again, the definitions file is what it was, byte for byte.
    for (const auto& [name, bytes] : document->document->extraFiles)
        if (name == "PaintBrushDefinitions.xml")
            CHECK(fla::writePaintBrushDefinitions(library->definitions) ==
                std::string(bytes.get().begin(), bytes.get().end()));
}

namespace {

/// A shape on \a frame with one stroke drawn with brush \a index.
fla::Shape* addBrushStroke(fla::Frame* frame, int index)
{
    fla::Shape* shape = new fla::Shape(frame);
    fla::PaintBrushStroke* stroke = new fla::PaintBrushStroke(shape);
    stroke->weight = 10.0;
    stroke->paintBrushDefinition = index;
    shape->strokes.push_back(stroke);
    shape->strokesMap[1] = stroke;
    frame->elements.push_back(shape);
    return shape;
}

/// A document of one scene whose frame has a stroke for each of \a brushes.
void fillScene(fla::Document& document, const std::vector<int>& brushes)
{
    fla::Timeline* timeline = new fla::Timeline(&document);
    fla::Layer* layer = new fla::Layer(timeline);
    fla::Frame* frame = new fla::Frame(layer);
    layer->frames.push_back(frame);
    timeline->layers.push_back(layer);
    document.timelines.push_back(timeline);
    for (const int index : brushes)
        addBrushStroke(frame, index);
}

/// \a json with its guid and name set.
fla::PaintBrushDefinition named(const char* json, int index, const std::string& guid, const std::string& name)
{
    return fla::withParam(fla::withParam(fla::paintBrushDefinition(index, json), "guid", guid), "name", name);
}

/// The farthest any point of \a pieces lies from the polyline \a line.
double farthestFrom(const std::vector<fla::PaintBrushArt::Piece>& pieces, const std::vector<P>& line)
{
    double farthest = 0.0;
    for (const auto& piece : pieces)
        for (const auto& ring : piece.rings)
            for (const P& p : ring)
            {
                double nearest = 1.0e300;
                for (size_t i = 0; i + 1 < line.size(); ++i)
                {
                    const P& a = line[i];
                    const P& b = line[i + 1];
                    const double dx = b.x - a.x, dy = b.y - a.y;
                    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
                    nearest = std::min(nearest, std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t));
                }
                farthest = std::max(farthest, nearest);
            }
    return farthest;
}

} // namespace

TEST(paint_brush_settings_change_in_their_json_and_write_back_as_read)
{
    // A member changed in place, the rest as they were; a new one added.
    const std::string json = R"({"a":"1","name":"Old \"one\"","svgData":"<svg/>","z":{"x":"name"}})";
    CHECK(fla::withJsonMember(json, "name", "New") == R"({"a":"1","name":"New","svgData":"<svg/>","z":{"x":"name"}})");
    CHECK(fla::withJsonMember(json, "spacing", "5") ==
        R"({"a":"1","name":"Old \"one\"","svgData":"<svg/>","z":{"x":"name"},"spacing":"5"})");
    CHECK(fla::withJsonMember("{}", "k", "a\"b") == R"({"k":"a\"b"})");
    const fla::PaintBrushDefinition renamed = fla::withParam(fla::paintBrushDefinition(4, kArtJson), "name", "Bar's");
    CHECK(renamed.name == "Bar's" && renamed.index == 4 && !renamed.pattern);

    // Written as FLA files lay the file out, and read back the same.
    const std::vector<fla::PaintBrushDefinition> definitions = fla::readPaintBrushDefinitions(definitionsXml());
    const std::string xml = fla::writePaintBrushDefinitions(definitions);
    CHECK(xml.rfind("<paintbrushdefinitions xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    "xmlns=\"http://ns.adobe.com/xfl/2008/\">\r\n  <paintbrushdefinition index=\"1\" brushParam='{",
              0) == 0);
    CHECK(xml.size() > 30 && xml.compare(xml.size() - 29, 29, "'/>\r\n</paintbrushdefinitions>") == 0);
    CHECK(xml.find("&lt;svg") != std::string::npos);
    const std::vector<fla::PaintBrushDefinition> again = fla::readPaintBrushDefinitions(xml);
    CHECK(again.size() == 2 && again[0].json == definitions[0].json && again[1].json == definitions[1].json);
    CHECK(fla::writePaintBrushDefinitions(again) == xml);

    const std::string guid = fla::newPaintBrushGuid();
    CHECK(guid.size() == 36 && guid[8] == '-' && guid[14] == '4' && guid != fla::newPaintBrushGuid());

    // Filed by name.
    const auto category = [](const char* name, bool pattern) {
        fla::PaintBrushDefinition definition;
        definition.name = name;
        definition.pattern = pattern;
        return fla::paintBrushCategory(definition);
    };
    CHECK(category("Arrow 2.01", false) == "Arrows");
    CHECK(category("Charcoal - Soft", false) == "Chalk, Charcoal and Pencil");
    CHECK(category("Dry Brush 7", false) == "Paint Brushes");
    CHECK(category("Floral Brush 3", false) == "Floral");
    CHECK(category("Text Divider 12", false) == "Text Dividers");
    CHECK(category("DashedCircle", true) == "Dashed Borders");
    CHECK(category("Gothic", true) == "Pattern Borders");
}

TEST(paint_brushes_come_into_a_document_once_by_guid)
{
    fla::Document document(nullptr);
    fillScene(document, {});
    CHECK(!fla::paintBrushesOf(document));

    // The first brush: the file and the section including it, undoably.
    const fla::PaintBrushDefinition bar = named(kArtJson, 7, "AAAAAAAA-0000-4000-8000-000000000001", "Bar");
    const fla::PaintBrushDefinition squares = named(kPatternJson, 9, "AAAAAAAA-0000-4000-8000-000000000002", "Squares");
    std::map<int, int> indexes;
    fla::CommandPtr first = fla::adoptPaintBrushes(document, {bar}, indexes);
    CHECK(first != nullptr);
    if (!first)
        return;
    first->redo();
    CHECK(indexes.size() == 1 && indexes[7] == 1);
    auto library = fla::paintBrushesOf(document);
    CHECK(library && library->definitions.size() == 1 && library->find(1) && library->find(1)->name == "Bar");
    CHECK(document.extraSections.size() == 1 &&
        document.extraSections[0].xml.find("<Include href=\"PaintBrushDefinitions.xml\"/>") != std::string::npos &&
        document.extraSections[0].beforeTimelines);

    // The same brush again adds nothing; another comes after it.
    indexes.clear();
    CHECK(fla::adoptPaintBrushes(document, {bar}, indexes) == nullptr && indexes[7] == 1);
    indexes.clear();
    fla::CommandPtr second = fla::adoptPaintBrushes(document, {squares, bar}, indexes);
    CHECK(second != nullptr);
    if (!second)
        return;
    second->redo();
    CHECK(indexes[9] == 2 && indexes[7] == 1);
    library = fla::paintBrushesOf(document);
    CHECK(library && library->definitions.size() == 2 && library->find(2) && library->find(2)->pattern);

    // Undone, step by step, to none at all.
    second->undo();
    library = fla::paintBrushesOf(document);
    CHECK(library && library->definitions.size() == 1);
    first->undo();
    CHECK(!fla::paintBrushesOf(document) && document.extraSections.empty() && document.extraFiles.empty());

    // A document saved with them writes the file and the include.
    first->redo();
    second->redo();
    const fs::path out = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "adopted";
    fs::remove_all(out);
    fla::XFLFolderWriter writer;
    CHECK(writer.write(document, out.string()));
    CHECK(readText(out / "PaintBrushDefinitions.xml") ==
        fla::writePaintBrushDefinitions(fla::paintBrushesOf(document)->definitions));
    CHECK(readText(out / "DOMDocument.xml").find("<Include href=\"PaintBrushDefinitions.xml\"/>") != std::string::npos);
}

TEST(paint_brush_uses_are_counted_and_remapped)
{
    fla::Document document(nullptr);
    fillScene(document, {1, 2, 2, 5});
    const std::map<int, int> uses = fla::paintBrushUses(document);
    CHECK(uses.size() == 3 && uses.at(1) == 1 && uses.at(2) == 2 && uses.at(5) == 1);
    fla::Frame* frame = document.timelines[0]->layers[0]->frames[0];
    for (fla::Element* element : frame->elements)
        fla::remapPaintBrushes(*element, {{2, 1}, {1, 2}});
    const std::map<int, int> after = fla::paintBrushUses(document);
    CHECK(after.at(1) == 2 && after.at(2) == 1 && after.at(5) == 1);
}

TEST(pasted_and_copied_strokes_bring_their_brushes)
{
    // The source has brushes 1 (Bar) and 2 (Squares); the target has its
    // own Squares as 1, so Squares is used as it is there and Bar comes in
    // as 2.
    fla::Document source(nullptr);
    fillScene(source, {1, 2});
    const fla::PaintBrushDefinition bar = named(kArtJson, 1, "AAAAAAAA-0000-4000-8000-000000000001", "Bar");
    const fla::PaintBrushDefinition squares = named(kPatternJson, 2, "AAAAAAAA-0000-4000-8000-000000000002", "Squares");
    fla::setPaintBrushDefinitions(source, {bar, squares}, "Brushes")->redo();
    fla::Document target(nullptr);
    fillScene(target, {});
    fla::PaintBrushDefinition theirs = squares;
    theirs.index = 1;
    fla::setPaintBrushDefinitions(target, {theirs}, "Brushes")->redo();

    // A paste: the brushes of what was copied, adopted, the copies remapped.
    std::vector<const fla::Element*> copied;
    for (const fla::Element* element : source.timelines[0]->layers[0]->frames[0]->elements)
        copied.push_back(element);
    const std::vector<fla::PaintBrushDefinition> carried = fla::paintBrushesFor(source, copied);
    CHECK(carried.size() == 2);
    std::map<int, int> indexes;
    fla::CommandPtr adopt = fla::adoptPaintBrushes(target, carried, indexes);
    CHECK(adopt != nullptr);
    if (adopt)
        adopt->redo();
    CHECK(indexes[1] == 2 && indexes[2] == 1);
    const auto library = fla::paintBrushesOf(target);
    CHECK(library && library->definitions.size() == 2 && library->find(2) && library->find(2)->name == "Bar");

    // A symbol copied to another document's library: its strokes' brushes
    // come with it.
    fla::Document from(nullptr);
    fillScene(from, {});
    from.symbolList = new fla::SymbolList(&from);
    fla::Symbol* symbol = new fla::Symbol(from.symbolList);
    symbol->name = "Fence";
    fla::Timeline* inner = new fla::Timeline(symbol);
    fla::Layer* layer = new fla::Layer(inner);
    fla::Frame* frame = new fla::Frame(layer);
    layer->frames.push_back(frame);
    inner->layers.push_back(layer);
    symbol->timelines.push_back(inner);
    from.symbolList->symbols.push_back(symbol);
    from.symbolList->symbolMap["Fence"] = symbol;
    addBrushStroke(frame, 2);
    fla::setPaintBrushDefinitions(from, {bar, squares}, "Brushes")->redo();

    fla::Document into(nullptr);
    fillScene(into, {});
    fla::setPaintBrushDefinitions(into, {theirs}, "Brushes")->redo();
    fla::CommandPtr copy = fla::transfer::copyItems(into, from, {"Fence"}, fla::transfer::Resolution::KeepBoth);
    CHECK(copy != nullptr);
    if (!copy)
        return;
    copy->redo();
    const fla::Symbol* fence = into.symbolList ? into.symbolList->symbolMap["Fence"] : nullptr;
    CHECK(fence != nullptr);
    if (!fence)
        return;
    const auto strokeOf = [](const fla::Symbol& symbol) {
        const fla::Element* shape = symbol.timelines[0]->layers[0]->frames[0]->elements[0];
        const auto& strokes = static_cast<const fla::Shape*>(shape)->strokes;
        return strokes.empty() ? -1 : strokes[0]->paintBrushDefinition;
    };
    // Squares is the target's 1 already: nothing new to add.
    CHECK(strokeOf(*fence) == 1);
    CHECK(fla::paintBrushesOf(into)->definitions.size() == 1);
    copy->undo();

    // Into a document with no brushes, Squares comes in as its first.
    fla::Document bare(nullptr);
    fillScene(bare, {});
    fla::CommandPtr second = fla::transfer::copyItems(bare, from, {"Fence"}, fla::transfer::Resolution::KeepBoth);
    CHECK(second != nullptr);
    if (!second)
        return;
    second->redo();
    const auto brought = fla::paintBrushesOf(bare);
    CHECK(brought && brought->definitions.size() == 1 && brought->find(1) && brought->find(1)->name == "Squares");
    CHECK(strokeOf(*bare.symbolList->symbolMap["Fence"]) == 1);
    second->undo();
    CHECK(!fla::paintBrushesOf(bare));
}

TEST(brush_library_keeps_categories_and_takes_each_brush_once)
{
    std::vector<fla::BrushLibraryEntry> library;
    const fla::PaintBrushDefinition arrow = named(kArtJson, 3, "AAAAAAAA-0000-4000-8000-000000000003", "Arrow 2.01");
    const fla::PaintBrushDefinition dashes = named(kPatternJson, 4, "AAAAAAAA-0000-4000-8000-000000000004", "Dashed Line");
    CHECK(fla::addToBrushLibrary(library, {arrow, dashes}) == 2);
    CHECK(fla::addToBrushLibrary(library, {dashes}) == 0);
    CHECK(library.size() == 2 && library[0].category == "Arrows" && library[1].category == "Dashed Borders");
    CHECK(fla::addToBrushLibrary(
              library, {named(kArtJson, 1, "AAAAAAAA-0000-4000-8000-000000000005", "Mine")}, "Favorites") == 1);
    library[0].category = "My Arrows";

    // Written and read back: the categories and the definitions as they were.
    const std::string xml = fla::writeBrushLibrary(library);
    const std::vector<fla::BrushLibraryEntry> back = fla::readBrushLibrary(xml);
    CHECK(back.size() == 3);
    if (back.size() != 3)
        return;
    CHECK(back[0].category == "My Arrows" && back[0].definition.json == arrow.json);
    CHECK(back[1].definition.pattern && back[2].category == "Favorites");
    const fla::BrushLibraryEntry* found = fla::findInBrushLibrary(back, "AAAAAAAA-0000-4000-8000-000000000004");
    CHECK(found && found->definition.name == "Dashed Line");
    CHECK(!fla::findInBrushLibrary(back, "nothing"));

    // Phoenix's own: each draws, in grays the stroke's color tints, with a
    // guid of its own, and none is written to the library's file.
    const std::vector<fla::BrushLibraryEntry>& own = fla::phoenixBrushes();
    CHECK(own.size() == 11);
    std::set<std::string> guids;
    int patterns = 0;
    for (const fla::BrushLibraryEntry& entry : own)
    {
        CHECK(entry.builtIn && entry.category == "Phoenix");
        guids.insert(entry.definition.guid());
        patterns += entry.definition.pattern ? 1 : 0;
        const fla::PaintBrushArt art = fla::paintBrushArt(entry.definition);
        CHECK(art.tinted && !art.pieces.empty());
        const auto laid =
            fla::layPaintBrush(entry.definition, art, {{0, 0}, {150, 20}, {300, 0}}, false, 20.0, {0, 0, 0, 255});
        if (laid.empty())
            std::printf("    %s draws nothing\n", entry.definition.name.c_str());
        CHECK(!laid.empty());
    }
    CHECK(guids.size() == own.size() && patterns == 5);
    std::vector<fla::BrushLibraryEntry> with = own;
    with.push_back(back[0]);
    CHECK(fla::readBrushLibrary(fla::writeBrushLibrary(with)).size() == 1);
}

TEST(paint_brush_patterns_turn_corners)
{
    fla::PaintBrushDefinition squares = fla::paintBrushDefinition(2, kPatternJson);
    // A solid tile, so what covers the corner shows.
    squares = fla::withParam(squares, "svgData",
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10"><rect width="20" height="10"/></svg>)");
    const fla::PaintBrushArt tile = fla::paintBrushArt(squares);
    // Along x to (100, 0), then down to (100, 100): a right angle.
    const std::vector<P> corner{{0, 0}, {100, 0}, {100, 100}};
    const auto tilesOf = [&](fla::PaintBrushDefinition::Corner mode) {
        fla::PaintBrushDefinition definition = squares;
        definition.corner = mode;
        return fla::layPaintBrush(definition, tile, corner, false, 10.0, {0, 0, 0, 255});
    };

    // Between: five tiles to each side, the corner tiles folded into it.
    const auto between = tilesOf(fla::PaintBrushDefinition::Corner::Between);
    CHECK(between.size() == 1 && between[0].rings.size() == 10);
    // Centered: one across the corner and the sides fitted round it.
    const auto centered = tilesOf(fla::PaintBrushDefinition::Corner::Centered);
    CHECK(centered.size() == 1 && centered[0].rings.size() == 11);
    CHECK(farthestFrom(between, corner) < 5.0 * std::sqrt(2.0) + 0.01);
    CHECK(farthestFrom(centered, corner) < 5.0 * std::sqrt(2.0) + 0.01);

    // Sliced: the sides run on to the corner's outer point, each cut along
    // the bisector (the line x + y = 100 through the corner): nothing of
    // the first side past it, nothing of the second before it.
    const auto sliced = tilesOf(fla::PaintBrushDefinition::Corner::Sliced);
    CHECK(sliced.size() == 1 && sliced[0].rings.size() == 10);
    if (sliced.empty() || sliced[0].rings.size() != 10)
        return;
    const Box box = boxOf(sliced);
    CHECK(near(box.right, 105.0, 0.01) && near(box.top, -5.0, 0.01));
    bool cut = true;
    bool reaches = false;
    for (size_t r = 0; r < sliced[0].rings.size(); ++r)
        for (const P& p : sliced[0].rings[r])
        {
            // The first side's tiles come first.
            const double side = (p.x - 100.0) + p.y;
            if (r < 5 ? side > 1.0e-6 : side < -1.0e-6)
                cut = false;
            reaches = reaches || (near(p.x, 105.0, 0.01) && near(p.y, -5.0, 0.01));
        }
    CHECK(cut);
    // The outer corner is covered, where the two sides meet.
    CHECK(reaches);

    // Overlap: run on past the corner, uncut.
    const auto overlap = tilesOf(fla::PaintBrushDefinition::Corner::Overlap);
    CHECK(overlap.size() == 1 && overlap[0].rings.size() == 10);
    const Box over = boxOf(overlap);
    CHECK(near(over.right, 105.0, 0.01) && near(over.top, -5.0, 0.01) && over.bottom > 99.0);

    // A closed square: four corners, a run of tiles to each side.
    const auto square =
        fla::layPaintBrush(squares, tile, {{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true, 10.0, {0, 0, 0, 255});
    CHECK(square.size() == 1 && square[0].rings.size() == 24);
    const Box ring = boxOf(square);
    CHECK(near(ring.left, -5.0, 0.01) && near(ring.right, 105.0, 0.01) && near(ring.top, -5.0, 0.01));

    // Read from the definition: cornerValue 0 to 3.
    CHECK(fla::withParam(squares, "cornerValue", "0").corner == fla::PaintBrushDefinition::Corner::Centered);
    CHECK(fla::withParam(squares, "cornerValue", "1").corner == fla::PaintBrushDefinition::Corner::Between);
    CHECK(squares.corner == fla::PaintBrushDefinition::Corner::Sliced);
    CHECK(fla::withParam(squares, "cornerValue", "3").corner == fla::PaintBrushDefinition::Corner::Overlap);
}

TEST(paint_brush_art_bends_round_a_sharp_turn_with_folds_adjusted)
{
    const fla::PaintBrushDefinition bar = fla::paintBrushDefinition(1, kArtJson);
    CHECK(bar.foldAdjust);
    const fla::PaintBrushArt art = fla::paintBrushArt(bar);
    const std::vector<P> turn{{0, 0}, {100, 0}, {100, 100}};
    // Bent round: nothing farther from the line than half the weight.
    CHECK(farthestFrom(fla::layPaintBrush(bar, art, turn, false, 20.0, {0, 0, 0, 255}), turn) < 10.01);
    // Mitered, its outer corner reaches past that.
    const fla::PaintBrushDefinition mitered = fla::withParam(bar, "foldAdjust", "0");
    CHECK(!mitered.foldAdjust);
    CHECK(farthestFrom(fla::layPaintBrush(mitered, art, turn, false, 20.0, {0, 0, 0, 255}), turn) > 13.0);
}

TEST(paint_brush_width_follows_pressure_within_the_brush_range)
{
    fla::PaintBrushDefinition bar = fla::paintBrushDefinition(1, kArtJson);
    bar = fla::withParam(fla::withParam(bar, "minimumWidthPercentage", "20.00"), "maximumWidthPercentage", "100.00");
    CHECK(near(bar.minimumWidth, 0.2) && near(bar.maximumWidth, 1.0));

    // A mouse gives no profile.
    std::vector<fla::BrushSample> samples;
    for (int i = 0; i <= 100; ++i)
    {
        fla::BrushSample sample;
        sample.x = i * 2.0;
        samples.push_back(sample);
    }
    CHECK(fla::paintBrushWidthProfile(bar, samples).empty());

    // A pen pressing harder along the stroke: from the narrowest to the
    // widest.
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i].pressure = static_cast<double>(i) / (samples.size() - 1);
    const std::vector<fla::StrokeStyle::WidthMarker> profile = fla::paintBrushWidthProfile(bar, samples);
    CHECK(profile.size() >= 2);
    if (profile.size() < 2)
        return;
    CHECK(near(profile.front().position, 0.0) && near(profile.back().position, 1.0));
    CHECK(profile.front().left + profile.front().right < 0.3);
    CHECK(profile.back().left + profile.back().right > 0.95);

    // Leaning the pen over narrows it toward the narrowest tilt width.
    std::vector<fla::BrushSample> leaning = samples;
    for (fla::BrushSample& sample : leaning)
    {
        sample.pressure = 1.0;
        sample.tiltX = 60.0;
    }
    bar = fla::withParam(bar, "minimumTiltWidthPercentage", "50.00");
    const std::vector<fla::StrokeStyle::WidthMarker> leaned = fla::paintBrushWidthProfile(bar, leaning);
    CHECK(!leaned.empty() && near(leaned.front().left + leaned.front().right, 0.5, 0.01));

    // Laid with the profile: narrow at the start, full at the end.
    const fla::PaintBrushArt art = fla::paintBrushArt(bar);
    const auto laid = fla::layPaintBrush(bar, art, {{0, 0}, {200, 0}}, false, 30.0, {0, 0, 0, 255}, profile);
    double startHalf = 0.0, endHalf = 0.0;
    for (const auto& piece : laid)
        for (const auto& ring : piece.rings)
            for (const P& p : ring)
            {
                if (p.x < 1.0)
                    startHalf = std::max(startHalf, std::fabs(p.y));
                if (p.x > 199.0)
                    endHalf = std::max(endHalf, std::fabs(p.y));
            }
    CHECK(startHalf < 4.0 && endHalf > 14.0);
    // A profile wider than the brush's widest is kept to it.
    const std::vector<fla::StrokeStyle::WidthMarker> wide{{0.0, 1.5, 1.5, false}, {1.0, 1.5, 1.5, false}};
    const Box kept = boxOf(fla::layPaintBrush(bar, art, {{0, 0}, {200, 0}}, false, 30.0, {0, 0, 0, 255}, wide));
    CHECK(near(kept.top, -15.0, 1e-6));
}

TEST(document_brushes_lists_paint_brushes_and_symbols_with_their_strokes)
{
    fla::Document document(nullptr);
    fillScene(document, {2, 2});
    const fla::PaintBrushDefinition bar = named(kArtJson, 1, "AAAAAAAA-0000-4000-8000-000000000001", "Bar");
    const fla::PaintBrushDefinition squares = named(kPatternJson, 2, "AAAAAAAA-0000-4000-8000-000000000002", "Squares");
    fla::setPaintBrushDefinitions(document, {bar, squares}, "Brushes")->redo();
    // A stroke drawn with a graphic symbol, repeated.
    fla::Frame* frame = document.timelines[0]->layers[0]->frames[0];
    fla::Shape* shape = new fla::Shape(frame);
    fla::SolidStroke* stroke = new fla::SolidStroke(shape);
    stroke->extraAttributes = {{"phoenixBrush", "Leaf"}, {"phoenixBrushPattern", "true"}};
    shape->strokes.push_back(stroke);
    frame->elements.push_back(shape);

    const std::vector<fla::DocumentBrush> brushes = fla::documentBrushes(document);
    CHECK(brushes.size() == 3);
    if (brushes.size() != 3)
        return;
    // Unused brushes are listed too, with no strokes.
    CHECK(brushes[0].paintBrush && brushes[0].name == "Bar" && brushes[0].strokes == 0 && !brushes[0].pattern);
    CHECK(brushes[1].paintBrush && brushes[1].name == "Squares" && brushes[1].strokes == 2 && brushes[1].pattern);
    CHECK(!brushes[2].paintBrush && brushes[2].symbol == "Leaf" && brushes[2].strokes == 1 && brushes[2].pattern);
}

TEST(custom_stroke_style_offers_each_kind_of_brush_its_own_options)
{
    // An art brush's, then a pattern brush's, as the definitions spell them.
    const auto keys = [](bool pattern) {
        std::vector<std::string> out;
        for (const fla::PaintBrushOption& option : fla::paintBrushOptions(pattern))
            out.push_back(option.key);
        return out;
    };
    const std::vector<std::string> art = keys(false);
    const std::vector<std::string> pattern = keys(true);
    CHECK(std::find(art.begin(), art.end(), "scaleOptionm") != art.end());
    CHECK(std::find(art.begin(), art.end(), "foldAdjust") != art.end());
    CHECK(std::find(art.begin(), art.end(), "cornerValue") == art.end());
    CHECK(std::find(pattern.begin(), pattern.end(), "cornerValue") != pattern.end());
    CHECK(std::find(pattern.begin(), pattern.end(), "spacing") != pattern.end());
    CHECK(std::find(pattern.begin(), pattern.end(), "maximumWidthPercentage") != pattern.end());

    // Values as the definition has them, or the fallback; set, they read
    // back into the definition's settings.
    const fla::PaintBrushDefinition squares = fla::paintBrushDefinition(2, kPatternJson);
    for (const fla::PaintBrushOption& option : fla::paintBrushOptions(true))
    {
        const std::string value = fla::paintBrushOptionValue(squares, option);
        CHECK(!value.empty());
        if (option.kind != fla::PaintBrushOption::Kind::Percent)
        {
            // Every choice is one the option offers.
            bool offered = false;
            for (const auto& choice : option.choices)
                offered = offered || choice.first == value;
            CHECK(offered);
        }
    }
    const fla::PaintBrushOption* fit = nullptr;
    const fla::PaintBrushOption* spacing = nullptr;
    for (const fla::PaintBrushOption& option : fla::paintBrushOptions(true))
    {
        if (option.key == "adjustment")
            fit = &option;
        if (option.key == "spacing")
            spacing = &option;
    }
    CHECK(fit && spacing);
    if (!fit || !spacing)
        return;
    const fla::PaintBrushDefinition spaced =
        fla::withParam(fla::withParam(squares, fit->key, fit->choices[1].first), spacing->key, "50");
    CHECK(spaced.fit == fla::PaintBrushDefinition::Fit::AddSpace && near(spaced.spacing, 0.5));
    CHECK(fla::paintBrushOptionValue(spaced, *spacing) == "50");
    // The art and the rest are as they were.
    CHECK(spaced.params.at("svgData") == squares.params.at("svgData") && spaced.name == squares.name);
}

TEST(paint_brush_width_from_a_pen_is_saved_with_the_stroke)
{
    const fs::path folder = writeSample("pressure");
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> original(parser.parse(folder.string()));
    CHECK(original && original->document);
    if (!original || !original->document)
        return;
    // The first stroke as a pen would leave it: narrow, then full.
    const std::vector<const fla::StrokeStyle*> strokes = strokesOf(*original->document);
    CHECK(strokes.size() == 2);
    if (strokes.size() != 2)
        return;
    const_cast<fla::StrokeStyle*>(strokes[0])->widthProfile = {{0.0, 0.1, 0.1, false}, {1.0, 0.5, 0.5, false}};

    const fs::path out = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "pressure_written";
    fs::remove_all(out);
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*original->document, out.string()));
    CHECK(readText(out / "DOMDocument.xml").find("<WidthMarker position=\"0\" left=\"0.1\" right=\"0.1\"/>") !=
        std::string::npos);
    FLAParser again;
    std::unique_ptr<fla::FLADocument> back(again.parse(out.string()));
    CHECK(back && back->document);
    if (!back || !back->document)
        return;
    const std::vector<const fla::StrokeStyle*> read = strokesOf(*back->document);
    CHECK(read.size() == 2 && read[0]->widthProfile == strokes[0]->widthProfile && read[1]->widthProfile.empty());

    // Changed again, the profile written is the new one, once.
    const_cast<fla::StrokeStyle*>(read[0])->widthProfile = {{0.0, 0.5, 0.5, false}, {0.5, 0.2, 0.2, true}};
    const fs::path twice = fs::temp_directory_path() / "phoenix_paint_brush_tests" / "pressure_twice";
    fs::remove_all(twice);
    CHECK(writer.write(*back->document, twice.string()));
    const std::string dom = readText(twice / "DOMDocument.xml");
    const size_t first = dom.find("<VariablePointWidth");
    CHECK(first != std::string::npos && dom.find("<VariablePointWidth", first + 1) == std::string::npos);
    CHECK(dom.find("<WidthMarker position=\"0.5\" left=\"0.2\" right=\"0.2\" type=\"corner\"/>") != std::string::npos);
}
