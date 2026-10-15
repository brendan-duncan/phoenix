#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
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
    // LZMA movies are named as such.
    CHECK(!SwfImport::convert({'Z', 'W', 'S', 13, 0, 0, 0, 0, 0, 0}, files, error) &&
          error.find("LZMA") != std::string::npos);
}
