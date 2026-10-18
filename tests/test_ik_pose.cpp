#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/edge.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/ik_pose.h"
#include "../src/edit/new_document.h"
#include "../src/edit/shape_merge.h"
#include "../src/parser/fla_parser.h"
#include "../src/parser/path_parser.h"
#include "../src/writer/xfl_folder_writer.h"

#include <tinyxml2.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <tuple>
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
    // the in-betweens stored in ik_poses.fla show.
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
    // poses are the ones the file stores for every frame, and posing a pose
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
    // As ik_symbols.fla stores its root: the instance's registration
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

namespace {

/// Where \a t takes \a p.
fla::Point mapped(const fla::Transform& t, const fla::Point& p)
{
    return fla::Point(t.m11 * p.x + t.m21 * p.y + t.tx, t.m12 * p.x + t.m22 * p.y + t.ty);
}

/// Three symbol joints in a row along x, 100 pixels apart, posed over 11
/// frames.
fla::IKArmature threeSymbols()
{
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 + 100.0 * i, 100.0);
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
        joints[i].reference = "R" + std::to_string(i + 1);
        // Each instance's registration point up and left of its joint.
        joints[i].registration = fla::Point(90.0 + 100.0 * i, 60.0);
    }
    fla::IKArmature armature;
    armature.parse(fla::symbolArmatureXml("Armature_1", "ikTreeName1", joints, 11));
    return armature;
}

/// A rectangle 200 by 40 pixels from the origin, filled.
std::unique_ptr<fla::Shape> rectangle()
{
    std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|4000 0|4000 800|0 800|0 0", shape.get());
    if (edge)
    {
        edge->fillStyle1 = 1;
        shape->edges.push_back(edge);
    }
    return shape;
}

} // namespace

TEST(symbol_armatures_play_between_poses)
{
    fla::IKArmature armature = threeSymbols();
    CHECK(!armature.bendsShape() && armature.frames() == 11);

    // Posed at the span's end: the root turned a quarter, the middle joint
    // a quarter more; eased at full strength.
    const std::vector<fla::IKJoint> start = armature.jointsAt(0);
    CHECK(armature.setPose(10, {start[0].angle + kPi / 2.0, start[1].angle + kPi, start[2].angle + kPi}));
    CHECK(armature.poses() == std::vector<int>({0, 10}));
    fla::IKArmature eased;
    std::string xml = armature.xml();
    const size_t state = xml.find("<IKState duration=\"10\"");
    CHECK(state != std::string::npos);
    xml.insert(state + 8, " easeType=\"simple quadratic\" easeValue=\"100\"");
    CHECK(eased.parse(xml));
    // Every frame worked out again with the ease.
    CHECK(eased.setFrames(11));

    // Halfway, the ease has run three quarters of the way: (1-s)t + s(2t - t^2).
    const std::vector<fla::IKJoint> middle = eased.posedAt(5);
    CHECK(std::fabs(middle[0].angle - (start[0].angle + 0.75 * kPi / 2.0)) < 1.0e-6);
    CHECK(near(eased.shownAt(5)[1].position, middle[1].position.x, middle[1].position.y, 0.01));

    // Each instance, saved as the first frame has it, turns and moves with
    // its joint: its transformation point (the joint) lands on the joint.
    for (int joint = 0; joint < 3; ++joint)
    {
        const fla::Point registration(90.0 + 100.0 * joint, 60.0);
        const fla::Transform stored(2.0, 0.0, 0.0, 2.0, registration.x, registration.y);
        const fla::Point pivot((start[joint].position.x - registration.x) / 2.0, (start[joint].position.y - registration.y) / 2.0);
        CHECK(near(mapped(stored, pivot), start[joint].position.x, start[joint].position.y));
        for (int frame : {0, 3, 5, 10})
        {
            const fla::Transform shown = eased.carried(joint, stored, frame);
            const fla::IKJoint at = eased.shownAt(frame)[joint];
            CHECK(near(mapped(shown, pivot), at.position.x, at.position.y, 0.01));
            // Turned as the joint has turned, its scale kept.
            const double turned = std::atan2(shown.m12, shown.m11);
            CHECK(std::fabs(std::remainder(turned - (at.angle - start[joint].angle), 2.0 * kPi)) < 1.0e-6);
            CHECK(std::fabs(std::hypot(shown.m11, shown.m12) - 2.0) < 1.0e-9);
        }
    }
    // The last joint has swung round to the root's left.
    const fla::IKJoint last = eased.shownAt(10)[2];
    CHECK(near(last.position, 0.0, 200.0, 0.01));

    // How each instance moves from the first frame, frame by frame, the
    // instances in the order given: the first frame's no move at all.
    const fla::Transform a(1.0, 0.0, 0.0, 1.0, 90.0, 60.0);
    const fla::Transform b(1.0, 0.0, 0.0, 1.0, 190.0, 60.0);
    const std::string list = eased.betweenFrameMatrixList({{1, &b}, {0, &a}});
    size_t matrices = 0;
    for (size_t at = list.find("<Matrix"); at != std::string::npos; at = list.find("<Matrix", at + 1))
        ++matrices;
    CHECK(list.find("<betweenFrameMatrixList><Matrix/>") == 0 && matrices == 22);
    tinyxml2::XMLDocument moves;
    CHECK(moves.Parse(list.c_str()) == tinyxml2::XML_SUCCESS);
    std::vector<fla::Transform> move;
    for (const tinyxml2::XMLElement* m = moves.RootElement()->FirstChildElement("Matrix"); m; m = m->NextSiblingElement("Matrix"))
    {
        move.emplace_back(m->DoubleAttribute("a", 1.0), m->DoubleAttribute("b", 0.0), m->DoubleAttribute("c", 0.0),
            m->DoubleAttribute("d", 1.0), m->DoubleAttribute("tx", 0.0), m->DoubleAttribute("ty", 0.0));
    }
    // The first eleven are b's, then a's: each frame's matrix is the first
    // frame's followed by the move.
    for (int frame : {3, 10})
    {
        for (const auto& [index, joint, stored] : {std::tuple(frame, 1, b), std::tuple(11 + frame, 0, a)})
        {
            const fla::Transform shown = eased.carried(joint, stored, frame);
            const fla::Point origin = mapped(move[static_cast<size_t>(index)], fla::Point(stored.tx, stored.ty));
            CHECK(near(origin, shown.tx, shown.ty, 0.01));
        }
    }
    std::vector<std::string> children = {"<IKTree/>"};
    fla::setBetweenFrames(children, list);
    CHECK(children.size() == 2 && children[0] == list);
    fla::setBetweenFrames(children, "<betweenFrameMatrixList/>");
    CHECK(children.size() == 2 && children[0] == "<betweenFrameMatrixList/>");

    // A longer span holds the last pose.
    CHECK(eased.setFrames(20) && eased.frames() == 20 && eased.poses() == std::vector<int>({0, 10}));
    CHECK(near(eased.jointsAt(19)[2].position, 0.0, 200.0, 0.01));
}

