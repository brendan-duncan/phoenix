#include "timeline_edit.h"

#include "element_clone.h"

#include "../data/action_script.h"
#include "../data/element.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/morph_shape.h"

#include <algorithm>

namespace fla {

namespace timeline {

namespace {

void sortFrames(Layer& layer)
{
    std::stable_sort(layer.frames.begin(), layer.frames.end(),
        [](const Frame* a, const Frame* b) { return a->index < b->index; });
}

void updateExtent(Layer& layer)
{
    layer.firstFrame = layer.frames.empty() ? 0 : layer.frames.front()->index;
    layer.lastFrame = layerEnd(layer);
}

/// The keyframe after \a frame, or null.
Frame* nextOf(Layer& layer, const Frame* frame)
{
    auto at = std::find(layer.frames.begin(), layer.frames.end(), frame);
    if (at == layer.frames.end() || at + 1 == layer.frames.end())
        return nullptr;
    return *(at + 1);
}

Frame* previousOf(Layer& layer, const Frame* frame)
{
    auto at = std::find(layer.frames.begin(), layer.frames.end(), frame);
    if (at == layer.frames.end() || at == layer.frames.begin())
        return nullptr;
    return *(at - 1);
}

Frame* lastOf(Layer& layer)
{
    return layer.frames.empty() ? nullptr : layer.frames.back();
}

} // namespace

int layerEnd(const Layer& layer)
{
    int end = 0;
    for (const Frame* frame : layer.frames)
    {
        if (frame)
            end = std::max(end, frame->index + std::max(frame->duration, 1));
    }
    return end;
}

Frame* spanAt(Layer& layer, int index)
{
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index <= index && index < frame->index + std::max(frame->duration, 1))
            return frame;
    }
    return nullptr;
}

Frame* cloneFrame(const Frame& frame, Layer* parent)
{
    Frame* copy = new Frame(parent);
    copy->index = frame.index;
    copy->duration = frame.duration;
    copy->keyMode = frame.keyMode;
    copy->name = frame.name;
    copy->labelType = frame.labelType;
    copy->tweenType = frame.tweenType;
    copy->motionTWeenSnap = frame.motionTWeenSnap;
    copy->motionTweenSync = frame.motionTweenSync;
    copy->acceleration = frame.acceleration;
    copy->hasCustomEase = frame.hasCustomEase;
    copy->customEase = frame.customEase;
    copy->motionTweenRotate = frame.motionTweenRotate;
    copy->motionTweenRotateTimes = frame.motionTweenRotateTimes;
    copy->extraAttributes = frame.extraAttributes;
    copy->extraChildren = frame.extraChildren;
    if (frame.actionScript)
    {
        copy->actionScript = new ActionScript(copy);
        copy->actionScript->code = frame.actionScript->code;
    }
    if (frame.morphShape)
    {
        MorphShape* morph = new MorphShape(copy);
        for (const MorphSegment* segment : frame.morphShape->segments)
        {
            MorphSegment* segmentCopy = new MorphSegment(morph);
            segmentCopy->startPointA = segment->startPointA;
            segmentCopy->startPointB = segment->startPointB;
            segmentCopy->strokeIndex1 = segment->strokeIndex1;
            segmentCopy->strokeIndex2 = segment->strokeIndex2;
            segmentCopy->fillIndex1 = segment->fillIndex1;
            segmentCopy->fillIndex2 = segment->fillIndex2;
            for (const MorphCurves* curve : segment->curves)
            {
                MorphCurves* curveCopy = new MorphCurves(segmentCopy);
                curveCopy->controlPointA = curve->controlPointA;
                curveCopy->anchorPointA = curve->anchorPointA;
                curveCopy->controlPointB = curve->controlPointB;
                curveCopy->anchorPointB = curve->anchorPointB;
                segmentCopy->curves.push_back(curveCopy);
            }
            morph->segments.push_back(segmentCopy);
        }
        copy->morphShape = morph;
    }
    for (const Element* element : frame.elements)
    {
        if (element)
            copy->elements.push_back(cloneElement(*element, copy));
    }
    return copy;
}

