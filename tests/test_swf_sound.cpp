#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/edit/publish_settings.h"
#include "../src/edit/swf_event_sounds.h"
#include "../src/parser/document_parser.h"
#include "../src/parser/swf_import.h"
#include "../src/parser/zip_reader.h"
#include "../src/util/swf_sound.h"
#include "../src/util/swf_writer.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// A tag: its code and body.
using Tag = std::pair<int, std::vector<uint8_t>>;

/// The tags of an SWF's body (or a DefineSprite's, from \a at), in order.
std::vector<Tag> tagsFrom(const std::vector<uint8_t>& data, size_t at)
{
    std::vector<Tag> tags;
    while (at + 2 <= data.size())
    {
        const int header = data[at] | (data[at + 1] << 8);
        at += 2;
        size_t length = static_cast<size_t>(header & 0x3F);
        if (length == 0x3F)
        {
            length = data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) | (static_cast<size_t>(data[at + 3]) << 24);
            at += 4;
        }
        tags.emplace_back(header >> 6, std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(at),
                                           data.begin() + static_cast<std::ptrdiff_t>(at + length)));
        at += length;
    }
    return tags;
}

/// The tags of an uncompressed SWF.
std::vector<Tag> tagsOf(const std::vector<uint8_t>& file)
{
    const int bits = file[8] >> 3;
    return tagsFrom(file, 8 + static_cast<size_t>((5 + 4 * bits + 7) / 8) + 4);
}

uint32_t u32At(const std::vector<uint8_t>& data, size_t at)
{
    return data[at] | (data[at + 1] << 8) | (data[at + 2] << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
}

int u16At(const std::vector<uint8_t>& data, size_t at)
{
    return data[at] | (data[at + 1] << 8);
}

/// A document read from the files of an XFL package in memory.
struct Package
{
    std::unique_ptr<ZipReader> reader;
    std::unique_ptr<fla::FLADocument> root;
    fla::Document* document = nullptr;
};

Package open(const std::map<std::string, std::vector<uint8_t>>& files)
{
    Package package;
    package.reader.reset(new ZipReader());
    package.reader->openMemory(files);
    package.root.reset(new fla::FLADocument(nullptr));
    DocumentParser parser(package.reader.get());
    const auto& xml = files.at("DOMDocument.xml");
    package.document = parser.parse(std::string(xml.begin(), xml.end()), package.root.get());
    package.root->document = package.document;
    return package;
}

std::vector<uint8_t> bytesOf(const std::string& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// A second of a 440 Hz tone at \a rate, interleaved for \a channels.
std::vector<int16_t> tone(int rate, int channels, size_t count)
{
    std::vector<int16_t> samples;
    for (size_t i = 0; i < count; ++i)
    {
        const double value = std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / rate) * 12000.0;
        for (int c = 0; c < channels; ++c)
            samples.push_back(static_cast<int16_t>(c ? -value : value));
    }
    return samples;
}

/// The tag codes of an uncompressed SWF, past its header.
std::vector<int> tagCodes(const std::vector<uint8_t>& file)
{
    size_t at = 8;
    const int bits = file[at] >> 3;
    at += static_cast<size_t>((5 + 4 * bits + 7) / 8) + 4;
    std::vector<int> codes;
    while (at + 2 <= file.size())
    {
        const int header = file[at] | (file[at + 1] << 8);
        at += 2;
        size_t length = static_cast<size_t>(header & 0x3F);
        if (length == 0x3F)
        {
            length = file[at] | (file[at + 1] << 8) | (file[at + 2] << 16) | (static_cast<size_t>(file[at + 3]) << 24);
            at += 4;
        }
        codes.push_back(header >> 6);
        at += length;
    }
    return codes;
}

} // namespace

