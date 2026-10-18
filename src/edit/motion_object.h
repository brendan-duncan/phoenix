#pragma once

#include "../data/color_transform.h"
#include "../data/filter.h"
#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <string>
#include <vector>

namespace fla {

/// Where an object motion tween has its object at one moment: the offset
/// from where it started, in pixels, and its turn, skew (degrees) and scale
/// (percent). A clip tweened in 3D also turns about its horizontal and
/// vertical axes (degrees) and moves in depth (pixels, away from the viewer
/// for positive).
struct MotionState
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double skewX = 0.0;
    double skewY = 0.0;
    double scaleX = 100.0;
    double scaleY = 100.0;
    double rotationX = 0.0;
    double rotationY = 0.0;
    double z = 0.0;

    bool flat() const { return rotationX == 0.0 && rotationY == 0.0 && z == 0.0; }
};

/// An ease of a motion tween, a TimeMap of its AnimationCore: the whole
/// span's (the first) or one that properties name by TimeMapIndex, as the
/// Motion Editor gives each property its own. \a type is as the file names
/// it: Quadratic, Cubic, Quartic and Quintic ease in (\a strength below 0)
/// or out (above), by -100 to 100; DualQuadratic to DualQuintic stop and
/// start in the middle (above 0) or ease in and out (below); BounceIn
/// bounces \a strength times on its way in; Custom follows \a points, a
/// chain of cubic Beziers from (0,0) to (1,1) as the custom ease dialog
/// draws it. Others leave the time as it is.
struct MotionEase
{
    std::string type = "Quadratic";
    double strength = 0.0;
    std::vector<Point> points;

    /// Leaves the time as it is.
    bool none() const;
    /// The eased fraction of the way at \a t (0 to 1) of a span \a frames
    /// frames long, from its first frame to its last.
    double at(double t, double frames) const;
};

/// An object motion tween, read from the
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

    /// The color effect \a frame frames in: \a base (the object's own) with
    /// whatever the tween's color curves set over it.
    ColorTransform colorAt(double frame, double frameRate, const ColorTransform& base) const;

    /// The filters \a frame frames in: \a base with the values the tween's
    /// filter curves set.
    std::vector<Filter> filtersAt(double frame, double frameRate, const std::vector<Filter>& base) const;

    /// Whether the tween has color curves, or filter curves.
    bool animatesColor() const;
    bool animatesFilters() const;

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

    /// Whether the tween turns or moves its object in 3D.
    bool threeD() const;

    /// How far behind the object the point it turns about in 3D is, in
    /// pixels (the Settings' xformPtZOffsetPixels).
    double pivotZ() const { return _pivotZ; }

    /// The tween's eases: the first is the span's, others are a property's
    /// own.
    const std::vector<MotionEase>& eases() const { return _eases; }

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
    /// The time \a raw (TimeScale units) comes to under ease \a index, at
    /// \a frameRate.
    double eased(size_t index, double raw, double frameRate) const;
    /// A property's value at the span's \a raw time, eased by its own
    /// TimeMap or the span's, or not at all when it ignores the TimeMap.
    double sample(const std::string& property, double raw, double frameRate, double fallback) const;
    /// The direction the position curve heads at \a raw time, in degrees.
    double heading(double raw, double frameRate) const;
    bool has(const std::string& property) const;

    std::map<std::string, std::vector<Key>> _properties;
    /// Properties marked ignoreTimeMap: the span's ease leaves them be.
    std::vector<std::string> _ignoreTimeMap;
    /// The TimeMaps, and the one each property names when not the first.
    std::vector<MotionEase> _eases;
    std::map<std::string, size_t> _easeOf;
    /// Orient to Path: the object turns as its path does.
    bool _orientToPath = false;
    double _pivotZ = 0.0;

public:
    bool orientsToPath() const { return _orientToPath; }
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
/// as the FLA does: the rotation is the x axis's angle, Skew_X the y axis's
/// angle past it, Skew_Y zero. The offset is zero.
MotionState stateOf(const Transform& transform);

/// The text of a new motion tween \a frames long at \a frameRate, holding
/// its object as \a start has it, a keyframe for every property at its
/// start. \a pivotX and \a pivotY place the transformation point in the
/// object's bounds (0 to 1), which the FLA keeps with the tween.
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

/// The motion moved onto an object that starts as \a to rather than \a from:
/// its turns and skews moved by the difference, its scales in proportion.
/// Its offsets are as they were. What Paste Motion does to what it pastes.
bool rebase(std::string& xml, const MotionState& from, const MotionState& to);

/// A motion as text, the motionObjectXML element, from \a text that is that
/// or a bare AnimationCore (as motion preset files keep it). Empty when
/// it is neither.
std::string wrapped(const std::string& text);

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

/// A color property of a tween: its id, its name in the Motion Editor, and
/// the scale between the curve and the color effect (percent for the
/// multipliers, 1 for the offsets and channel values).
struct ColorChannel
{
    const char* id;
    const char* label;
};
extern const ColorChannel kColorChannels[];
extern const int kColorChannelCount;

/// What color property \a id is on \a color now, in the curve's units.
double colorValue(const ColorTransform& color, const std::string& id);

/// The numeric fields of a filter that a tween animates, and their ids:
/// "<FilterElement>_<index>_<field>".
std::vector<std::string> filterFields(const Filter& filter);
std::string filterPropertyId(const Filter& filter, size_t index, const std::string& field);
double filterValue(const Filter& filter, const std::string& field);

/// The keyframes of property \a id, in time order; none when the tween
/// does not animate it.
std::vector<PropertyKey> keysOf(const std::string& xml, const std::string& id);

/// Puts \a keys in place of property \a id's keyframes, adding the property
/// in the container the FLA files it under when the tween has none.
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

/// Whether the motion tween \a xml (a motionObjectXML) turns its object as
/// its path does (Orient to Path).
bool orientsToPath(const std::string& xml);

/// \a xml with Orient to Path on or off: the Settings tag's orientToPath
/// changed in place, the rest as the file had it. As it was when it has no
/// Settings.
std::string withOrientToPath(const std::string& xml, bool on);

} // namespace fla
