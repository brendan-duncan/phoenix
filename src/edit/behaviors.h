#pragma once

#include "../data/color_transform.h"
#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fla {

class DOMElement;
class SymbolInstance;

/// Behaviors: animation worked out each frame rather than keyed. A symbol
/// instance carries a list of them (Wiggle, Oscillate, Follow with lag...),
/// each with its own settings; on every frame they move, turn, scale and
/// fade the instance on top of what its keyframes and tweens give it. They
/// are Phoenix's own: kept in the instance's phoenixBehaviors attribute,
/// which other apps that open the FLA ignore, showing the instance as keyed.
/// Free of Qt; the behaviors themselves run in behavior_engine.h.
namespace behaviors {

/// One behavior on an instance: which (its id, the name of its file
/// without .js), whether it is on, and its settings by name, as text.
struct Applied
{
    std::string type;
    bool enabled = true;
    std::map<std::string, std::string> params;

    /// A setting as a number, or \a fallback when it is not set.
    double number(const std::string& name, double fallback) const;
    bool operator==(const Applied& other) const;

    /// A setting can be keyed: its text is then "@frame=value;frame=value"
    /// (frames on the instance's own timeline, from 0). Numbers run in a
    /// straight line from key to key and hold past the first and last;
    /// anything else holds until the next key.
    bool keyed(const std::string& name) const;
    std::vector<std::pair<int, std::string>> keys(const std::string& name) const;
    /// The setting's value on \a frame, or \a fallback when it is not set.
    std::string valueAt(const std::string& name, int frame, const std::string& fallback) const;
    /// Keys \a value on \a frame, making the setting keyed when it was not.
    void setKey(const std::string& name, int frame, const std::string& value);
    /// Takes the key on \a frame away; with no key left, the setting is
    /// \a value from then on.
    void removeKey(const std::string& name, int frame, const std::string& value);
};

/// A setting a behavior offers, as its file declares it; the Behaviors
/// panel makes a control of each.
struct ParamSpec
{
    std::string name;
    std::string label;
    /// "number", "angle", "percent", "seed", "checkbox", "choice", "text",
    /// "object" (another instance's name) or "controller" (a master
    /// controller's name).
    std::string type = "number";
    double value = 0.0;
    double min = 0.0;
    double max = 100.0;
    double step = 1.0;
    /// A choice's options; its value is the option's text.
    std::vector<std::string> choices;
    std::string text;
};

/// A behavior as its file defines it.
struct Definition
{
    /// The file's name without .js: what an instance's list names it by.
    std::string id;
    std::string name;
    std::string description;
    std::vector<ParamSpec> params;
    bool builtIn = false;
    /// Where it was read from; empty for a built-in one.
    std::string file;
};

/// The behaviors on \a instance, in the order they apply.
std::vector<Applied> of(const SymbolInstance& instance);
/// Whether \a instance has any behavior that is on.
bool any(const SymbolInstance& instance);

/// \a instance's attributes with \a list in them (none when empty).
std::vector<std::pair<std::string, std::string>> withBehaviors(const SymbolInstance& instance,
    const std::vector<Applied>& list);

/// The same kind of list kept on any element under \a attribute (shape
/// operators are kept on shapes as phoenixShapeOperators), and the
/// element's attributes with \a list in its place.
std::vector<Applied> listOf(const DOMElement& element, const std::string& attribute);
std::vector<std::pair<std::string, std::string>> withList(const DOMElement& element, const std::string& attribute,
    const std::vector<Applied>& list);

/// The list as the attribute keeps it, and back.
std::string encode(const std::vector<Applied>& list);
std::vector<Applied> decode(const std::string& text);

/// What the instance's keyframes and tweens give it on a frame, as the
/// behaviors see it: its place (the transformation point on the stage),
/// turn and scale in degrees and as factors, and its alpha from 0 to 1.
struct Keyed
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double alpha = 1.0;
};

/// \a transform and \a color as Keyed, about the pivot \a pivot (the
/// transformation point, in the instance's own space).
Keyed keyedOf(const Transform& transform, const Point& pivot, const ColorTransform& color);

/// What the behaviors add: a move, a turn about the transformation point,
/// a scale about it, an alpha factor, and frames on for a graphic's loop.
/// Several add up: moves, turns and frames sum, scales and alphas
/// multiply.
struct Delta
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double alpha = 1.0;
    int frame = 0;

    bool none() const
    {
        return x == 0.0 && y == 0.0 && rotation == 0.0 && scaleX == 1.0 && scaleY == 1.0 && alpha == 1.0 &&
            frame == 0;
    }
    void add(const Delta& other);
};

/// What behaviors make of an instance on a frame: one Delta for each copy
/// drawn. Most behaviors leave one; a behavior that repeats (in a circle,
/// along a line or a path) makes several, and the behaviors after it work
/// on each.
using Copies = std::vector<Delta>;

/// \a keyed with \a delta on it: turned and scaled about \a pivot (in the
/// instance's own space, so a turned instance scales along its own axes),
/// then moved.
Transform apply(const Transform& keyed, const Point& pivot, const Delta& delta);

} // namespace behaviors

} // namespace fla