TEST(swf_adpcm_plays_back_close_to_the_sound)
{
    for (int channels = 1; channels <= 2; ++channels)
    {
        // Past one 4096-sample packet, so the second packet starts fresh.
        const size_t count = 5000;
        const std::vector<int16_t> samples = tone(22050, channels, count);
        for (int bits = 2; bits <= 5; ++bits)
        {
            const std::vector<uint8_t> data = swf::encodeAdpcm(samples, channels, bits);
            // Code size, then per packet a full sample and index a channel,
            // then a code each for the rest.
            const size_t expectBits = 2 + 2 * 22 * channels + (count - 2) * bits * channels;
            CHECK(data.size() == (expectBits + 7) / 8);
            const std::vector<int16_t> back = swf::decodeAdpcm(data, channels, count);
            CHECK(back.size() == samples.size());
            double error = 0.0;
            for (size_t i = 0; i < back.size() && i < samples.size(); ++i)
                error += std::abs(back[i] - samples[i]);
            error /= static_cast<double>(samples.size());
            // More bits, closer: 5 bits within a percent of the tone's swing.
            const double allowed[6] = {0, 0, 3000.0, 1200.0, 500.0, 250.0};
            CHECK(error < allowed[bits]);
            CHECK(back[0] == samples[0] && back[4096 * channels] == samples[4096 * channels]);
        }
    }
}

TEST(swf_sound_streams_cut_a_block_for_each_frame)
{
    // 24 fps at 22,050 Hz: 918.75 samples a frame, rounding never drifts.
    const std::vector<int16_t> samples = tone(22050, 2, 22050);
    const swf::SoundStream raw = swf::rawStream(samples, 2, 22050, 24.0, 30);
    CHECK(raw.codec == 3 && raw.stereo && raw.samplesPerFrame == 919);
    CHECK(raw.blocks.size() == 30);
    size_t total = 0;
    for (int f = 0; f < 24; ++f)
        total += raw.blocks[static_cast<size_t>(f)].size();
    CHECK(total == 22050u * 2u * 2u);
    // Past the sound's end, frames carry nothing.
    CHECK(raw.blocks[24].empty() && raw.blocks[29].empty());

    const swf::SoundStream adpcm = swf::adpcmStream(samples, 2, 22050, 4, 24.0, 24);
    CHECK(adpcm.codec == 1 && adpcm.blocks.size() == 24);
    // Each block decodes alone, from its own code size.
    const std::vector<int16_t> first = swf::decodeAdpcm(adpcm.blocks[0], 2, 919);
    CHECK(first.size() == 919u * 2u && first[0] == samples[0]);

    // Mono at 2 bits: a decoder that counts samples from a block's size
    // never takes padding for one more, so the sound keeps up with the
    // movie (the last block may run a few samples of silence over).
    const std::vector<int16_t> mono = tone(11025, 1, 11025);
    const swf::SoundStream small = swf::adpcmStream(mono, 1, 11025, 2, 24.0, 24);
    size_t counted = 0;
    bool keepsUp = true;
    for (size_t f = 0; f < small.blocks.size(); ++f)
    {
        counted += 1 + (small.blocks[f].size() * 8 - 2 - 22) / 2;
        const double due = static_cast<double>(f + 1) * 11025.0 / 24.0;
        keepsUp = keepsUp && std::fabs(static_cast<double>(counted) - due) <= 5.0;
    }
    CHECK(keepsUp);
}

TEST(swf_mp3_streams_deal_frames_to_keep_up_with_the_movie)
{
    // MPEG-1 layer III at 128 kbps and 44.1 kHz: 417-byte frames of 1152
    // samples, after an ID3 tag.
    std::vector<uint8_t> mp3 = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0};
    for (int i = 0; i < 40; ++i)
    {
        std::vector<uint8_t> frame(417, 0);
        frame[0] = 0xFF;
        frame[1] = 0xFB;
        frame[2] = 0x90;
        mp3.insert(mp3.end(), frame.begin(), frame.end());
    }
    const std::vector<swf::Mp3Frame> frames = swf::mp3Frames(mp3);
    CHECK(frames.size() == 40);
    CHECK(frames[0].offset == 14 && frames[0].length == 417 && frames[0].samples == 1152);

    const swf::SoundStream stream = swf::mp3Stream(mp3, 2, 44100, 24.0, 30);
    CHECK(stream.codec == 2 && stream.blocks.size() == 30);
    // By each frame's end, the blocks so far hold at least its sound.
    size_t dealt = 0;
    bool keepsUp = true;
    for (int f = 0; f < 24; ++f)
    {
        const std::vector<uint8_t>& block = stream.blocks[static_cast<size_t>(f)];
        if (!block.empty())
            dealt += static_cast<size_t>(block[0] | (block[1] << 8));
        if (dealt < static_cast<size_t>(std::llround((f + 1) * 44100.0 / 24.0)))
            keepsUp = false;
    }
    CHECK(keepsUp);
    CHECK(stream.blocks[0].size() == 4 + 2 * 417);
}