TEST(boned_shapes_bind_points_to_their_nearest_bones)
{
    // Bones along the middle of a 200 by 40 rectangle.
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 20.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
    }
    fla::IKArmature armature;
    CHECK(armature.parse(fla::armatureXml("Armature_1", "ikTreeName1", joints, 11, true)));
    CHECK(armature.bendsShape() && armature.frames() == 11);
    CHECK(armature.xml().find("nodeType=\"lsd\"") != std::string::npos);
    CHECK(armature.xml().find("<worldMatrix>") != std::string::npos);
    CHECK(near(armature.jointsAt(0)[2].position, 200.0, 20.0));

    std::unique_ptr<fla::Shape> shape = rectangle();
    const std::vector<fla::Point> anchors = fla::shapeAnchors(*shape);
    CHECK(anchors.size() == 4);
    CHECK(near(anchors[1], 200.0, 0.0) && near(anchors[3], 0.0, 40.0));
    const fla::ShapeBinding binding = fla::bindToNearestBones(anchors, armature.jointsAt(0));
    CHECK(binding.size() == 4 && binding[0].size() == 1 && binding[0][0].bone == 1);
    CHECK(binding[1].size() == 1 && binding[1][0].bone == 2);

    // Kept on the tree as the sample keeps it, and read back.
    armature.setBinding(binding, anchors, fla::Transform(), fla::contourStyle(*shape));
    fla::IKArmature again;
    CHECK(again.parse(armature.xml()) && again.bindsAnchors());
    const fla::ShapeBinding read = again.binding();
    CHECK(read.size() == 4);
    for (size_t i = 0; i < read.size() && i < binding.size(); ++i)
    {
        CHECK(read[i].size() == binding[i].size());
        if (!read[i].empty())
            CHECK(read[i][0].bone == binding[i][0].bone && std::fabs(read[i][0].weight - 1.0) < 1.0e-9);
    }
    const std::string xml = again.xml();
    CHECK(xml.find("connectionIndices=\"4.000000,0.000000,1.000000,0.000000,1.000000,4.000000,8.000000,-1.000000") !=
        std::string::npos);
    // The first point: at the bone's head, 20 pixels to its left side.
    CHECK(xml.find("connectionData=\"0.000000,-20.000000,20.000000,1.000000") != std::string::npos);
    CHECK(xml.find("<IKContour interior=\"true\"") != std::string::npos);
    CHECK(xml.find("rectRight=\"4000\"") != std::string::npos);

    // The middle joint turns a quarter: the far corners swing round it and
    // the near ones stay.
    std::vector<fla::IKJoint> before = again.shownAt(0);
    std::vector<double> angles = {before[0].angle, before[1].angle + kPi / 2.0, before[2].angle + kPi / 2.0};
    CHECK(again.setPose(10, angles));
    fla::bindShape(*shape, fla::Transform(), read, before, again.shownAt(10));
    const std::vector<fla::Point> bent = fla::shapeAnchors(*shape);
    CHECK(bent.size() == 4);
    CHECK(near(bent[0], 0.0, 0.0, 0.01) && near(bent[3], 0.0, 40.0, 0.01));
    CHECK(near(bent[1], 120.0, 120.0, 0.01) && near(bent[2], 80.0, 120.0, 0.01));

    // A point bound to no bone stays where it is.
    std::unique_ptr<fla::Shape> other = rectangle();
    fla::ShapeBinding loose = read;
    loose[1].clear();
    fla::bindShape(*other, fla::Transform(), loose, before, again.shownAt(10));
    CHECK(near(fla::shapeAnchors(*other)[1], 200.0, 0.0, 0.01));
}

TEST(shapes_the_reference_app_bound_bend_by_its_binding)
{
    // Bones along the middle of a 200 by 40 rectangle, its corners bound as
    // the reference app binds them: numbered its own way round, each with
    // its place along its bone and to its side, and two handle groups.
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 20.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
    }
    std::string xml = fla::armatureXml("Armature_5", "ikTreeName6", joints, 11, true);
    const std::string corner[4] = {"1.000000,-20.000000,20.000000,1.000000,", "1.000000,20.000000,20.000000,1.000000,",
        "0.000000,20.000000,20.000000,1.000000,", "0.000000,-20.000000,20.000000,1.000000,"};
    std::string data;
    for (int point = 0; point < 4; ++point)
        for (int copy = 0; copy < 3; ++copy)
            data += corner[point];
    data.pop_back();
    xml.insert(xml.find(" physicsEnabled"), " connectionIndices=\"4.000000,0.000000,2.000000,0.000000,1.000000,4.000000,"
        "8.000000,-1.000000,1.000000,2.000000,12.000000,1.000000,16.000000,20.000000,-1.000000,2.000000,1.000000,"
        "24.000000,1.000000,28.000000,32.000000,-1.000000,3.000000,1.000000,36.000000,1.000000,40.000000,44.000000,"
        "-1.000000\" connectionData=\"" + data + "\"");
    fla::IKArmature armature;
    CHECK(armature.parse(xml) && armature.bendsShape() && !armature.bindsAnchors());
    // Numbered from the top right corner: bones 2, 2, 1, 1.
    const fla::ShapeBinding raw = armature.binding();
    CHECK(raw.size() == 4 && raw[0][0].bone == 2 && raw[2][0].bone == 1);

    // Found by where they were bound, the rectangle's own corners (top
    // left first) take bones 1, 2, 2, 1.
    std::unique_ptr<fla::Shape> shape = rectangle();
    const std::vector<fla::Point> anchors = fla::shapeAnchors(*shape);
    const fla::ShapeBinding found = armature.bindingFor(anchors);
    CHECK(found.size() == 4);
    const int bones[4] = {1, 2, 2, 1};
    for (size_t i = 0; i < found.size(); ++i)
        CHECK(found[i].size() == 1 && found[i][0].bone == bones[i]);

    // Posed with the middle joint turned a quarter, the far corners swing
    // round it and the near ones stay.
    const std::vector<fla::IKJoint> before = armature.shownAt(0);
    CHECK(armature.setPose(10, {before[0].angle, before[1].angle + kPi / 2.0, before[2].angle + kPi / 2.0}));
    fla::bindShape(*shape, fla::Transform(), found, before, armature.shownAt(10));
    const std::vector<fla::Point> bent = fla::shapeAnchors(*shape);
    CHECK(near(bent[0], 0.0, 0.0, 0.01) && near(bent[3], 0.0, 40.0, 0.01));
    CHECK(near(bent[1], 120.0, 120.0, 0.01) && near(bent[2], 80.0, 120.0, 0.01));
}

TEST(sprung_bones_trail_overshoot_and_settle)
{
    // An arm of three joints that swings a quarter turn up over ten frames
    // and holds there, its last bone on a spring.
    std::vector<fla::ArmatureJoint> joints(3);
    const char* names[3] = {"ikNode_1", "ikNode_2", "ikNode_3"};
    const char* references[3] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 0.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = names[i];
        joints[i].reference = references[i];
    }
    fla::IKArmature armature;
    CHECK(armature.parse(fla::symbolArmatureXml("Armature_1", "ikTreeName1", joints, 60)));
    CHECK(armature.setPose(10, {kPi / 2.0, kPi / 2.0, kPi / 2.0}));
    // No spring: the joints as posed.
    CHECK(!armature.hasSprings());
    CHECK(std::fabs(armature.jointsAt(5)[1].angle - armature.posedAt(5)[1].angle) < 1.0e-6);

    // On the bone the middle joint turns, kept at its tail as FLA files
    // keep it, and every frame stored with the spring at work.
    fla::BoneSpring spring;
    spring.present = true;
    spring.strength = 20.0;
    spring.damping = 20.0;
    armature.setSpring(1, spring);
    CHECK(armature.hasSprings() && !armature.playsSprings() && armature.spring(1).strength == 20.0);
    CHECK(armature.xml().find("referenceID=\"C\" speed=\"1\" springStrength=\"20\" dampingValue=\"20\"") != std::string::npos);
    CHECK(armature.xml().find("springDamping") == std::string::npos);
    // It trails the swing, then overshoots it, then settles where posed.
    CHECK(armature.jointsAt(10)[1].angle < kPi / 2.0 - 0.05);
    double most = 0.0;
    for (int frame = 10; frame < 40; ++frame)
        most = std::max(most, armature.jointsAt(frame)[1].angle);
    CHECK(most > kPi / 2.0 + 0.02);
    CHECK(std::fabs(armature.jointsAt(59)[1].angle - kPi / 2.0) < 0.02);
    // The joint above keeps its pose; the one below hangs off the sprung one.
    const std::vector<fla::IKJoint> mid = armature.jointsAt(10);
    CHECK(std::fabs(mid[0].angle - armature.posedAt(10)[0].angle) < 1.0e-6);
    CHECK(std::fabs(mid[2].angle - mid[1].angle) < 1.0e-6);
    CHECK(std::fabs(armature.sprungAt(10)[1].angle - mid[1].angle) < 1.0e-9);
    // Its instance turns with the sprung joint.
    const fla::Transform carried = armature.carried(1, fla::Transform(1, 0, 0, 1, 100, 0), 10);
    CHECK(std::fabs(std::atan2(carried.m12, carried.m11) - mid[1].angle) < 1.0e-6);
    // Turned off for the armature, the poses alone.
    fla::IKArmature off;
    std::string xml = armature.xml();
    xml.replace(xml.find("physicsEnabled=\"true\""), 21, "physicsEnabled=\"false\"");
    CHECK(off.parse(xml) && !off.hasSprings() && off.setFrames(60));
    CHECK(std::fabs(off.jointsAt(10)[1].angle - kPi / 2.0) < 1.0e-6);
    // Taken away: the pose again.
    armature.setSpring(1, fla::BoneSpring());
    CHECK(!armature.hasSprings());
    CHECK(std::fabs(armature.jointsAt(10)[1].angle - kPi / 2.0) < 1.0e-6);
}

