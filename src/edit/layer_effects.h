#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fla {

class Layer;
class Timeline;

/// Composited effects, as layers: a tone (a soft shadow multiplied
/// over the character), a highlight (a soft light screened over it) or a
/// glow (a soft light added round it). The effect layer's art is drawn,
/// blurred, colored and composited over the layer under it, clipped to that
/// layer's art unless it glows outside. It is a guide layer marked with its
/// effect, so other apps that open the FLA, which cannot composite it,
/// leave it out rather than drawing its art flat; Phoenix renders it on the stage and in exports.
/// Free of Qt.
namespace effects {

enum class Kind
{
    Tone,
    Highlight,
    Glow
};

struct Effect
{
    Kind kind = Kind::Tone;
    uint8_t color[4] = {40, 30, 80, 255};
    /// Softness: the blur's width, in stage pixels.
    double blur = 6.0;
    /// 0 to 1.
    double opacity = 0.5;
    /// Kept within the layer under it.
    bool clip = true;
};

const char* kindName(Kind kind);

/// Whether \a layer is an effect layer, and its effect.
bool isEffect(const Layer& layer);
Effect effectOf(const Layer& layer);
/// \a layer's attributes with \a effect kept in them.
std::vector<std::pair<std::string, std::string>> withEffect(const Layer& layer, const Effect& effect);

/// A new effect layer named after \a target ("Hair Tone"), as long as the
/// timeline, holding one empty keyframe for the effect's art. The caller
/// owns it until it is added above \a target.
Layer* newEffectLayer(const Timeline& timeline, const Layer& target, const Effect& effect);

/// The effect's default for \a kind: a dark multiplied tone, a light
/// screened highlight, a warm added glow that is not clipped.
Effect defaults(Kind kind);

} // namespace effects

} // namespace fla
