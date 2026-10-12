#pragma once

#include "command.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fla {

class Layer;
class SymbolInstance;

/// Lip sync from a voice track: mouth shapes over time, as Rhubarb Lip
/// Sync (MIT) works them out from the audio, or as Papagayo exports them,
/// put onto a mouth graphic's layer as keyframes that show the frame of
/// the graphic each shape is drawn on (its mouth chart). Free of Qt.
namespace lipsync {

/// One mouth shape, from \a time seconds into the track.
struct Cue
{
    double time = 0.0;
    std::string shape;
};

/// Reads Rhubarb's TSV (time, tab, shape a line) or a Papagayo / Moho
/// switch file ("MohoSwitch1", then frame and phoneme a line, frames from
/// 1 at \a frameRate). Empty when it is neither.
std::vector<Cue> parse(const std::string& text, double frameRate);

/// Mouth shapes heard in a recording without Rhubarb: Phoenix's own guess
/// from how loud and how hissy each moment is, in Rhubarb's letters: X at
/// rest in the quiet, A closed for the short dips between syllables, B for
/// hissing and other teeth-together sounds, C, D and E opening wider as the
/// voice gets louder. Each shape is held at least two frames at
/// \a frameRate. \a samples are 16-bit, \a channels interleaved.
std::vector<Cue> listen(const std::vector<int16_t>& samples, int channels, int sampleRate, double frameRate);

/// Cues as Rhubarb writes them (time, tab, shape a line).
std::string tsvOf(const std::vector<Cue>& cues);

/// The shapes \a cues use, in the order a mouth chart lists them: Rhubarb's
/// A to H and X, Papagayo's phonemes, then any others.
std::vector<std::string> shapesOf(const std::vector<Cue>& cues);

/// A chart's starting guess: the shapes on the graphic's frames 1, 2, 3...
/// in that order.
std::map<std::string, int> defaultChart(const std::vector<std::string>& shapes);

/// A mouth chart as kept on a symbol ("A=1;B=2;X=9") and read back.
std::string chartText(const std::map<std::string, int>& chart);
std::map<std::string, int> chartFrom(const std::string& text);

/// Puts \a cues on \a layer from frame \a start: the frames they cover are
/// cleared and each cue that changes the mouth gets a keyframe holding a
/// copy of \a mouth showing its shape's frame of the chart, held as a
/// single frame. One undoable step; null when there is nothing to put.
CommandPtr apply(Layer& layer, const SymbolInstance& mouth, const std::vector<Cue>& cues,
    const std::map<std::string, int>& chart, int start, double frameRate);

} // namespace lipsync

} // namespace fla