std::vector<Frame*> copyFrames(const Layer& layer, int first, int last)
{
    std::vector<Frame*> copies;
    if (last < first)
        return copies;
    for (const Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        const int start = std::max(frame->index, first);
        const int end = std::min(frame->index + std::max(frame->duration, 1), last + 1);
        if (start >= end)
            continue;
        Frame* copy = cloneFrame(*frame, nullptr);
        copy->index = start - first;
        copy->duration = end - start;
        copies.push_back(copy);
    }
    // Where the layer ends inside the range, a blank span fills the rest.
    const int covered = copies.empty() ? 0 : copies.back()->index + copies.back()->duration;
    if (covered < last - first + 1)
    {
        Frame* blank = new Frame(nullptr);
        blank->index = covered;
        blank->duration = last - first + 1 - covered;
        blank->keyMode = "9728";
        copies.push_back(blank);
    }
    return copies;
}

void pasteFrames(Layer& layer, int at, const std::vector<const Frame*>& copied, std::vector<Frame*>& removed)
{
    if (copied.empty() || at < 0)
        return;
    int length = 0;
    for (const Frame* frame : copied)
        length = std::max(length, frame->index + std::max(frame->duration, 1));
    const int end = at + length;

    // Make room: a span running into the range stops at its start, one
    // running past its end carries on from there as a span of its own.
    std::vector<Frame*> kept;
    std::vector<Frame*> tails;
    for (Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        const int start = frame->index;
        const int stop = start + std::max(frame->duration, 1);
        if (stop <= at || start >= end)
        {
            kept.push_back(frame);
            continue;
        }
        if (stop > end)
        {
            // The part after the paste: the frame itself when nothing of it
            // comes before, else a copy.
            Frame* tail = start < at ? cloneFrame(*frame, &layer) : frame;
            tail->index = end;
            tail->duration = stop - end;
            tails.push_back(tail);
            if (tail == frame)
                continue;
        }
        if (start < at)
        {
            frame->duration = at - start;
            kept.push_back(frame);
        }
        else
        {
            removed.push_back(frame);
        }
    }

    // A gap before the paste, past the end of the layer, is filled by the
    // last span reaching out to it.
    std::sort(kept.begin(), kept.end(), [](const Frame* a, const Frame* b) { return a->index < b->index; });
    if (!kept.empty())
    {
        Frame* last = kept.back();
        const int lastEnd = last->index + std::max(last->duration, 1);
        if (lastEnd < at && last->index < at)
            last->duration = at - last->index;
    }
    else if (at > 0)
    {
        Frame* lead = new Frame(&layer);
        lead->index = 0;
        lead->duration = at;
        lead->keyMode = "9728";
        kept.push_back(lead);
    }

    for (const Frame* source : copied)
    {
        Frame* copy = cloneFrame(*source, &layer);
        copy->index = at + source->index;
        kept.push_back(copy);
    }
    for (Frame* tail : tails)
        kept.push_back(tail);
    layer.frames = kept;
    sortFrames(layer);
    updateExtent(layer);
}

bool moveFrames(const std::vector<Layer*>& sources, const std::vector<Layer*>& targets, int first, int last,
    int to, bool copy, std::vector<Frame*>& removed)
{
    if (sources.empty() || sources.size() != targets.size() || last < first || to < 0)
        return false;
    if (!copy && to == first && sources == targets)
        return false;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        if (!sources[i] || !targets[i])
            return false;
    }

    // Everything is copied before anything changes, so a block moved onto
    // part of itself carries what it held.
    std::vector<std::vector<Frame*>> blocks;
    for (Layer* source : sources)
        blocks.push_back(copyFrames(*source, first, last));

    if (!copy)
    {
        // What is left behind is blank, for as long as the block was, but
        // no further than the layer reached.
        for (Layer* source : sources)
        {
            const int end = std::min(last + 1, layerEnd(*source));
            if (first >= end)
                continue;
            Frame blank(nullptr);
            blank.index = 0;
            blank.duration = end - first;
            blank.keyMode = "9728";
            pasteFrames(*source, first, {&blank}, removed);
        }
    }

    for (size_t i = 0; i < targets.size(); ++i)
    {
        std::vector<const Frame*> block(blocks[i].begin(), blocks[i].end());
        pasteFrames(*targets[i], to, block, removed);
        for (Frame* frame : blocks[i])
            delete frame;
    }
    return true;
}

void insertFrames(Layer& layer, int index, int count)
{
    if (count <= 0 || index < 0)
        return;
    if (layer.frames.empty())
    {
        Frame* frame = new Frame(&layer);
        frame->index = 0;
        frame->duration = index + 1;
        frame->keyMode = "9728";
        layer.frames.push_back(frame);
        updateExtent(layer);
        return;
    }

    Frame* span = spanAt(layer, index);
    if (!span)
    {
        // Past the end: the last span reaches out to the index.
        Frame* last = lastOf(layer);
        last->duration = index + 1 - last->index;
        updateExtent(layer);
        return;
    }
    span->duration += count;
    for (Frame* frame : layer.frames)
    {
        if (frame != span && frame->index > span->index)
            frame->index += count;
    }
    updateExtent(layer);
}

