#pragma once

#include <string>
#include <vector>

namespace fla {

class Document;

/// Shot breakdown: an episode file split into shot files. Free of Qt.
namespace shots {

/// Where an episode is cut.
enum class CutBy
{
    /// Each scene is a shot.
    Scene,
    /// Each frame label (named or anchor) starts a shot.
    Label,
    /// The camera cuts where it jumps: a camera keyframe not tweened into.
    Camera
};

struct Shot
{
    std::string name;
    int scene = 0;
    /// The frames of the scene it takes, from 0, both included.
    int first = 0;
    int last = 0;
};

/// The shots \a document falls into, in order.
std::vector<Shot> find(const Document& document, CutBy by);

/// Trims \a document to \a shot: the other scenes go, the scene's frames
/// before and after it go and the rest move to frame 0, and library items
/// nothing uses any more go too. A tween running across a cut is baked
/// into a keyframe per frame on the shot's side, so the shot looks as it
/// did in the episode; the camera is keyed where it was at each cut.
void keepOnly(Document& document, const Shot& shot);

/// \a name made safe as a file name.
std::string fileName(const std::string& name);

} // namespace shots

} // namespace fla
