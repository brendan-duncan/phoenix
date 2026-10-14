#include "test_util.h"

#include "../src/util/swf_sound.h"
#include "../src/util/swf_writer.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

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
