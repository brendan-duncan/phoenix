#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/morph_segment.h"
#include "../src/data/morph_shape.h"
#include "../src/data/resource.h"
#include "../src/data/shape.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/swf_import.h"
#include "../src/parser/zip_reader.h"
#include "../src/util/swf_sound.h"
#include "../src/util/swf_writer.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace {

/// A square of \a size pixels at the origin.
std::vector<swf::Segment> square(double size)
{
    std::vector<swf::Segment> outline;
    outline.push_back({swf::Segment::Kind::Move, 0, 0});
    outline.push_back({swf::Segment::Kind::Line, size, 0});
    outline.push_back({swf::Segment::Kind::Line, size, size});
    outline.push_back({swf::Segment::Kind::Line, 0, size});
    outline.push_back({swf::Segment::Kind::Line, 0, 0});
    return outline;
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

/// Bits written most significant first, as SWF shape records are.
struct Bits
{
    std::vector<uint8_t> bytes;
    int used = 8;
    void put(uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            if (used == 8)
            {
                bytes.push_back(0);
                used = 0;
            }
            if ((value >> i) & 1u)
                bytes.back() |= static_cast<uint8_t>(0x80u >> used);
            ++used;
        }
    }
    void signedBits(int value, int count) { put(static_cast<uint32_t>(value) & ((1u << count) - 1u), count); }
    void align() { used = 8; }
    void byte(uint8_t value)
    {
        align();
        bytes.push_back(value);
    }
};

/// A square's four edges from where the pen is, \a size twips a side.
void squareEdges(Bits& bits, int size)
{
    const int deltas[4][2] = {{size, 0}, {0, size}, {-size, 0}, {0, -size}};
    for (const auto& delta : deltas)
    {
        bits.put(1, 1); // an edge
        bits.put(1, 1); // straight
        bits.put(15 - 2, 4);
        bits.put(0, 1); // not both ways
        bits.put(delta[0] == 0 ? 1 : 0, 1); // up and down
        bits.signedBits(delta[0] == 0 ? delta[1] : delta[0], 15);
    }
}

/// A tag with a long header, as nested in a sprite's body.
std::vector<uint8_t> rawTag(int code, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> out = {static_cast<uint8_t>(((code << 6) | 0x3F) & 0xFF), static_cast<uint8_t>(code >> 2)};
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>((body.size() >> (8 * i)) & 0xFF));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/// A DefineSprite body: its id, one frame, then \a tags.
std::vector<uint8_t> spriteBody(int id, const std::vector<uint8_t>& tags)
{
    std::vector<uint8_t> body = {static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8), 1, 0};
    body.insert(body.end(), tags.begin(), tags.end());
    return body;
}

/// PlaceObject2 of character \a id at depth 1.
std::vector<uint8_t> placeTag(int id)
{
    return rawTag(26, {0x02, 1, 0, static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8)});
}

} // namespace

