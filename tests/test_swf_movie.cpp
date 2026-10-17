#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/swf_movie.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/swf_import.h"
#include "../src/parser/zip_reader.h"
#include "../src/util/swf_writer.h"

#include "avm1/avm1.h"
#include "zlib.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> bytesOf(const std::string& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

struct Parsed
{
    std::unique_ptr<ZipReader> reader;
    std::unique_ptr<fla::DOMElement> root;
    fla::Document* document = nullptr;
};

Parsed parse(const std::map<std::string, std::vector<uint8_t>>& files)
{
    Parsed parsed;
    parsed.reader.reset(new ZipReader());
    parsed.reader->openMemory(files);
    parsed.root.reset(new fla::FLADocument(nullptr));
    DocumentParser parser(parsed.reader.get());
    const auto xml = files.at("DOMDocument.xml");
    parsed.document = parser.parse(std::string(xml.begin(), xml.end()), parsed.root.get());
    static_cast<fla::FLADocument*>(parsed.root.get())->document = parsed.document;
    return parsed;
}

/// Draws every element as a square of its own, the way a test can: movie
/// clips, buttons and text fields as pieces, placed where the keyframe has
/// them (moved evenly toward the next keyframe in a classic tween).
class SquareDrawer : public fla::swfmovie::Drawer
{
public:
    std::vector<fla::swfmovie::Piece> layerFrame(const fla::Layer& layer, int frame, swf::Writer& writer) override
    {
        ++calls;
        std::vector<fla::swfmovie::Piece> pieces;
        const fla::Frame* key = nullptr;
        const fla::Frame* next = nullptr;
        for (const fla::Frame* f : layer.frames)
        {
            if (frame >= f->index && frame < f->index + f->duration)
                key = f;
            if (key && f->index == key->index + key->duration)
                next = f;
        }
        if (!key)
            return pieces;
        const double t = key->tweenType == fla::TweenType::Motion && next
            ? static_cast<double>(frame - key->index) / key->duration : 0.0;
        for (size_t i = 0; i < key->elements.size(); ++i)
        {
            const fla::Element* element = key->elements[i];
            fla::swfmovie::Piece piece;
            piece.element = static_cast<int>(i);
            const fla::Transform& at = element->transform;
            piece.matrix = swf::Matrix{at.m11, at.m12, at.m21, at.m22, at.tx, at.ty};
            if (t > 0.0 && i < next->elements.size())
            {
                piece.matrix.tx += (next->elements[i]->transform.tx - at.tx) * t;
                piece.matrix.ty += (next->elements[i]->transform.ty - at.ty) * t;
            }
            const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element);
            const auto* text = dynamic_cast<const fla::StaticText*>(element);
            if (instance && instance->symbolType != fla::SymbolType::Graphic)
            {
                piece.instance = instance;
                piece.color = instance->colorTransform;
                piece.filters = instance->filters;
                piece.blendMode = instance->blendMode;
            }
            else if (text && text->textType != fla::StaticText::TextType::Static)
                piece.text = text;
            else
            {
                const auto found = _shapes.find(element);
                int id = found == _shapes.end() ? 0 : found->second;
                if (!id)
                {
                    swf::Fill fill;
                    fill.color = swf::Color{200, 40, 40, 255};
                    const double x = piece.matrix.tx, y = piece.matrix.ty;
                    id = writer.defineShape(fill, {{swf::Segment::Kind::Move, x, y}, {swf::Segment::Kind::Line, x + 10, y},
                        {swf::Segment::Kind::Line, x + 10, y + 10}, {swf::Segment::Kind::Line, x, y + 10},
                        {swf::Segment::Kind::Line, x, y}});
                    _shapes[element] = id;
                }
                piece.shapes.push_back({id, 1.0, swf::Writer::Blend::Normal});
            }
            pieces.push_back(piece);
        }
        return pieces;
    }
    int calls = 0;

private:
    std::map<const fla::Element*, int> _shapes;
};

