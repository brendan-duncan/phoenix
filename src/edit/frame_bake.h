#pragma once

#include <vector>

namespace fla {

class DOMElement;
class Element;
class Frame;
class Layer;

/// Frame commands that rewrite spans: Convert to Frame-by-Frame Animation,
/// Reverse Frames and Paste Frames (inserting). Free of Qt.
namespace frames {

/// What the span starting at \a span shows \a framesIn frames in: copies of
/// its elements, moved along its classic, shape or motion tween toward
/// \a next (the keyframe after it, for classic and shape tweens) as the
/// stage draws them. The copies belong to \a parent; the caller owns them.
std::vector<Element*> elementsAt(const Frame& span, const Frame* next, int framesIn, double frameRate,
    DOMElement* parent);

/// Convert to Frame-by-Frame Animation: every span of \a layer that
/// overlaps frames \a first to \a last becomes a keyframe per frame, each
/// holding what that frame showed; their tweens go.
void convertToFrameByFrame(Layer& layer, int first, int last, double frameRate);

/// Frames \a first to \a last of \a layer that show just what the frame
/// before them shows (what a baked keyframe a frame often leaves) made part
/// of that frame's span, so a keyframe stays only where something changes.
/// A frame with a label, a script or a sound stays a keyframe. A graphic
/// that plays on counts as unchanged only where it shows the frame its loop
/// would reach anyway. Returns how many keyframes went.
int mergeHeldFrames(Layer& layer, int first, int last);

/// Reverse Frames: frames \a first to \a last play backward. Keyframes are
/// made at both ends when there are none, every keyframe goes to the mirror
/// of its place, and a tween moves to the keyframe it ran toward, so it
/// plays back over the same frames (its ease flipped). An object motion
/// tween in the range is made static, its motion being stored in time.
void reverseFrames(Layer& layer, int first, int last, std::vector<Frame*>& removed);

/// Paste Frames, inserting: everything from \a at moves on by the copied
/// frames' length, and the copies go in the gap. (Paste and Overwrite is
/// timeline::pasteFrames.)
void insertFrames(Layer& layer, int at, const std::vector<const Frame*>& copied, std::vector<Frame*>& removed);

} // namespace frames

} // namespace fla
