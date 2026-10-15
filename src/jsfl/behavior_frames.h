#pragma once

#include "behavior_engine.h"

#include <string>
#include <vector>

namespace fla {

class Layer;
class Frame;
class Symbol;
class SymbolInstance;

namespace behaviors {

/// The run of keyframes \a keyframe is in on \a layer: the keyframes one
/// after another that hold an instance of \a symbol at \a index, as first
/// and last frames.
void runOf(const Layer& layer, const Frame& keyframe, size_t index, const std::string& symbol, int& start, int& end);

/// The keyed place, turn, scale and alpha of the instance at \a index on
/// \a layer's keyframe holding \a frame, tweened there as a classic tween
/// runs; false when that keyframe has no instance of \a symbol there.
bool keyedOn(const Layer& layer, size_t index, const std::string& symbol, int frame, Transform& transform,
    Keyed& keyed);

/// What \a instance's behaviors make of it on frame \a frame of its
/// timeline, given its keyed transform there.
Copies evaluateOn(Engine& engine, const SymbolInstance& instance, const Transform& keyed, int frame, double frameRate,
    std::string* error = nullptr);

/// \a key (as the Function view reads it: x, y, rotation, scaleX, alpha...)
/// of the first instance with behaviors on \a layer's keyframe holding
/// \a frame, as its behaviors leave it (their first copy); false when none
/// is there, or its behaviors leave no copy.
bool drivenValue(Engine& engine, const Layer& layer, int frame, const std::string& key, double frameRate, double& value);

/// A symbol's length in frames.
int lengthOf(const Symbol* symbol);

/// Bake to Keyframes: frames \a first to \a last of \a layer become a
/// keyframe each, and every instance with behaviors there is left where its
/// behaviors put it on that frame, its copies as instances of their own
/// beside it, and its behaviors taken off. What the frames show does not
/// change. Returns how many instances were baked (0: nothing to bake, and
/// the layer is untouched).
int bake(Engine& engine, Layer& layer, int first, int last, double frameRate, std::vector<std::string>* errors = nullptr);

/// The frames of \a layer that instances with behaviors are on, as runs
/// (first and last frame), joined where they touch.
std::vector<std::pair<int, int>> runsWithBehaviors(const Layer& layer);

/// Bakes every behavior on \a layer, run by run. Returns how many
/// instances were baked.
int bakeAll(Engine& engine, Layer& layer, double frameRate, std::vector<std::string>* errors = nullptr);

} // namespace behaviors

} // namespace fla