TEST(swf_import_brings_back_the_stage_shapes_and_frames)
{
    // A red square that moves over three frames, and a blue one placed once.
    swf::Writer writer(320, 240, 12.0, {10, 20, 30, 255});
    swf::Fill red;
    red.color = {255, 0, 0, 255};
    const int moving = writer.defineShape(red, square(50));
    swf::Fill blue;
    blue.color = {0, 0, 255, 128};
    const int still = writer.defineShape(blue, square(20));
    for (int f = 0; f < 3; ++f)
    {
        if (f > 0)
        {
            writer.remove(1);
            writer.remove(2);
        }
        writer.place(moving, 1);
        if (f == 0 || f == 1)
            writer.place(still, 2);
        writer.showFrame();
    }
    const std::vector<uint8_t> movie = writer.finish(true);

    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::isSwf(movie.data(), movie.size()));
    CHECK(SwfImport::convert(movie, files, error, &report));
    CHECK(files.count("DOMDocument.xml") == 1);
    CHECK(report.find("baked") != std::string::npos);
    Parsed parsed = parse(files);
    fla::Document* document = parsed.document;
    CHECK(document != nullptr);
    if (!document)
        return;
    CHECK(document->width == 320 && document->height == 240);
    CHECK(std::fabs(document->frameRate - 12.0) < 1e-6);
    CHECK(document->backgroundColor[0] == 10 && document->backgroundColor[2] == 30);
    CHECK(document->timelines.size() == 1);
    fla::Timeline* scene = document->timelines[0];
    // A layer for each depth, the deeper one last.
    CHECK(scene->layers.size() == 2);
    if (scene->layers.size() != 2)
        return;
    // The writer places anew each frame, so each frame is a keyframe of
    // its own; the square drawn on it as a shape.
    fla::Layer* bottom = scene->layers[1];
    CHECK(bottom->frames.size() == 3);
    // Placed in three spans, the red square is a graphic symbol.
    CHECK(document->symbolList && document->symbolList->symbolMap.size() == 2);
    // The blue one's two spans make it a symbol too; its layer is empty on
    // the last frame.
    fla::Layer* top = scene->layers[0];
    CHECK(top->frames.size() == 3 && top->frames[2]->elements.empty());
}

TEST(swf_import_draws_a_shape_placed_once_where_it_is)
{
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    swf::Fill green;
    green.color = {0, 255, 0, 255};
    writer.place(writer.defineShape(green, square(10)), 1);
    writer.showFrame();
    writer.showFrame();
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(false), files, error));
    Parsed parsed = parse(files);
    CHECK(parsed.document && parsed.document->timelines.size() == 1);
    if (!parsed.document)
        return;
    fla::Layer* layer = parsed.document->timelines[0]->layers[0];
    // One keyframe across both frames, holding the shape itself.
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 2);
    CHECK(layer->frames[0]->elements.size() == 1);
    const auto* shape = dynamic_cast<const fla::Shape*>(layer->frames[0]->elements.empty() ? nullptr
                                                                                          : layer->frames[0]->elements[0]);
    CHECK(shape && shape->fills.size() == 1 && !shape->edges.empty());
    CHECK(!parsed.document->symbolList || parsed.document->symbolList->symbolMap.empty());
}

TEST(swf_import_keeps_bitmaps_and_the_soundtrack)
{
    swf::Writer writer(64, 64, 10.0, {255, 255, 255, 255});
    std::vector<uint8_t> pixels(8 * 8 * 4, 0xFF);
    const int bitmap = writer.defineBitmap(8, 8, pixels);
    swf::Fill fill;
    fill.type = swf::Fill::Type::Bitmap;
    fill.bitmap = bitmap;
    const int shape = writer.defineShape(fill, square(8));
    // A second of tone, streamed as ADPCM.
    std::vector<int16_t> tone;
    for (int i = 0; i < 11025; ++i)
        tone.push_back(static_cast<int16_t>(std::sin(i * 0.25) * 8000));
    const swf::SoundStream stream = swf::adpcmStream(tone, 1, 11025, 4, 10.0, 10);
    writer.soundStream(stream.codec, stream.rate, stream.stereo, stream.samplesPerFrame);
    writer.place(shape, 1);
    for (const auto& block : stream.blocks)
    {
        writer.soundBlock(block);
        writer.showFrame();
    }
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(true), files, error));
    // The bitmap's pixels and the sound's samples, under bin.
    int data = 0;
    for (const auto& [path, bytes] : files)
        if (path.rfind("bin/", 0) == 0)
            ++data;
    CHECK(data == 2);
    const auto xml = files["DOMDocument.xml"];
    const std::string text(xml.begin(), xml.end());
    CHECK(text.find("<DOMBitmapItem") != std::string::npos && text.find("BitmapFill") != std::string::npos);
    CHECK(text.find("soundSync=\"stream\"") != std::string::npos && text.find("11kHz 16bit Mono") != std::string::npos);
    Parsed parsed = parse(files);
    CHECK(parsed.document != nullptr);
}