TEST(sprung_bones_swing_the_shape_they_bend)
{
    // A 200 by 40 rectangle boned along its middle, its second bone turning
    // a quarter turn down over ten frames and holding, on a spring.
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 20.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
    }
    fla::IKArmature armature;
    CHECK(armature.parse(fla::armatureXml("Armature_1", "ikTreeName1", joints, 60, true)));
    std::unique_ptr<fla::Shape> rest = rectangle();
    const std::vector<fla::Point> anchors = fla::shapeAnchors(*rest);
    armature.setBinding(fla::bindToNearestBones(anchors, armature.jointsAt(0)), anchors, fla::Transform());
    CHECK(armature.parse(armature.xml()) && armature.bendsShape() && armature.bindsAnchors());
    const std::vector<fla::IKJoint> start = armature.shownAt(0);
    CHECK(armature.setPose(10, {start[0].angle, start[1].angle + kPi / 2.0, start[2].angle + kPi / 2.0}));

    fla::BoneSpring spring;
    spring.present = true;
    spring.strength = 20.0;
    spring.damping = 20.0;
    // On the joint the far bone turns about, as the joint dialog sets it.
    armature.setSpring(1, spring);
    CHECK(armature.hasSprings() && !armature.playsSprings());
    CHECK(armature.shownAt(10)[1].angle < armature.posedAt(10)[1].angle - 0.05);
    // The shapes the container keeps for each frame are bent to the joints
    // stored: on the pose frame the far corner has not come all the way
    // round, where the pose alone takes it; nothing more is bent in play.
    std::unique_ptr<fla::Shape> posed(fla::cloneShape(*rest, nullptr));
    fla::bindShape(*posed, fla::Transform(), armature.binding(), start, armature.posedAt(10));
    CHECK(near(fla::shapeAnchors(*posed)[1], 120.0, 120.0, 0.01));
    std::unique_ptr<fla::Shape> sprung(fla::cloneShape(*rest, nullptr));
    fla::bindShape(*sprung, fla::Transform(), armature.binding(), start, armature.shownAt(10));
    const fla::Point lagging = fla::shapeAnchors(*sprung)[1];
    CHECK(lagging.x > 121.0 && lagging.y < 119.0);
    CHECK(near(fla::shapeAnchors(*sprung)[0], 0.0, 0.0, 0.01));
    CHECK(!fla::springShape(*sprung, *rest, fla::Transform(), armature, 10));
    // Long after, it has settled where posed.
    std::unique_ptr<fla::Shape> settled(fla::cloneShape(*rest, nullptr));
    fla::bindShape(*settled, fla::Transform(), armature.binding(), start, armature.shownAt(59));
    CHECK(near(fla::shapeAnchors(*settled)[1], 120.0, 120.0, 3.0));
}

TEST(springs_phoenix_saved_on_their_joints_still_play)
{
    // As Phoenix 0.12 to 0.18 saved them: the spring on the joint it turns,
    // as springStrength and springDamping, and limits as phoenixMinRotation
    // and phoenixMaxRotation; the stored joints as posed.
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 0.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
        joints[i].reference = std::string(1, static_cast<char>('A' + i));
    }
    fla::IKArmature posed;
    CHECK(posed.parse(fla::symbolArmatureXml("Armature_1", "ikTreeName1", joints, 60)));
    CHECK(posed.setPose(10, {kPi / 2.0, kPi / 2.0, kPi / 2.0}));
    std::string xml = posed.xml();
    const size_t middle = xml.find("referenceID=\"B\"");
    CHECK(middle != std::string::npos);
    xml.insert(middle, "springStrength=\"20\" springDamping=\"20\" phoenixMinRotation=\"-0.5\" phoenixMaxRotation=\"0.5\" ");

    fla::IKArmature armature;
    CHECK(armature.parse(xml));
    CHECK(armature.hasSprings() && armature.playsSprings());
    const fla::BoneSpring spring = armature.spring(1);
    CHECK(spring.present && spring.strength == 20.0 && spring.damping == 20.0);
    const fla::JointLimit limit = armature.limits()[1];
    CHECK(limit.constrained && std::fabs(limit.min + 0.5) < 1.0e-9 && std::fabs(limit.max - 0.5) < 1.0e-9);
    // Limits take the names FLA files use at once.
    CHECK(armature.xml().find("phoenixMinRotation") == std::string::npos);
    CHECK(armature.xml().find("rotationConstrained=\"true\" rotationMin=\"5.78318530717959\" rotationMax=\"0.5\"") !=
        std::string::npos);
    // Stored as posed, the springs play: the instance trails.
    CHECK(std::fabs(armature.jointsAt(10)[1].angle - kPi / 2.0) < 1.0e-6);
    CHECK(armature.sprungAt(10)[1].angle < kPi / 2.0 - 0.05);
    const fla::Transform carried = armature.carried(1, fla::Transform(1, 0, 0, 1, 100, 0), 10);
    CHECK(std::fabs(std::atan2(carried.m12, carried.m11) - armature.sprungAt(10)[1].angle) < 1.0e-6);

    // Worked out again, the spring moves to the bone's tail under the FLA
    // names and is stored in the frames.
    CHECK(armature.setFrames(60));
    CHECK(!armature.playsSprings() && armature.hasSprings());
    CHECK(armature.spring(1).strength == 20.0 && armature.spring(1).damping == 20.0);
    const std::string upgraded = armature.xml();
    CHECK(upgraded.find("springDamping") == std::string::npos);
    CHECK(upgraded.find("referenceID=\"C\" speed=\"1\" springStrength=\"20\" dampingValue=\"20\"") != std::string::npos);
    CHECK(armature.jointsAt(10)[1].angle < kPi / 2.0 - 0.05);
}