Frame* insertKeyframe(Layer& layer, int index, bool blank)
{
    if (index < 0)
        return nullptr;
    for (Frame* frame : layer.frames)
    {
        if (frame && frame->index == index)
            return frame;
    }

    Frame* span = spanAt(layer, index);
    if (!span && !layer.frames.empty())
    {
        Frame* last = lastOf(layer);
        last->duration = index - last->index;
        span = last;
    }

    Frame* frame = new Frame(&layer);
    frame->index = index;
    frame->keyMode = span ? span->keyMode : std::string("9728");
    if (frame->keyMode.empty())
        frame->keyMode = "9728";
    if (span)
    {
        const int spanEnd = span->index + std::max(span->duration, 1);
        frame->duration = std::max(1, spanEnd - index);
        span->duration = index - span->index;
        if (!blank)
        {
            for (const Element* element : span->elements)
            {
                if (element)
                    frame->elements.push_back(cloneElement(*element, frame));
            }
        }
    }
    else
    {
        // An empty layer: a blank span leads up to the new keyframe.
        frame->duration = 1;
        if (index > 0)
        {
            Frame* lead = new Frame(&layer);
            lead->index = 0;
            lead->duration = index;
            lead->keyMode = "9728";
            layer.frames.push_back(lead);
        }
    }
    layer.frames.push_back(frame);
    sortFrames(layer);
    updateExtent(layer);
    return frame;
}

void removeFrames(Layer& layer, int first, int count, std::vector<Frame*>& removed)
{
    if (count <= 0 || first < 0)
        return;
    const int last = first + count; // one past
    std::vector<Frame*> kept;
    for (Frame* frame : layer.frames)
    {
        if (!frame)
            continue;
        const int start = frame->index;
        const int end = start + std::max(frame->duration, 1);
        const int overlap = std::max(0, std::min(end, last) - std::max(start, first));
        const int remaining = (end - start) - overlap;
        if (remaining <= 0)
        {
            removed.push_back(frame);
            continue;
        }
        frame->duration = remaining;
        if (start >= last)
            frame->index = start - count;
        else if (start >= first)
            frame->index = first;
        kept.push_back(frame);
    }
    layer.frames = kept;
    sortFrames(layer);

    // Two spans that now start at the same frame: the earlier keeps it.
    for (size_t i = 1; i < layer.frames.size(); ++i)
    {
        Frame* previous = layer.frames[i - 1];
        Frame* frame = layer.frames[i];
        if (frame->index < previous->index + previous->duration)
        {
            const int shift = previous->index + previous->duration - frame->index;
            frame->index += shift;
            frame->duration -= shift;
            if (frame->duration <= 0)
            {
                removed.push_back(frame);
                layer.frames.erase(layer.frames.begin() + static_cast<long>(i));
                --i;
            }
        }
    }
    updateExtent(layer);
}

bool clearKeyframe(Layer& layer, int index, std::vector<Frame*>& removed)
{
    Frame* frame = nullptr;
    for (Frame* candidate : layer.frames)
    {
        if (candidate && candidate->index == index)
            frame = candidate;
    }
    if (!frame)
        return false;
    Frame* previous = previousOf(layer, frame);
    if (!previous)
        return false;
    previous->duration += std::max(frame->duration, 1);
    layer.frames.erase(std::find(layer.frames.begin(), layer.frames.end(), frame));
    removed.push_back(frame);
    updateExtent(layer);
    return true;
}

bool moveKeyframe(Layer& layer, int from, int to)
{
    if (from == to || to < 0)
        return false;
    Frame* frame = nullptr;
    for (Frame* candidate : layer.frames)
    {
        if (candidate && candidate->index == from)
            frame = candidate;
    }
    if (!frame)
        return false;

    Frame* previous = previousOf(layer, frame);
    Frame* next = nextOf(layer, frame);
    const int low = previous ? previous->index + 1 : 0;
    const int high = next ? next->index - 1 : to;
    to = std::max(low, std::min(high, to));
    if (to == from)
        return false;

    const int end = frame->index + std::max(frame->duration, 1);
    if (previous)
    {
        previous->duration = to - previous->index;
    }
    else if (to > 0)
    {
        // The first keyframe moved right leaves blank frames behind it.
        Frame* lead = new Frame(&layer);
        lead->index = 0;
        lead->duration = to;
        lead->keyMode = frame->keyMode.empty() ? std::string("9728") : frame->keyMode;
        layer.frames.push_back(lead);
    }
    frame->index = to;
    // Up to the next keyframe it reaches as before; the last keeps its length.
    frame->duration = next ? next->index - to : std::max(1, end - from);
    sortFrames(layer);
    updateExtent(layer);
    return true;
}

