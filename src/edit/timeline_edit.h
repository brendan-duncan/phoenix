#pragma once

#include "command.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace fla {

class Frame;
class Layer;

/// Animate's frame operations on one layer. Each works on the layer in
/// place and leaves its frames in index order with no gaps or overlaps; a
/// frame taken off the layer is not deleted but handed back through
/// \a removed, so an undoable command can keep it. Frame numbers are
/// zero-based, as the file stores them.
namespace timeline {

/// Insert Frame (F5): the span covering \a index grows by \a count frames
/// and everything after it moves along. Past the end of the layer, the last
/// span is stretched to reach \a index instead.
void insertFrames(Layer& layer, int index, int count);

/// Insert Keyframe (F6) or, with \a blank, Insert Blank Keyframe (F7): the
/// span covering \a index is cut there and the new keyframe takes the rest
/// of it, holding a copy of the span's contents or nothing. Past the end,
/// the last span is stretched up to \a index first. A keyframe already at
/// \a index is left as it is. Returns the keyframe at \a index.
Frame* insertKeyframe(Layer& layer, int index, bool blank);

/// Remove Frames (shift+F5): frames \a first to \a first + \a count - 1 go
/// and everything after them moves back. A span loses the frames of it
/// that fall in the range and goes when it has none left; a keyframe whose
/// first frame is removed starts at \a first instead.
void removeFrames(Layer& layer, int first, int count, std::vector<Frame*>& removed);

/// Clear Keyframe (shift+F6): the keyframe at \a index goes and the span
/// before it takes over its frames. The first keyframe of a layer cannot be
/// cleared. False when there is no keyframe to clear.
bool clearKeyframe(Layer& layer, int index, std::vector<Frame*>& removed);

/// Moves the keyframe at \a from to \a to, as dragging it in the timeline
/// does. It cannot pass the keyframes either side of it. The span before it
/// grows or shrinks to meet it; the first keyframe moved right leaves blank
/// frames before it. The last keyframe keeps its length. False when nothing
/// moved.
bool moveKeyframe(Layer& layer, int from, int to);

/// Sets the tween of the span covering \a index: a classic tween, or none
/// to remove one. A classic tween needs a keyframe after the span to tween
/// to; false when there is none, or nothing changed.
bool setTween(Layer& layer, int index, bool classic);

/// Makes the span covering \a index a shape tween, which needs a shape on
/// it and on the keyframe after it. The morph is worked out from the two
/// shapes when it is first needed. False when it cannot be one, or already
/// is.
bool setShapeTween(Layer& layer, int index);

/// The keyframe whose span covers \a index, or null past the end.
Frame* spanAt(Layer& layer, int index);

/// A deep copy of \a frame: its settings, elements, shape tween and
/// script. The copy belongs to \a parent but is not added to it.
Frame* cloneFrame(const Frame& frame, Layer* parent);

/// Copy Frames: frames \a first to \a last of the layer as spans of their
/// own, cut to the range and numbered from 0, which the caller owns. Every
/// frame of the range is covered; where the layer has none, a blank span
/// stands in.
std::vector<Frame*> copyFrames(const Layer& layer, int first, int last);

/// Paste Frames: the copied spans go in at \a at and replace what was
/// there for as many frames as they cover; a span running on past the
/// pasted frames keeps its remaining frames, starting where they end.
void pasteFrames(Layer& layer, int at, const std::vector<const Frame*>& copied, std::vector<Frame*>& removed);

/// Dragging a block of selected frames, as in Animate: frames \a first to
/// \a last of each layer in \a sources go to frame \a to of the layer at the
/// same place in \a targets (the same layers, or as many rows up or down),
/// replacing what was there. Unless \a copy (an Alt-drag), the frames left
/// behind become blank; nothing after them moves. False when the lists do
/// not pair up or the block would not move.
bool moveFrames(const std::vector<Layer*>& sources, const std::vector<Layer*>& targets, int first, int last,
    int to, bool copy, std::vector<Frame*>& removed);

/// One past the layer's last frame.
int layerEnd(const Layer& layer);

} // namespace timeline

/// An undoable edit of some layers' frames. The edit runs on the first
/// redo; the command records each layer's frames (which frames, at which
/// index, how long, with what tween) before and after, and later undos and
/// redos put those records back. Frames the edit takes off a layer are kept
/// by the command for as long as they may come back.
class EditFramesCommand : public Command
{
public:
    /// \a edit is given the layers and a list to put removed frames in.
    EditFramesCommand(std::vector<Layer*> layers, const std::string& name,
        std::function<void(std::vector<Frame*>& removed)> edit);
    ~EditFramesCommand() override;

    EditFramesCommand(const EditFramesCommand&) = delete;
    EditFramesCommand& operator=(const EditFramesCommand&) = delete;

    void redo() override;
    void undo() override;
    std::string name() const override { return _name; }

    /// Whether the edit changed anything; a command that did not is best
    /// left off the history.
    bool changedAnything() const { return _changed; }

private:
    struct FrameState
    {
        Frame* frame;
        int index;
        int duration;
        int tweenType;
    };
    struct LayerState
    {
        std::vector<FrameState> frames;
        int firstFrame;
        int lastFrame;
    };

    static LayerState capture(const Layer& layer);
    static void restore(Layer& layer, const LayerState& state);

    std::vector<Layer*> _layers;
    std::string _name;
    std::function<void(std::vector<Frame*>& removed)> _edit;
    std::vector<LayerState> _before;
    std::vector<LayerState> _after;
    bool _ran = false;
    bool _done = false;
    bool _changed = false;
    /// Frames the edit created or removed, which one side of the history
    /// holds and the other does not.
    std::set<Frame*> _created;
    std::set<Frame*> _removed;
};

} // namespace fla
