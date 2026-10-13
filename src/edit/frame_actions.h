#pragma once

#include <map>
#include <string>
#include <vector>

namespace fla {

class Timeline;

/// Simple frame actions: the playback calls in frame scripts that preview
/// and export can honor without running the scripts, as Control > Enable
/// Simple Frame Actions has them do. Only stop(), play(), gotoAndPlay(),
/// gotoAndStop(), nextFrame() and prevFrame(), on the timeline itself (bare
/// or after "this."), with a frame number or a quoted label. Free of Qt.
namespace frameactions {

enum class Call
{
    Stop,
    Play,
    GotoAndPlay,
    GotoAndStop,
    NextFrame,
    PrevFrame,
};

/// One call: a goto's frame (from 0, where the script counts from 1) or
/// label; frame is -1 for a label and for the calls that take no target.
struct Action
{
    Call call = Call::Stop;
    int frame = -1;
    std::string label;
};

/// The playback calls in a script, in order. Comments and string contents
/// are skipped. Only a call that is a statement of its own at the top level
/// of the script counts: nothing inside braces (a function, an if block, a
/// listener), after an if, else, while, for and the like, inside another
/// call's arguments, or with a receiver other than "this" (another clip,
/// the parent, the root). A goto whose target is not a plain number or a
/// quoted label, or that names a scene, is left out. Conditions are never
/// worked out, so leaving those out is the safe side: a stop() that might
/// not run is not taken.
std::vector<Action> parse(const std::string& script);

/// Where the playhead is after the actions run, and whether it plays on.
struct State
{
    int frame = 0;
    bool playing = true;
};

/// A timeline's frame actions: each keyframe's script, which runs when the
/// playhead enters the keyframe's first frame, and the frame labels a goto
/// can name (comments excepted).
class TimelineActions
{
public:
    TimelineActions() = default;
    explicit TimelineActions(const fla::Timeline& timeline);

    /// No frame has any action: playback runs as without them.
    bool empty() const { return _actions.empty(); }

    /// How many frames the timeline runs to.
    int length() const { return _length; }

    /// The frame a label names, or -1.
    int labelFrame(const std::string& label) const;

    /// The playhead enters \a frame while playing: its actions run, and a
    /// goto's target frame's actions after them, up to a few in a chain.
    /// A goto out of range goes to the nearest end; one to an unknown label
    /// is left out.
    State enter(int frame) const;

    /// The frames a clip shows from its start: frame 0 is entered (its
    /// actions run), then each step plays on to the next frame, round to
    /// the first after the last, until an action stops it. The result is
    /// what shows at each step until it stops, or until the steps go round
    /// the same frames again: loopFrom is where that repeat starts, -1 when
    /// it stopped on the last one.
    struct Sequence
    {
        std::vector<int> frames;
        int loopFrom = 0;

        /// The frame shown \a elapsed steps from the start.
        int frameAt(int elapsed) const;
    };
    Sequence sequence() const;

private:
    std::map<int, std::vector<Action>> _actions;
    std::map<std::string, int> _labels;
    int _length = 1;
};

} // namespace frameactions

} // namespace fla
