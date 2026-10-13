#pragma once

#include "audio_engine.h"

#include <QObject>

#include <map>
#include <memory>
#include <string>

class Player;

namespace fla {
class FLADocument;
class Frame;
}

/// Plays a document's sounds along with its timeline, as Animate does:
///
/// - **Stream** sounds are tied to their frames. They play from the point
///   matching the frame shown, for as long as their span lasts, and a
///   scrub plays a frame's worth of them so the sound can be found.
/// - **Event** sounds start when the playhead reaches their keyframe and
///   play to the end whatever the timeline does.
/// - **Start** sounds are event sounds that do not start again while
///   already playing.
/// - **Stop** silences the sound named.
///
/// A frame's in and out points (inPoint44, outPoint44) trim the sound, and
/// its loop settings repeat it.
class TimelineAudio : public QObject
{
    Q_OBJECT

public:
    TimelineAudio(Player* player, QObject* parent = nullptr);

    void setDocument(const fla::FLADocument* document);
    void setMuted(bool muted);

    /// A library sound decoded, by name; null when it cannot be read.
    std::shared_ptr<const PcmBuffer> librarySound(const std::string& name) { return buffer(name); }
    bool muted() const { return _muted; }

    /// The decoded sound a frame plays, for drawing its waveform; null when
    /// it has none or it cannot be read. \a in is where in the sound the
    /// frame starts it.
    std::shared_ptr<const PcmBuffer> soundOf(const fla::Frame* frame, size_t* in = nullptr);

    /// The sound of frames \a first to \a last played through, mixed to
    /// interleaved stereo at 44.1 kHz, for a video's soundtrack: event and
    /// start sounds from their keyframes (a start sound not again while it
    /// plays, a stop sound cutting them off), streams for their spans.
    std::vector<float> mixdown(int first, int last);

public slots:
    void onPlayingChanged(bool playing);
    void onFrameChanged(int frame);

private:
    struct FrameSound
    {
        std::string name;
        std::string sync;
        size_t in = 0;
        size_t out = 0;
        int loops = 1;
        bool hasOut = false;
        std::shared_ptr<const AudioEnvelope> envelope;
    };
    static bool soundSettings(const fla::Frame& frame, FrameSound& sound);
    std::shared_ptr<const PcmBuffer> buffer(const std::string& name);
    double frameRate() const;
    void stopStreams();

    Player* _player;
    const fla::FLADocument* _document = nullptr;
    bool _playing = false;
    bool _muted = false;
    int _lastFrame = -1;
    /// Decoded sounds by name, each with the bytes it was decoded from (a
    /// sound edited, or a name reused, decodes again) and when it was last
    /// played. Decoded audio is large (about 1.3 GB an hour), so the least
    /// recently used go once the total passes a budget; voices still
    /// playing one keep their own share of it.
    struct Decoded
    {
        std::shared_ptr<const PcmBuffer> buffer;
        std::string dataPath;
        std::string format;
        std::weak_ptr<const void> source;
        uint64_t used = 0;
    };
    std::map<std::string, Decoded> _decoded;
    uint64_t _useClock = 0;
    static constexpr size_t kDecodedBudgetBytes = size_t(768) * 1024 * 1024;
    void trimDecoded();
    /// The stream voice playing for each keyframe.
    std::map<const fla::Frame*, int> _streams;
    /// Event and start voices by sound name, so Start and Stop can find them.
    std::multimap<std::string, int> _events;
    int _scrubVoice = -1;
};