namespace {

/// Twenty numbers, one a frame: \a first, running evenly to \a tenth at
/// frame 10 and on to \a twentieth at frame 20.
std::string twenty(double first, double tenth, double twentieth)
{
    std::string text;
    for (int i = 0; i < 20; ++i)
    {
        const double value = i <= 9 ? first + (tenth - first) * i / 9.0 : tenth + (twentieth - tenth) * (i - 9) / 10.0;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%s%.6f", i ? "," : "", value);
        text += buffer;
    }
    return text;
}

/// An arm of three movie clips as the reference app saves one (after
/// bones_springs.fla): the upper arm's joint limited to -45..90 degrees,
/// springs of 60 (damping 20) and 100 on the two bones, posed at frame 20
/// with the hand raised. The stored joints, which carry the springs, are
/// the file's at frames 1, 10 and 20.
std::string armSample()
{
    return "<IKTree name=\"Armature_3\" treeName=\"ikTreeName4\" physicsEnabled=\"true\">"
        "<IKNode name=\"ikNode_1\" boneName=\"ikBoneName1\" location=\"#7.9, #F.C8\" referenceID=\"U\" speed=\"1\">"
        "<childNodes><ChildNode name=\"ikNode_1\" boneName=\"ikBoneName1\" location=\"#7.9, #F.C8\" xArray=\"" +
        twenty(2300.0, 2287.953487, 2211.618076) + "\" yArray=\"" + twenty(4000.0, 3999.815771, 3806.952802) +
        "\" angleArray=\"" + twenty(0.0, -0.006623, -0.651389) +
        "\" referenceID=\"U\" speed=\"1\" rotationConstrained=\"true\" rotationMin=\"5.497787296772\" "
        "rotationMax=\"1.57079637050629\"><states><State duration=\"19\" x=\"1939\" y=\"4020\" blendMode=\"1\"/>"
        "<State duration=\"1\" angle=\"-0.398666504170809\" x=\"1959.00000000962\" y=\"4020.00009123025\" "
        "blendMode=\"1\"/></states><childNodes>"
        "<ChildNode name=\"ikNode_2\" boneName=\"ikBoneName2\" location=\"#F.74, #9.A6\" xArray=\"" +
        twenty(4700.0, 4655.284877, 3854.903477) + "\" yArray=\"" + twenty(4000.0, 3871.366394, 2298.089003) +
        "\" angleArray=\"" + twenty(0.0, -0.597908, -2.107464) +
        "\" referenceID=\"F\" speed=\"1\" springStrength=\"60\" dampingValue=\"20\"><states>"
        "<State duration=\"19\" x=\"2560\" y=\"-20\" blendMode=\"1\"/>"
        "<State duration=\"1\" angle=\"-1.38119753133563\" x=\"2560.00000001443\" y=\"-19.9999999995447\" "
        "blendMode=\"1\"/></states><childNodes>"
        "<ChildNode name=\"ikNode_4\" location=\"#A.43, #0.EA\" xArray=\"" + twenty(7100.0, 6638.920403, 2627.843380) +
        "\" yArray=\"" + twenty(4000.0, 2520.370427, 235.487849) + "\" angleArray=\"" + twenty(0.0, -0.406590, -1.703570) +
        "\" referenceID=\"H\" speed=\"1\" springStrength=\"100\"><states>"
        "<State duration=\"19\" x=\"2601\" blendMode=\"1\"/>"
        "<State duration=\"1\" angle=\"0.403894164501524\" x=\"2601\" blendMode=\"1\"/></states></ChildNode>"
        "</childNodes></ChildNode></childNodes></ChildNode></childNodes></IKNode>"
        "<states><IKState duration=\"19\" blendMode=\"1\"/><IKState duration=\"1\" blendMode=\"1\"/></states></IKTree>";
}

} // namespace

TEST(armatures_read_limits_and_springs_as_the_reference_app_saves_them)
{
    fla::IKArmature armature;
    CHECK(armature.parse(armSample()) && !armature.bendsShape() && armature.frames() == 20);
    // Limits on the joint the bone turns about, in radians from 0 up to a
    // whole turn: -45 degrees is stored as 315.
    const std::vector<fla::JointLimit> limits = armature.limits();
    CHECK(limits.size() == 3 && limits[0].constrained && !limits[1].constrained && !limits[2].constrained);
    CHECK(std::fabs(limits[0].min + kPi / 4.0) < 1.0e-6 && std::fabs(limits[0].max - kPi / 2.0) < 1.0e-6);
    // Springs at each bone's tail: the upper arm's on the forearm's node,
    // the forearm's on the hand's, its damping of 0 left out.
    const fla::BoneSpring upper = armature.spring(0);
    const fla::BoneSpring fore = armature.spring(1);
    CHECK(upper.present && upper.strength == 60.0 && upper.damping == 20.0);
    CHECK(fore.present && fore.strength == 100.0 && fore.damping == 0.0);
    CHECK(!armature.spring(2).present);
    CHECK(armature.hasSprings() && !armature.playsSprings());

    // A range across 180 degrees: from 240 to 300 is -120 to -60.
    fla::IKArmature shape;
    std::string xml = armSample();
    xml.replace(xml.find("rotationMin=\"5.497787296772\""), 28, "rotationMin=\"4.18879036506149\"");
    xml.replace(xml.find("rotationMax=\"1.57079637050629\""), 30, "rotationMax=\"5.23598794539901\"");
    CHECK(shape.parse(xml));
    CHECK(std::fabs(shape.limits()[0].min + 2.0 * kPi / 3.0) < 1.0e-6);
    CHECK(std::fabs(shape.limits()[0].max + kPi / 3.0) < 1.0e-6);

    // Written back as they were read.
    fla::IKArmature written;
    CHECK(written.parse(armSample()));
    written.setLimit(0, limits[0]);
    CHECK(written.xml().find("rotationConstrained=\"true\" rotationMin=\"5.4977872967") !=
        std::string::npos);
}

TEST(the_reference_app_joins_a_bone_where_it_was_dragged)
{
    // The forearm's transformation point had been moved to its far end; the
    // bone was dragged from the upper arm to near the forearm's near end,
    // and the joint is there: the instance (at 235, 200) has its
    // transformation point moved to it, 10.05 to the left. The upper arm's
    // joint likewise sits where its bone started, the hand's at its own
    // transformation point (its middle).
    fla::IKArmature armature;
    CHECK(armature.parse(armSample()));
    const std::vector<fla::IKJoint> joints = armature.jointsAt(0);
    CHECK(joints.size() == 3);
    CHECK(near(joints[0].position, 96.95, 201.0, 0.01));
    CHECK(near(joints[1].position, 224.95, 200.0, 0.01));
    CHECK(near(joints[2].position, 355.0, 200.0, 0.01));
    const std::vector<fla::ArmatureJoint> built = armature.symbolJoints();
    CHECK(near(built[1].registration, 235.0, 200.0, 0.01));
}

