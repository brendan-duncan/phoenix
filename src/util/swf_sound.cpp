#include "swf_sound.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace swf {

namespace {

const int kSteps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767};

// How the step index moves after each code, by code size (2 to 5 bits),
// indexed by the code's magnitude.
const int kIndex2[2] = {-1, 2};
const int kIndex3[4] = {-1, -1, 2, 4};
const int kIndex4[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kIndex5[16] = {-1, -1, -1, -1, -1, -1, -1, -1, 1, 2, 4, 6, 8, 10, 13, 16};

const int* indexTable(int bits)
{
    switch (bits)
    {
    case 2: return kIndex2;
    case 3: return kIndex3;
    case 4: return kIndex4;
    default: return kIndex5;
    }
}

constexpr int kPacket = 4096;

class BitWriter
{
public:
    void put(uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            _byte = static_cast<uint8_t>((_byte << 1) | ((value >> i) & 1u));
            if (++_used == 8)
            {
                _out.push_back(_byte);
                _byte = 0;
                _used = 0;
            }
        }
    }
    std::vector<uint8_t> finish()
    {
        if (_used > 0)
            _out.push_back(static_cast<uint8_t>(_byte << (8 - _used)));
        _used = 0;
        _byte = 0;
        return std::move(_out);
    }

private:
    std::vector<uint8_t> _out;
    uint8_t _byte = 0;
    int _used = 0;
};

class BitReader
{
public:
    explicit BitReader(const std::vector<uint8_t>& data)
        : _data(data)
    {}
    uint32_t get(int count)
    {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i)
        {
            const size_t byte = _bit / 8;
            const int bit = byte < _data.size() ? (_data[byte] >> (7 - _bit % 8)) & 1 : 0;
            value = (value << 1) | static_cast<uint32_t>(bit);
            ++_bit;
        }
        return value;
    }
    int32_t getSigned(int count)
    {
        const uint32_t value = get(count);
        return (value & (1u << (count - 1))) ? static_cast<int32_t>(value) - (1 << count) : static_cast<int32_t>(value);
    }

private:
    const std::vector<uint8_t>& _data;
    size_t _bit = 0;
};

struct Channel
{
    int predictor = 0;
    int index = 0;
};

/// The difference a code makes, as the decoder works it out; the encoder
/// takes the same steps so the two stay together.
int stepOnce(Channel& channel, int code, int bits)
{
    const int signMask = 1 << (bits - 1);
    int step = kSteps[channel.index];
    int difference = 0;
    for (int k = 1 << (bits - 2); k; k >>= 1)
    {
        if (code & k)
            difference += step;
        step >>= 1;
    }
    difference += step;
    channel.predictor += (code & signMask) ? -difference : difference;
    channel.predictor = std::clamp(channel.predictor, -32768, 32767);
    channel.index = std::clamp(channel.index + indexTable(bits)[code & ~signMask], 0, 88);
    return channel.predictor;
}

int codeFor(const Channel& channel, int sample, int bits)
{
    const int signMask = 1 << (bits - 1);
    int difference = sample - channel.predictor;
    int code = 0;
    if (difference < 0)
    {
        code = signMask;
        difference = -difference;
    }
    int step = kSteps[channel.index];
    for (int k = 1 << (bits - 2); k; k >>= 1)
    {
        if (difference >= step)
        {
            code |= k;
            difference -= step;
        }
        step >>= 1;
    }
    return code;
}

int nearestIndex(int difference)
{
    // A starting step near the packet's first change, so it settles fast.
    int index = 0;
    while (index < 88 && kSteps[index] < difference)
        ++index;
    return index;
}

} // namespace

std::vector<uint8_t> encodeAdpcm(const std::vector<int16_t>& samples, int channels, int bits)
{
    bits = std::clamp(bits, 2, 5);
    channels = channels == 2 ? 2 : 1;
    const size_t count = samples.size() / static_cast<size_t>(channels);
    BitWriter out;
    out.put(static_cast<uint32_t>(bits - 2), 2);
    Channel state[2];
    for (size_t start = 0; start < count; start += kPacket)
    {
        const size_t end = std::min(count, start + kPacket);
        for (int c = 0; c < channels; ++c)
        {
            state[c].predictor = samples[start * channels + c];
            const int next = end > start + 1 ? samples[(start + 1) * channels + c] : state[c].predictor;
            state[c].index = nearestIndex(std::abs(next - state[c].predictor));
            out.put(static_cast<uint16_t>(static_cast<int16_t>(state[c].predictor)), 16);
            out.put(static_cast<uint32_t>(state[c].index), 6);
        }
        for (size_t i = start + 1; i < end; ++i)
        {
            for (int c = 0; c < channels; ++c)
            {
                const int code = codeFor(state[c], samples[i * channels + c], bits);
                stepOnce(state[c], code, bits);
                out.put(static_cast<uint32_t>(code), bits);
            }
        }
    }
    return out.finish();
}