TEST(swf_import_keeps_button_sounds_on_their_state_frames)
{
    swf::Writer writer(64, 64, 12.0, {255, 255, 255, 255});
    swf::Fill red;
    red.color = {255, 0, 0, 255};
    const int shape = writer.defineShape(red, square(10));
    // Sound 50: a tenth of a second of raw 16-bit mono at 11 kHz.
    std::vector<uint8_t> sound = {50, 0, 0x36, 0x4D, 0x04, 0, 0};
    sound.resize(sound.size() + 1101 * 2, 0);
    writer.tag(14, sound);
    // Button 60: the shape in every state.
    writer.tag(7, {60, 0, 0x0F, static_cast<uint8_t>(shape), 0, 1, 0, 0x00, 0x00, 0x00});
    // Its sounds: none on rolling out, sound 50 on rolling over and on
    // pressing, none on releasing.
    writer.tag(17, {60, 0, 0, 0, 50, 0, 0x00, 50, 0, 0x00, 0, 0});
    writer.place(60, 1);
    writer.showFrame();
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(false), files, error));
    std::string button;
    for (const auto& [path, bytes] : files)
    {
        const std::string text(bytes.begin(), bytes.end());
        if (text.find("symbolType=\"button\"") != std::string::npos && text.find("<DOMTimeline") != std::string::npos)
            button = text;
    }
    CHECK(!button.empty());
    // The sound on the Over and Down frames, and on no other.
    size_t count = 0;
    for (size_t at = button.find("soundName="); at != std::string::npos; at = button.find("soundName=", at + 1))
        ++count;
    CHECK(count == 2);
    const size_t first = button.find("soundName=");
    const size_t start = button.rfind("<DOMFrame", first);
    CHECK(start != std::string::npos && button.find("index=\"1\"", start) < first);
}

TEST(swf_import_turns_down_what_is_not_a_movie)
{
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(!SwfImport::convert({'P', 'K', 3, 4, 0, 0, 0, 0}, files, error) && !error.empty());
    // An LZMA movie cut short is damaged.
    CHECK(!SwfImport::convert({'Z', 'W', 'S', 13, 0, 0, 0, 0, 0, 0}, files, error) &&
          error.find("damaged") != std::string::npos);
}

TEST(swf_import_cuts_the_main_timeline_into_its_scenes)
{
    // Four frames, a square on each; DefineSceneAndFrameLabelData says
    // frames 1-2 are "Intro" and 3-4 "Main", and labels frame 4 "end".
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    swf::Fill green;
    green.color = {0, 255, 0, 255};
    const int shape = writer.defineShape(green, square(10));
    for (int f = 0; f < 4; ++f)
    {
        if (f > 0)
            writer.remove(1);
        writer.place(shape, 1);
        writer.showFrame();
    }
    std::vector<uint8_t> movie = writer.finish(false);
    std::vector<uint8_t> body = {2, 0};
    for (const char c : std::string("Intro"))
        body.push_back(static_cast<uint8_t>(c));
    body.push_back(0);
    body.push_back(2);
    for (const char c : std::string("Main"))
        body.push_back(static_cast<uint8_t>(c));
    body.push_back(0);
    body.push_back(1);
    body.push_back(3);
    for (const char c : std::string("end"))
        body.push_back(static_cast<uint8_t>(c));
    body.push_back(0);
    std::vector<uint8_t> tag = {static_cast<uint8_t>((86 << 6) | 0x3F), static_cast<uint8_t>(86 >> 2)};
    for (int i = 0; i < 4; ++i)
        tag.push_back(static_cast<uint8_t>((body.size() >> (8 * i)) & 0xFF));
    tag.insert(tag.end(), body.begin(), body.end());
    // After the header and FileAttributes, the first tag.
    size_t at = 8;
    at += static_cast<size_t>((5 + 4 * (movie[at] >> 3) + 7) / 8) + 4;
    at += 2 + static_cast<size_t>((movie[at] | (movie[at + 1] << 8)) & 0x3F);
    movie.insert(movie.begin() + static_cast<std::ptrdiff_t>(at), tag.begin(), tag.end());
    const uint32_t length = static_cast<uint32_t>(movie.size());
    for (int i = 0; i < 4; ++i)
        movie[4 + i] = static_cast<uint8_t>((length >> (8 * i)) & 0xFF);

    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(movie, files, error));
    Parsed parsed = parse(files);
    CHECK(parsed.document && parsed.document->timelines.size() == 2);
    if (!parsed.document || parsed.document->timelines.size() != 2)
        return;
    CHECK(parsed.document->timelines[0]->name == "Intro" && parsed.document->timelines[1]->name == "Main");
    // Two frames each, and the label on the second scene's second frame.
    const auto length2 = [](const fla::Timeline* timeline) {
        int frames = 0;
        for (const fla::Layer* layer : timeline->layers)
            for (const fla::Frame* frame : layer->frames)
                frames = std::max(frames, frame->index + frame->duration);
        return frames;
    };
    CHECK(length2(parsed.document->timelines[0]) == 2 && length2(parsed.document->timelines[1]) == 2);
    bool labeled = false;
    for (const fla::Layer* layer : parsed.document->timelines[1]->layers)
        for (const fla::Frame* frame : layer->frames)
            labeled = labeled || (frame->name == "end" && frame->index == 1);
    CHECK(labeled);
}

