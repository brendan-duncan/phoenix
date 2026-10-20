#include "audio_engine.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "../third_party/miniaudio/miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

void AudioEnvelope::gainAt(size_t position, float& left, float& right) const
{
    if (points.empty())
    {
        left = right = 1.0f;
        return;
    }
    if (position <= points.front().mark)
    {
        left = points.front().left;
        right = points.front().right;
        return;
    }
    for (size_t i = 1; i < points.size(); ++i)
    {
        const Point& a = points[i - 1];
        const Point& b = points[i];
        if (position < b.mark)
        {
            const float t = b.mark > a.mark ? static_cast<float>(position - a.mark) / (b.mark - a.mark) : 1.0f;
            left = a.left + (b.left - a.left) * t;
            right = a.right + (b.right - a.right) * t;
            return;
        }
    }
    left = points.back().left;
    right = points.back().right;
}

namespace {

/// The value of \a name in one element's text, or empty.
std::string attributeOf(const std::string& tag, const std::string& name)
{
    const std::string key = " " + name + "=\"";
    const size_t at = tag.find(key);
    if (at == std::string::npos)
        return {};
    const size_t from = at + key.size();
    const size_t to = tag.find('"', from);
    return to == std::string::npos ? std::string() : tag.substr(from, to - from);
}

} // namespace

std::shared_ptr<const AudioEnvelope> AudioEnvelope::parse(const std::string& xml)
{
    auto envelope = std::make_shared<AudioEnvelope>();
    bool flat = true;
    size_t at = 0;
    while ((at = xml.find("<SoundEnvelopePoint", at)) != std::string::npos)
    {
        const size_t end = xml.find('>', at);
        if (end == std::string::npos)
            break;
        const std::string tag = xml.substr(at, end - at);
        at = end;
        Point point;
        const std::string mark = attributeOf(tag, "mark44");
        const std::string left = attributeOf(tag, "level0");
        const std::string right = attributeOf(tag, "level1");
        point.mark = mark.empty() ? 0 : static_cast<size_t>(std::max(0LL, std::atoll(mark.c_str())));
        point.left = left.empty() ? 0.0f : std::atoi(left.c_str()) / 32768.0f;
        point.right = right.empty() ? 0.0f : std::atoi(right.c_str()) / 32768.0f;
        flat = flat && point.left == 1.0f && point.right == 1.0f;
        envelope->points.push_back(point);
    }
    if (envelope->points.empty() || flat)
        return nullptr;
    std::stable_sort(envelope->points.begin(), envelope->points.end(),
        [](const Point& a, const Point& b) { return a.mark < b.mark; });
    return envelope;
}