struct Tag
{
    int code;
    std::vector<uint8_t> body;
    /// The sprite it is in, 0 for the main timeline.
    int sprite = 0;
};

/// Every tag of a movie, those inside sprites too (after their sprite).
std::vector<Tag> tagsOf(const std::vector<uint8_t>& file, int* frames = nullptr)
{
    std::vector<uint8_t> body(file.begin() + 8, file.end());
    if (file[0] == 'C')
    {
        uLongf size = file[4] | (file[5] << 8) | (file[6] << 16) | (static_cast<uLongf>(file[7]) << 24);
        std::vector<uint8_t> out(size);
        uncompress(out.data(), &size, body.data(), static_cast<uLong>(body.size()));
        out.resize(size);
        body = out;
    }
    const int bits = body[0] >> 3;
    size_t at = (5 + bits * 4 + 7) / 8 + 4 - 8 + 8;
    if (frames)
        *frames = body[at - 2] | (body[at - 1] << 8);
    std::vector<Tag> tags;
    std::function<void(size_t, size_t, int)> read = [&](size_t from, size_t to, int sprite) {
        size_t p = from;
        while (p + 2 <= to)
        {
            const int header = body[p] | (body[p + 1] << 8);
            p += 2;
            const int code = header >> 6;
            size_t length = header & 0x3F;
            if (length == 0x3F)
            {
                length = body[p] | (body[p + 1] << 8) | (body[p + 2] << 16) | (static_cast<size_t>(body[p + 3]) << 24);
                p += 4;
            }
            Tag tag{code, std::vector<uint8_t>(body.begin() + p, body.begin() + p + length), sprite};
            tags.push_back(tag);
            if (code == 39)
                read(p + 4, p + length, body[p] | (body[p + 1] << 8));
            p += length;
            if (code == 0)
                break;
        }
    };
    read(at, body.size(), 0);
    return tags;
}

std::string text(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& line : lines)
        out += line + "\n";
    return out;
}

/// The listings of a movie's DoAction tags, by the sprite they are in.
std::multimap<int, std::string> actionsOf(const std::vector<Tag>& tags)
{
    std::multimap<int, std::string> out;
    for (const Tag& tag : tags)
        if (tag.code == 12)
            out.emplace(tag.sprite, as3::avm1::disassemble(tag.body));
    return out;
}

/// A cstring at \a at in \a body.
std::string stringAt(const std::vector<uint8_t>& body, size_t at)
{
    std::string out;
    while (at < body.size() && body[at])
        out += static_cast<char>(body[at++]);
    return out;
}

