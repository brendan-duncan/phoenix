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

/// A sound's volume envelope, as a frame's SoundEnvelope gives it: points
/// at sample positions in the sound (44.1 kHz), with a level for each
/// channel, joined by straight lines and held flat past either end.
struct AudioEnvelope
{
    struct Point
    {
        size_t mark = 0;
        float left = 0.0f;
        float right = 0.0f;
    };
    std::vector<Point> points;

    /// The gain of each channel at \a position.
    void gainAt(size_t position, float& left, float& right) const;

    /// Reads the SoundEnvelopePoint elements in \a xml, where 32768 is full
    /// volume and a level left out is silence. Null when there are none or
    /// they leave the sound as it is.
    static std::shared_ptr<const AudioEnvelope> parse(const std::string& xml);
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
    /// An \a envelope shapes the volume by position in the sound, counting
    /// on through loops as Animate's does. \a elapsed starts the voice that
    /// many frames into its playing, loops included, as a stream does when
    /// the playhead starts part way through its span.
    int play(std::shared_ptr<const PcmBuffer> buffer, size_t start, size_t end, int loops = 1, float volume = 1.0f,
        std::shared_ptr<const AudioEnvelope> envelope = nullptr, size_t elapsed = 0);

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
        std::shared_ptr<const AudioEnvelope> envelope;
        /// Frames played so far, which is where the envelope is.
        size_t played = 0;
    };

    std::mutex _mutex;
    std::map<int, Voice> _voices;
    int _nextVoice = 1;
    ma_device* _device = nullptr;
    bool _tried = false;
    bool _open = false;
};