TEST(reference_armatures_play_the_springs_they_store)
{
    // The stored joints already swing on their springs: frame 20 is not the
    // pose there (the arm has swung past it), and Phoenix shows what is
    // stored rather than swinging it again.
    fla::IKArmature armature;
    CHECK(armature.parse(armSample()));
    const std::vector<fla::IKJoint> posed = armature.posedAt(19);
    CHECK(std::fabs(posed[0].angle + 0.398667) < 1.0e-5);
    CHECK(std::fabs(posed[1].angle + 1.779864) < 1.0e-5);
    CHECK(std::fabs(posed[2].angle + 1.375970) < 1.0e-5);
    const double stored[2][3] = {{-0.006623, -0.597908, -0.406590}, {-0.651389, -2.107464, -1.703570}};
    for (int k = 0; k < 2; ++k)
    {
        const int frame = k == 0 ? 9 : 19;
        const std::vector<fla::IKJoint> shown = armature.shownAt(frame);
        const std::vector<fla::IKJoint> sprung = armature.sprungAt(frame);
        for (int joint = 0; joint < 3; ++joint)
        {
            CHECK(std::fabs(shown[joint].angle - stored[k][joint]) < 1.0e-6);
            CHECK(std::fabs(sprung[joint].angle - stored[k][joint]) < 1.0e-6);
        }
    }
    CHECK(std::fabs(armature.shownAt(19)[0].angle - posed[0].angle) > 0.2);

    // The instances follow the stored joints: the upper arm's registration
    // point and turn at frames 10 and 20, the forearm's joint (its
    // transformation point) where the stored joint is.
    const fla::Transform upper(1.0, 0.0, 0.0, 1.0, 115.0, 200.0);
    const fla::Transform fore(1.0, 0.0, 0.0, 1.0, 235.0, 200.0);
    const fla::Transform upper10 = armature.carried(0, upper, 9);
    CHECK(near(fla::Point(upper10.tx, upper10.ty), 114.397674, 199.990789, 1.0e-4));
    const fla::Transform upper20 = armature.carried(0, upper, 19);
    CHECK(near(fla::Point(upper20.tx, upper20.ty), 110.580904, 190.347640, 1.0e-4));
    CHECK(std::fabs(std::atan2(upper20.m12, upper20.m11) + 0.651389) < 1.0e-6);
    for (int frame : {9, 19})
    {
        const fla::Transform shown = armature.carried(1, fore, frame);
        const fla::IKJoint joint = armature.shownAt(frame)[1];
        CHECK(near(mapped(shown, fla::Point(-10.05, 0.0)), joint.position.x, joint.position.y, 1.0e-3));
    }

    // The betweenFrameMatrixList the file keeps: frame 20's move of the
    // upper arm from where frame 1 has it.
    const std::string list = armature.betweenFrameMatrixList({{0, &upper}});
    CHECK(list.find("<Matrix a=\"0.795242") != std::string::npos);
}

TEST(rebuilt_armatures_keep_springs_and_other_attributes)
{
    const std::string xml = "<IKTree name=\"Armature_1\" treeName=\"ikTreeName2\" physicsEnabled=\"true\" custom=\"1\">"
        "<IKNode name=\"ikNode_1\" location=\"0, 0\"><childNodes>"
        "<ChildNode name=\"ikNode_1\" location=\"0, 0\" xArray=\"0\" yArray=\"0\" angleArray=\"0\" referenceID=\"A\">"
        "<states><State duration=\"1\" angle=\"0\" x=\"0\" y=\"0\"/></states><childNodes>"
        "<ChildNode name=\"ikNode_2\" location=\"7.8125, 0\" xArray=\"2000\" yArray=\"0\" angleArray=\"0\" "
        "referenceID=\"B\" springStrength=\"40\" springDamping=\"25\">"
        "<states><State duration=\"1\" angle=\"0\" x=\"2000\" y=\"0\"/></states></ChildNode>"
        "</childNodes></ChildNode></childNodes></IKNode><states><IKState duration=\"1\"/></states></IKTree>";
    fla::IKArmature armature;
    CHECK(armature.parse(xml));
    const fla::BoneSpring spring = armature.spring(1);
    CHECK(spring.present && spring.strength == 40.0 && spring.damping == 25.0);
    CHECK(!armature.spring(0).present);

    // Built again with a bone more, as the Bone tool does.
    std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    fla::ArmatureJoint added;
    added.position = fla::Point(200.0, 0.0);
    added.registration = added.position;
    added.parent = 1;
    added.node = "ikNode_3";
    added.reference = "C";
    joints.push_back(added);
    fla::IKArmature rebuilt;
    CHECK(rebuilt.parse(fla::symbolArmatureXml(armature.name(), armature.treeName(), joints, armature.frames())));
    CHECK(!rebuilt.spring(1).present);
    rebuilt.keepAttributesOf(armature);
    CHECK(rebuilt.spring(1).present && rebuilt.spring(1).strength == 40.0 && rebuilt.spring(1).damping == 25.0);
    CHECK(!rebuilt.spring(2).present);
    CHECK(rebuilt.xml().find("custom=\"1\"") != std::string::npos);
    CHECK(rebuilt.jointsAt(0).size() == 3);
    // And they survive posing.
    std::vector<double> angles;
    for (const fla::IKJoint& joint : rebuilt.jointsAt(0))
        angles.push_back(joint.angle + 0.25);
    CHECK(rebuilt.setPose(0, angles));
    CHECK(rebuilt.spring(1).strength == 40.0);
}

TEST(a_bone_added_to_an_armature_with_poses_turns_with_its_parent_in_each)
{
    // Three joints along x, a second pose at frame 10 with the whole arm
    // turned a quarter round from its root.
    fla::IKArmature armature = threeSymbols();
    std::vector<double> angles;
    for (const fla::IKJoint& joint : armature.jointsAt(0))
        angles.push_back(joint.angle + 3.14159265358979 / 2.0);
    CHECK(armature.setPose(10, angles));
    CHECK(armature.poses().size() == 2);

    // A fourth joint hanging off the last, 50 pixels on along x on the
    // first frame, as the Bone tool adds it.
    std::vector<fla::ArmatureJoint> joints = armature.symbolJoints();
    fla::ArmatureJoint added;
    added.position = fla::Point(350.0, 100.0);
    added.registration = fla::Point(340.0, 60.0);
    added.parent = 2;
    added.node = "ikNode_4";
    added.reference = "R4";
    joints.push_back(added);
    fla::IKArmature rebuilt;
    CHECK(rebuilt.parse(fla::symbolArmatureXml(armature.name(), armature.treeName(), joints, armature.frames())));
    rebuilt.keepAttributesOf(armature);
    fla::carryPoses(armature, rebuilt);

    // Both poses kept; on the second the new joint has turned with the arm,
    // still 50 pixels from its parent, now straight down from it.
    CHECK(rebuilt.poses().size() == 2);
    const std::vector<fla::IKJoint> before = armature.jointsAt(10);
    const std::vector<fla::IKJoint> after = rebuilt.jointsAt(10);
    CHECK(after.size() == 4);
    if (after.size() != 4)
        return;
    for (int j = 0; j < 3; ++j)
    {
        CHECK(std::fabs(after[j].angle - before[j].angle) < 1.0e-6);
        CHECK(std::fabs(after[j].position.x - before[j].position.x) < 0.01);
        CHECK(std::fabs(after[j].position.y - before[j].position.y) < 0.01);
    }
    CHECK(std::fabs(after[3].position.x - after[2].position.x) < 0.01);
    CHECK(std::fabs(after[3].position.y - after[2].position.y - 50.0) < 0.01);
    // The first frame as it was drawn.
    CHECK(std::fabs(rebuilt.jointsAt(0)[3].position.x - 350.0) < 0.01);

    // The ease from the first pose to the second goes along too.
    std::string xml = armature.xml();
    const size_t state = xml.find("<IKState ");
    CHECK(state != std::string::npos);
    xml.insert(state + 9, "easeType=\"simple quadratic\" easeValue=\"50\" ");
    fla::IKArmature eased;
    CHECK(eased.parse(xml));
    fla::IKArmature again;
    CHECK(again.parse(fla::symbolArmatureXml(armature.name(), armature.treeName(), joints, armature.frames())));
    fla::carryPoses(eased, again);
    CHECK(again.xml().find("easeValue=\"50\"") != std::string::npos);
    CHECK(armature.xml().find("easeValue=\"50\"") == std::string::npos);
}