std::vector<int16_t> decodeAdpcm(const std::vector<uint8_t>& data, int channels, size_t count)
{
    channels = channels == 2 ? 2 : 1;
    // Each sample takes at least two bits, so the data holds no more than
    // four a byte, whatever count the header claims.
    count = std::min(count, data.size() * 4 + 1);
    std::vector<int16_t> samples;
    samples.reserve(count * static_cast<size_t>(channels));
    BitReader in(data);
    const int bits = static_cast<int>(in.get(2)) + 2;
    Channel state[2];
    for (size_t start = 0; start < count; start += kPacket)
    {
        const size_t end = std::min(count, start + kPacket);
        for (int c = 0; c < channels; ++c)
        {
            state[c].predictor = in.getSigned(16);
            state[c].index = std::clamp(static_cast<int>(in.get(6)), 0, 88);
            samples.push_back(static_cast<int16_t>(state[c].predictor));
        }
        for (size_t i = start + 1; i < end; ++i)
            for (int c = 0; c < channels; ++c)
                samples.push_back(static_cast<int16_t>(stepOnce(state[c], static_cast<int>(in.get(bits)), bits)));
    }
    return samples;
}

std::vector<Mp3Frame> mp3Frames(const std::vector<uint8_t>& mp3)
{
    // MPEG-1, 2 and 2.5 layer III: bit rates (kbps) by version row and
    // index, and sample rates.
    static const int kRates1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int kRates2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const int kSampleRates[3] = {44100, 48000, 32000};
    std::vector<Mp3Frame> frames;
    size_t at = 0;
    // An ID3v2 tag first: ten bytes, its size in four seven-bit bytes.
    if (mp3.size() >= 10 && mp3[0] == 'I' && mp3[1] == 'D' && mp3[2] == '3')
        at = 10 + ((mp3[6] & 0x7F) << 21 | (mp3[7] & 0x7F) << 14 | (mp3[8] & 0x7F) << 7 | (mp3[9] & 0x7F));
    while (at + 4 <= mp3.size())
    {
        const uint8_t* h = &mp3[at];
        if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
            break;
        const int version = (h[1] >> 3) & 3; // 3: MPEG-1, 2: MPEG-2, 0: MPEG-2.5
        const int layer = (h[1] >> 1) & 3;   // 1: layer III
        const int rateIndex = (h[2] >> 4) & 0xF;
        const int sampleIndex = (h[2] >> 2) & 3;
        const int padding = (h[2] >> 1) & 1;
        if (version == 1 || layer != 1 || rateIndex == 0 || rateIndex == 15 || sampleIndex == 3)
            break;
        const bool mpeg1 = version == 3;
        const int bitRate = (mpeg1 ? kRates1 : kRates2)[rateIndex] * 1000;
        int sampleRate = kSampleRates[sampleIndex];
        if (version == 2)
            sampleRate /= 2;
        else if (version == 0)
            sampleRate /= 4;
        const int samples = mpeg1 ? 1152 : 576;
        const size_t length = static_cast<size_t>(samples / 8 * bitRate / sampleRate + padding);
        if (length < 4 || at + length > mp3.size())
            break;
        frames.push_back(Mp3Frame{at, length, samples});
        at += length;
    }
    return frames;
}

std::vector<int16_t> pcm16(const std::vector<float>& stereo, int from, int rate, int channels)
{
    channels = channels == 2 ? 2 : 1;
    const size_t frames = stereo.size() / 2;
    const size_t count = static_cast<size_t>(static_cast<double>(frames) * rate / from);
    std::vector<int16_t> out;
    out.reserve(count * static_cast<size_t>(channels));
    const auto toInt = [](double value) {
        return static_cast<int16_t>(std::lround(std::clamp(value, -1.0, 1.0) * 32767.0));
    };
    for (size_t i = 0; i < count; ++i)
    {
        const size_t first = static_cast<size_t>(static_cast<double>(i) * from / rate);
        const size_t last = std::max(first + 1, std::min(frames, static_cast<size_t>(static_cast<double>(i + 1) * from / rate)));
        double left = 0.0, right = 0.0;
        for (size_t j = first; j < last && j < frames; ++j)
        {
            left += stereo[j * 2];
            right += stereo[j * 2 + 1];
        }
        const double n = static_cast<double>(last - first);
        if (channels == 2)
        {
            out.push_back(toInt(left / n));
            out.push_back(toInt(right / n));
        }
        else
            out.push_back(toInt((left + right) / (2.0 * n)));
    }
    return out;
}