TEST(swf_import_stacks_each_style_chunk_of_a_shape)
{
    // One DefineShape2 drawn in two style chunks: a blue square with a
    // black outline, then new styles and a red square over its corner.
    // Flash draws the first chunk's fill and outline before the second
    // chunk's fill, so the red covers the outline there.
    Bits bits;
    bits.byte(1);
    bits.byte(0); // id 1
    bits.put(15, 5);
    for (int v : {0, 2000, 0, 2000})
        bits.signedBits(v, 15);
    bits.byte(1); // fills
    bits.byte(0x00);
    for (uint8_t c : {0, 0, 255})
        bits.byte(c);
    bits.byte(1); // lines
    bits.byte(200);
    bits.byte(0);
    for (uint8_t c : {0, 0, 0})
        bits.byte(c);
    bits.align();
    bits.put(1, 4);
    bits.put(1, 4);
    // Move to the first square, fill 1 and line 1.
    bits.put(0, 1);
    bits.put(0, 1); // new styles
    bits.put(1, 1); // line
    bits.put(1, 1); // fill 1
    bits.put(0, 1); // fill 0
    bits.put(1, 1); // move
    bits.put(15, 5);
    bits.signedBits(0, 15);
    bits.signedBits(0, 15);
    bits.put(1, 1);
    bits.put(1, 1);
    squareEdges(bits, 1000);
    // New styles: a red fill, no line.
    bits.put(0, 1);
    bits.put(1, 1);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.byte(1);
    bits.byte(0x00);
    for (uint8_t c : {255, 0, 0})
        bits.byte(c);
    bits.byte(0);
    bits.align();
    bits.put(1, 4);
    bits.put(0, 4);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.put(1, 1); // fill 1
    bits.put(0, 1);
    bits.put(1, 1); // move
    bits.put(15, 5);
    bits.signedBits(500, 15);
    bits.signedBits(500, 15);
    bits.put(1, 1);
    squareEdges(bits, 1000);
    bits.put(0, 6); // the end
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    writer.tag(22, bits.bytes);
    writer.place(1, 1);
    writer.showFrame();
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(false), files, error));
    Parsed parsed = parse(files);
    CHECK(parsed.document && parsed.document->timelines.size() == 1);
    if (!parsed.document)
        return;
    const fla::Frame* frame = parsed.document->timelines[0]->layers[0]->frames[0];
    CHECK(frame->elements.size() == 1);
    // A group of the two, the outlined square underneath.
    const auto* group = dynamic_cast<const fla::Group*>(frame->elements.empty() ? nullptr : frame->elements[0]);
    CHECK(group && group->members.size() == 2);
    if (!group || group->members.size() != 2)
        return;
    const auto* below = dynamic_cast<const fla::Shape*>(group->members[0]);
    const auto* above = dynamic_cast<const fla::Shape*>(group->members[1]);
    CHECK(below && below->fills.size() == 1 && below->strokes.size() == 1);
    CHECK(above && above->fills.size() == 1 && above->strokes.empty());
}

