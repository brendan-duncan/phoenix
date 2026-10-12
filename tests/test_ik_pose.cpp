#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/ik_pose.h"
#include "../src/parser/fla_parser.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool near(const fla::Point& p, double x, double y, double tolerance = 1.0e-3)
{
    return std::fabs(p.x - x) < tolerance && std::fabs(p.y - y) < tolerance;
}

/// A posed node: one State per pose, each at \a x, \a y twips and a
/// relative angle of zero.
std::string node(const char* name, double x, double y, const std::string& children)
{
    std::string xml = std::string("<ChildNode name=\"") + name + "\" nodeType=\"lsd\"><states>" +
        "<State duration=\"10\" angle=\"0\" x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\"/>" +
        "<State duration=\"1\" angle=\"0\" x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\"/>" +
        "</states>";
    if (!children.empty())
        xml += "<childNodes>" + children + "</childNodes>";
    return xml + "</ChildNode>";
}

/// Three joints in a row along x, 100 pixels apart, posed the same at
/// frames 0 and 10.
std::string chain()
{
    return "<IKTree><IKNode name=\"root\"><childNodes>" +
        node("a", 0.0, 0.0, node("b", 2000.0, 0.0, node("c", 2000.0, 0.0, ""))) +
        "</childNodes></IKNode><states><IKState duration=\"10\"/><IKState duration=\"1\"/></states></IKTree>";
}

} // namespace

TEST(ik_armature_poses_a_frame_and_stores_every_frame)
{
    fla::IKArmature armature;
    CHECK(armature.parse(chain()));
    CHECK(armature.bendsShape());
    CHECK(armature.frames() == 11);
    CHECK(armature.poses() == std::vector<int>({0, 10}));

    std::vector<fla::IKJoint> joints = armature.posedAt(4);
    CHECK(joints.size() == 3);
    CHECK(near(joints[2].position, 200.0, 0.0));

    // Bend the middle joint a right angle at frame 5: a pose goes in there.
    CHECK(armature.setPose(5, {0.0, kPi / 2.0, kPi / 2.0}));
    CHECK(armature.poses() == std::vector<int>({0, 5, 10}));
    joints = armature.jointsAt(5);
    CHECK(near(joints[1].position, 100.0, 0.0));
    CHECK(near(joints[2].position, 100.0, 100.0));
    CHECK(std::fabs(joints[2].angle - kPi / 2.0) < 1.0e-6);
    // The poses either side are as they were, and frames between ease
    // linearly with no ease set.
    CHECK(near(armature.jointsAt(0)[2].position, 200.0, 0.0));
    CHECK(near(armature.jointsAt(10)[2].position, 200.0, 0.0));
    const double half = kPi / 4.0;
    CHECK(std::fabs(armature.jointsAt(2)[1].angle - kPi / 2.0 * 2.0 / 5.0) < 1.0e-6);
    CHECK(near(armature.jointsAt(7)[2].position, 100.0 + 100.0 * std::cos(kPi / 2.0 * 3.0 / 5.0),
        100.0 * std::sin(kPi / 2.0 * 3.0 / 5.0)));
    (void)half;

    // Written out and read back, it is the same armature.
    fla::IKArmature again;
    CHECK(again.parse(armature.xml()));
    CHECK(near(again.jointsAt(5)[2].position, 100.0, 100.0));

    // Posing an existing pose changes it in place.
    CHECK(armature.setPose(5, {0.0, 0.0, 0.0}));
    CHECK(armature.poses() == std::vector<int>({0, 5, 10}));
    CHECK(near(armature.jointsAt(5)[2].position, 200.0, 0.0));

    CHECK(!armature.setPose(11, {0.0, 0.0, 0.0}));
    CHECK(!armature.setPose(3, {0.0}));
    CHECK(!fla::IKArmature().parse("<betweenFrameMatrixList/>"));
}

TEST(ik_ease_out_starts_fast)
{
    // A positive simple quadratic ease of 50 runs time as 1.5t - 0.5t^2, as
    // the in-betweens Animate stores in ik_poses.fla show.
    std::string xml = chain();
    xml.replace(xml.find("<IKState duration=\"10\"/>"), 24,
        "<IKState duration=\"10\" easeType=\"simple quadratic\" easeValue=\"50\"/>");
    fla::IKArmature armature;
    CHECK(armature.parse(xml));
    xml = armature.xml();
    // Turn the first joint at frame 10, then look at frame 2.
    CHECK(armature.setPose(10, {1.0, 1.0, 1.0}));
    const double t = 0.2;
    CHECK(std::fabs(armature.jointsAt(2)[0].angle - (1.5 * t - 0.5 * t * t)) < 1.0e-6);
}

