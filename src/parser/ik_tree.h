#pragma once

#include "../data/point.h"

#include <string>
#include <vector>

namespace fla {

/// One bone of an IK armature as the frame's pose has it: from its head
/// joint to its tail joint, in the coordinates of the timeline the armature
/// is on.
struct IKBone
{
    Point head;
    Point tail;
    std::string name;
};

/// The bones of an armature, read from the IKTree element the FLA keeps on
/// each pose frame (as XML text; Phoenix does not model IK yet), at \a frame
/// frames into the pose span. Each node of the tree is a joint, and a bone
/// runs from a joint to each of its child joints; a child at its parent's
/// very place (the root's first child) makes no bone.
///
/// Where a joint is: in a shape's armature (nodeType "lsd") the node's
/// xArray and yArray give it for every frame of the span, in twips; in an
/// armature of symbols they give the symbol's position instead, and the
/// joint is the node's location, as saved; with neither, its worldMatrix.
/// Empty when \a xml is not an IKTree.
std::vector<IKBone> ikBones(const std::string& xml, int frame = 0);

/// A coordinate as IK data writes it, in pixels: "#1F.8F" is hex fixed
/// point (24-bit signed whole part, 8-bit fraction) and "-5.296875" plain,
/// both in units of 256 twips. False when \a text is neither.
bool parseIKCoordinate(const std::string& text, double& pixels);

} // namespace fla
