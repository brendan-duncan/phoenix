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
#include "../src/edit/graphic_loop.h"
#include "../src/edit/html5_export.h"
#include "../src/edit/swf_actionscript3.h"
#include "../src/edit/swf_movie.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/swf_import.h"
#include "../src/parser/zip_reader.h"
#include "../src/util/swf_writer.h"

#include "abc/abc_file.h"
#include "abc/abc_verifier.h"
#include "avm1/avm1.h"
#include "zlib.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
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
            const bool opened = instance && instance->symbolType == fla::SymbolType::Graphic && instance->symbol &&
                fla::swfmovie::holdsClips(*instance->symbol);
            if (instance && (instance->symbolType != fla::SymbolType::Graphic || opened))
            {
                piece.instance = instance;
                piece.color = instance->colorTransform;
                piece.filters = instance->filters;
                piece.blendMode = instance->blendMode;
                if (opened)
                {
                    int length = 1;
                    for (const fla::Layer* inner : instance->symbol->timelines.front()->layers)
                        for (const fla::Frame* f : inner->frames)
                            length = std::max(length, f->index + f->duration);
                    piece.graphicFrame = fla::graphicFrame(instance->loopType, instance->firstFrame,
                        instance->lastFrame, length, frame - key->index);
                }
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
                // Drawn where it is: placed as it is.
                piece.matrix = swf::Matrix();
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

TEST(swf_movie_takes_hidden_layers_as_the_settings_say)
{
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"100\" height=\"100\" frameRate=\"12\">"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"shown\"><frames><DOMFrame index=\"0\"><Actionscript><script><![CDATA[stop();]]></script>"
        "</Actionscript><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"hidden\" visible=\"false\"><frames><DOMFrame index=\"0\"><Actionscript><script>"
        "<![CDATA[play();]]></script></Actionscript><elements/></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    Parsed movie = parse(files);
    CHECK(movie.document != nullptr);
    if (!movie.document)
        return;
    const auto actions = [&movie](bool hiddenLayers) {
        fla::swfmovie::Options options;
        options.hiddenLayers = hiddenLayers;
        std::string all;
        for (const Tag& tag : tagsOf(movieOf(*movie.document, nullptr, options)))
            if (tag.code == 12)
                all += as3::avm1::disassemble(tag.body);
        return all;
    };
    CHECK(actions(true).find("Play") != std::string::npos);
    CHECK(actions(false).find("Play") == std::string::npos);
    CHECK(actions(false).find("Stop") != std::string::npos);
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

// ---- ActionScript 3

namespace {

/// An ActionScript 3 movie: a frame script on the main timeline that
/// reaches a text field and a clip by name, a clip with a frame script of
/// its own, symbols linked to a class with a file and to one without, and
/// two CheckBox components, one named, with a label set in the inspector.
std::map<std::string, std::vector<uint8_t>> actionScript3Package(const std::string& frameScript,
    const std::string& documentClass = std::string())
{
    std::map<std::string, std::vector<uint8_t>> files;
    const std::string square = "<DOMShape><fills><FillStyle index=\"1\"><SolidColor color=\"#FF0000\"/></FillStyle></fills>"
                               "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/>"
                               "</edges></DOMShape>";
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"24\">"
        "<symbols><Include href=\"Ball.xml\"/><Include href=\"Box.xml\"/><Include href=\"CheckBox.xml\"/>"
        "<Include href=\"Star.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"actions\"><frames><DOMFrame index=\"0\" duration=\"2\">"
        "<Actionscript><script><![CDATA[" +
        frameScript +
        "]]></script></Actionscript><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"ui\"><frames><DOMFrame index=\"0\" duration=\"2\"><elements>"
        "<DOMDynamicText name=\"message\" left=\"0\" top=\"0\" width=\"60\" height=\"20\">"
        "<textRuns><DOMTextRun><characters>0</characters><textAttrs><DOMTextAttrs face=\"Arial\" size=\"14\"/>"
        "</textAttrs></DOMTextRun></textRuns></DOMDynamicText>"
        "<DOMComponentInstance libraryItemName=\"CheckBox\" name=\"agree\"><matrix><Matrix tx=\"10\" ty=\"60\"/></matrix>"
        "<parametersAsXML><![CDATA[<property id=\"label\"><Inspectable name=\"label\" variable=\"label\" "
        "defaultValue=\"I agree\" type=\"String\"/></property><property id=\"selected\"><Inspectable name=\"selected\" "
        "variable=\"selected\" defaultValue=\"true\" type=\"Boolean\"/></property>]]></parametersAsXML>"
        "</DOMComponentInstance>"
        "<DOMComponentInstance libraryItemName=\"CheckBox\"><matrix><Matrix tx=\"110\" ty=\"60\"/></matrix>"
        "</DOMComponentInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"clips\"><frames><DOMFrame index=\"0\" duration=\"2\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Ball\" name=\"ball\"><matrix><Matrix tx=\"20\" ty=\"30\"/></matrix>"
        "</DOMSymbolInstance>"
        "<DOMSymbolInstance libraryItemName=\"Box\" name=\"box\"><matrix><Matrix tx=\"80\" ty=\"30\"/></matrix>"
        "</DOMSymbolInstance>"
        "</elements></DOMFrame></frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    const auto symbol = [&](const std::string& name, const std::string& attributes, const std::string& script) {
        return bytesOf("<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"" + name + "\"" + attributes +
            "><timeline><DOMTimeline name=\"" + name + "\"><layers>" +
            (script.empty() ? std::string()
                            : "<DOMLayer name=\"actions\"><frames><DOMFrame index=\"0\" duration=\"2\"><Actionscript><script>"
                              "<![CDATA[" + script + "]]></script></Actionscript><elements/></DOMFrame></frames></DOMLayer>") +
            "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"2\"><elements>" + square +
            "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    };
    files["LIBRARY/Ball.xml"] = symbol("Ball", "", "stop();");
    files["LIBRARY/Box.xml"] = symbol("Box", " linkageExportForAS=\"true\" linkageClassName=\"shapes.Box\"", "");
    files["LIBRARY/Star.xml"] = symbol("Star", " linkageExportForAS=\"true\" linkageClassName=\"Star\"", "");
    files["LIBRARY/CheckBox.xml"] = bytesOf(
        "<DOMComponentItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"CheckBox\" linkageExportForAS=\"true\" "
        "linkageClassName=\"fl.controls.CheckBox\"><timeline><DOMTimeline name=\"CheckBox\"><layers>"
        "<DOMLayer name=\"avatar\"><frames><DOMFrame index=\"0\"><elements>" +
        square + "</elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMComponentItem>");
    files["PublishSettings.xml"] = bytesOf(
        "<flash_profiles><flash_profile name=\"Default\" current=\"true\"><PublishFlashProperties enabled=\"true\">"
        "<ActionScriptVersion>3</ActionScriptVersion><DocumentClass>" +
        documentClass +
        "</DocumentClass><AS3AutoDeclare>4096</AS3AutoDeclare></PublishFlashProperties></flash_profile>"
        "</flash_profiles>");
    return files;
}

/// A package parsed with its publish settings, as Phoenix opens a file.
Parsed parseWithSettings(const std::map<std::string, std::vector<uint8_t>>& files)
{
    Parsed parsed = parse(files);
    const auto settings = files.find("PublishSettings.xml");
    if (parsed.document && settings != files.end())
        parsed.document->extraFiles.emplace_back("PublishSettings.xml", fla::LazyBytes(settings->second));
    return parsed;
}

/// The classes a test movie finds: its own, and Phoenix's components from
/// the source tree's resources.
std::optional<std::string> testClass(const std::string& name)
{
    if (name == "shapes.Box")
        return std::string("package shapes {\n  import flash.display.MovieClip;\n"
                           "  public class Box extends MovieClip {\n    public var size:Number = 2;\n  }\n}\n");
    if (name == "Main")
        return std::string("package {\n  import flash.display.MovieClip;\n"
                           "  public class Main extends MovieClip {\n    public function Main() { trace(\"main\"); }\n  }\n}\n");
    std::string path = name;
    std::replace(path.begin(), path.end(), '.', '/');
    std::ifstream in(std::filesystem::u8path(std::string(PHOENIX_TEST_DATA) + "/../../resources/as3/" + path + ".as"),
        std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

struct Published
{
    std::vector<uint8_t> swf;
    std::vector<std::string> problems;
    std::vector<std::string> notes;
};

Published publishActionScript3(const fla::Document& document)
{
    Published out;
    fla::swfmovie::ActionScript3Options as3;
    as3.findClass = testClass;
    as3.documentName = "test";
    fla::swfmovie::Options options;
    options.actionScript3 = [&](swf::Writer& writer, const std::map<const fla::Symbol*, int>& sprites) {
        out.notes = fla::swfmovie::writeActionScript3(document, writer, sprites, as3);
    };
    out.swf = movieOf(document, &out.problems, options);
    return out;
}

/// A movie's SymbolClass entries.
std::vector<std::pair<int, std::string>> symbolClassesOf(const std::vector<Tag>& tags)
{
    std::vector<std::pair<int, std::string>> out;
    for (const Tag& tag : tags)
    {
        if (tag.code != 76)
            continue;
        const int count = tag.body[0] | (tag.body[1] << 8);
        size_t at = 2;
        for (int i = 0; i < count; ++i)
        {
            const int id = tag.body[at] | (tag.body[at + 1] << 8);
            const std::string name = stringAt(tag.body, at + 2);
            out.emplace_back(id, name);
            at += 2 + name.size() + 1;
        }
    }
    return out;
}

/// The abcFile of a movie's DoABC tag, empty when it has none.
std::vector<uint8_t> abcOf(const std::vector<Tag>& tags)
{
    for (const Tag& tag : tags)
        if (tag.code == 82)
        {
            const size_t name = 4 + stringAt(tag.body, 4).size() + 1;
            return std::vector<uint8_t>(tag.body.begin() + static_cast<std::ptrdiff_t>(name), tag.body.end());
        }
    return {};
}

} // namespace

TEST(swf_movie_compiles_actionscript_3_to_abc)
{
    Parsed game = parseWithSettings(actionScript3Package("message.text = \"hi\";\nball.x = box.size + agree.width;\nstop();"));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    const Published published = publishActionScript3(*game.document);
    CHECK(published.problems.empty());
    if (!published.problems.empty())
        std::printf("    problems: %s\n", text(published.problems).c_str());
    // The library has none of the components' skins: they are drawn plain.
    for (const std::string& note : published.notes)
        CHECK(note.find("fl.skins.DefaultSkin") != std::string::npos);
    CHECK(published.notes.size() == 1);
    CHECK(!published.notes.empty() && published.notes[0].find("CheckBox_upIcon") != std::string::npos &&
        published.notes[0].find("Button_upSkin") == std::string::npos);

    const std::vector<Tag> tags = tagsOf(published.swf);
    // The ABC first, then the classes of the characters.
    size_t abcAt = tags.size(), classesAt = tags.size(), shownAt = tags.size();
    std::set<int> sprites;
    std::map<std::string, std::vector<int>> placed;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        const Tag& tag = tags[i];
        if (tag.sprite != 0)
            continue;
        if (tag.code == 82 && abcAt == tags.size())
            abcAt = i;
        if (tag.code == 76)
            classesAt = i;
        if (tag.code == 1 && shownAt == tags.size())
            shownAt = i;
        if (tag.code == 39)
            sprites.insert(tag.body[0] | (tag.body[1] << 8));
        if (tag.code == 26 && (tag.body[0] & 0x22) == 0x22)
        {
            // Placed with a name: the name ends the tag's fields here.
            const int character = tag.body[3] | (tag.body[4] << 8);
            for (const char* name : {"agree", "__id0_", "ball", "box", "message"})
            {
                const std::string wanted = std::string(name) + '\0';
                if (std::search(tag.body.begin(), tag.body.end(), wanted.begin(), wanted.end()) != tag.body.end())
                    placed[name].push_back(character);
            }
        }
    }
    CHECK(abcAt < classesAt && classesAt < shownAt);
    const std::vector<std::pair<int, std::string>> classes = symbolClassesOf(tags);
    std::map<std::string, int> idOf;
    for (const auto& [id, name] : classes)
    {
        idOf[name.rfind("test_fla.Ball_", 0) == 0 ? std::string("Ball") : name] = id;
        // Each a sprite of the movie, or the main timeline.
        CHECK(id == 0 || sprites.count(id));
    }
    CHECK(idOf.count("test_fla.MainTimeline") && idOf["test_fla.MainTimeline"] == 0);
    CHECK(idOf.count("Ball") && idOf.count("shapes.Box") && idOf.count("Star") && idOf.count("fl.controls.CheckBox"));
    // The instances placed are of the characters their classes are on.
    CHECK(placed["ball"] == std::vector<int>{idOf["Ball"]});
    CHECK(placed["box"] == std::vector<int>{idOf["shapes.Box"]});
    CHECK(placed["agree"] == std::vector<int>{idOf["fl.controls.CheckBox"]});
    CHECK(placed["__id0_"] == std::vector<int>{idOf["fl.controls.CheckBox"]});
    CHECK(placed["message"].size() == 1);

    // The ABC reads and verifies, and has the classes the movie needs.
    as3::abc::AbcFile file;
    std::string error;
    CHECK(as3::abc::read(abcOf(tags), file, error));
    CHECK(as3::abc::verify(file).empty());
    const std::string listing = as3::abc::disassemble(file);
    for (const char* defined : {" fl.controls:CheckBox extends fl.controls:LabelButton",
             " fl.core:UIComponent extends flash.display:Sprite", " shapes:Box extends flash.display:MovieClip",
             " Star extends flash.display:MovieClip", " test_fla:MainTimeline extends flash.display:MovieClip",
             "slot message slot 0 : flash.text:TextField", "slot agree slot 0 : fl.controls:CheckBox",
             "slot __id0_ slot 0 : fl.controls:CheckBox", "slot box slot 0 : shapes:Box", "componentInspectorSetting"})
    {
        CHECK(listing.find(defined) != std::string::npos);
        if (listing.find(defined) == std::string::npos)
            std::printf("    missing: %s\n", defined);
    }
    // A superclass is defined before the classes that extend it.
    CHECK(listing.find(" fl.core:UIComponent extends") < listing.find(" fl.controls:BaseButton extends"));
    CHECK(listing.find(" fl.controls:BaseButton extends") < listing.find(" fl.controls:LabelButton extends"));
}

TEST(swf_movie_tells_where_actionscript_3_does_not_compile)
{
    Parsed game = parseWithSettings(actionScript3Package("message.text = \"hi\";\nvar = 3;\nstop();"));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    const Published published = publishActionScript3(*game.document);
    bool told = false;
    for (const std::string& note : published.notes)
        told = told || note.rfind("Scene 1, layer actions, frame 1, line 2, column 5: ", 0) == 0;
    CHECK(told);
    if (!told)
        std::printf("    notes: %s\n", text(published.notes).c_str());
    // Nothing that would not run: no ABC, no classes.
    const std::vector<Tag> tags = tagsOf(published.swf);
    CHECK(abcOf(tags).empty());
    CHECK(symbolClassesOf(tags).empty());
}

TEST(swf_movie_puts_a_document_class_on_the_main_timeline)
{
    // With the stage's instances declared by the class under it.
    Parsed game = parseWithSettings(actionScript3Package("", "Main"));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    fla::swfmovie::ActionScript3Options as3;
    as3.findClass = testClass;
    as3.documentName = "test";
    fla::swfmovie::ActionScript3 compiled = fla::swfmovie::compileActionScript3(*game.document, as3);
    CHECK(compiled.mainClass == "test_fla.MainTimeline");
    as3::abc::AbcFile file;
    std::string error;
    CHECK(as3::abc::read(compiled.abc, file, error));
    CHECK(as3::abc::disassemble(file).find(" test_fla:MainTimeline extends Main") != std::string::npos);

    // Without them, the document class itself.
    std::map<std::string, std::vector<uint8_t>> files = actionScript3Package("", "Main");
    std::string dom(files["DOMDocument.xml"].begin(), files["DOMDocument.xml"].end());
    for (size_t at = dom.find("<DOMComponentInstance"); at != std::string::npos; at = dom.find("<DOMComponentInstance"))
        dom.erase(at, dom.find("</DOMComponentInstance>", at) + 23 - at);
    files["DOMDocument.xml"] = bytesOf(dom);
    std::string settings(files["PublishSettings.xml"].begin(), files["PublishSettings.xml"].end());
    settings.replace(settings.find("4096"), 4, "0");
    files["PublishSettings.xml"] = bytesOf(settings);
    Parsed plain = parseWithSettings(files);
    if (!plain.document)
        return;
    compiled = fla::swfmovie::compileActionScript3(*plain.document, as3);
    CHECK(compiled.mainClass == "Main");
    CHECK(std::find(compiled.defined.begin(), compiled.defined.end(), "Main") != compiled.defined.end());
}

namespace {

/// Art as one small shape, for the HTML5 content.
class OneShape : public fla::html5::ArtSource
{
public:
    std::string art(const fla::Element&) override
    {
        return "{\"o\":[{\"p\":\"M0 0L1 0L1 1Z\",\"f\":{\"c\":0,\"a\":1}}],\"b\":[0,0,1,1]}";
    }
    int bitmap(const fla::Bitmap&) override { return -1; }
    int sound(const std::string&) override { return -1; }
    std::string images() override { return "[]"; }
    std::string sounds() override { return "[]"; }
};

} // namespace

TEST(html5_translates_a_movies_actionscript3_as_swf_publishing_compiles_it)
{
    // The same classes SWF publishing compiles, translated for an HTML5
    // page: the document class under the timeline's, the symbols' classes,
    // the components (Phoenix's own, translated too) with their parameters.
    Parsed game = parseWithSettings(actionScript3Package("message.text = String(box.size);", "Main"));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    fla::swfmovie::ActionScript3Options as3;
    as3.findClass = testClass;
    as3.documentName = "test";
    const fla::swfmovie::JavaScript3 translated = fla::swfmovie::translateActionScript3(*game.document, as3);
    CHECK(translated.mainClass == "test_fla.MainTimeline");
    for (const char* defined : {"as3.define(\"test_fla.MainTimeline\"", "class MainTimeline extends Main",
             "as3.define(\"Main\"", "as3.define(\"shapes.Box\"", "as3.define(\"fl.controls.CheckBox\"",
             "this.agree.label = \"I agree\"", "this.addFrameScript(0, as3.bind(this, \"frame1\"))"})
    {
        const bool found = translated.bundle.find(defined) != std::string::npos;
        CHECK(found);
        if (!found)
            std::printf("    not in the bundle: %s\n", defined);
    }
    CHECK(translated.sourceMap.find("\"mappings\"") != std::string::npos);
    // The clip with a frame script of its own has a class made for it.
    bool ball = false;
    for (const auto& [symbol, name] : translated.classes)
        ball = ball || (symbol && symbol->name == "Ball" && name.rfind("test_fla.", 0) == 0);
    CHECK(ball);

    // Its components are on the stage, named as the code setting their
    // parameters names them.
    OneShape art;
    std::vector<std::string> notes;
    const std::string content = fla::html5::content(*game.document, art, fla::html5::Options(), &notes);
    CHECK(content.find("\"n\":\"agree\"") != std::string::npos);
    CHECK(content.find("\"n\":\"__id0_\"") != std::string::npos);
    CHECK(content.find("\"linkage\":\"fl.controls.CheckBox\"") != std::string::npos);
}

TEST(html5_tells_where_actionscript3_does_not_translate)
{
    Parsed game = parseWithSettings(actionScript3Package("var a:int = 1;\nvar b:int = ;"));
    CHECK(game.document != nullptr);
    if (!game.document)
        return;
    fla::swfmovie::ActionScript3Options as3;
    as3.findClass = testClass;
    as3.documentName = "test";
    const fla::swfmovie::JavaScript3 translated = fla::swfmovie::translateActionScript3(*game.document, as3);
    CHECK(translated.bundle.empty());
    CHECK(translated.mainClass.empty());
    bool told = false;
    for (const std::string& note : translated.notes)
        told = told || note.rfind("Scene 1, layer actions, frame 1, line 2", 0) == 0;
    CHECK(told);
    if (!told)
        std::printf("    notes: %s\n", text(translated.notes).c_str());
}

namespace {

/// The placements (PlaceObject2 and 3) on a timeline (0 the main one) by the
/// name they give, and how many there are.
std::map<std::string, int> namesPlaced(const std::vector<Tag>& tags, int sprite)
{
    std::map<std::string, int> out;
    for (const Tag& tag : tags)
    {
        if (tag.sprite != sprite || (tag.code != 26 && tag.code != 70) || !(tag.body[0] & 0x20))
            continue;
        // The name is the only text in these bodies: the longest printable
        // run.
        std::string best, run;
        for (const uint8_t c : tag.body)
        {
            if (c >= 'a' && c <= 'z')
                run += static_cast<char>(c);
            else
            {
                if (run.size() > best.size())
                    best = run;
                run.clear();
            }
        }
        ++out[run.size() > best.size() ? run : best];
    }
    return out;
}

const char* const kSquare = "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0\"/></edges>";

std::string fill(const std::string& color)
{
    return "<fills><FillStyle index=\"1\"><SolidColor color=\"" + color + "\"/></FillStyle></fills>";
}

std::map<std::string, std::vector<uint8_t>> package(const std::string& layers, const std::map<std::string, std::string>& symbols)
{
    std::map<std::string, std::vector<uint8_t>> files;
    std::string includes;
    for (const auto& [name, xml] : symbols)
    {
        includes += "<Include href=\"" + name + ".xml\"/>";
        files["LIBRARY/" + name + ".xml"] = bytesOf(xml);
    }
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"200\" height=\"100\" frameRate=\"12\">"
        "<symbols>" + includes + "</symbols><timelines><DOMTimeline name=\"Scene 1\"><layers>" + layers +
        "</layers></DOMTimeline></timelines></DOMDocument>");
    return files;
}

std::string symbolXml(const std::string& name, const std::string& type, const std::string& layers)
{
    return "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"" + name + "\"" +
        (type.empty() ? std::string() : " symbolType=\"" + type + "\"") + "><timeline><DOMTimeline name=\"" + name +
        "\"><layers>" + layers + "</layers></DOMTimeline></timeline></DOMSymbolItem>";
}

/// A movie clip of three frames, a square on each.
std::string spinner()
{
    return symbolXml("Spinner", "", "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements>"
        "<DOMShape>" + fill("#FF0000") + kSquare + "</DOMShape></elements></DOMFrame></frames></DOMLayer>");
}

/// The movie \a document makes, with an event sound for every keyframe
/// that names one (a sound of its own for each name, in \a sounds).
std::vector<uint8_t> movieWithSounds(const fla::Document& document, std::map<std::string, int>& sounds,
    std::vector<std::string>* problems = nullptr)
{
    swf::Writer writer(static_cast<int>(document.width), static_cast<int>(document.height), document.frameRate,
        swf::Color{255, 255, 255, 255});
    SquareDrawer drawer;
    fla::swfmovie::Options options;
    options.eventSound = [&](const fla::Frame& key, int& id, swf::SoundInfo& info) {
        std::string name;
        for (const auto& [attribute, value] : key.extraAttributes)
            if (attribute == "soundName")
                name = value;
        if (name.empty())
            return false;
        int& known = sounds[name];
        if (!known)
            known = writer.defineSound(3, 11025, false, 4, std::vector<uint8_t>(8, 0));
        id = known;
        info = swf::SoundInfo();
        return true;
    };
    fla::swfmovie::Movie movie(document, writer, drawer, options);
    movie.write();
    if (problems)
        *problems = movie.problems();
    return writer.finish(true);
}

} // namespace

TEST(swf_movie_opens_graphics_with_clips_into_the_timeline)
{
    // A graphic of three frames holds a different named clip on its first
    // frame and on the two after it; played once from its second frame, it
    // shows the second clip throughout, a placed clip scripts reach.
    const std::string box = symbolXml("Box", "graphic",
        "<DOMLayer name=\"clips\"><frames>"
        "<DOMFrame index=\"0\" duration=\"1\"><elements><DOMSymbolInstance libraryItemName=\"Spinner\" name=\"first\">"
        "<matrix><Matrix tx=\"5\"/></matrix></DOMSymbolInstance></elements></DOMFrame>"
        "<DOMFrame index=\"1\" duration=\"2\"><elements><DOMSymbolInstance libraryItemName=\"Spinner\" name=\"later\">"
        "<matrix><Matrix tx=\"5\"/></matrix></DOMSymbolInstance></elements></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"back\"><frames><DOMFrame index=\"0\" duration=\"3\"><elements><DOMShape>" + fill("#00FF00") +
        kSquare + "</DOMShape></elements></DOMFrame></frames></DOMLayer>");
    const std::string layers =
        "<DOMLayer name=\"box\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Box\" symbolType=\"graphic\" loop=\"play once\" firstFrame=\"1\">"
        "<matrix><Matrix tx=\"100\" ty=\"10\"/></matrix><color><Color alphaMultiplier=\"0.5\"/></color>"
        "</DOMSymbolInstance></elements></DOMFrame></frames></DOMLayer>";
    Parsed parsed = parse(package(layers, {{"Box", box}, {"Spinner", spinner()}}));
    CHECK(parsed.document != nullptr);
    if (!parsed.document)
        return;
    const auto found = parsed.document->symbolList->symbolMap.find("Box");
    CHECK(found != parsed.document->symbolList->symbolMap.end() && fla::swfmovie::holdsClips(*found->second));
    std::vector<std::string> problems;
    const std::vector<uint8_t> swf = movieOf(*parsed.document, &problems);
    CHECK(problems.empty());
    const std::vector<Tag> tags = tagsOf(swf);
    // The clip the graphic shows is placed once, by its name, and stays.
    const std::map<std::string, int> placed = namesPlaced(tags, 0);
    CHECK(placed.count("later") && placed.at("later") == 1);
    CHECK(!placed.count("first"));

    // Through the import: a movie clip named later on the scene, where the
    // graphic puts it, faded as the graphic is.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(swf, files, error));
    Parsed back = parse(files);
    CHECK(back.document && !back.document->timelines.empty());
    if (!back.document || back.document->timelines.empty())
        return;
    bool later = false;
    for (const fla::SymbolInstance* instance : instancesOn(*back.document->timelines.front()))
        if (instance->name == "later")
        {
            later = true;
            CHECK(instance->symbolType == fla::SymbolType::MovieClip);
            CHECK_NEAR(instance->transform.tx, 105.0);
            CHECK_NEAR(instance->transform.ty, 10.0);
            CHECK_NEAR(instance->colorTransform.alphaMultiplier, 0.5);
        }
    CHECK(later);
}

TEST(swf_movie_morphs_a_shape_tween)
{
    // A red square that grows into a blue one over four frames: one morph
    // shape, placed with its ratio frame by frame.
    const std::string layers =
        "<DOMLayer name=\"grow\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\" tweenType=\"shape\"><elements><DOMShape>" + fill("#FF0000") + kSquare +
        "</DOMShape></elements></DOMFrame>"
        "<DOMFrame index=\"4\" duration=\"1\"><elements><DOMShape>" + fill("#0000FF") +
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|800 0!800 0|800 800!800 800|0 800!0 800|0 0\"/></edges>"
        "</DOMShape></elements></DOMFrame></frames></DOMLayer>";
    Parsed parsed = parse(package(layers, {}));
    CHECK(parsed.document != nullptr);
    if (!parsed.document)
        return;
    const std::vector<uint8_t> swf = movieOf(*parsed.document);
    const std::vector<Tag> tags = tagsOf(swf);
    int morphs = 0;
    int morph = 0;
    std::vector<int> ratios;
    for (const Tag& tag : tags)
    {
        if (tag.code == 46)
        {
            ++morphs;
            morph = tag.body[0] | (tag.body[1] << 8);
        }
        if (tag.sprite == 0 && tag.code == 26 && (tag.body[0] & 0x10))
        {
            // Flags, depth, the character when placed anew, then the
            // matrix: the ratio is easiest found by its place from the end
            // (no name, no clip depth, no actions follow it).
            ratios.push_back(tag.body[tag.body.size() - 2] | (tag.body[tag.body.size() - 1] << 8));
        }
    }
    CHECK(morphs == 1);
    CHECK(morph > 0);
    // Frames 2 to 4 of the tween, a quarter further each.
    CHECK(ratios.size() == 3);
    if (ratios.size() == 3)
    {
        CHECK(std::abs(ratios[0] - 16384) <= 1);
        CHECK(std::abs(ratios[1] - 32768) <= 1);
        CHECK(std::abs(ratios[2] - 49151) <= 1);
    }
    // Through the import: a shape tween again.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(swf, files, error));
    Parsed back = parse(files);
    bool tween = false;
    if (back.document && !back.document->timelines.empty())
        for (const fla::Layer* layer : back.document->timelines.front()->layers)
            for (const fla::Frame* frame : layer ? layer->frames : std::vector<fla::Frame*>())
                tween = tween || frame->tweenType == fla::TweenType::Shape;
    CHECK(tween);
}

TEST(swf_movie_starts_sounds_on_their_own_timelines_and_buttons)
{
    // A movie clip's sound starts in the clip's own timeline, the scene's
    // in the movie's; a button sounds its Over and Down frames' sounds; a
    // button's mask clips in a clip of its own for each state.
    const std::string clip = symbolXml("Ticker", "",
        "<DOMLayer name=\"art\"><frames><DOMFrame index=\"0\" duration=\"1\"><elements><DOMShape>" + fill("#FF0000") +
        kSquare + "</DOMShape></elements></DOMFrame>"
        "<DOMFrame index=\"1\" duration=\"2\" soundName=\"tick.wav\"><elements><DOMShape>" + fill("#FF0000") + kSquare +
        "</DOMShape></elements></DOMFrame></frames></DOMLayer>");
    const std::string button = symbolXml("Peek", "button",
        "<DOMLayer name=\"sounds\"><frames><DOMFrame index=\"0\" duration=\"1\"><elements/></DOMFrame>"
        "<DOMFrame index=\"1\" duration=\"1\" soundName=\"over.wav\"><elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"2\" soundName=\"down.wav\"><elements/></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"window\" layerType=\"mask\"><frames><DOMFrame index=\"0\" duration=\"4\"><elements><DOMShape>" +
        fill("#000000") + kSquare + "</DOMShape></elements></DOMFrame></frames></DOMLayer>"
        "<DOMLayer name=\"view\" layerType=\"masked\" parentLayerIndex=\"1\"><frames>"
        "<DOMFrame index=\"0\" duration=\"4\"><elements><DOMShape>" + fill("#00FF00") +
        "<edges><Edge fillStyle1=\"1\" edges=\"!0 0|800 0!800 0|800 800!800 800|0 800!0 800|0 0\"/></edges>"
        "</DOMShape></elements></DOMFrame></frames></DOMLayer>");
    const std::string layers =
        "<DOMLayer name=\"ui\"><frames><DOMFrame index=\"0\" duration=\"3\" soundName=\"start.wav\"><elements>"
        "<DOMSymbolInstance libraryItemName=\"Peek\" symbolType=\"button\" name=\"peek\"/>"
        "<DOMSymbolInstance libraryItemName=\"Ticker\" name=\"ticker\"><matrix><Matrix tx=\"50\"/></matrix>"
        "</DOMSymbolInstance></elements></DOMFrame></frames></DOMLayer>";
    Parsed parsed = parse(package(layers, {{"Peek", button}, {"Ticker", clip}}));
    CHECK(parsed.document != nullptr);
    if (!parsed.document)
        return;
    std::map<std::string, int> sounds;
    std::vector<std::string> problems;
    const std::vector<uint8_t> swf = movieWithSounds(*parsed.document, sounds, &problems);
    for (const std::string& problem : problems)
        std::printf("    %s\n", problem.c_str());
    CHECK(problems.empty());
    const std::vector<Tag> tags = tagsOf(swf);
    std::map<int, int> sprites;
    std::multimap<int, int> started;
    int buttonSounds = 0;
    std::vector<uint8_t> buttonSound, defineButton;
    std::set<int> clipping;
    for (const Tag& tag : tags)
    {
        if (tag.code == 15)
            started.emplace(tag.sprite, tag.body[0] | (tag.body[1] << 8));
        if (tag.code == 17)
        {
            ++buttonSounds;
            buttonSound = tag.body;
        }
        if (tag.code == 34)
            defineButton = tag.body;
        // A placement that clips, in a clip.
        if (tag.sprite != 0 && tag.code == 26 && (tag.body[0] & 0x40))
            clipping.insert(tag.sprite);
    }
    // The scene's sound in the movie's timeline; the clip's in the clip's.
    CHECK(sounds.count("start.wav") && sounds.count("tick.wav"));
    bool sceneStarts = false, clipStarts = false;
    for (const auto& [sprite, id] : started)
    {
        sceneStarts = sceneStarts || (sprite == 0 && id == sounds["start.wav"]);
        clipStarts = clipStarts || (sprite != 0 && id == sounds["tick.wav"]);
        CHECK(!(sprite == 0 && id == sounds["tick.wav"]));
    }
    CHECK(sceneStarts);
    CHECK(clipStarts);
    // The button's sounds: none leaving, Over's coming on, Down's pressing,
    // none letting go.
    CHECK(buttonSounds == 1);
    if (buttonSound.size() >= 12)
    {
        CHECK((buttonSound[2] | (buttonSound[3] << 8)) == 0);
        CHECK((buttonSound[4] | (buttonSound[5] << 8)) == sounds["over.wav"]);
        CHECK((buttonSound[7] | (buttonSound[8] << 8)) == sounds["down.wav"]);
        CHECK((buttonSound[10] | (buttonSound[11] << 8)) == 0);
    }
    else
        CHECK(false);
    // Its first record shows a clip that clips with its mask.
    CHECK(defineButton.size() > 8);
    if (defineButton.size() > 8)
    {
        const int character = defineButton[6] | (defineButton[7] << 8);
        CHECK(clipping.count(character) == 1);
    }
}