/// The library and scene of a small game: a hat movie clip with labels and
/// its own stop, a button whose release sends the hat on, a score field, a
/// shape, a frame script that stops the main timeline.
std::map<std::string, std::vector<uint8_t>> gamePackage(const std::string& extraLayers = std::string())
{
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<symbols><Include href=\"Hat.xml\"/><Include href=\"Go.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\" name=\"start\" labelType=\"name\">"
        "<Actionscript><script><![CDATA[stop();]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"ui\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Go\" symbolType=\"button\" name=\"go\">"
        "<matrix><Matrix tx=\"150\" ty=\"10\"/></matrix>"
        "<Actionscript><script><![CDATA[on (release) {\n  _root.hat.gotoAndPlay(\"end\");\n}]]></script></Actionscript>"
        "</DOMSymbolInstance>"
        "<DOMDynamicText instanceName=\"score\" variableName=\"points\" left=\"0\" top=\"0\" width=\"60\" height=\"20\">"
        "<matrix><Matrix tx=\"5\" ty=\"70\"/></matrix>"
        "<textRuns><DOMTextRun><characters>0</characters><textAttrs><DOMTextAttrs face=\"Arial\" size=\"14\"/>"
        "</textAttrs></DOMTextRun></textRuns></DOMDynamicText>"
        "</elements></DOMFrame></frames></DOMLayer>" +
        extraLayers +
        "<DOMLayer name=\"hat\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"hat\"><matrix><Matrix tx=\"20\" ty=\"30\"/></matrix>"
        "<color><Color alphaMultiplier=\"0.5\"/></color></DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"back\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#336699\"/></FillStyle></fills>"
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|400 0!400 0|400 400!400 400|0 400!0 400|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    files["LIBRARY/Hat.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Hat\"><timeline><DOMTimeline name=\"Hat\">"
        "<layers><DOMLayer name=\"actions\"><frames>"
        "<DOMFrame index=\"0\" duration=\"2\"><Actionscript><script><![CDATA[stop();]]></script></Actionscript>"
        "<elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"1\" name=\"end\" labelType=\"name\"><Actionscript><script><![CDATA["
        "_parent.points = 10;\nstop();]]></script></Actionscript><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/></FillStyle></fills>"
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timeline></DOMSymbolItem>");
    files["LIBRARY/Go.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Go\" symbolType=\"button\"><timeline>"
        "<DOMTimeline name=\"Go\"><layers><DOMLayer name=\"face\"><frames>"
        "<DOMFrame index=\"0\" duration=\"2\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#00FF00\"/>"
        "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges>"
        "</DOMShape></elements></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"2\"><elements><DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#0000FF\"/>"
        "</FillStyle></fills><edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges>"
        "</DOMShape></elements></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    return files;
}

/// The movie \a document makes, with \a options.
std::vector<uint8_t> movieOf(const fla::Document& document, std::vector<std::string>* problems = nullptr,
    const fla::swfmovie::Options& options = fla::swfmovie::Options(), int* frames = nullptr)
{
    swf::Writer writer(static_cast<int>(document.width), static_cast<int>(document.height), document.frameRate,
        swf::Color{255, 255, 255, 255});
    SquareDrawer drawer;
    fla::swfmovie::Movie movie(document, writer, drawer, options);
    const int written = movie.write();
    if (frames)
        *frames = written;
    if (problems)
        *problems = movie.problems();
    return writer.finish(true);
}

const fla::Layer* layerNamed(const fla::Timeline& timeline, const std::function<bool(const fla::Layer&)>& wanted)
{
    for (const fla::Layer* layer : timeline.layers)
        if (layer && wanted(*layer))
            return layer;
    return nullptr;
}

/// Every instance on \a timeline's keyframes.
std::vector<const fla::SymbolInstance*> instancesOn(const fla::Timeline& timeline)
{
    std::vector<const fla::SymbolInstance*> out;
    for (const fla::Layer* layer : timeline.layers)
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            for (const fla::Element* element : frame->elements)
                if (const auto* instance = dynamic_cast<const fla::SymbolInstance*>(element))
                    out.push_back(instance);
    return out;
}

std::vector<std::string> labelsOn(const fla::Timeline& timeline)
{
    std::vector<std::string> out;
    for (const fla::Layer* layer : timeline.layers)
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            if (!frame->name.empty() && frame->labelType != "comment")
                out.push_back(frame->name);
    return out;
}

} // namespace

TEST(swf_movie_keeps_clips_buttons_names_labels_and_scripts)
{
    Parsed game = parse(gamePackage());
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    CHECK(fla::swfmovie::hasScripts(*game.document));
    CHECK(fla::swfmovie::unstructured(*game.document).empty());
    std::vector<std::string> problems;
    int frames = 0;
    const std::vector<uint8_t> swf = movieOf(*game.document, &problems, fla::swfmovie::Options(), &frames);
    CHECK(problems.empty());
    CHECK(frames == 4);
    if (!problems.empty())
        std::printf("    problems: %s\n", text(problems).c_str());

    // The scripts, compiled: the main timeline's stop, the hat's two.
    const std::vector<Tag> tags = tagsOf(swf);
    const std::multimap<int, std::string> actions = actionsOf(tags);
    CHECK(actions.count(0) == 1 && actions.find(0)->second == "Stop\nEnd\n");
    int hatSprite = 0;
    for (const Tag& tag : tags)
        if (tag.code == 12 && tag.sprite != 0)
            hatSprite = tag.sprite;
    CHECK(hatSprite != 0);
    CHECK(actions.count(hatSprite) == 2);
    bool parentSet = false;
    for (auto it = actions.lower_bound(hatSprite); it != actions.upper_bound(hatSprite); ++it)
        parentSet = parentSet || it->second ==
            "ConstantPool \"_parent\" \"points\"\nPush \"_parent\"\nGetVariable\nPush \"points\" 10\nSetMember\nStop\nEnd\n";
    CHECK(parentSet);

    // The button: two looks, and its release handler.
    bool button = false;
    for (const Tag& tag : tags)
    {
        if (tag.code != 34)
            continue;
        button = true;
        const size_t offset = tag.body[3] | (tag.body[4] << 8);
        CHECK(offset > 0);
        const size_t cond = 3 + offset;
        CHECK((tag.body[cond + 2] | (tag.body[cond + 3] << 8)) == swf::ButtonCondition::OverDownToOverUp);
        const std::vector<uint8_t> code(tag.body.begin() + static_cast<long>(cond) + 4, tag.body.end());
        CHECK(as3::avm1::disassemble(code) ==
            "ConstantPool \"end\" \"_root\" \"hat\" \"gotoAndPlay\"\n"
            "Push \"end\" 1 \"_root\"\nGetVariable\nPush \"hat\"\nGetMember\nPush \"gotoAndPlay\"\nCallMethod\nPop\nEnd\n");
        // Up and over show the green, down the blue, hit the green (as up).
        int records = 0;
        size_t at = 6;
        while (at < tag.body.size() && tag.body[at] != 0 && records < 8)
        {
            ++records;
            const int states = tag.body[at] & 0x0F;
            CHECK(states == (1 | 2 | 8) || states == 4);
            // Past the record: flags, id, depth, a matrix and a color.
            at += 5;
            const int bits = tag.body[at] >> 3;
            (void)bits;
            break;
        }
        CHECK(records >= 1);
    }
    CHECK(button);

    // Through Phoenix's own SWF import: the structure is all there.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(swf, files, error, &report));
    Parsed back = parse(files);
    CHECK(back.document && !back.document->timelines.empty());
    if (!back.document || back.document->timelines.empty())
        return;
    const fla::Timeline& scene = *back.document->timelines.front();
    const std::vector<std::string> labels = labelsOn(scene);
    CHECK(std::find(labels.begin(), labels.end(), "start") != labels.end());
    const std::vector<const fla::SymbolInstance*> placed = instancesOn(scene);
    const auto named = [&](const std::string& name) {
        for (const fla::SymbolInstance* instance : placed)
            if (instance->name == name)
                return instance;
        return static_cast<const fla::SymbolInstance*>(nullptr);
    };
    const fla::SymbolInstance* hat = named("hat");
    const fla::SymbolInstance* go = named("go");
    CHECK(hat && hat->symbolType == fla::SymbolType::MovieClip);
    CHECK(go && go->symbolType == fla::SymbolType::Button);
    if (hat)
    {
        CHECK_NEAR(hat->transform.tx, 20.0);
        CHECK_NEAR(hat->transform.ty, 30.0);
        CHECK_NEAR(hat->colorTransform.alphaMultiplier, 0.5);
        // The hat's own timeline: three frames, its label.
        CHECK(hat->symbol && !hat->symbol->timelines.empty());
        if (hat->symbol && !hat->symbol->timelines.empty())
        {
            const std::vector<std::string> own = labelsOn(*hat->symbol->timelines.front());
            CHECK(std::find(own.begin(), own.end(), "end") != own.end());
        }
    }
    // The score field and the variable it shows.
    bool field = false;
    for (const fla::Layer* layer : scene.layers)
        for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
            for (const fla::Element* element : frame->elements)
                if (const auto* text = dynamic_cast<const fla::StaticText*>(element))
                    if (text->instanceName == "score")
                    {
                        field = true;
                        bool variable = false;
                        for (const auto& [key, value] : text->extraAttributes)
                            variable = variable || (key == "variableName" && value == "points");
                        CHECK(variable);
                        CHECK(text->textType == fla::StaticText::TextType::Dynamic);
                    }
    CHECK(field);
}

TEST(swf_movie_masks_with_a_clipping_depth)
{
    const std::string layers =
        "<DOMLayer name=\"window\" layerType=\"mask\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMShape><fills><FillStyle index=\"1\"><SolidColor/></FillStyle></fills>"
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|400 0!400 0|400 400!400 400|0 400!0 400|0 0\"/></edges></DOMShape>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"view\" layerType=\"masked\" parentLayerIndex=\"2\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"inside\"/></elements></DOMFrame></frames></DOMLayer>";
    Parsed game = parse(gamePackage(layers));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    const std::vector<uint8_t> swf = movieOf(*game.document);
    // The mask placed with the depth it clips up to: the clip under it.
    int clipDepth = 0, maskDepth = 0, insideDepth = 0;
    for (const Tag& tag : tagsOf(swf))
    {
        if (tag.sprite != 0 || (tag.code != 26 && tag.code != 70))
            continue;
        const uint8_t flags = tag.body[0];
        const size_t start = tag.code == 70 ? 2 : 1;
        const int depth = tag.body[start] | (tag.body[start + 1] << 8);
        if (flags & 0x40)
        {
            maskDepth = depth;
            // The clip depth is the last field here (no actions, no filters).
            clipDepth = tag.body[tag.body.size() - 2] | (tag.body[tag.body.size() - 1] << 8);
        }
        if (flags & 0x20)
        {
            // Its name, after the matrix.
            const std::string body(tag.body.begin(), tag.body.end());
            if (body.find("inside") != std::string::npos)
                insideDepth = depth;
        }
    }
    CHECK(maskDepth > 0);
    CHECK(insideDepth == maskDepth + 1);
    CHECK(clipDepth == insideDepth);

    // And the import puts the mask back as a mask layer over the clip.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(swf, files, error));
    Parsed back = parse(files);
    CHECK(back.document && !back.document->timelines.empty());
    if (back.document && !back.document->timelines.empty())
    {
        const fla::Timeline& scene = *back.document->timelines.front();
        CHECK(layerNamed(scene, [](const fla::Layer& l) { return l.layerType == fla::Layer::Type::Mask; }) != nullptr);
        CHECK(layerNamed(scene, [](const fla::Layer& l) { return l.layerType == fla::Layer::Type::Masked; }) != nullptr);
    }
}

TEST(swf_movie_moves_a_tweened_clip_a_frame_at_a_time)
{
    const std::string layers =
        "<DOMLayer name=\"slide\"><frames>"
        "<DOMFrame index=\"0\" duration=\"3\" tweenType=\"motion\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"slider\"><matrix><Matrix tx=\"0\"/></matrix>"
        "</DOMSymbolInstance></elements></DOMFrame>"
        "<DOMFrame index=\"3\" duration=\"1\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"slider\"><matrix><Matrix tx=\"90\"/></matrix>"
        "</DOMSymbolInstance></elements></DOMFrame></frames></DOMLayer>";
    Parsed game = parse(gamePackage(layers));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    const std::vector<uint8_t> swf = movieOf(*game.document);
    // Placed once, then moved on each frame after: the same clip, playing on.
    int places = 0, moves = 0;
    for (const Tag& tag : tagsOf(swf))
    {
        if (tag.sprite != 0 || (tag.code != 26 && tag.code != 70))
            continue;
        const std::string body(tag.body.begin(), tag.body.end());
        const bool named = body.find("slider") != std::string::npos;
        if (named && (tag.body[0] & 0x02))
            ++places;
        // A move at the slider's depth: a matrix and nothing else.
        if (tag.body[0] == (0x04 | 0x01))
            ++moves;
    }
    CHECK(places == 1);
    CHECK(moves == 3);
}

TEST(swf_movie_plays_scenes_one_after_another)
{
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"100\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Intro\"><layers><DOMLayer name=\"a\"><frames>"
        "<DOMFrame index=\"0\" duration=\"2\"><elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"1\"><Actionscript><script><![CDATA[gotoAndPlay(\"Game\", 2);]]></script>"
        "</Actionscript><elements/></DOMFrame></frames></DOMLayer></layers></DOMTimeline>"
        "<DOMTimeline name=\"Game\"><layers><DOMLayer name=\"a\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\"><Actionscript><script><![CDATA[gotoAndStop(3); prevScene();]]></script>"
        "</Actionscript><elements/></DOMFrame></frames></DOMLayer></layers></DOMTimeline>"
        "</timelines></DOMDocument>");
    Parsed movie = parse(files);
    CHECK(movie.document && movie.document->timelines.size() == 2);
    if (!movie.document)
        return;
    int frames = 0;
    std::vector<std::string> problems;
    const std::vector<uint8_t> swf = movieOf(*movie.document, &problems, fla::swfmovie::Options(), &frames);
    CHECK(frames == 7);
    CHECK(problems.empty());
    std::vector<std::string> listings;
    for (const Tag& tag : tagsOf(swf))
        if (tag.code == 12)
            listings.push_back(as3::avm1::disassemble(tag.body));
    // The Game scene starts on frame 3 (from 0): its frame 2 is 4, its 3 is 5.
    CHECK(listings.size() == 2);
    if (listings.size() == 2)
    {
        CHECK(listings[0] == "GotoFrame 4\nPlay\nEnd\n");
        CHECK(listings[1] == "GotoFrame 5\nGotoFrame 0\nPlay\nEnd\n");
    }
}

