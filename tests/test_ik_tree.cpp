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

TEST(ik_coordinates_are_256_twip_units)
{
    double pixels = 0.0;
    // The sample's root joint: 0x1F8F twips across, 0x1CCA down.
    CHECK(fla::parseIKCoordinate("#1F.8F", pixels) && std::fabs(pixels - 403.95) < 1.0e-9);
    CHECK(fla::parseIKCoordinate(" #1C.CA", pixels) && std::fabs(pixels - 368.5) < 1.0e-9);
    // Negative in hex is 24-bit two's complement: -11083 twips.
    CHECK(fla::parseIKCoordinate("#FFFFD4.B5", pixels) && std::fabs(pixels + 554.15) < 1.0e-9);
    // Plain numbers are in the same units: -1356 twips.
    CHECK(fla::parseIKCoordinate("-5.296875", pixels) && std::fabs(pixels + 67.8) < 1.0e-9);
    CHECK(!fla::parseIKCoordinate("", pixels));
    CHECK(!fla::parseIKCoordinate("#XY.1", pixels));
}

TEST(ik_joints_follow_the_pose_span)
{
    // A shape's armature keeps every frame's joints in twips; an armature of
    // symbols keeps its symbols' places there, so its joints come from
    // location.
    const std::string shape =
        "<IKTree><IKNode nodeType=\"lsd\" location=\"#1F.8F, #1C.CA\"><childNodes>"
        "<ChildNode nodeType=\"lsd\" boneName=\"b\" location=\"#0.0, #0.0\" xArray=\"8079,8079\" yArray=\"7370,7370\">"
        "<childNodes><ChildNode nodeType=\"lsd\" boneName=\"c\" location=\"#0.0, #0.0\" xArray=\"15000,10000\" yArray=\"7370,2000\"/>"
        "</childNodes></ChildNode></childNodes></IKNode></IKTree>";
    std::vector<fla::IKBone> bones = fla::ikBones(shape, 0);
    CHECK(bones.size() == 1 && near(bones[0].head, 403.95, 368.5) && near(bones[0].tail, 750.0, 368.5));
    bones = fla::ikBones(shape, 1);
    CHECK(bones.size() == 1 && near(bones[0].tail, 500.0, 100.0));

    const std::string symbols =
        "<IKTree><IKNode location=\"#1D.D8, #20.EE\"><childNodes>"
        "<ChildNode location=\"#1D.D8, #20.EE\" xArray=\"7467.29\" yArray=\"6087.28\"><childNodes>"
        "<ChildNode location=\"#40.43, #1E.48\" xArray=\"15548.96\" yArray=\"5816.74\"/>"
        "</childNodes></ChildNode></childNodes></IKNode></IKTree>";
    bones = fla::ikBones(symbols, 0);
    CHECK(bones.size() == 1);
    if (bones.size() == 1)
    {
        CHECK(std::fabs(bones[0].head.x - 0x1DD8 / 20.0) < 1.0e-9 && std::fabs(bones[0].head.y - 0x20EE / 20.0) < 1.0e-9);
        CHECK(std::fabs(bones[0].tail.x - 0x4043 / 20.0) < 1.0e-9 && std::fabs(bones[0].tail.y - 0x1E48 / 20.0) < 1.0e-9);
    }
}

TEST(ik_joints_follow_the_poses_ease)
{
    // Two poses 4 frames apart, eased in at strength 50: frame 2 is half way
    // in time but 0.5 * 0.5 + 0.5 * 0.25 = 0.375 of the way in the poses,
    // entry 1.5 of the evenly spaced joint lists.
    const std::string tree =
        "<IKTree><IKNode nodeType=\"lsd\" location=\"#0.0, #0.0\"><childNodes>"
        "<ChildNode nodeType=\"lsd\" xArray=\"0,0,0,0,0\" yArray=\"0,0,0,0,0\"><childNodes>"
        "<ChildNode nodeType=\"lsd\" xArray=\"0,200,400,600,800\" yArray=\"2000,2000,2000,2000,2000\"/>"
        "</childNodes></ChildNode></childNodes></IKNode>"
        "<states><IKState duration=\"4\" easeValue=\"50\" easeType=\"simple quadratic\"/><IKState duration=\"1\"/></states>"
        "</IKTree>";
    std::vector<fla::IKBone> bones = fla::ikBones(tree, 2);
    CHECK(bones.size() == 1 && near(bones[0].tail, 15.0, 100.0));
    // The ends are the poses themselves.
    bones = fla::ikBones(tree, 0);
    CHECK(bones.size() == 1 && near(bones[0].tail, 0.0, 100.0));
    bones = fla::ikBones(tree, 4);
    CHECK(bones.size() == 1 && near(bones[0].tail, 40.0, 100.0));
}
