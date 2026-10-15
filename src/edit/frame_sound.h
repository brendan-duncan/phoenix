#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;
class Frame;
class Layer;

/// The sound a keyframe plays, as the Frame tab's Sound section sets it.
/// A frame keeps these among its kept attributes (soundName, soundSync,
/// soundLoopMode, soundLoop, soundEffect) and its effect as a SoundEnvelope
/// child, as FLA files store them.
namespace frame_sound {

/// The sound effects, in the order the menu lists them.
extern const char* const kEffects[8];

/// The sound syncs, in the order the menu lists them; "event" is the default
/// and is not written.
extern const char* const kSyncs[4];

struct Settings
{
    std::string name;
    std::string effect = "none";
    std::string sync = "event";
    /// Repeat plays the sound \a loops times; Loop plays it for as long as
    /// the frames last.
    bool loop = false;
    int loops = 1;
};

Settings read(const Frame& frame);

/// \a frame's attributes and children with the sound set to \a settings,
/// and with no sound when its name is empty. An effect other than "none"
/// and "custom" gets the envelope that effect stands for, the fades over a sound
/// \a sampleCount samples long; "custom" keeps the envelope there was.
void write(const Settings& settings, size_t sampleCount,
    std::vector<std::pair<std::string, std::string>>& attributes, std::vector<std::string>& children);

/// The length of the library sound \a name in 44.1 kHz samples, from its
/// item's sampleCount; 0 when it is not known.
size_t sampleCount(const Document& document, const std::string& name);

/// One point of a sound's volume envelope: where, in 44.1 kHz samples into
/// the sound, and each channel's level there, 0 to 32768 (full).
struct EnvelopePoint
{
    size_t mark = 0;
    int left = 32768;
    int right = 32768;
    bool operator==(const EnvelopePoint& other) const
    {
        return mark == other.mark && left == other.left && right == other.right;
    }
};

/// The keyframe's envelope as Edit Envelope shows it: its SoundEnvelope's
/// points in order; none for a sound at full volume throughout.
std::vector<EnvelopePoint> envelope(const Frame& frame);

/// Where the keyframe's sound starts and stops playing, in 44.1 kHz samples
/// into it (inPoint44, outPoint44); \a out is 0 for its end.
void trim(const Frame& frame, size_t& in, size_t& out);

/// \a frame's attributes and children with a hand-made envelope and trim:
/// the effect becomes "custom" (or "none" for no points), the points are
/// written as a SoundEnvelope, the in and out points as the FLA keeps them.
void setEnvelope(const std::vector<EnvelopePoint>& points, size_t in, size_t out,
    std::vector<std::pair<std::string, std::string>>& attributes, std::vector<std::string>& children);

/// Split Audio: a stream sound cut at \a frame (inside its span, not on its
/// keyframe) into two keyframes, the first stopping where the second starts
/// in the sound, each keeping the sound's settings and envelope. False when
/// there is no stream sound playing there to cut.
bool splitAudio(Layer& layer, int frame, double frameRate);

} // namespace frame_sound

} // namespace fla