TEST(swf_movies_stream_their_sound_before_each_frame)
{
    swf::Writer writer(100, 100, 12.0, {255, 255, 255, 255});
    const swf::SoundStream stream = swf::adpcmStream(tone(11025, 1, 1838), 1, 11025, 4, 12.0, 3);
    writer.soundStream(stream.codec, stream.rate, stream.stereo, stream.samplesPerFrame);
    for (const std::vector<uint8_t>& block : stream.blocks)
    {
        writer.soundBlock(block);
        writer.showFrame();
    }
    const std::vector<int> codes = tagCodes(writer.finish(false));
    // FileAttributes, background, the stream head, then a block and a
    // frame for the two frames with sound, a bare frame, and the end.
    const std::vector<int> expected = {69, 9, 45, 19, 1, 19, 1, 1, 0};
    CHECK(codes == expected);
}

TEST(swf_movies_start_and_stop_event_sounds)
{
    swf::Writer writer(100, 100, 12.0, {255, 255, 255, 255});
    const std::vector<int16_t> samples = tone(11025, 1, 1102);
    const int id = writer.defineSound(1, 11025, false, 1102, swf::encodeAdpcm(samples, 1, 4));
    writer.startSound(id, 3, true, false);
    writer.showFrame();
    writer.startSound(id, 1, false, true);
    writer.showFrame();
    const std::vector<uint8_t> file = writer.finish(false);
    const std::vector<int> codes = tagCodes(file);
    // The sound defined once, started (three times round, not over itself),
    // then stopped.
    const std::vector<int> expected = {69, 9, 14, 15, 1, 15, 1, 0};
    CHECK(codes == expected);
    // DefineSound: its id, then ADPCM, 11 kHz, 16-bit, mono, and the samples;
    // StartSound: the id, then no multiple and a loop count of 3, or stop.
    size_t at = 8;
    at += static_cast<size_t>((5 + 4 * (file[at] >> 3) + 7) / 8) + 4;
    std::vector<std::vector<uint8_t>> bodies;
    while (at + 2 <= file.size())
    {
        const int header = file[at] | (file[at + 1] << 8);
        at += 2;
        size_t length = static_cast<size_t>(header & 0x3F);
        if (length == 0x3F)
        {
            length = file[at] | (file[at + 1] << 8) | (file[at + 2] << 16) | (static_cast<size_t>(file[at + 3]) << 24);
            at += 4;
        }
        bodies.emplace_back(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
    }
    const std::vector<uint8_t>& define = bodies[2];
    CHECK((define[0] | (define[1] << 8)) == id);
    CHECK(define[2] == ((1 << 4) | (1 << 2) | (1 << 1)));
    CHECK((define[3] | (define[4] << 8)) == 1102);
    const std::vector<uint8_t> start = {static_cast<uint8_t>(id), 0, 0x14, 3, 0};
    const std::vector<uint8_t> stop = {static_cast<uint8_t>(id), 0, 0x20};
    CHECK(bodies[3] == start && bodies[5] == stop);
}

TEST(swf_start_sounds_carry_in_and_out_points_and_envelopes)
{
    swf::Writer writer(100, 100, 12.0, {255, 255, 255, 255});
    const int id = writer.defineSound(3, 11025, false, 4, std::vector<uint8_t>(8, 0));
    swf::SoundInfo info;
    info.loops = 2;
    info.inPoint = 4410;
    info.outPoint = 7000;
    info.envelope.push_back({0, 32768, 0});
    info.envelope.push_back({8000, 0, 32768});
    writer.startSound(id, info);
    writer.showFrame();
    const std::vector<Tag> tags = tagsOf(writer.finish(false));
    CHECK(tags.size() == 6 && tags[3].first == 15);
    if (tags.size() != 6)
        return;
    // SOUNDINFO: envelope, loops, out and in point flags, then the in
    // point, out point, loop count and the envelope's points.
    const std::vector<uint8_t>& body = tags[3].second;
    CHECK(body.size() == 2 + 1 + 4 + 4 + 2 + 1 + 2 * 8);
    CHECK(u16At(body, 0) == id && body[2] == 0x0F);
    CHECK(u32At(body, 3) == 4410 && u32At(body, 7) == 7000 && u16At(body, 11) == 2);
    CHECK(body[13] == 2);
    CHECK(u32At(body, 14) == 0 && u16At(body, 18) == 32768 && u16At(body, 20) == 0);
    CHECK(u32At(body, 22) == 8000 && u16At(body, 26) == 0 && u16At(body, 28) == 32768);
}

TEST(swf_sprites_play_their_own_sounds)
{
    swf::Writer writer(100, 100, 12.0, {255, 255, 255, 255});
    std::vector<uint8_t> raw(1102 * 2, 0);
    const int sound = writer.defineSound(3, 11025, false, 1102, raw);
    swf::Sprite sprite;
    sprite.showFrame();
    swf::SoundInfo info;
    info.noMultiple = true;
    sprite.startSound(sound, info);
    sprite.showFrame();
    const int clip = writer.defineSprite(sprite);
    writer.place(clip, 1);
    writer.showFrame();
    const std::vector<Tag> tags = tagsOf(writer.finish(false));
    // The sprite: its id, two frames, then a bare frame, the sound started
    // and a frame, and its end.
    CHECK(tags.size() == 7 && tags[3].first == 39);
    if (tags.size() != 7)
        return;
    const std::vector<uint8_t>& body = tags[3].second;
    CHECK(u16At(body, 0) == clip && u16At(body, 2) == 2);
    const std::vector<Tag> inner = tagsFrom(body, 4);
    CHECK(inner.size() == 4 && inner[0].first == 1 && inner[1].first == 15 && inner[2].first == 1 && inner[3].first == 0);
    const std::vector<uint8_t> start = {static_cast<uint8_t>(sound), 0, 0x10};
    CHECK(inner.size() == 4 && inner[1].second == start);

    // Read back, the clip keeps its sound on its second frame, as a start
    // sound.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(writer.finish(true), files, error));
    bool found = false;
    for (const auto& [path, bytes] : files)
    {
        const std::string text(bytes.begin(), bytes.end());
        if (path.rfind("LIBRARY/", 0) == 0 && text.find("soundSync=\"start\"") != std::string::npos &&
            text.find("index=\"1\"") != std::string::npos)
            found = true;
    }
    CHECK(found);
}

namespace {

/// A fifth of a second of tone, 16-bit mono at 11 kHz, as an FLA keeps it.
std::vector<uint8_t> beep()
{
    std::vector<uint8_t> data;
    for (int i = 0; i < 2205; ++i)
    {
        const int16_t value = static_cast<int16_t>(std::sin(i * 0.25) * 8000);
        data.push_back(static_cast<uint8_t>(value & 0xFF));
        data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }
    return data;
}

/// A scene with the beep on its first frame and, for five frames, a movie
/// clip that starts it on its own third frame; \a item adds to the
/// sound's library item.
std::map<std::string, std::vector<uint8_t>> beepPackage(const std::string& item = std::string())
{
    std::map<std::string, std::vector<uint8_t>> files;
    files["DOMDocument.xml"] = bytesOf(
        "<DOMDocument xmlns=\"http://ns.adobe.com/xfl/2008/\" width=\"100\" height=\"100\" frameRate=\"10\">"
        "<media><DOMSoundItem name=\"beep.wav\" href=\"beep.wav\" soundDataHRef=\"M 1.dat\" "
        "format=\"11kHz 16bit Mono\" sampleCount=\"2205\"" + item + "/></media>"
        "<symbols><Include href=\"Clip.xml\"/></symbols>"
        "<timelines><DOMTimeline name=\"Scene 1\"><layers>"
        "<DOMLayer name=\"Sound\"><frames>"
        "<DOMFrame index=\"0\" duration=\"10\" soundName=\"beep.wav\" soundLoopMode=\"repeat\" soundLoop=\"2\" "
        "inPoint44=\"4410\" outPoint44=\"7000\"><SoundEnvelope><SoundEnvelopePoint level0=\"32768\" level1=\"0\"/>"
        "<SoundEnvelopePoint mark44=\"8000\" level0=\"0\" level1=\"32768\"/></SoundEnvelope><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "<DOMLayer name=\"Clip\"><frames>"
        "<DOMFrame index=\"0\" duration=\"5\"><elements><DOMSymbolInstance libraryItemName=\"Clip\"/></elements></DOMFrame>"
        "<DOMFrame index=\"5\" duration=\"5\"><elements/></DOMFrame>"
        "</frames></DOMLayer>"
        "</layers></DOMTimeline></timelines></DOMDocument>");
    files["LIBRARY/Clip.xml"] = bytesOf(
        "<DOMSymbolItem xmlns=\"http://ns.adobe.com/xfl/2008/\" name=\"Clip\"><timeline><DOMTimeline name=\"Clip\">"
        "<layers><DOMLayer name=\"Sound\"><frames>"
        "<DOMFrame index=\"0\" duration=\"2\"><elements/></DOMFrame>"
        "<DOMFrame index=\"2\" duration=\"2\" soundName=\"beep.wav\" soundSync=\"start\"><elements/></DOMFrame>"
        "</frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>");
    files["bin/M 1.dat"] = beep();
    return files;
}

/// \a document's event sounds written into a movie of \a frames frames.
std::vector<uint8_t> eventMovie(const fla::Document& document, const fla::publish::Settings& settings, int frames)
{
    swf::Writer writer(100, 100, 10.0, {255, 255, 255, 255});
    fla::SwfEventSounds events(document, settings);
    events.define(writer, frames);
    for (int frame = 0; frame < frames; ++frame)
    {
        events.frame(writer, frame);
        writer.showFrame();
    }
    return writer.finish(false);
}

fla::publish::Settings rawEvents()
{
    fla::publish::Settings settings;
    settings.event.compression = "raw";
    settings.event.rate = 11025;
    settings.event.stereo = false;
    return settings;
}

} // namespace

TEST(swf_event_sounds_play_on_the_scene_and_in_movie_clips)
{
    Package package = open(beepPackage());
    CHECK(package.document != nullptr);
    if (!package.document)
        return;
    const std::vector<uint8_t> file = eventMovie(*package.document, rawEvents(), 10);
    const std::vector<Tag> tags = tagsOf(file);
    std::vector<int> codes;
    for (const Tag& tag : tags)
        codes.push_back(tag.first);
    // The sound once, the clip's sounds, then the scene's sound and the
    // clip on the first frame, the clip gone on the sixth.
    const std::vector<int> expected = {69, 9, 14, 39, 15, 26, 1, 1, 1, 1, 1, 28, 1, 1, 1, 1, 1, 0};
    CHECK(codes == expected);
    if (codes != expected)
        return;
    // Raw, 11 kHz, 16-bit mono: all 2205 samples.
    const std::vector<uint8_t>& define = tags[2].second;
    CHECK(define[2] == ((3 << 4) | (1 << 2) | (1 << 1)) && u32At(define, 3) == 2205);
    CHECK(define.size() == 7 + 2205 * 2);
    // The scene's: twice round, the part between the in and out points, its
    // envelope.
    const std::vector<uint8_t>& start = tags[4].second;
    CHECK(start[2] == 0x0F && u32At(start, 3) == 4410 && u32At(start, 7) == 7000 && u16At(start, 11) == 2);
    CHECK(start[13] == 2 && u32At(start, 22) == 8000 && u16At(start, 18) == 32768 && u16At(start, 20) == 0);
    // The clip: four frames, the beep starting on the third as a start
    // sound; placed high, out of the drawing's way, and removed there.
    const std::vector<uint8_t>& sprite = tags[3].second;
    CHECK(u16At(sprite, 2) == 4);
    const std::vector<Tag> inner = tagsFrom(sprite, 4);
    CHECK(inner.size() == 6 && inner[2].first == 15 && inner[2].second.size() == 3 && inner[2].second[2] == 0x10);
    CHECK(u16At(tags[5].second, 1) == 65535 && u16At(tags[5].second, 3) == u16At(sprite, 0));
    CHECK(u16At(tags[11].second, 0) == 65535);

    // Read back, the scene keeps its sound and the clip its own.
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    CHECK(SwfImport::convert(file, files, error));
    const std::vector<uint8_t>& scene = files["DOMDocument.xml"];
    const std::string sceneText(scene.begin(), scene.end());
    CHECK(sceneText.find("soundLoop=\"2\"") != std::string::npos);
    bool clipSound = false;
    for (const auto& [path, bytes] : files)
    {
        const std::string text(bytes.begin(), bytes.end());
        clipSound = clipSound || (path.rfind("LIBRARY/", 0) == 0 && text.find("soundSync=\"start\"") != std::string::npos);
    }
    CHECK(clipSound);
}

TEST(swf_event_sounds_keep_their_library_compression)
{
    // The item asks for 3-bit ADPCM at 11 kHz, mono.
    Package package = open(beepPackage(" exportFormat=\"6\" exportBits=\"3\""));
    CHECK(package.document != nullptr);
    if (!package.document)
        return;
    fla::publish::Settings settings = rawEvents();
    std::vector<Tag> tags = tagsOf(eventMovie(*package.document, settings, 10));
    CHECK(tags.size() > 2 && tags[2].first == 14);
    if (tags.size() > 2)
    {
        const std::vector<uint8_t>& define = tags[2].second;
        CHECK(define[2] == ((1 << 4) | (1 << 2) | (1 << 1)) && (define[7] >> 6) == 1);
    }
    // Unless the publish settings override it.
    settings.overrideSounds = true;
    tags = tagsOf(eventMovie(*package.document, settings, 10));
    CHECK(tags.size() > 2 && tags[2].first == 14 && (tags[2].second[2] >> 4) == 3);
    // An imported MP3 kept at its own quality goes in as it came: its
    // frames, MPEG-1 at 44.1 kHz in stereo, 1152 samples each.
    std::map<std::string, std::vector<uint8_t>> mp3Files = beepPackage(" exportFormat=\"1\" exportBits=\"7\" exportNative=\"true\"");
    std::vector<uint8_t> mp3;
    for (int i = 0; i < 40; ++i)
    {
        std::vector<uint8_t> frame(417, 0);
        frame[0] = 0xFF;
        frame[1] = 0xFB;
        frame[2] = 0x90;
        mp3.insert(mp3.end(), frame.begin(), frame.end());
    }
    mp3Files["bin/M 1.dat"] = mp3;
    Package mp3Package = open(mp3Files);
    CHECK(mp3Package.document != nullptr);
    if (mp3Package.document)
    {
        settings.overrideSounds = false;
        tags = tagsOf(eventMovie(*mp3Package.document, settings, 10));
        CHECK(tags.size() > 2 && tags[2].first == 14);
        if (tags.size() > 2)
        {
            const std::vector<uint8_t>& define = tags[2].second;
            CHECK(define[2] == ((2 << 4) | (3 << 2) | (1 << 1) | 1) && u32At(define, 3) == 40u * 1152u);
            CHECK(define.size() == 7 + 2 + mp3.size() && u16At(define, 7) == 0);
        }
    }

    // Disabled, no sounds at all.
    settings.overrideSounds = true;
    settings.event.compression = "none";
    tags = tagsOf(eventMovie(*package.document, settings, 10));
    bool none = true;
    for (const Tag& tag : tags)
        none = none && tag.first != 14 && tag.first != 15 && tag.first != 39;
    CHECK(none);
}
