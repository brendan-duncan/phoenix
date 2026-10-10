#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Frame;
class Layer;

/// Inserts a keyframe at a frame index on a layer, as Animate's Insert
/// Keyframe (F6) does: the span the index falls in is cut there and the new
/// keyframe takes over the rest of it with a copy of the span's contents;
/// past the end of the layer, the last span is stretched up to the index and
/// the new keyframe starts there with a copy of its contents. An index that
/// already holds a keyframe is left alone.
class InsertKeyframeCommand : public Command
{
public:
    InsertKeyframeCommand(Layer* layer, int index, const std::string& name);

    ~InsertKeyframeCommand() override;

    InsertKeyframeCommand(const InsertKeyframeCommand&) = delete;
    InsertKeyframeCommand& operator=(const InsertKeyframeCommand&) = delete;

    void redo() override;

    void undo() override;

    std::string name() const override { return _name; }

    /// The keyframe at the index once the command has run: the one inserted,
    /// or the one that was already there.
    Frame* frame() const { return _frame; }

    /// The keyframe whose index is exactly \a index, or null.
    static Frame* keyframeAt(Layer& layer, int index);

    /// The keyframe whose span covers \a index, or null past the end.
    static Frame* spanAt(Layer& layer, int index);

private:
    Layer* _layer;
    int _index;
    std::string _name;

    /// The inserted keyframe. Owned by the command while it is undone.
    Frame* _frame = nullptr;
    bool _owned = false;
    bool _inserted = false;

    /// The span that was cut or stretched, and its duration before.
    Frame* _span = nullptr;
    int _spanDuration = 0;
    int _lastFrameBefore = 0;
};

} // namespace fla