TEST(swf_import_survives_sprites_nested_in_themselves)
{
    // Sprite 5 holds a sprite also numbered 5, which places itself; then a
    // tower of sprites nested 300 deep.
    const std::vector<uint8_t> frame = rawTag(1, {});
    std::vector<uint8_t> outerTags = rawTag(39, spriteBody(5, placeTag(5)));
    outerTags.insert(outerTags.end(), frame.begin(), frame.end());
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    writer.tag(39, spriteBody(5, outerTags));
    std::vector<uint8_t> tower = frame;
    for (int level = 300; level >= 10; --level)
        tower = rawTag(39, spriteBody(level, tower));
    writer.tag(39, std::vector<uint8_t>(tower.begin() + 6, tower.end()));
    writer.place(5, 1);
    writer.place(10, 2);
    writer.showFrame();
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(writer.finish(false), files, error, &report));
    CHECK(report.find("nested too deeply") != std::string::npos);
}

TEST(swf_import_survives_a_long_chain_of_sprites)
{
    // Each sprite places the one before it, 2000 deep.
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    const std::vector<uint8_t> frame = rawTag(1, {});
    for (int id = 1; id <= 2000; ++id)
    {
        std::vector<uint8_t> tags = id > 1 ? placeTag(id - 1) : std::vector<uint8_t>();
        tags.insert(tags.end(), frame.begin(), frame.end());
        writer.tag(39, spriteBody(id, tags));
    }
    writer.place(2000, 1);
    writer.showFrame();
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(writer.finish(false), files, error, &report));
    CHECK(report.find("nested too deeply") != std::string::npos);
}

TEST(swf_import_survives_damaged_headers)
{
    // A font name longer than its tag, a bitmap claiming 65535 pixels a
    // side, and a compressed movie claiming 4 GB.
    swf::Writer writer(100, 100, 24.0, {255, 255, 255, 255});
    writer.tag(10, {1, 0, 2, 0});
    writer.tag(13, {1, 0, 200, 'A'});
    writer.tag(20, {2, 0, 5, 0xFF, 0xFF, 0xFF, 0xFF, 0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01});
    writer.showFrame();
    std::vector<uint8_t> movie = writer.finish(true);
    for (size_t i = 4; i < 8; ++i)
        movie[i] = 0xFF;
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(movie, files, error, &report));
    CHECK(report.find("too large") != std::string::npos);
    // ADPCM claiming far more samples than its data holds.
    const std::vector<int16_t> samples = swf::decodeAdpcm({0x00, 0x00, 0x00}, 1, 0xFFFFFFFFu);
    CHECK(samples.size() <= 13);
}