bool setTween(Layer& layer, int index, bool classic)
{
    Frame* span = spanAt(layer, index);
    if (!span)
        return false;
    if (classic)
    {
        if (!nextOf(layer, span) || span->tweenType == TweenType::Motion)
            return false;
        span->tweenType = TweenType::Motion;
        return true;
    }
    if (span->tweenType == TweenType::None)
        return false;
    span->tweenType = TweenType::None;
    return true;
}

} // namespace timeline

// ---------------------------------------------------------------------------

EditFramesCommand::EditFramesCommand(std::vector<Layer*> layers, const std::string& name,
    std::function<void(std::vector<Frame*>& removed)> edit)
    : _layers(std::move(layers))
    , _name(name)
    , _edit(std::move(edit))
{
}

EditFramesCommand::~EditFramesCommand()
{
    // While done the layers hold the created frames and the command the
    // removed ones; while undone the other way round.
    const std::set<Frame*>& owned = _done ? _removed : _created;
    for (Frame* frame : owned)
        delete frame;
}

EditFramesCommand::LayerState EditFramesCommand::capture(const Layer& layer)
{
    LayerState state;
    for (Frame* frame : layer.frames)
    {
        if (frame)
            state.frames.push_back({frame, frame->index, frame->duration, static_cast<int>(frame->tweenType)});
    }
    state.firstFrame = layer.firstFrame;
    state.lastFrame = layer.lastFrame;
    return state;
}

void EditFramesCommand::restore(Layer& layer, const LayerState& state)
{
    layer.frames.clear();
    for (const FrameState& entry : state.frames)
    {
        entry.frame->index = entry.index;
        entry.frame->duration = entry.duration;
        entry.frame->tweenType = static_cast<TweenType>(entry.tweenType);
        layer.frames.push_back(entry.frame);
    }
    layer.firstFrame = state.firstFrame;
    layer.lastFrame = state.lastFrame;
}

void EditFramesCommand::redo()
{
    if (!_ran)
    {
        for (Layer* layer : _layers)
            _before.push_back(capture(*layer));
        std::vector<Frame*> removed;
        _edit(removed);
        for (Layer* layer : _layers)
            _after.push_back(capture(*layer));
        _ran = true;

        std::set<Frame*> beforeSet;
        std::set<Frame*> afterSet;
        for (const LayerState& state : _before)
            for (const FrameState& entry : state.frames)
                beforeSet.insert(entry.frame);
        for (const LayerState& state : _after)
            for (const FrameState& entry : state.frames)
                afterSet.insert(entry.frame);
        for (Frame* frame : afterSet)
            if (!beforeSet.count(frame))
                _created.insert(frame);
        for (Frame* frame : beforeSet)
            if (!afterSet.count(frame))
                _removed.insert(frame);
        // Anything the edit both made and dropped is the command's alone.
        for (Frame* frame : removed)
            if (!beforeSet.count(frame) && !afterSet.count(frame))
                delete frame;

        for (size_t i = 0; i < _before.size() && !_changed; ++i)
        {
            const LayerState& a = _before[i];
            const LayerState& b = _after[i];
            if (a.frames.size() != b.frames.size())
            {
                _changed = true;
                break;
            }
            for (size_t k = 0; k < a.frames.size(); ++k)
            {
                if (a.frames[k].frame != b.frames[k].frame || a.frames[k].index != b.frames[k].index ||
                    a.frames[k].duration != b.frames[k].duration || a.frames[k].tweenType != b.frames[k].tweenType)
                {
                    _changed = true;
                    break;
                }
            }
        }
    }
    else
    {
        for (size_t i = 0; i < _layers.size(); ++i)
            restore(*_layers[i], _after[i]);
    }
    _done = true;
}

void EditFramesCommand::undo()
{
    for (size_t i = 0; i < _layers.size() && i < _before.size(); ++i)
        restore(*_layers[i], _before[i]);
    _done = false;
}

} // namespace fla
