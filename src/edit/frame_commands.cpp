#include "frame_commands.h"

#include "element_clone.h"

#include "../data/element.h"
#include "../data/frame.h"
#include "../data/layer.h"

#include <algorithm>

namespace fla {

InsertKeyframeCommand::InsertKeyframeCommand(Layer* layer, int index, const std::string& name)
    : _layer(layer)
    , _index(index)
    , _name(name)
{
}

InsertKeyframeCommand::~InsertKeyframeCommand()
{
    if (_owned)
        delete _frame;
}

Frame* InsertKeyframeCommand::keyframeAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index == index)
            return frame;
    }
    return nullptr;
}

Frame* InsertKeyframeCommand::spanAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index <= index && index < frame->index + frame->duration)
            return frame;
    }
    return nullptr;
}

void InsertKeyframeCommand::redo()
{
    if (!_layer || _index < 0)
        return;

    if (Frame* existing = keyframeAt(*_layer, _index))
    {
        // Nothing to do: the index is a keyframe already.
        _frame = existing;
        _inserted = false;
        return;
    }

    _lastFrameBefore = _layer->lastFrame;

    if (!_frame)
    {
        // First time through: find the span to cut or stretch, and build the
        // keyframe from a copy of its contents.
        _span = spanAt(*_layer, _index);
        if (!_span)
        {
            // Past the end: the last span is stretched up to the index.
            for (Frame* frame : _layer->frames)
            {
                if (frame && (!_span || frame->index + frame->duration > _span->index + _span->duration))
                    _span = frame;
            }
        }

        _frame = new Frame(_layer);
        _frame->index = _index;
        if (_span)
        {
            _spanDuration = _span->duration;
            const int spanEnd = _span->index + _span->duration;
            _frame->duration = spanEnd > _index ? spanEnd - _index : 1;
            _frame->keyMode = _span->keyMode;
            for (const Element* element : _span->elements)
            {
                if (element)
                    _frame->elements.push_back(cloneElement(*element, _frame));
            }
        }
        else
        {
            _frame->duration = 1;
        }
    }

    if (_span)
        _span->duration = std::max(1, _index - _span->index);

    // Kept in index order, which is how the file writes them and the
    // timeline reads them.
    auto at = std::find_if(_layer->frames.begin(), _layer->frames.end(),
        [this](const Frame* frame) { return frame && frame->index > _index; });
    _layer->frames.insert(at, _frame);
    _layer->lastFrame = std::max(_layer->lastFrame, _frame->index + _frame->duration);
    _owned = false;
    _inserted = true;
}

void InsertKeyframeCommand::undo()
{
    if (!_inserted || !_layer || !_frame)
        return;

    auto at = std::find(_layer->frames.begin(), _layer->frames.end(), _frame);
    if (at != _layer->frames.end())
        _layer->frames.erase(at);
    if (_span)
        _span->duration = _spanDuration;
    _layer->lastFrame = _lastFrameBefore;
    _owned = true;
    _inserted = false;
}

} // namespace fla