namespace AudioDecode {

bool parseFormat(const std::string& format, int& rate, int& bits, int& channels)
{
    // "44kHz 16bit Stereo": the FLA's four rates are 5.5, 11, 22 and 44.1 kHz.
    int khz = 0;
    if (std::sscanf(format.c_str(), "%dkHz %dbit", &khz, &bits) != 2)
        return false;
    switch (khz)
    {
    case 5: rate = 5512; break;
    case 11: rate = 11025; break;
    case 22: rate = 22050; break;
    case 44: rate = 44100; break;
    case 48: rate = 48000; break;
    // Any other rate within reason; one outside it (a damaged name) would
    // overflow, or make the resampling expand the sound many times over.
    default: rate = khz >= 4 && khz <= 192 ? khz * 1000 : 0; break;
    }
    channels = format.find("Stereo") != std::string::npos ? 2 : 1;
    return rate > 0 && (bits == 8 || bits == 16);
}

namespace {

bool looksLikeMp3(const std::vector<uint8_t>& data)
{
    if (data.size() >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3')
        return true;
    // A real MPEG audio frame header, not just its sync bits: raw PCM can
    // start with 0xFF 0xFF too. The version, layer, bitrate and sample rate
    // fields must all hold allowed values.
    if (data.size() < 4 || data[0] != 0xFF || (data[1] & 0xE0) != 0xE0)
        return false;
    const int version = (data[1] >> 3) & 0x3;
    const int layer = (data[1] >> 1) & 0x3;
    const int bitrate = (data[2] >> 4) & 0xF;
    const int sampleRate = (data[2] >> 2) & 0x3;
    return version != 1 && layer != 0 && bitrate != 0 && bitrate != 0xF && sampleRate != 3;
}

/// Linear resampling of interleaved stereo to 44.1 kHz.
std::vector<float> resample(const std::vector<float>& stereo, int rate)
{
    if (rate == PcmBuffer::kRate || stereo.empty())
        return stereo;
    const size_t inFrames = stereo.size() / 2;
    const double step = static_cast<double>(rate) / PcmBuffer::kRate;
    const size_t outFrames = static_cast<size_t>(inFrames / step);
    std::vector<float> out(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i)
    {
        const double at = i * step;
        const size_t a = std::min(static_cast<size_t>(at), inFrames - 1);
        const size_t b = std::min(a + 1, inFrames - 1);
        const float t = static_cast<float>(at - a);
        for (int c = 0; c < 2; ++c)
            out[i * 2 + c] = stereo[a * 2 + c] * (1.0f - t) + stereo[b * 2 + c] * t;
    }
    return out;
}

} // namespace

std::shared_ptr<const PcmBuffer> decode(const std::vector<uint8_t>& data, const std::string& format)
{
    if (data.empty())
        return nullptr;
    auto buffer = std::make_shared<PcmBuffer>();

    if (looksLikeMp3(data))
    {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, PcmBuffer::kRate);
        config.encodingFormat = ma_encoding_format_mp3;
        ma_decoder decoder;
        if (ma_decoder_init_memory(data.data(), data.size(), &config, &decoder) != MA_SUCCESS)
            return nullptr;
        float chunk[4096 * 2];
        for (;;)
        {
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read);
            if (read == 0)
                break;
            buffer->samples.insert(buffer->samples.end(), chunk, chunk + read * 2);
        }
        ma_decoder_uninit(&decoder);
        return buffer->samples.empty() ? nullptr : buffer;
    }

    int rate = 0;
    int bits = 0;
    int channels = 0;
    if (!parseFormat(format, rate, bits, channels))
        return nullptr;
    const size_t bytesPerFrame = static_cast<size_t>(bits / 8) * channels;
    const size_t frames = data.size() / bytesPerFrame;
    std::vector<float> stereo(frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        float value[2] = {0.0f, 0.0f};
        for (int c = 0; c < channels; ++c)
        {
            const size_t at = i * bytesPerFrame + static_cast<size_t>(c) * (bits / 8);
            if (bits == 16)
                value[c] = static_cast<int16_t>(data[at] | (data[at + 1] << 8)) / 32768.0f;
            else
                value[c] = (static_cast<int>(data[at]) - 128) / 128.0f;
        }
        stereo[i * 2] = value[0];
        stereo[i * 2 + 1] = channels == 2 ? value[1] : value[0];
    }
    buffer->samples = resample(stereo, rate);
    return buffer;
}

} // namespace AudioDecode

AudioEngine& AudioEngine::instance()
{
    static AudioEngine engine;
    return engine;
}

AudioEngine::~AudioEngine()
{
    if (_device)
    {
        ma_device_uninit(_device);
        delete _device;
    }
}

bool AudioEngine::open()
{
    if (_tried)
        return _open;
    _tried = true;
    _device = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = PcmBuffer::kRate;
#ifdef PHOENIX_WEB
    // In a browser the sound is filled in on the page's own thread, between
    // the frames it draws; a frame that takes longer than the buffer lasts
    // leaves a gap. 4096 frames (93 ms) covers a slow frame; the page can
    // ask for another size (?audiobuffer=).
    const char* asked = std::getenv("PHOENIX_AUDIO_BUFFER");
    const int frames = asked ? std::atoi(asked) : 0;
    config.periodSizeInFrames = frames >= 256 && frames <= 16384 ? static_cast<ma_uint32>(frames) : 4096;
#endif
    config.dataCallback = [](ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
        AudioEngine::callback(device, output, input, frameCount);
    };
    config.pUserData = this;
    if (ma_device_init(nullptr, &config, _device) != MA_SUCCESS)
    {
        delete _device;
        _device = nullptr;
        return false;
    }
    if (ma_device_start(_device) != MA_SUCCESS)
    {
        ma_device_uninit(_device);
        delete _device;
        _device = nullptr;
        return false;
    }
    _open = true;
    return true;
}

