#pragma once

#include "publish_settings.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct PcmBuffer;

namespace swf {
class Writer;
struct SoundInfo;
}

namespace fla {

class Document;
class Frame;
class Layer;
class Symbol;
class Timeline;

/// A SWF movie's event, start and stop sounds, played as the movie plays
/// them rather than mixed into its soundtrack:
///
/// - Each library sound used is written once (DefineSound), compressed as
///   its library item says or else as the publish settings' event sounds
///   say; an MP3 kept at its imported quality goes in as it is.
/// - The scene's keyframes start and stop them (StartSound), with their
///   loops, sync, in and out points and volume envelope.
/// - Movie clips play their own: each clip with sounds in it, or in the
///   clips inside it, becomes a clip of just those sounds (DefineSprite),
///   placed while the clip is on the stage so it plays and loops along
///   with it.
///
/// The soundtrack streamed with the movie leaves these out (see
/// TimelineAudio::mixdown). Free of Qt.
class SwfEventSounds
{
public:
    /// Encodes interleaved stereo float samples at 44.1 kHz as a bare MP3
    /// stream at a rate, channels and bit rate; false when it cannot.
    using Mp3Encoder = std::function<bool(const std::vector<float>& samples, int rate, int channels, int kbps,
        std::vector<uint8_t>& mp3)>;

    SwfEventSounds(const Document& document, const publish::Settings& settings, Mp3Encoder mp3 = Mp3Encoder());

    /// Writes the sounds and the movie clips that play them, before the
    /// scene's first frame; \a frames is how many the movie has.
    void define(swf::Writer& writer, int frames);

    /// The scene's \a frame: its sounds started and stopped, and the clips
    /// that play sounds placed as they come on and removed as they go.
    /// Before the frame is shown.
    void frame(swf::Writer& writer, int frame);

    /// For a movie that starts its sounds on the timelines they are on
    /// (swfmovie::Options::eventSound), in place of define and frame:
    /// keyframe \a key's event, start or stop sound's id, written to
    /// \a writer the first time, and how it plays; false when it has none
    /// or the sound cannot go in.
    bool keySound(swf::Writer& writer, const Frame& key, int& id, swf::SoundInfo& info);

    /// Whether a sound asked for MP3 that could not be encoded, and went in
    /// as ADPCM instead.
    bool mp3Missing() const { return _mp3Missing; }

    /// A library sound's own compression, from its item's exportFormat and
    /// exportBits; false when it leaves it to the publish settings. With
    /// \a native, an imported MP3 is kept as it is (exportNative).
    static bool itemFormat(const Document& document, const std::string& name,
        publish::Settings::SoundFormat& format, bool& native);

private:
    /// A keyframe starting a sound, a clip of sounds coming on, or one
    /// going, on a timeline's frame.
    struct Action
    {
        enum class Kind
        {
            Sound,
            Place,
            Remove
        };
        Kind kind = Kind::Sound;
        const Frame* key = nullptr;
        int id = 0;
        int depth = 0;
    };
    /// What \a timelines do frame by frame over their first \a length
    /// frames, their clips at depths counted from \a firstDepth by \a step.
    std::map<int, std::vector<Action>> actions(const std::vector<const Timeline*>& timelines, int length,
        int firstDepth, int step, int level);
    /// The clip of sounds for \a symbol's timelines; 0 when none sound.
    int spriteOf(const Symbol* symbol, int level);
    /// The sound \a name's id, written the first time; 0 when it cannot be.
    int soundOf(const std::string& name);
    std::shared_ptr<const PcmBuffer> pcmOf(const std::string& name);
    /// How keyframe \a key plays its sound; false when it cannot.
    bool soundInfo(const Frame& key, swf::SoundInfo& info);

    const Document& _document;
    publish::Settings _settings;
    Mp3Encoder _mp3;
    swf::Writer* _writer = nullptr;
    bool _mp3Missing = false;
    std::map<std::string, int> _sounds;
    std::map<std::string, std::shared_ptr<const PcmBuffer>> _pcm;
    std::map<const Symbol*, int> _sprites;
    std::map<int, std::vector<Action>> _scene;
};

} // namespace fla
