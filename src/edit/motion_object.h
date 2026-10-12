#pragma once

#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <string>
#include <vector>

namespace fla {

/// Where an object motion tween has its object at one moment: the offset
/// from where it started, in pixels, and its turn, skew (degrees) and scale
/// (percent).
struct MotionState
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double skewX = 0.0;
    double skewY = 0.0;
    double scaleX = 100.0;
    double scaleY = 100.0;
};

/// An object motion tween (Animate's Create Motion Tween), read from the
/// AnimationCore document a frame keeps in motionObjectXML. Each property
/// is a curve of keyframes over time in TimeScale units per second: a value
/// at a time, with Bezier handles (next, previous) that are a time offset
/// and a value. Handles at the keyframe itself make a straight line. A
/// TimeMap eases the whole span.
class MotionObject
{
public:
    /// Reads \a xml, the motionObjectXML element or the AnimationCore inside
    /// it. False when it is neither.
    bool parse(const std::string& xml);

    /// The state \a frame frames into the span, at \a frameRate frames per
    /// second.
    MotionState at(double frame, double frameRate) const;

    /// The span's length in frames.
    double frames(double frameRate) const;

    /// A property's value at \a time (TimeScale units, before the TimeMap's
    /// ease), or \a fallback when the tween does not animate it.
    double valueAt(const std::string& property, double time, double fallback) const
    {
        return value(property, time, fallback);
    }

    /// The span's length in TimeScale units.
    double duration() const { return _duration; }

    /// When the tween's property keyframes fall, in TimeScale units, in
    /// order and each once, whichever properties they key.
    std::vector<double> keyTimes() const;

private:
    struct Key
    {
        double time = 0.0;
        double value = 0.0;
        double nextTime = 0.0;
        double nextValue = 0.0;
        double previousTime = 0.0;
        double previousValue = 0.0;
    };
    double value(const std::string& property, double time, double fallback) const;

    std::map<std::string, std::vector<Key>> _properties;
    double _timeScale = 1.0;
    double _duration = 0.0;
    double _ease = 0.0;
};

/// The transform of an element whose stored transform \a stored shows the
/// motion at \a start, moved to show it at \a now: the turn, skew and scale
/// taken about \a pivot (in the element's own space, its transformation
/// point) and the position moved by the change in offset.
Transform applyMotion(const Transform& stored, const Point& pivot, const MotionState& start,
    const MotionState& now);

/// The stored transform that applyMotion turns into \a shown: its inverse.
Transform unapplyMotion(const Transform& shown, const Point& pivot, const MotionState& start,
    const MotionState& now);

/// Edits of an object motion tween as its frame keeps it, the text of the
/// motionObjectXML element. Each returns false, leaving it alone, when the
/// text is not one.
namespace motion {

/// Stretches (or squeezes) the tween in time by \a factor: its length,
/// every property keyframe's time and the time of its handles. What a span
/// made longer or shorter does to the keyframes on it.
bool rescale(std::string& xml, double factor);

/// Keeps the tween up to \a time (TimeScale units) and drops the rest, a
/// keyframe at \a time holding each property where it was there.
bool truncate(std::string& xml, double time);

/// A property keyframe at \a time for every property, holding the value it
/// has there, so the motion is unchanged: Insert Keyframe in a motion
/// tween span.
bool addKeys(std::string& xml, double time);

/// The turn, skew and scale of \a transform as a tween's state keeps them,
/// as Animate does: the rotation is the x axis's angle, Skew_X the y axis's
/// angle past it, Skew_Y zero. The offset is zero.
MotionState stateOf(const Transform& transform);

/// The text of a new motion tween \a frames long at \a frameRate, holding
/// its object as \a start has it, a keyframe for every property at its
/// start. \a pivotX and \a pivotY place the transformation point in the
/// object's bounds (0 to 1), which Animate keeps with the tween.
std::string create(const MotionState& start, int frames, double frameRate, double pivotX = 0.5,
    double pivotY = 0.5);

/// Keys \a state at \a time: for each property whose value there differs
/// from the state's, a keyframe at \a time holding the new value (one there
/// already is moved, its handles with it). A property the tween lacks is
/// added, with a keyframe at its start keeping what it had.
bool setKeys(std::string& xml, double time, const MotionState& state);

/// The object's start moved by \a dx, \a dy with the rest of its path
/// where it was: every later position keyframe's offset less the move.
bool moveStart(std::string& xml, double dx, double dy);

/// Bends the object's path between the position keyframes either side of
/// \a time so that it passes through the offset \a x, \a y there, as
/// dragging the motion path does. A position with no keyframe after
/// \a time gets one at \a end, the tween's end, holding where it was.
bool bend(std::string& xml, double time, double end, double x, double y);

/// One keyframe of a property's curve, as the Motion Editor edits it: a
/// value at a time (TimeScale units) and its handles, each a time offset
/// from the keyframe and a value.
struct PropertyKey
{
    double time = 0.0;
    double value = 0.0;
    double nextTime = 0.0;
    double nextValue = 0.0;
    double previousTime = 0.0;
    double previousValue = 0.0;

    /// A keyframe with its handles on it, a straight line either side.
    static PropertyKey at(double time, double value)
    {
        PropertyKey key;
        key.time = time;
        key.value = key.nextValue = key.previousValue = value;
        return key;
    }
};

/// The keyframes of property \a id, in time order; none when the tween
/// does not animate it.
std::vector<PropertyKey> keysOf(const std::string& xml, const std::string& id);

/// Puts \a keys in place of property \a id's keyframes, adding the property
/// in Animate's container for it when the tween has none.
bool setKeysOf(std::string& xml, const std::string& id, const std::vector<PropertyKey>& keys);

/// Shapes the stretch from keys[\a segment] to the next by an ease given as
/// a cubic Bezier from (0,0) to (1,1) with handles (x1, y1) and (x2, y2),
/// as CSS writes one; (1/3, 1/3, 2/3, 2/3) is straight.
void easeSegment(std::vector<PropertyKey>& keys, size_t segment, double x1, double y1, double x2, double y2);

/// The value of the curve \a keys make at \a time.
double valueOf(const std::vector<PropertyKey>& keys, double time);

/// A keyframe added to \a keys at \a time, the curve split there so that
/// it keeps its shape. Returns its index; one already there is left be.
size_t splitAt(std::vector<PropertyKey>& keys, double time);

/// The tween's ease for the whole span, -100 (in) to 100 (out), and
/// setting it.
double spanEase(const std::string& xml);
bool setSpanEase(std::string& xml, double strength);

/// The state that shows an element as \a wanted, where \a stored is its
/// transform as the span keeps it (showing \a start) and \a pivot its
/// transformation point: what applyMotion(stored, pivot, start, state)
/// gives back \a wanted. Its rotation is the one nearest \a near's.
MotionState solve(const Transform& stored, const Point& pivot, const MotionState& start, const Transform& wanted,
    const MotionState& near);

} // namespace motion

} // namespace fla
