#pragma once

#include "command.h"

#include <string>
#include <vector>

namespace fla {

class Layer;
class SymbolInstance;
class Timeline;

/// Function curves, as Harmony's Function view shows them: any animatable
/// property of a layer as a value over time, read through its keyframes
/// and whatever tweens run between them (classic, motion, the camera's),
/// with each keyframe's value there to change. Free of Qt.
namespace curves {

struct Property
{
    std::string key;
    std::string label;
};

/// What \a layer has to animate: its camera's pan, zoom and turn, or its
/// instances' place, scale, skew, turn, color and graphic frame.
std::vector<Property> propertiesOf(const Layer& layer);

/// A symbol instance's \a key, and set to \a value: x, y, rotation, scaleX,
/// scaleY, skew, alpha, brightness, tint (percent) and frame (a graphic's
/// first frame, from 1).
double get(const SymbolInstance& instance, const std::string& key);
void set(SymbolInstance& instance, const std::string& key, double value);

/// The scene the layers are in, for the camera, and its size.
struct Context
{
    const Timeline* timeline = nullptr;
    double frameRate = 24.0;
    double width = 550.0;
    double height = 400.0;
};

/// \a key of \a layer on \a frame, through the tween there; false where the
/// layer shows nothing to read it from.
bool valueAt(const Layer& layer, int frame, const std::string& key, const Context& context, double& value);

struct Key
{
    int frame = 0;
    double value = 0.0;
};

/// The keyframes that set \a key, and their values.
std::vector<Key> keys(const Layer& layer, const std::string& key, const Context& context);

/// Sets \a key on the keyframe at \a frame to \a value, undoably; null when
/// there is no keyframe there to set.
CommandPtr setKey(Layer& layer, int frame, const std::string& key, double value, const Context& context);

} // namespace curves

} // namespace fla
