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

/// The bones of an armature, read from the IKTree element Animate keeps on
/// each pose frame (as XML text; Phoenix does not model IK yet). Each node
/// of the tree is a joint placed by its worldMatrix, and a bone runs from a
/// joint to each of its child joints; a child at its parent's very place
/// (the root's first child) makes no bone. Empty when \a xml is not an
/// IKTree.
std::vector<IKBone> ikBones(const std::string& xml);

} // namespace fla