TEST(a_symbol_armature_posed_twice_saves_and_plays)
{
    // Opt-in: the corpus's ik_symbols.fla, given a second pose 29 frames on
    // with Phoenix's posing code, saved as an XFL folder for a render check.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    const std::filesystem::path source = corpus ? std::filesystem::path(corpus) / "ik_symbols.fla"
                                                : std::filesystem::path();
    if (!corpus || !std::filesystem::exists(source))
    {
        std::printf("    skipped: no ik_symbols.fla in PHOENIX_FLA_CORPUS\n");
        return;
    }
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> fla(parser.parse(source.string()));
    CHECK(fla && fla->document && !fla->document->timelines.empty());
    if (!fla || !fla->document || fla->document->timelines.empty())
        return;
    fla::Timeline* scene = fla->document->timelines.front();
    fla::Frame* pose = nullptr;
    for (fla::Layer* layer : scene->layers)
    {
        for (fla::Frame* frame : layer->frames)
        {
            frame->duration = 30;
            if (frame->otherTweenType == "IK pose")
                pose = frame;
        }
    }
    CHECK(pose != nullptr);
    if (!pose)
        return;
    size_t child = 0;
    while (child < pose->extraChildren.size() && pose->extraChildren[child].find("<IKTree") == std::string::npos)
        ++child;
    fla::IKArmature armature;
    CHECK(child < pose->extraChildren.size() && armature.parse(pose->extraChildren[child]));
    CHECK(armature.setFrames(30));
    std::vector<double> angles;
    const std::vector<fla::IKJoint> start = armature.jointsAt(0);
    for (size_t i = 0; i < start.size(); ++i)
        angles.push_back(start[i].angle + (i == 0 ? 0.6 : 1.4));
    CHECK(armature.setPose(29, angles));
    fla::PoseArmatureCommand::apply(pose, child, armature.xml(), "0,29");

    // Every instance at frame 15 has its joint where the armature does.
    std::vector<std::pair<int, const fla::Transform*>> placed;
    for (fla::Element* element : pose->elements)
    {
        const fla::SymbolInstance* instance = dynamic_cast<const fla::SymbolInstance*>(element);
        if (!instance)
            continue;
        for (const auto& attribute : instance->extraAttributes)
        {
            for (size_t joint = 0; joint < start.size(); ++joint)
            {
                if (attribute.first != "referenceID" || attribute.second != armature.referenceOf(static_cast<int>(joint)))
                    continue;
                placed.emplace_back(static_cast<int>(joint), &instance->transform);
                const fla::Transform shown = armature.carried(static_cast<int>(joint), instance->transform, 15);
                const fla::IKJoint at = armature.shownAt(15)[joint];
                CHECK(near(mapped(shown, instance->transformationPoint), at.position.x, at.position.y, 0.2));
            }
        }
    }
    fla::setBetweenFrames(pose->extraChildren, armature.betweenFrameMatrixList(placed));

    const std::filesystem::path out = std::filesystem::temp_directory_path() / "phoenix_ik_tests" / "ik_symbols_posed";
    std::filesystem::remove_all(out);
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*fla->document, out.string(), "ik_symbols_posed"));
    std::printf("    written: %s\n", out.string().c_str());
}

TEST(the_bone_tool_bones_a_shape_and_its_poses_bend_it)
{
    // A 200 by 40 rectangle at (100, 100) on a new document's first layer,
    // held for 30 frames, boned along its middle.
    std::unique_ptr<fla::FLADocument> fla(fla::createEmptyDocument(640, 360));
    CHECK(fla && fla->document && !fla->document->timelines.empty());
    if (!fla || !fla->document || fla->document->timelines.empty())
        return;
    fla::Document* document = fla->document;
    fla::Timeline* scene = document->timelines.front();
    CHECK(!scene->layers.empty() && !scene->layers.front()->frames.empty());
    fla::Layer* layer = scene->layers.front();
    fla::Frame* frame = layer->frames.front();
    frame->duration = 30;
    std::unique_ptr<fla::Shape> made = rectangle();
    fla::Shape* shape = made.release();
    shape->parent = frame;
    shape->transform.tx = 100.0;
    shape->transform.ty = 100.0;
    fla::SolidColor* fill = new fla::SolidColor(shape);
    fill->color[0] = 0;
    fill->color[1] = 160;
    fill->color[2] = 255;
    shape->fills.push_back(fill);
    shape->fillsMap[1] = fill;
    frame->elements.push_back(shape);

    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 + 100.0 * i, 120.0);
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
    }
    const fla::BonedShape boned = fla::boneShape(*scene, *layer, *frame, *shape, joints, "lsd_1", "Armature_1", "ikTreeName1");
    CHECK(boned.symbol && boned.layer && boned.pose && boned.instance);
    if (!boned.symbol)
        return;
    CHECK(boned.symbol->otherSymbolType == "ik container");
    CHECK(boned.symbol->timelines.front()->layers.front()->frames.size() == 30);
    CHECK(boned.pose->duration == 30 && boned.pose->otherTweenType == "IK pose");
    CHECK(near(fla::Point(boned.instance->transform.tx, boned.instance->transform.ty), 200.0, 120.0));
    fla::IKArmature armature;
    CHECK(armature.parse(boned.pose->extraChildren.back()) && armature.bendsShape() && armature.bindsAnchors());
    CHECK(armature.binding().size() == 4 && armature.frames() == 30);

    // Into the document, as the Bone tool's commands put it.
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);
    boned.symbol->parent = document->symbolList;
    document->symbolList->symbols.push_back(boned.symbol);
    document->symbolList->link(boned.symbol);
    frame->elements.clear();
    delete shape;
    scene->layers.insert(scene->layers.begin(), boned.layer);

    // Posed at the last frame, its second bone turned a third of a half
    // turn down; every frame bent from the first's shape by the binding,
    // as the Selection tool's drag bends them.
    const std::vector<fla::IKJoint> start = armature.shownAt(0);
    CHECK(armature.setPose(29, {start[0].angle, start[1].angle + kPi / 3.0, start[2].angle + kPi / 3.0}));
    fla::PoseArmatureCommand::apply(boned.pose, boned.pose->extraChildren.size() - 1, armature.xml(), "0,29");
    std::vector<fla::Frame*>& frames = boned.symbol->timelines.front()->layers.front()->frames;
    fla::Shape* rest = static_cast<fla::Shape*>(frames.front()->elements.front());
    std::unique_ptr<fla::Shape> first(fla::cloneShape(*rest, nullptr));
    for (size_t k = 0; k < frames.size(); ++k)
    {
        fla::Shape* own = static_cast<fla::Shape*>(frames[k]->elements.front());
        std::unique_ptr<fla::Shape> bent(fla::cloneShape(*first, nullptr));
        fla::bindShape(*bent, boned.instance->transform, armature.binding(), armature.shownAt(0),
            armature.shownAt(static_cast<int>(k)));
        fla::setShapeContents(*own, *bent);
    }
    // At the end the far corners have swung down round the middle joint.
    const std::vector<fla::Point> last = fla::shapeAnchors(*static_cast<fla::Shape*>(frames.back()->elements.front()));
    const double c = std::cos(kPi / 3.0);
    const double s = std::sin(kPi / 3.0);
    CHECK(last.size() == 4 &&
        near(last[1], 100.0 * c + 20.0 * s, 100.0 * s - 20.0 * c, 0.05) && near(last[0], -100.0, -20.0, 0.05));

    const std::filesystem::path out = std::filesystem::temp_directory_path() / "phoenix_ik_tests" / "boned_shape";
    std::filesystem::remove_all(out);
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*document, out.string(), "boned_shape"));
    std::printf("    written: %s\n", out.string().c_str());

    // Read back, the armature and its binding are as written.
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> back(parser.parse((out / "boned_shape.xfl").string()));
    CHECK(back && back->document && !back->document->timelines.empty());
    if (!back || !back->document || back->document->timelines.empty())
        return;
    const fla::Layer* armatureLayer = back->document->timelines.front()->layers.front();
    CHECK(armatureLayer && !armatureLayer->frames.empty());
    const fla::Frame* pose = armatureLayer->frames.front();
    fla::IKArmature read;
    CHECK(!pose->extraChildren.empty() && read.parse(pose->extraChildren.back()));
    CHECK(read.bindsAnchors() && read.binding().size() == 4 && read.poses() == std::vector<int>({0, 29}));
    CHECK(back->document->symbolList && back->document->symbolList->symbols.size() == 1);
}

