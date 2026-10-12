#pragma once

#include "../data/dom_element.h"

#include <algorithm>

namespace fla {

/// Which frame of its symbol a graphic instance shows \a elapsed frames
/// after its keyframe, as Animate plays its looping options: from
/// \a first, within the symbol's \a length frames, and no further than
/// \a last when it is set (Animate's Last field; -1 for none).
///
/// - Loop plays on and starts again, from frame 1 or, with a last frame,
///   from the first frame.
/// - Play Once plays on and holds the last frame (or the end).
/// - Play Once Reverse plays back and holds the last frame (or frame 1).
/// - Loop Reverse plays back and starts again from the end (or, with a
///   last frame, from the first frame).
/// - Single Frame holds the first frame; Ping Pong plays to the end and back.
///
/// A looping first frame past the end plays on round the loop, as a synced
/// tween leaves it.
inline int graphicFrame(LoopType loop, int first, int last, int length, int elapsed)
{
    length = std::max(1, length);
    elapsed = std::max(0, elapsed);
    const int start = std::max(0, std::min(first, length - 1));
    const bool ranged = last >= 0;
    const int end = ranged ? std::max(0, std::min(last, length - 1)) : -1;
    switch (loop)
    {
    case LoopType::SingleFrame:
        return start;
    case LoopType::PlayOnce:
        return std::min(start + elapsed, ranged && end >= start ? end : length - 1);
    case LoopType::PlayOnceReverse:
        return std::max(start - elapsed, ranged && end <= start ? end : 0);
    case LoopType::PingPong:
    {
        if (length <= 1)
            return 0;
        const int period = 2 * (length - 1);
        const int t = (std::max(0, first) + elapsed) % period;
        return t < length ? t : period - t;
    }
    case LoopType::LoopReverse:
    {
        // Between the first and last frames, or the whole symbol.
        const int low = ranged ? std::min(start, end) : 0;
        const int high = ranged ? std::max(start, end) : length - 1;
        const int span = high - low + 1;
        const int offset = ((start - low - elapsed) % span + span) % span;
        return low + offset;
    }
    default:
    {
        if (ranged && end >= start)
            return start + elapsed % (end - start + 1);
        return (std::max(0, first) + elapsed) % length;
    }
    }
}

} // namespace fla
