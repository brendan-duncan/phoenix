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

} // namespace motion

} // namespace fla