TEST(ik_solver_reaches_for_the_target)
{
    std::vector<fla::IKJoint> joints(3);
    joints[0].position = fla::Point(0.0, 0.0);
    joints[1].position = fla::Point(100.0, 0.0);
    joints[1].parent = 0;
    joints[2].position = fla::Point(200.0, 0.0);
    joints[2].parent = 1;

    const std::vector<fla::IKJoint> posed = fla::solveIK(joints, 2, fla::Point(100.0, 100.0));
    CHECK(near(posed[2].position, 100.0, 100.0, 0.05));
    // The bones keep their lengths and the root stays put.
    CHECK(near(posed[0].position, 0.0, 0.0));
    CHECK(std::fabs(std::hypot(posed[1].position.x, posed[1].position.y) - 100.0) < 1.0e-6);
    // Out of reach, it points at the target.
    const std::vector<fla::IKJoint> far = fla::solveIK(joints, 2, fla::Point(0.0, 500.0));
    CHECK(near(far[2].position, 0.0, 200.0, 0.05));
}

TEST(ik_skin_carries_points_with_their_bone)
{
    std::vector<fla::IKJoint> before(3);
    before[1].position = fla::Point(100.0, 0.0);
    before[1].parent = 0;
    before[2].position = fla::Point(200.0, 0.0);
    before[2].parent = 1;

    // Unmoved, nothing moves.
    CHECK(near(fla::skinPoint(fla::Point(150.0, 10.0), before, before), 150.0, 10.0));

    // The second bone turned a right angle about its head: a point on it,
    // far from the first bone, swings round with it.
    std::vector<fla::IKJoint> after = before;
    after[1].angle = kPi / 2.0;
    after[2].position = fla::Point(100.0, 100.0);
    after[2].angle = kPi / 2.0;
    CHECK(near(fla::skinPoint(fla::Point(200.0, 0.0), before, after), 100.0, 100.0, 0.5));
    // One on the first bone stays.
    CHECK(near(fla::skinPoint(fla::Point(10.0, 0.0), before, after), 10.0, 0.0, 0.5));
}

TEST(ik_armature_works_out_the_joints_animate_stored)
{
    // Opt-in, with the corpus's ik_poses.fla: a shape's four bones posed at
    // frames 0 and 29 with an ease of 50. The joints worked out from the
    // poses are the ones Animate stored for every frame, and posing a pose
    // as it is leaves them so.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus)
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS\n");
        return;
    }
    const std::filesystem::path source = std::filesystem::path(corpus) / "ik_poses.fla";
    if (!std::filesystem::exists(source))
    {
        std::printf("    skipped: no ik_poses.fla\n");
        return;
    }
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> file(parser.parse(source.string()));
    CHECK(file && file->document);
    if (!file || !file->document)
        return;
    std::string tree;
    for (const fla::Timeline* timeline : file->document->timelines)
        for (const fla::Layer* layer : timeline->layers)
            for (const fla::Frame* frame : layer->frames)
                for (const std::string& child : frame->extraChildren)
                    if (child.find("<IKTree") != std::string::npos)
                        tree = child;
    fla::IKArmature armature;
    CHECK(armature.parse(tree));
    CHECK(armature.frames() == 30);
    CHECK(armature.poses() == std::vector<int>({0, 29}));
    const auto close = [](const std::vector<fla::IKJoint>& a, const std::vector<fla::IKJoint>& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double turn = std::remainder(a[i].angle - b[i].angle, 2.0 * kPi);
            if (std::fabs(a[i].position.x - b[i].position.x) > 0.01 ||
                std::fabs(a[i].position.y - b[i].position.y) > 0.01 || std::fabs(turn) > 1.0e-5)
            {
                return false;
            }
        }
        return true;
    };
    for (int frame = 0; frame < 30; ++frame)
        CHECK(close(armature.posedAt(frame), armature.jointsAt(frame)));

    fla::IKArmature again = armature;
    std::vector<double> angles;
    for (const fla::IKJoint& joint : armature.jointsAt(29))
        angles.push_back(joint.angle);
    CHECK(again.setPose(29, angles));
    CHECK(again.poses() == std::vector<int>({0, 29}));
    for (int frame = 0; frame < 30; ++frame)
        CHECK(close(again.jointsAt(frame), armature.jointsAt(frame)));
}