namespace {

/// PlaceObject2 of character \a id at depth 1, or a move of what is there,
/// with the matrix (a, b, c, d, then tx, ty in twips) and, unless 256, an
/// alpha multiplier in 256ths.
std::vector<uint8_t> placeAt(int id, bool move, double a, double b, double c, double d, int tx, int ty,
    int alpha = 256)
{
    Bits bits;
    bits.byte(static_cast<uint8_t>((move ? 0x01 : 0x02) | 0x04 | (alpha != 256 ? 0x08 : 0)));
    bits.byte(1);
    bits.byte(0);
    if (!move)
    {
        bits.byte(static_cast<uint8_t>(id & 0xFF));
        bits.byte(static_cast<uint8_t>(id >> 8));
    }
    bits.align();
    const auto fixed = [](double v) { return static_cast<int>(std::lround(v * 65536.0)); };
    bits.put(1, 1);
    bits.put(22, 5);
    bits.signedBits(fixed(a), 22);
    bits.signedBits(fixed(d), 22);
    bits.put(1, 1);
    bits.put(22, 5);
    bits.signedBits(fixed(b), 22);
    bits.signedBits(fixed(c), 22);
    bits.put(20, 5);
    bits.signedBits(tx, 20);
    bits.signedBits(ty, 20);
    if (alpha != 256)
    {
        bits.align();
        bits.put(0, 1); // no offsets
        bits.put(1, 1); // multipliers
        bits.put(10, 4);
        for (int m : {256, 256, 256, alpha})
            bits.signedBits(m, 10);
    }
    return bits.bytes;
}

struct Converted
{
    Parsed parsed;
    std::string report;
    const fla::Layer* layer = nullptr;
};

/// A movie of \a frames frames, each placing a red square by \a placeFrame
/// (the shape's id and the frame to a PlaceObject2), converted with
/// \a options; its one layer.
Converted convertFrames(int frames, const std::function<std::vector<uint8_t>(int, int)>& placeFrame,
    const SwfImport::Options& options = SwfImport::Options())
{
    swf::Writer writer(400, 400, 24.0, {255, 255, 255, 255});
    swf::Fill red;
    red.color = {255, 0, 0, 255};
    const int shape = writer.defineShape(red, square(20));
    for (int f = 0; f < frames; ++f)
    {
        writer.tag(26, placeFrame(shape, f));
        writer.showFrame();
    }
    Converted converted;
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(false), files, error, &converted.report, options));
    converted.parsed = parse(files);
    if (converted.parsed.document && !converted.parsed.document->timelines.empty() &&
        !converted.parsed.document->timelines[0]->layers.empty())
        converted.layer = converted.parsed.document->timelines[0]->layers[0];
    return converted;
}

} // namespace

TEST(swf_import_puts_even_movement_back_as_a_classic_tween)
{
    // Ten frames sliding right 5 pixels a frame and fading out, then four
    // frames held.
    const auto slide = [](int shape, int f) {
        const int at = std::min(f, 9);
        return placeAt(shape, f > 0, 1, 0, 0, 1, at * 100, 400, 256 - at * 24);
    };
    Converted converted = convertFrames(14, slide);
    CHECK(converted.layer != nullptr);
    if (!converted.layer)
        return;
    // A tween over frames 0 to 8, and the last keyframe held to the end.
    CHECK(converted.layer->frames.size() == 2);
    if (converted.layer->frames.size() != 2)
        return;
    const fla::Frame* tween = converted.layer->frames[0];
    CHECK(tween->tweenType == fla::TweenType::Motion && tween->duration == 9 && tween->acceleration == 0);
    CHECK(converted.layer->frames[1]->index == 9 && converted.layer->frames[1]->duration == 5);
    CHECK(converted.layer->frames[1]->tweenType == fla::TweenType::None);
    CHECK(converted.report.find("1 classic tween;") != std::string::npos);

    // Kept baked when asked: a keyframe for each placement.
    SwfImport::Options baked;
    baked.recognizeTweens = false;
    Converted kept = convertFrames(14, slide, baked);
    CHECK(kept.layer && kept.layer->frames.size() == 14);
}