TEST(clearing_a_pose_lets_the_armature_tween_past_it)
{
    // Poses at frames 0, 5 and 10; clearing the middle one leaves 0 and 10,
    // and frame 5 between them.
    fla::IKArmature armature = threeSymbols();
    std::vector<double> turned;
    for (const fla::IKJoint& joint : armature.jointsAt(0))
        turned.push_back(joint.angle + 1.0);
    CHECK(armature.setPose(10, turned));
    std::vector<double> odd;
    for (const fla::IKJoint& joint : armature.jointsAt(0))
        odd.push_back(joint.angle - 2.0);
    CHECK(armature.setPose(5, odd));
    CHECK(armature.poses().size() == 3);
    CHECK(!armature.removePose(0));
    CHECK(!armature.removePose(3));
    CHECK(armature.removePose(5));
    CHECK(armature.poses().size() == 2 && armature.poses()[1] == 10);
    // Halfway, about halfway round (no ease).
    const double start = armature.jointsAt(0)[0].angle;
    const double end = armature.jointsAt(10)[0].angle;
    const double middle = armature.jointsAt(5)[0].angle;
    CHECK(std::fabs(end - start - 1.0) < 1.0e-6);
    CHECK(middle > start + 0.3 && middle < end - 0.3);
}

namespace {

/// Three symbol joints in a row along x, 100 pixels apart, one pose held
/// for 60 frames, the bone from the middle joint on a spring.
fla::IKArmature sprungTail(double strength = 20.0)
{
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 0.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
        joints[i].reference = "R" + std::to_string(i + 1);
    }
    fla::IKArmature armature;
    armature.parse(fla::symbolArmatureXml("Armature_1", "ikTreeName1", joints, 60));
    fla::BoneSpring spring;
    spring.present = true;
    spring.strength = strength;
    spring.damping = 30.0;
    armature.setSpring(1, spring);
    return armature;
}

std::vector<double> anglesOf(const fla::IKArmature& armature, int joint)
{
    std::vector<double> angles;
    for (int frame = 0; frame < armature.frames(); ++frame)
        angles.push_back(armature.jointsAt(frame)[static_cast<size_t>(joint)].angle);
    return angles;
}

} // namespace

TEST(gravity_hangs_sprung_bones_and_is_stored_in_their_frames)
{
    fla::IKArmature armature = sprungTail();
    CHECK(armature.hasSprings() && !armature.forces().any());
    CHECK(std::fabs(armature.jointsAt(30)[1].angle) < 1.0e-6);

    // Gravity straight down: the sprung bone hangs from the first frame on,
    // and stays hanging; the joint above, with no spring, keeps its pose.
    fla::BoneForces forces;
    forces.gravity = 50.0;
    armature.setForces(forces);
    CHECK(armature.forces().gravity == 50.0 && armature.forces().gravityAngle == 90.0);
    const std::vector<double> hanging = anglesOf(armature, 1);
    CHECK(hanging[0] > 0.2 && hanging[0] < kPi / 2.0);
    CHECK(std::fabs(hanging[59] - hanging[0]) < 0.01);
    CHECK(std::fabs(armature.jointsAt(30)[0].angle) < 1.0e-6);
    // The bone below hangs off it.
    CHECK(std::fabs(armature.jointsAt(30)[2].angle - hanging[30]) < 1.0e-6);

    // A stiffer spring gives way less.
    fla::IKArmature stiff = sprungTail(80.0);
    stiff.setForces(forces);
    CHECK(stiff.jointsAt(0)[1].angle > 0.0 && stiff.jointsAt(0)[1].angle < hanging[0] - 0.1);

    // Kept on the tree, and the frames store the hang: read back, or with
    // Phoenix's own settings gone as another app sees them, the same.
    const std::string xml = armature.xml();
    CHECK(xml.find("phoenixGravity=\"50\"") != std::string::npos);
    CHECK(xml.find("phoenixGravityAngle=\"90\"") != std::string::npos);
    CHECK(xml.find("phoenixWind") == std::string::npos);
    fla::IKArmature again;
    CHECK(again.parse(xml) && again.forces().gravity == 50.0);
    CHECK(anglesOf(again, 1) == hanging);
    tinyxml2::XMLDocument plain;
    CHECK(plain.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS);
    plain.FirstChildElement("IKTree")->DeleteAttribute("phoenixGravity");
    plain.FirstChildElement("IKTree")->DeleteAttribute("phoenixGravityAngle");
    tinyxml2::XMLPrinter printer(nullptr, true);
    plain.Print(&printer);
    fla::IKArmature other;
    CHECK(other.parse(printer.CStr()) && !other.forces().any());
    CHECK(anglesOf(other, 1) == hanging);

    // Posing a frame where it hangs poses only what changed, not the hang
    // again on top of itself.
    std::vector<double> shown;
    for (const fla::IKJoint& joint : armature.shownAt(10))
        shown.push_back(joint.angle);
    const std::vector<double> pose = armature.poseFromShown(10, shown);
    CHECK(pose.size() == 3 && std::fabs(pose[1]) < 1.0e-9 && std::fabs(pose[2]) < 1.0e-9);
    shown[0] += 0.5;
    shown[1] += 0.5;
    shown[2] += 0.5;
    const std::vector<double> turned = armature.poseFromShown(10, shown);
    CHECK(std::fabs(turned[0] - 0.5) < 1.0e-9 && std::fabs(turned[1] - 0.5) < 1.0e-9);

    // Taken away, the pose again.
    armature.setForces(fla::BoneForces());
    CHECK(armature.xml().find("phoenixGravity") == std::string::npos);
    CHECK(std::fabs(armature.jointsAt(30)[1].angle) < 1.0e-6);
}

TEST(wind_gusts_sway_sprung_bones_the_same_every_time)
{
    // Wind blowing up across the bone, gusting.
    fla::BoneForces forces;
    forces.wind = 60.0;
    forces.windAngle = -90.0;
    forces.gusts = 80.0;
    forces.gustRate = 1.0;
    forces.seed = 7;
    fla::IKArmature armature = sprungTail();
    armature.setForces(forces);
    const std::vector<double> swaying = anglesOf(armature, 1);
    double low = swaying[0];
    double high = swaying[0];
    double sum = 0.0;
    for (double angle : swaying)
    {
        low = std::min(low, angle);
        high = std::max(high, angle);
        sum += angle;
    }
    // Blown up (turned back from the right), more and less as it gusts.
    CHECK(sum / swaying.size() < -0.1 && low < -0.2);
    CHECK(high - low > 0.05);

    // Worked out again, it is the same; read back, the same frames.
    fla::IKArmature same = sprungTail();
    same.setForces(forces);
    CHECK(anglesOf(same, 1) == swaying);
    fla::IKArmature back;
    CHECK(back.parse(armature.xml()));
    const fla::BoneForces read = back.forces();
    CHECK(read.wind == 60.0 && read.windAngle == -90.0 && read.gusts == 80.0 && read.gustRate == 1.0 && read.seed == 7);
    CHECK(anglesOf(back, 1) == swaying);
    CHECK(back.setFrames(60));
    CHECK(anglesOf(back, 1) == swaying);

    // Another seed, other gusts; a steady wind holds still once settled.
    forces.seed = 8;
    fla::IKArmature other = sprungTail();
    other.setForces(forces);
    CHECK(anglesOf(other, 1) != swaying);
    forces.gusts = 0.0;
    fla::IKArmature steady = sprungTail();
    steady.setForces(forces);
    CHECK(steady.jointsAt(0)[1].angle < -0.2);
    CHECK(std::fabs(steady.jointsAt(59)[1].angle - steady.jointsAt(0)[1].angle) < 0.01);
    CHECK(steady.xml().find("phoenixWindSeed") == std::string::npos);

    // No spring, no sway.
    fla::IKArmature loose = sprungTail();
    loose.setSpring(1, fla::BoneSpring());
    loose.setForces(forces);
    CHECK(std::fabs(loose.jointsAt(30)[1].angle) < 1.0e-6);
}

