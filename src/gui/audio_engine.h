#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ma_device;

/// Sound as Phoenix plays it: interleaved stereo float samples at 44.1 kHz,
/// whatever it was stored as.
struct PcmBuffer
{
    static constexpr int kRate = 44100;
    std::vector<float> samples;

    size_t frames() const { return samples.size() / 2; }
};

namespace AudioDecode {

/// Turns a sound's data, as an FLA keeps it under bin, into PCM. The data is
/// either raw PCM in the layout \a format names ("44kHz 16bit Stereo",
/// "22kHz 8bit Mono"...), little-endian, or MP3 frames, which are told
/// apart by their sync word. Null when the data cannot be read.
std::shared_ptr<const PcmBuffer> decode(const std::vector<uint8_t>& data, const std::string& format);

/// The rate, bits and channels in an FLA format string, or false.
bool parseFormat(const std::string& format, int& rate, int& bits, int& channels);

} // namespace AudioDecode

/// The audio output: one stereo device, opened the first time anything
/// plays, mixing any number of voices. A voice plays part of a buffer, a set
/// number of times or forever, and can be stopped. Thread-safe: the device
/// mixes on its own thread.
class AudioEngine
{
public:
    static AudioEngine& instance();

    /// Whether a device could be opened.
    bool available();

    /// Starts playing \a buffer from frame \a start up to \a end (clamped
    /// to its length), \a loops times in all (0 for forever). Returns the
    /// voice, or -1 when nothing can play.
    int play(std::shared_ptr<const PcmBuffer> buffer, size_t start, size_t end, int loops = 1, float volume = 1.0f);

    void stop(int voice);
    void stopAll();
    bool isPlaying(int voice);

    ~AudioEngine();

private:
    AudioEngine() = default;
    bool open();
    static void callback(ma_device* device, void* output, const void* input, unsigned frameCount);
    void mix(float* output, unsigned frameCount);

    struct Voice
    {
        std::shared_ptr<const PcmBuffer> buffer;
        size_t start = 0;
        size_t end = 0;
        size_t position = 0;
        int loopsLeft = 1;
        float volume = 1.0f;
    };

    std::mutex _mutex;
    std::map<int, Voice> _voices;
    int _nextVoice = 1;
    ma_device* _device = nullptr;
    bool _tried = false;
    bool _open = false;
};