TEST(swf_import_finds_the_ease_and_turn_of_a_tween)
{
    // Easing in (-50) across 2000 twips over 10 frames.
    Converted eased = convertFrames(11, [](int shape, int f) {
        const double t = f / 10.0;
        const double p = t * 0.5 + t * t * 0.5;
        return placeAt(shape, f > 0, 1, 0, 0, 1, static_cast<int>(std::lround(2000 * p)), 0);
    });
    CHECK(eased.layer && eased.layer->frames.size() == 2);
    if (eased.layer && !eased.layer->frames.empty())
        CHECK(eased.layer->frames[0]->tweenType == fla::TweenType::Motion &&
              eased.layer->frames[0]->acceleration == -50);

    // A whole turn clockwise over 12 frames about the origin, ending as it
    // started.
    Converted spun = convertFrames(13, [](int shape, int f) {
        const double angle = f * 3.14159265358979323846 / 6.0;
        return placeAt(shape, f > 0, std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 4000, 4000);
    });
    CHECK(spun.layer && spun.layer->frames.size() == 2);
    if (spun.layer && !spun.layer->frames.empty())
    {
        const fla::Frame* tween = spun.layer->frames[0];
        CHECK(tween->tweenType == fla::TweenType::Motion && tween->duration == 12);
        CHECK(tween->motionTweenRotate == "clockwise" && tween->motionTweenRotateTimes == 1);
    }
}

TEST(swf_import_keeps_uneven_frames_baked)
{
    // Jumping about: no tween shows these, so each frame is a keyframe.
    const int xs[6] = {0, 100, 150, 400, 410, 900};
    Converted converted = convertFrames(6, [&xs](int shape, int f) {
        return placeAt(shape, f > 0, 1, 0, 0, 1, xs[f], 0);
    });
    CHECK(converted.layer && converted.layer->frames.size() == 6);
    if (converted.layer)
        for (const fla::Frame* frame : converted.layer->frames)
            CHECK(frame->tweenType == fla::TweenType::None);
    CHECK(converted.report.find("tween") == std::string::npos);
}

TEST(swf_import_puts_a_morph_back_as_a_shape_tween)
{
    // Morph 3: a red square 50 twips a side growing to a blue one of 100,
    // shown at ratios running evenly over ten frames.
    Bits head;
    head.byte(3);
    head.byte(0);
    for (int r = 0; r < 2; ++r)
    {
        head.align();
        head.put(15, 5);
        for (int v : {0, 2000, 0, 2000})
            head.signedBits(v, 15);
    }
    Bits body;
    body.byte(1); // one fill, solid
    body.byte(0x00);
    for (uint8_t c : {255, 0, 0, 255, 0, 0, 255, 255})
        body.byte(c);
    body.byte(0); // no lines
    body.align();
    body.put(1, 4);
    body.put(0, 4);
    for (int flag : {0, 0, 0, 0, 1, 1})
        body.put(static_cast<uint32_t>(flag), 1); // fill 0 and a move
    body.put(15, 5);
    body.signedBits(0, 15);
    body.signedBits(0, 15);
    body.put(1, 1);
    squareEdges(body, 1000);
    body.put(0, 6);
    Bits end;
    end.put(0, 4);
    end.put(0, 4);
    for (int flag : {0, 0, 0, 0, 0, 1})
        end.put(static_cast<uint32_t>(flag), 1);
    end.put(15, 5);
    end.signedBits(0, 15);
    end.signedBits(0, 15);
    squareEdges(end, 2000);
    end.put(0, 6);
    std::vector<uint8_t> tag = head.bytes;
    const uint32_t offset = static_cast<uint32_t>(body.bytes.size());
    for (int i = 0; i < 4; ++i)
        tag.push_back(static_cast<uint8_t>((offset >> (8 * i)) & 0xFF));
    tag.insert(tag.end(), body.bytes.begin(), body.bytes.end());
    tag.insert(tag.end(), end.bytes.begin(), end.bytes.end());

    swf::Writer writer(200, 200, 24.0, {255, 255, 255, 255});
    writer.tag(46, tag);
    for (int f = 0; f <= 10; ++f)
    {
        const int ratio = static_cast<int>(std::lround(65535.0 * f / 10.0));
        if (f == 0)
            writer.tag(26, {0x12, 1, 0, 3, 0, static_cast<uint8_t>(ratio & 0xFF), static_cast<uint8_t>(ratio >> 8)});
        else
            writer.tag(26, {0x11, 1, 0, static_cast<uint8_t>(ratio & 0xFF), static_cast<uint8_t>(ratio >> 8)});
        writer.showFrame();
    }
    const std::vector<uint8_t> movie = writer.finish(false);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(movie, files, error, &report));
    CHECK(report.find("1 shape tween") != std::string::npos);
    Parsed parsed = parse(files);
    CHECK(parsed.document && parsed.document->timelines.size() == 1);
    if (!parsed.document)
        return;
    const fla::Layer* layer = parsed.document->timelines[0]->layers[0];
    CHECK(layer->frames.size() == 2);
    if (layer->frames.size() != 2)
        return;
    const fla::Frame* tween = layer->frames[0];
    CHECK(tween->tweenType == fla::TweenType::Shape && tween->duration == 10);
    // One outline of four sides, from the small square's corner to the
    // large one's.
    CHECK(tween->morphShape && tween->morphShape->segments.size() == 1);
    if (tween->morphShape && tween->morphShape->segments.size() == 1)
    {
        const fla::MorphSegment* segment = tween->morphShape->segments[0];
        CHECK(segment->curves.size() == 4 && segment->fillIndex1 == 1);
        if (segment->curves.size() == 4)
        {
            CHECK(std::fabs(segment->curves[1]->anchorPointA.x - 50) < 1e-6 &&
                  std::fabs(segment->curves[1]->anchorPointA.y - 50) < 1e-6);
            CHECK(std::fabs(segment->curves[1]->anchorPointB.x - 100) < 1e-6 &&
                  std::fabs(segment->curves[1]->anchorPointB.y - 100) < 1e-6);
        }
    }
    CHECK(layer->frames[1]->index == 10 && layer->frames[1]->elements.size() == 1);

    // Kept baked when asked.
    std::map<std::string, std::vector<uint8_t>> baked;
    SwfImport::Options options;
    options.recognizeTweens = false;
    CHECK(SwfImport::convert(movie, baked, error, nullptr, options));
    Parsed kept = parse(baked);
    CHECK(kept.document && kept.document->timelines[0]->layers[0]->frames.size() == 11);
}

