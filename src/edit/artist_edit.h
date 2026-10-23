#pragma once

#include <string>

namespace fla {

class Frame;
class Layer;

/// The edits behind the Artist layout (src/gui/main_window_artist.cpp),
/// which puts animation in three ideas: draw the next frame, hold it, and
/// move something to a later frame. Each works on a layer in place, as the
/// timeline operations do, so a command can wrap it. Qt-free, for the
/// tests.
namespace artist {

/// New Frame: a keyframe one frame long right after the span covering
/// \a index, blank or, with \a copy, holding a copy of that span's drawing;
/// whatever came after moves along a frame. Past the end of the layer it
/// goes right after the last span; on an empty layer it is frame 0.
/// Returns the new keyframe's index.
int addFrameAfter(Layer& layer, int index, bool copy);

/// Whether \a index lies inside a span rather than on its keyframe, so a
/// move there wants a keyframe of its own first. False past the end.
bool insideSpan(const Layer& layer, int index);

/// Whether a classic tween can move the span's contents as they are:
/// symbol instances, and nothing else.
bool tweenable(const Frame& span);

/// Keys the layer at \a index for a move there: a keyframe holding the
/// pose shown, and the span before it a classic tween. Returns the
/// keyframe, or null when \a index is not inside a span.
Frame* keyForMove(Layer& layer, int index);

/// The named eases a span can have: "jump" (no tween, the next keyframe
/// shows when it comes), "even", "in" (a slow start), "out" (a slow stop)
/// and "both".
extern const char* const kEases[5];

/// The named ease of the span covering \a index; "jump" when it is not
/// tweened, or there is no span.
std::string easeOf(const Layer& layer, int index);

/// Gives the span covering \a index the named ease, making it a classic
/// tween or taking the tween away for "jump". False when nothing changed,
/// as when the span is the layer's last and has nothing to tween to.
bool setEase(Layer& layer, int index, const std::string& ease);

} // namespace artist
} // namespace fla
