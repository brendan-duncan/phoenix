#pragma once

#include "../data/transform.h"

#include <string>
#include <utility>
#include <vector>

namespace fla {

class Frame;
class Layer;
class Timeline;

/// Harmony's multiplane camera, kept so that Animate opens the file: the
/// camera is a guide layer (which Animate never draws) marked as Phoenix's,
/// its keyframes holding where the camera looks, how far it has moved in
/// (zoom) and its turn; each layer may sit at a depth behind or in front of
/// the stage's plane. Panning moves near layers more than far ones, and
/// moving in grows near layers faster: the parallax of a multiplane
/// camera. At rest (looking at the stage's middle, 100%, unturned) every
/// layer shows as drawn, whatever its depth. Free of Qt.
namespace camera {

/// Where the camera is on one frame: the point of the stage it looks at,
/// its zoom in percent, and its turn in degrees.
struct State
{
    double x = 0.0;
    double y = 0.0;
    double zoom = 100.0;
    double rotation = 0.0;
};

/// The distance from the camera to the stage's plane, in pixels: a layer
/// this deep moves half as much as the stage when the camera pans.
constexpr double kFocal = 1000.0;

/// Whether \a layer is a Phoenix camera layer.
bool isCamera(const Layer& layer);

/// The scene's camera layer, or null.
Layer* cameraOf(const Timeline& timeline);

/// The camera on \a frame of \a timeline (stage \a width by \a height),
/// tweened between its keyframes where they are tweened. A camera with no
/// keyframe yet looks at the stage's middle.
State stateAt(const Timeline& timeline, int frame, double width, double height);

/// A camera keyframe's state, and setting it in a frame's attributes.
State stateOf(const Frame& frame, double width, double height);
void set(std::vector<std::pair<std::string, std::string>>& attributes, const State& state);

/// A layer's depth in pixels (0 for the stage's plane; more is farther).
double depthOf(const Layer& layer);
void setDepth(std::vector<std::pair<std::string, std::string>>& attributes, double depth);

/// How the camera shows a layer at \a depth: stage coordinates of what is
/// drawn to stage coordinates of the picture. False when the layer is
/// behind the camera and so not seen.
bool view(const State& state, double depth, double width, double height, Transform& out);

/// A new camera layer for \a timeline, \a frames long, at rest. The caller
/// owns it.
Layer* newCameraLayer(const Timeline& timeline, int frames, double width, double height);

} // namespace camera

} // namespace fla
