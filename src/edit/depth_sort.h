#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fla {

class Document;
class Element;
class Frame;
class Layer;

/// Sorting by depth, Phoenix's own (phoenixSortByDepth on a layer): the
/// layer's movie clips are drawn by how far into the stage each is (its Z,
/// as the 3D tools set it), the furthest first, so a clip pushed back goes
/// behind one in front whatever order they were placed in, and the order
/// follows Z as it tweens. Clips at the same depth, and everything that is
/// not a clip, keep their stacking order. Free of Qt.
namespace depthsort {

bool sorts(const Layer& layer);
/// The layer's attributes with sorting on or off.
std::vector<std::pair<std::string, std::string>> withSorting(const Layer& layer, bool on);

/// How far into the stage \a element is: a movie clip's Z, 0 for anything
/// else.
double depthOf(const Element& element);

/// The depths of the elements of keyframe \a span \a framesIn frames into
/// it, part way along its classic tween to \a next (the keyframe after it)
/// as the stage draws it.
std::vector<double> depthsAt(const Frame& span, const Frame* next, int framesIn);

/// The order to draw elements of \a depths in: indices, the deepest first,
/// equal depths in their stacking order.
std::vector<size_t> order(const std::vector<double>& depths);

/// The keyframe after \a span on \a layer that its tween runs to, or null.
const Frame* tweenTarget(const Layer& layer, const Frame& span);

/// The order the elements of keyframe \a span of \a layer are drawn in,
/// \a framesIn frames into it: their stacking order, or by depth when the
/// layer sorts.
std::vector<size_t> drawOrder(const Layer& layer, const Frame& span, int framesIn);

/// For other apps, which stack a layer's elements as they are listed: each
/// sorting layer's keyframes put in depth order, and spans whose order
/// changes as Z tweens made a keyframe a frame (classic tweens kept where
/// the order holds), sorting then turned off. Within a layer only: clips on
/// different layers stack by layer whatever their depth, in Phoenix too.
/// Returns how many layers were baked; \a notes says what changed.
int bake(Document& document, double frameRate, std::vector<std::string>& notes);

} // namespace depthsort

} // namespace fla