bool AudioEngine::available()
{
    return open();
}

void AudioEngine::callback(ma_device* device, void* output, const void*, unsigned frameCount)
{
    static_cast<AudioEngine*>(device->pUserData)->mix(static_cast<float*>(output), frameCount);
}

void AudioEngine::mix(float* output, unsigned frameCount)
{
    std::memset(output, 0, sizeof(float) * frameCount * 2);
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _voices.begin(); it != _voices.end();)
    {
        Voice& voice = it->second;
        // A held voice keeps its place and adds nothing.
        if (voice.paused)
        {
            ++it;
            continue;
        }
        const std::vector<float>& samples = voice.buffer->samples;
        unsigned written = 0;
        bool finished = false;
        while (written < frameCount)
        {
            if (voice.position >= voice.end)
            {
                if (voice.loopsLeft == 0 || --voice.loopsLeft > 0)
                {
                    voice.position = voice.start;
                }
                else
                {
                    finished = true;
                    break;
                }
                if (voice.end <= voice.start)
                {
                    finished = true;
                    break;
                }
            }
            const size_t take = std::min<size_t>(frameCount - written, voice.end - voice.position);
            for (size_t i = 0; i < take; ++i)
            {
                float left = voice.volume;
                float right = voice.volume;
                if (voice.envelope)
                {
                    float gainLeft = 1.0f;
                    float gainRight = 1.0f;
                    voice.envelope->gainAt(voice.start + voice.played + i, gainLeft, gainRight);
                    left *= gainLeft;
                    right *= gainRight;
                }
                output[(written + i) * 2] += samples[(voice.position + i) * 2] * left;
                output[(written + i) * 2 + 1] += samples[(voice.position + i) * 2 + 1] * right;
            }
            written += static_cast<unsigned>(take);
            voice.position += take;
            voice.played += take;
        }
        it = finished ? _voices.erase(it) : std::next(it);
    }
    for (unsigned i = 0; i < frameCount * 2; ++i)
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));
}

int AudioEngine::play(std::shared_ptr<const PcmBuffer> buffer, size_t start, size_t end, int loops, float volume,
    std::shared_ptr<const AudioEnvelope> envelope, size_t elapsed)
{
    if (!buffer || buffer->frames() == 0 || !open())
        return -1;
    end = std::min(end, buffer->frames());
    if (start >= end)
        return -1;
    std::lock_guard<std::mutex> lock(_mutex);
    const int id = _nextVoice++;
    Voice voice;
    voice.buffer = std::move(buffer);
    voice.start = start;
    voice.end = end;
    // Part way in: the passes already over are spent, and the voice picks up
    // where the one under way has got to.
    const size_t length = end - start;
    const size_t passes = elapsed / length;
    if (loops > 0 && passes >= static_cast<size_t>(loops))
        return -1;
    voice.position = start + elapsed % length;
    voice.played = elapsed;
    voice.loopsLeft = loops <= 0 ? 0 : loops - static_cast<int>(passes);
    voice.volume = volume;
    voice.envelope = std::move(envelope);
    _voices[id] = std::move(voice);
    return id;
}

void AudioEngine::stop(int voice)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _voices.erase(voice);
}

void AudioEngine::stopAll()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _voices.clear();
}

void AudioEngine::setPaused(int voice, bool paused)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto found = _voices.find(voice);
    if (found != _voices.end())
        found->second.paused = paused;
}

bool AudioEngine::isPlaying(int voice)
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _voices.count(voice) != 0;
}
