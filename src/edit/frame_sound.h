#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;
class Frame;

/// The sound a keyframe plays, as the Frame tab's Sound section sets it.
/// A frame keeps these among its kept attributes (soundName, soundSync,
/// soundLoopMode, soundLoop, soundEffect) and its effect as a SoundEnvelope
/// child, as Animate writes them.
namespace frame_sound {

/// Animate's effects, in the order its menu lists them.
extern const char* const kEffects[8];

/// Animate's syncs, in the order its menu lists them; "event" is the default
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
/// and "custom" gets the envelope Animate gives it, the fades over a sound
/// \a sampleCount samples long; "custom" keeps the envelope there was.
void write(const Settings& settings, size_t sampleCount,
    std::vector<std::pair<std::string, std::string>>& attributes, std::vector<std::string>& children);

/// The length of the library sound \a name in 44.1 kHz samples, from its
/// item's sampleCount; 0 when it is not known.
size_t sampleCount(const Document& document, const std::string& name);

} // namespace frame_sound

} // namespace fla
