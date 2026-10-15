#pragma once

#include "../data/color_transform.h"
#include "../data/point.h"
#include "../data/transform.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fla {

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
};

/// The behaviors on \a instance, in the order they apply.
std::vector<Applied> of(const SymbolInstance& instance);
/// Whether \a instance has any behavior that is on.
bool any(const SymbolInstance& instance);

/// \a instance's attributes with \a list in them (none when empty).
std::vector<std::pair<std::string, std::string>> withBehaviors(const SymbolInstance& instance,
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
/// a scale about it, and an alpha factor. Several add up: moves and turns
/// sum, scales and alphas multiply.
struct Delta
{
    double x = 0.0;
    double y = 0.0;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double alpha = 1.0;

    bool none() const
    {
        return x == 0.0 && y == 0.0 && rotation == 0.0 && scaleX == 1.0 && scaleY == 1.0 && alpha == 1.0;
    }
    void add(const Delta& other);
};

/// \a keyed with \a delta on it: turned and scaled about \a pivot (in the
/// instance's own space, so a turned instance scales along its own axes),
/// then moved.
Transform apply(const Transform& keyed, const Point& pivot, const Delta& delta);

} // namespace behaviors

} // namespace fla