TEST(swf_movie_links_symbols_and_their_classes)
{
    std::map<std::string, std::vector<uint8_t>> files = gamePackage();
    files["LIBRARY/Ball.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Ball\" linkageExportForAS=\"true\" "
        "linkageIdentifier=\"ballClip\" linkageClassName=\"toys.Ball\"><timeline><DOMTimeline name=\"Ball\">"
        "<layers><DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"1\">"
        "<Actionscript><script><![CDATA[#initclip\nvar made = 0;\n#endinitclip\n]]></script></Actionscript>"
        "<elements/></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    std::string document(files["DOMDocument.xml"].begin(), files["DOMDocument.xml"].end());
    document.replace(document.find("<Include href=\"Go.xml\"/>"), 0, "<Include href=\"Ball.xml\"/>");
    files["DOMDocument.xml"] = bytesOf(document);
    Parsed game = parse(files);
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    fla::swfmovie::Options options;
    std::vector<std::string> asked;
    options.findClass = [&asked](const std::string& name) -> std::optional<std::string> {
        asked.push_back(name);
        if (name == "toys.Ball")
            return std::string("class toys.Ball extends MovieClip {\n  function bounce() { _y -= 10; }\n}\n");
        return std::nullopt;
    };
    std::vector<std::string> problems;
    const std::vector<uint8_t> swf = movieOf(*game.document, &problems, options);
    CHECK(problems.empty());
    if (!problems.empty())
        std::printf("    problems: %s\n", text(problems).c_str());
    CHECK(std::find(asked.begin(), asked.end(), "toys.Ball") != asked.end());
    std::vector<std::string> exported;
    int initActions = 0;
    bool registers = false, defines = false;
    for (const Tag& tag : tagsOf(swf))
    {
        if (tag.code == 56)
        {
            const int count = tag.body[0] | (tag.body[1] << 8);
            size_t at = 2;
            for (int i = 0; i < count; ++i)
            {
                at += 2;
                exported.push_back(stringAt(tag.body, at));
                at += exported.back().size() + 1;
            }
        }
        if (tag.code == 59)
        {
            ++initActions;
            const std::string listing =
                as3::avm1::disassemble(std::vector<uint8_t>(tag.body.begin() + 2, tag.body.end()));
            registers = registers || listing.find("\"registerClass\"") != std::string::npos;
            defines = defines || listing.find("Extends") != std::string::npos;
        }
    }
    CHECK(std::find(exported.begin(), exported.end(), "ballClip") != exported.end());
    CHECK(std::find(exported.begin(), exported.end(), "__Packages.toys.Ball") != exported.end());
    // The class, the registration and the #initclip.
    CHECK(initActions == 3);
    CHECK(registers && defines);
}

TEST(swf_movie_tells_of_scripts_that_do_not_compile)
{
    std::map<std::string, std::vector<uint8_t>> files = gamePackage();
    std::string document(files["DOMDocument.xml"].begin(), files["DOMDocument.xml"].end());
    const std::string stop = "<![CDATA[stop();]]>";
    document.replace(document.find(stop), stop.size(), "<![CDATA[stop();\nvar v:Vector.<int> = new <int>[1];]]>");
    files["DOMDocument.xml"] = bytesOf(document);
    Parsed game = parse(files);
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    std::vector<std::string> problems;
    const std::vector<uint8_t> swf = movieOf(*game.document, &problems);
    CHECK(problems.size() == 1);
    CHECK(!problems.empty() && problems[0].find("Scene 1, layer actions, frame 1") == 0);
    // The rest went in all the same.
    CHECK(actionsOf(tagsOf(swf)).size() == 2);
}

TEST(swf_movie_leaves_phoenix_effects_to_the_flat_movie)
{
    std::map<std::string, std::vector<uint8_t>> files = gamePackage();
    std::string document(files["DOMDocument.xml"].begin(), files["DOMDocument.xml"].end());
    const std::string hat = "<DOMSymbolInstance libraryItemName=\"Hat\" name=\"hat\"";
    document.replace(document.find(hat), hat.size(), hat + " phoenixBehaviors=\"wiggle\"");
    files["DOMDocument.xml"] = bytesOf(document);
    Parsed game = parse(files);
    CHECK(game.document != nullptr);
    if (game.document)
        CHECK(fla::swfmovie::unstructured(*game.document).find("Behaviors") == 0);
}

TEST(swf_movie_color_effects_and_filters_as_swf_has_them)
{
    fla::ColorTransform tint;
    tint.tintMultiplier = 0.5;
    tint.tintColor[0] = 255;
    tint.alphaMultiplier = 0.8;
    const swf::ColorTransform made = fla::swfmovie::colorTransform(tint);
    CHECK_NEAR(made.red, 0.5);
    CHECK(made.redAdd == 128 && made.greenAdd == 0);
    CHECK_NEAR(made.alpha, 0.8);
    fla::ColorTransform bright;
    bright.brightness = 0.5;
    bright.redMultiplier = 0.5;
    const swf::ColorTransform lit = fla::swfmovie::colorTransform(bright);
    CHECK_NEAR(lit.red, 0.25);
    CHECK(lit.redAdd == 64 && lit.greenAdd == 128);

    fla::Filter shadow;
    shadow.type = fla::Filter::Type::DropShadow;
    shadow.angle = 90.0;
    shadow.quality = 2;
    fla::Filter off;
    off.enabled = false;
    fla::Filter bevel;
    bevel.type = fla::Filter::Type::Bevel;
    bevel.bevelType = "full";
    const std::vector<swf::Filter> list = fla::swfmovie::filters({shadow, off, bevel});
    CHECK(list.size() == 2);
    if (list.size() == 2)
    {
        CHECK(list[0].type == swf::Filter::Type::DropShadow);
        CHECK_NEAR(list[0].angle, 3.14159265358979323846 / 2.0);
        CHECK(list[0].passes == 2);
        CHECK(list[1].type == swf::Filter::Type::Bevel && list[1].onTop && !list[1].inner);
    }
    CHECK(fla::swfmovie::blendMode("multiply") == 3 && fla::swfmovie::blendMode("") == 1);
}