TEST(symbol_armatures_keep_joints_apart_from_registration_points)
{
    // As Animate saves ik_symbols.fla's root: the instance's registration
    // point in xArray, the joint (its transformation point) in location.
    const std::string xml = "<IKTree name=\"Armature_1\" treeName=\"ikTreeName2\">"
        "<IKNode name=\"ikNode_1\" location=\"#1D.D8, #20.EE\"><childNodes>"
        "<ChildNode name=\"ikNode_1\" location=\"#1D.D8, #20.EE\" xArray=\"7467.289080\" yArray=\"6087.284405\" "
        "angleArray=\"-0.073165\" referenceID=\"A\"><states><State duration=\"1\" angle=\"-0.0731648417420849\" "
        "x=\"7639\" y=\"8430\"/></states></ChildNode></childNodes></IKNode>"
        "<states><IKState duration=\"1\"/></states></IKTree>";
    fla::IKArmature armature;
    CHECK(armature.parse(xml) && !armature.bendsShape());
    CHECK(near(armature.jointsAt(0)[0].position, 381.95, 421.5, 0.1));
    CHECK(armature.name() == "Armature_1" && armature.treeName() == "ikTreeName2");

    // Turned a quarter, the registration point swings round the joint.
    CHECK(armature.setPose(0, {-0.073165 + kPi / 2.0}));
    const fla::IKJoint turned = armature.jointsAt(0)[0];
    CHECK(near(turned.position, 381.95, 421.5, 0.1));
    const std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    CHECK(joints.size() == 1 && joints[0].reference == "A" && joints[0].node == "ikNode_1");
    // Before: (-8.59, -117.11) from the joint; a quarter turn makes it (117.11, -8.59).
    CHECK(near(joints[0].registration, 381.95 + 117.11, 421.5 - 8.59, 0.1));
}

TEST(the_bone_tool_builds_armatures_of_symbols)
{
    std::vector<fla::ArmatureJoint> joints(3);
    joints[0].position = fla::Point(100.0, 100.0);
    joints[0].node = "ikNode_1";
    joints[0].reference = "R1";
    joints[0].registration = fla::Point(90.0, 90.0);
    joints[1].position = fla::Point(200.0, 100.0);
    joints[1].parent = 0;
    joints[1].node = "ikNode_2";
    joints[1].reference = "R2";
    joints[1].registration = fla::Point(200.0, 80.0);
    joints[1].limit = {true, -0.5, 0.5};
    joints[2].position = fla::Point(200.0, 200.0);
    joints[2].parent = 1;
    joints[2].node = "ikNode_3";
    joints[2].reference = "R3";
    joints[2].registration = fla::Point(210.0, 200.0);

    fla::IKArmature armature;
    CHECK(armature.parse(fla::symbolArmatureXml("Armature_1", "ikTreeName1", joints)));
    CHECK(!armature.bendsShape() && armature.frames() == 1);
    const std::vector<fla::IKJoint> stored = armature.jointsAt(0);
    const std::vector<fla::IKJoint> posed = armature.posedAt(0);
    CHECK(stored.size() == 3 && posed.size() == 3);
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(near(stored[i].position, joints[i].position.x, joints[i].position.y));
        CHECK(near(posed[i].position, joints[i].position.x, joints[i].position.y));
        CHECK(armature.referenceOf(static_cast<int>(i)) == joints[i].reference);
    }
    // The root turned toward its child, the middle joint down, the leaf as its parent.
    CHECK(std::fabs(posed[0].angle) < 1.0e-9 && std::fabs(posed[1].angle - kPi / 2.0) < 1.0e-9);
    CHECK(std::fabs(posed[2].angle - kPi / 2.0) < 1.0e-9);
    const std::vector<fla::ArmatureJoint> back = armature.symbolJoints();
    CHECK(near(back[1].registration, 200.0, 80.0) && back[1].limit.constrained);
    CHECK(std::fabs(back[1].limit.max - 0.5) < 1.0e-9);

    // The middle joint turns no more than its limit to reach.
    const std::vector<fla::IKJoint> solved = fla::solveIK(posed, 2, fla::Point(400.0, 100.0), 32, armature.limits());
    const double relative = solved[1].angle - solved[0].angle;
    CHECK(relative >= -0.5 - 1.0e-9 && relative <= 0.5 + 1.0e-9);
}
