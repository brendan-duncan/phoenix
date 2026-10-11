#include "test_util.h"

#include "../src/parser/ik_tree.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

/// A node of an IKTree as Animate writes it, with its joint at x, y.
std::string node(const char* tag, const char* bone, double x, double y, const std::string& children)
{
    std::string xml = std::string("<") + tag + " name=\"n\" boneName=\"" + bone + "\"><worldMatrix><Matrix tx=\"" +
        std::to_string(x) + "\" ty=\"" + std::to_string(y) + "\"/></worldMatrix>";
    if (!children.empty())
        xml += "<childNodes>" + children + "</childNodes>";
    return xml + "</" + tag + ">";
}

bool near(const fla::Point& p, double x, double y)
{
    return std::fabs(p.x - x) < 1.0e-3 && std::fabs(p.y - y) < 1.0e-3;
}

} // namespace

TEST(ik_bones_run_from_joint_to_child_joint)
{
    // The chain in ik_bones.fla: the root, its first child at the same
    // place, then three more joints along the shape.
    const std::string chain =
        node("ChildNode", "ikBoneName1", 403.95, 368.5,
            node("ChildNode", "ikBoneName2", 760.6, 230.75,
                node("ChildNode", "ikBoneName3", 1085.15, 382.3,
                    node("ChildNode", "ikBoneName4", 1277.2, 643.45, ""))));
    const std::string tree = "<IKTree name=\"Armature_11\">" +
        node("IKNode", "ikBoneName1", 403.95, 368.5, chain) +
        "<states><IKState duration=\"1\"/></states></IKTree>";
    const std::vector<fla::IKBone> bones = fla::ikBones(tree);
    CHECK(bones.size() == 3);
    if (bones.size() == 3)
    {
        CHECK(near(bones[0].head, 403.95, 368.5) && near(bones[0].tail, 760.6, 230.75));
        CHECK(near(bones[1].head, 760.6, 230.75) && near(bones[1].tail, 1085.15, 382.3));
        CHECK(near(bones[2].head, 1085.15, 382.3) && near(bones[2].tail, 1277.2, 643.45));
        CHECK(bones[2].name == "ikBoneName4");
    }

    // A joint with two children branches into two bones.
    const std::string branch = "<IKTree>" + node("IKNode", "a", 0, 0,
        node("ChildNode", "b", 10, 0, "") + node("ChildNode", "c", 0, 10, "")) + "</IKTree>";
    CHECK(fla::ikBones(branch).size() == 2);

    CHECK(fla::ikBones("").empty());
    CHECK(fla::ikBones("<betweenFrameMatrixList><Matrix/></betweenFrameMatrixList>").empty());
}
