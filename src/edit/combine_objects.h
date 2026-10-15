#pragma once

#include <utility>
#include <vector>

namespace fla {

class Shape;

/// Modify > Combine Objects, on drawing objects.
namespace combine {

enum class Operation
{
    /// All of them as one drawing object, upper over lower.
    Union,
    /// What all of them share, in the topmost one's colors, as one object.
    Intersect,
    /// The topmost takes its area out of the ones under it, and goes.
    Punch,
    /// The topmost keeps only what of the ones under it lies inside it, and
    /// goes.
    Crop
};

struct Result
{
    /// Union and Intersect: the one new shape, in the space the shapes lie
    /// in, with no transform of its own. The caller owns it.
    Shape* replacement = nullptr;
    /// Punch and Crop: each shape under the topmost with its new contents,
    /// in its own space. The caller owns the new ones.
    std::vector<std::pair<Shape*, Shape*>> rewritten;
    /// The shapes that go.
    std::vector<Shape*> removed;
};

/// Combines \a shapes, bottom first; at least two. False when the result
/// would hold nothing (shapes that do not overlap, for Intersect).
bool apply(Operation operation, const std::vector<Shape*>& shapes, Result& result);

} // namespace combine

} // namespace fla