TEST(swf_import_brings_back_a_video_as_an_item_of_its_size)
{
    // Video 7: 48 frames of 320x240 VP6, shown over three frames with a
    // picture each.
    swf::Writer writer(400, 300, 24.0, {255, 255, 255, 255});
    writer.tag(60, {7, 0, 48, 0, 0x40, 0x01, 0xF0, 0x00, 0x00, 4});
    for (int f = 0; f < 3; ++f)
    {
        writer.tag(61, {7, 0, static_cast<uint8_t>(f), 0, 0x00});
        if (f == 0)
            writer.tag(26, {0x02, 1, 0, 7, 0});
        else
            writer.tag(26, {0x11, 1, 0, static_cast<uint8_t>(f), 0}); // a move to picture f
        writer.showFrame();
    }
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error, report;
    CHECK(SwfImport::convert(writer.finish(false), files, error, &report));
    CHECK(report.find("video pictures") != std::string::npos);
    Parsed parsed = parse(files);
    CHECK(parsed.document && parsed.document->videos.size() == 1);
    if (!parsed.document || parsed.document->videos.size() != 1)
        return;
    const fla::Document::VideoInfo& video = parsed.document->videos[0];
    CHECK(video.width == 320 && video.height == 240 && std::fabs(video.fps - 24.0) < 1e-9);
    const auto xml = files["DOMDocument.xml"];
    CHECK(std::string(xml.begin(), xml.end()).find("length=\"2\"") != std::string::npos);
    // Placed on the stage, one keyframe across the pictures it plays, on a
    // hidden layer as Phoenix cannot draw them.
    const fla::Layer* layer = parsed.document->timelines[0]->layers[0];
    CHECK(!layer->visible);
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 3);
    CHECK(layer->frames[0]->elements.size() == 1 &&
          layer->frames[0]->elements[0]->elementType() == fla::Element::Type::Raw);
}