namespace {

/// Where frame \a frame's sound starts, in samples a channel; counted from
/// the start so rounding never drifts.
size_t frameStart(int frame, int rate, double fps)
{
    return static_cast<size_t>(std::llround(static_cast<double>(frame) * rate / fps));
}

SoundStream streamOf(int codec, int channels, int rate, double fps)
{
    SoundStream stream;
    stream.codec = codec;
    stream.rate = rate;
    stream.stereo = channels == 2;
    stream.samplesPerFrame = static_cast<int>(std::lround(rate / fps));
    return stream;
}

/// Each frame's samples, interleaved, past the end of \a samples silence.
template <typename Encode>
SoundStream cut(SoundStream stream, const std::vector<int16_t>& samples, int channels, double fps, int frames,
    Encode encode)
{
    const size_t count = samples.size() / static_cast<size_t>(channels);
    for (int f = 0; f < frames; ++f)
    {
        const size_t from = frameStart(f, stream.rate, fps);
        const size_t to = frameStart(f + 1, stream.rate, fps);
        if (from >= count)
        {
            stream.blocks.emplace_back();
            continue;
        }
        std::vector<int16_t> part(samples.begin() + static_cast<std::ptrdiff_t>(from * channels),
            samples.begin() + static_cast<std::ptrdiff_t>(std::min(to, count) * channels));
        part.resize((to - from) * static_cast<size_t>(channels), 0);
        stream.blocks.push_back(encode(part));
    }
    return stream;
}

} // namespace

SoundStream rawStream(const std::vector<int16_t>& samples, int channels, int rate, double fps, int frames)
{
    channels = channels == 2 ? 2 : 1;
    return cut(streamOf(3, channels, rate, fps), samples, channels, fps, frames, [](const std::vector<int16_t>& part) {
        std::vector<uint8_t> bytes;
        bytes.reserve(part.size() * 2);
        for (int16_t sample : part)
        {
            bytes.push_back(static_cast<uint8_t>(sample & 0xFF));
            bytes.push_back(static_cast<uint8_t>((sample >> 8) & 0xFF));
        }
        return bytes;
    });
}

SoundStream adpcmStream(const std::vector<int16_t>& samples, int channels, int rate, int bits, double fps, int frames)
{
    channels = channels == 2 ? 2 : 1;
    bits = std::clamp(bits, 2, 5);
    SoundStream stream = streamOf(1, channels, rate, fps);
    const size_t count = samples.size() / static_cast<size_t>(channels);
    // A block's last byte is padded: too much padding and a decoder reads
    // it as one more sample, and the sound slips behind the movie.
    const auto tidy = [&](size_t length) {
        const size_t used = 2 + 22 * static_cast<size_t>(channels) + (length - 1) * static_cast<size_t>(bits * channels);
        return (8 - used % 8) % 8 < static_cast<size_t>(bits * channels);
    };
    size_t from = 0;
    for (int f = 0; f < frames; ++f)
    {
        if (from >= count)
        {
            stream.blocks.emplace_back();
            continue;
        }
        // Each block is ADPCM data of its own, code size and all, ending a
        // sample or two off the frame's end when that pads it tidily; the
        // next block makes up the difference.
        const size_t want = frameStart(f + 1, rate, fps);
        size_t to = want;
        for (int shift = 0; shift <= 4; ++shift)
        {
            if (want + shift > from + 1 && tidy(want + shift - from))
            {
                to = want + shift;
                break;
            }
            if (want > from + 1 + shift && tidy(want - shift - from))
            {
                to = want - shift;
                break;
            }
        }
        std::vector<int16_t> part(samples.begin() + static_cast<std::ptrdiff_t>(from * channels),
            samples.begin() + static_cast<std::ptrdiff_t>(std::min(to, count) * channels));
        part.resize((to - from) * static_cast<size_t>(channels), 0);
        stream.blocks.push_back(encodeAdpcm(part, channels, bits));
        from = to;
    }
    return stream;
}

SoundStream mp3Stream(const std::vector<uint8_t>& mp3, int channels, int rate, double fps, int frames, int delay)
{
    SoundStream stream = streamOf(2, channels == 2 ? 2 : 1, rate, fps);
    stream.latency = std::max(0, delay);
    const std::vector<Mp3Frame> found = mp3Frames(mp3);
    size_t next = 0;
    size_t dealt = 0; // samples a channel in the blocks so far
    for (int f = 0; f < frames; ++f)
    {
        const size_t start = frameStart(f, rate, fps) + static_cast<size_t>(stream.latency);
        const size_t end = frameStart(f + 1, rate, fps) + static_cast<size_t>(stream.latency);
        std::vector<uint8_t> block;
        const size_t before = dealt;
        size_t count = 0;
        // Enough MP3 frames that the sound runs to this frame's end.
        while (next < found.size() && dealt < end)
        {
            const Mp3Frame& frame = found[next++];
            block.insert(block.end(), mp3.begin() + static_cast<std::ptrdiff_t>(frame.offset),
                mp3.begin() + static_cast<std::ptrdiff_t>(frame.offset + frame.length));
            dealt += static_cast<size_t>(frame.samples);
            count += static_cast<size_t>(frame.samples);
        }
        if (block.empty())
        {
            stream.blocks.emplace_back();
            continue;
        }
        // SampleCount, then SeekSamples: how far into the block this
        // frame's sound starts.
        const int seek = static_cast<int>(start) - static_cast<int>(before);
        std::vector<uint8_t> head = {static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>((count >> 8) & 0xFF),
            static_cast<uint8_t>(seek & 0xFF), static_cast<uint8_t>((seek >> 8) & 0xFF)};
        block.insert(block.begin(), head.begin(), head.end());
        stream.blocks.push_back(std::move(block));
    }
    return stream;
}

} // namespace swf