namespace {

/// A rectangle 200 by 40 pixels from the origin with points along its top
/// at 60, 90, 100, 110 and 140.
std::unique_ptr<fla::Shape> jointedRectangle()
{
    std::unique_ptr<fla::Shape> shape(new fla::Shape(nullptr));
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|1200 0|1800 0|2000 0|2200 0|2800 0|4000 0|4000 800|0 800|0 0", shape.get());
    if (edge)
    {
        edge->fillStyle1 = 1;
        shape->edges.push_back(edge);
    }
    return shape;
}

int anchorAt(const std::vector<fla::Point>& anchors, double x, double y)
{
    for (size_t i = 0; i < anchors.size(); ++i)
        if (near(anchors[i], x, y, 0.01))
            return static_cast<int>(i);
    return -1;
}

double weightOf(const std::vector<fla::BoneWeight>& bones, int bone)
{
    double weight = 0.0;
    for (const fla::BoneWeight& bound : bones)
        if (bound.bone == bone)
            weight += bound.weight;
    return weight;
}

} // namespace

TEST(smoothed_joints_blend_points_between_their_bones)
{
    // Bones along the middle of the rectangle, joints at 0, 100 and 200.
    std::vector<fla::ArmatureJoint> joints(3);
    for (int i = 0; i < 3; ++i)
    {
        joints[i].position = fla::Point(100.0 * i, 20.0);
        joints[i].registration = joints[i].position;
        joints[i].parent = i - 1;
        joints[i].node = "ikNode_" + std::to_string(i + 1);
    }
    fla::IKArmature armature;
    CHECK(armature.parse(fla::armatureXml("Armature_1", "ikTreeName1", joints, 11, true)));
    std::unique_ptr<fla::Shape> shape = jointedRectangle();
    const std::vector<fla::Point> anchors = fla::shapeAnchors(*shape);
    const std::vector<fla::IKJoint> rest = armature.jointsAt(0);
    const fla::ShapeBinding nearest = fla::bindToNearestBones(anchors, rest);
    const int at60 = anchorAt(anchors, 60.0, 0.0);
    const int at90 = anchorAt(anchors, 90.0, 0.0);
    const int at100 = anchorAt(anchors, 100.0, 0.0);
    const int at110 = anchorAt(anchors, 110.0, 0.0);
    const int corner = anchorAt(anchors, 0.0, 0.0);
    CHECK(at60 >= 0 && at90 >= 0 && at100 >= 0 && at110 >= 0 && corner >= 0);
    if (at60 < 0 || at90 < 0 || at100 < 0 || at110 < 0 || corner < 0)
        return;
    // Nearest bones: one bone each side of the joint, both at it, evenly.
    CHECK(nearest[at90].size() == 1 && nearest[at90][0].bone == 1);
    CHECK(nearest[at100].size() == 2);
    CHECK(armature.smoothness() == 0.0);

    // Smoothed all the way: the weight passes from one bone to the next
    // over half the shorter bone, 50 pixels, either side of the joint.
    const fla::ShapeBinding smooth = fla::smoothJoints(nearest, anchors, rest, 100.0);
    CHECK(std::fabs(weightOf(smooth[at90], 1) - 0.648) < 1.0e-3 && std::fabs(weightOf(smooth[at90], 2) - 0.352) < 1.0e-3);
    CHECK(std::fabs(weightOf(smooth[at100], 1) - 0.5) < 1.0e-9 && std::fabs(weightOf(smooth[at100], 2) - 0.5) < 1.0e-9);
    CHECK(std::fabs(weightOf(smooth[at110], 2) - 0.648) < 1.0e-3);
    CHECK(weightOf(smooth[at60], 2) > 0.0 && weightOf(smooth[at60], 2) < 0.05);
    CHECK(smooth[corner].size() == 1 && smooth[corner][0].bone == 1 && smooth[corner][0].weight == 1.0);
    // Less smoothing reaches less far, and a point it no longer reaches
    // goes back to its own side's bone.
    const fla::ShapeBinding less = fla::smoothJoints(smooth, anchors, rest, 20.0);
    CHECK(less[at60].size() == 1 && less[at60][0].bone == 1);
    CHECK(std::fabs(weightOf(less[at100], 2) - 0.5) < 1.0e-9);
    // A point that follows some other bone too is left as bound.
    fla::ShapeBinding mixed = nearest;
    mixed[at90].push_back({0, 0.5});
    CHECK(fla::smoothJoint(mixed, anchors, rest, 1, 100.0)[at90].size() == 2);

    // Bent with the far bone turned a quarter down, the point at 90 lands
    // between where each bone alone takes it: (90, 0) and (120, 10).
    armature.setBinding(smooth, anchors, fla::Transform());
    armature.setSmoothness(100.0);
    fla::IKArmature again;
    CHECK(again.parse(armature.xml()) && again.bindsAnchors() && again.smoothness() == 100.0);
    const fla::ShapeBinding read = again.binding();
    CHECK(read.size() == anchors.size() && std::fabs(weightOf(read[at90], 2) - weightOf(smooth[at90], 2)) < 1.0e-5);
    CHECK(again.setPose(10, {rest[0].angle, rest[1].angle + kPi / 2.0, rest[2].angle + kPi / 2.0}));
    fla::bindShape(*shape, fla::Transform(), read, rest, again.shownAt(10));
    const std::vector<fla::Point> bent = fla::shapeAnchors(*shape);
    const double w = weightOf(smooth[at90], 2);
    CHECK(near(bent[at90], 90.0 * (1.0 - w) + 120.0 * w, 10.0 * w, 0.01));
    CHECK(near(bent[corner], 0.0, 0.0, 0.01));
    // Bound evenly as before, the point at 90 follows its one bone.
    std::unique_ptr<fla::Shape> plain = jointedRectangle();
    fla::bindShape(*plain, fla::Transform(), nearest, rest, again.shownAt(10));
    CHECK(near(fla::shapeAnchors(*plain)[at90], 90.0, 0.0, 0.01));
}

TEST(a_point_reweighted_shares_itself_out)
{
    std::vector<fla::BoneWeight> even = {{1, 0.5}, {2, 0.5}};
    std::vector<fla::BoneWeight> more = fla::reweighted(even, 1, 0.25);
    CHECK(std::fabs(weightOf(more, 1) - 0.75) < 1.0e-9 && std::fabs(weightOf(more, 2) - 0.25) < 1.0e-9);
    std::vector<fla::BoneWeight> gone = fla::reweighted(more, 1, -1.0);
    CHECK(gone.size() == 1 && gone[0].bone == 2 && std::fabs(gone[0].weight - 1.0) < 1.0e-9);
    std::vector<fla::BoneWeight> added = fla::reweighted({{2, 1.0}}, 3, 0.25);
    CHECK(added.size() == 2 && std::fabs(weightOf(added, 3) - 0.25) < 1.0e-9 && std::fabs(weightOf(added, 2) - 0.75) < 1.0e-9);
    // A point keeps a bone to follow.
    std::vector<fla::BoneWeight> last = fla::reweighted({{2, 1.0}}, 2, -0.25);
    CHECK(last.size() == 1 && last[0].bone == 2 && std::fabs(last[0].weight - 1.0) < 1.0e-9);
}
