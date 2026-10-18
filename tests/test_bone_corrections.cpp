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
#include "../src/edit/bone_corrections.h"
#include "../src/edit/ik_pose.h"
#include "../src/edit/new_document.h"
#include "../src/edit/phoenix_features.h"
#include "../src/edit/shape_merge.h"
#include "../src/parser/fla_parser.h"
#include "../src/parser/path_parser.h"
#include "../src/writer/xfl_folder_writer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

namespace corrections = fla::corrections;

bool near(const fla::Point& p, double x, double y, double tolerance = 1.0e-3)
{
    return std::fabs(p.x - x) < tolerance && std::fabs(p.y - y) < tolerance;
}

/// A document with a 200 by 40 arm at (100, 100), with points at its elbow
/// halfway along, boned along its middle by three joints (ikNode_1 to
/// ikNode_3, the elbow at ikNode_2), its second bone turned a
/// third of a half turn down by frame 29, every frame bent as posing bends
/// it; and a five-frame graphic, Hand, on a layer of its own for the
/// span's 30 frames.
struct Rigged
{
    std::unique_ptr<fla::FLADocument> fla;
    fla::Timeline* scene = nullptr;
    fla::Frame* pose = nullptr;
    fla::SymbolInstance* container = nullptr;
    fla::SymbolInstance* hand = nullptr;
    fla::IKArmature armature;

    std::vector<fla::Frame*>& frames() { return container->symbol->timelines.front()->layers.front()->frames; }
    fla::Shape* shapeAt(int k) { return static_cast<fla::Shape*>(frames()[static_cast<size_t>(k)]->elements.front()); }
};

bool rig(Rigged& made)
{
    made.fla.reset(fla::createEmptyDocument(640, 360));
    if (!made.fla || !made.fla->document || made.fla->document->timelines.empty())
        return false;
    fla::Document* document = made.fla->document;
    fla::Timeline* scene = document->timelines.front();
    made.scene = scene;
    fla::Layer* layer = scene->layers.front();
    fla::Frame* frame = layer->frames.front();
    frame->duration = 30;
    auto* shape = new fla::Shape(frame);
    PathParser parser;
    fla::Edge* edge = parser.parse("!0 0|2000 0|4000 0|4000 800|2000 800|0 800|0 0", shape);
    edge->fillStyle1 = 1;
    shape->edges.push_back(edge);
    shape->transform.tx = 100.0;
    shape->transform.ty = 100.0;
    auto* fill = new fla::SolidColor(shape);
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
    if (!boned.symbol)
        return false;
    if (!document->symbolList)
        document->symbolList = new fla::SymbolList(document);
    boned.symbol->parent = document->symbolList;
    document->symbolList->symbols.push_back(boned.symbol);
    document->symbolList->link(boned.symbol);
    frame->elements.clear();
    delete shape;
    scene->layers.insert(scene->layers.begin(), boned.layer);
    made.pose = boned.pose;
    made.container = boned.instance;

    fla::IKArmature& armature = made.armature;
    if (!armature.parse(*corrections::treeOf(*boned.pose)))
        return false;
    const std::vector<fla::IKJoint> start = armature.shownAt(0);
    armature.setPose(29, {start[0].angle, start[1].angle + kPi / 3.0, start[2].angle + kPi / 3.0});
    fla::PoseArmatureCommand::apply(boned.pose, boned.pose->extraChildren.size() - 1, armature.xml(), "0,29");
    fla::Shape* rest = made.shapeAt(0);
    std::unique_ptr<fla::Shape> first(fla::cloneShape(*rest, nullptr));
    for (size_t k = 0; k < made.frames().size(); ++k)
    {
        std::unique_ptr<fla::Shape> bent(fla::cloneShape(*first, nullptr));
        fla::bindShape(*bent, boned.instance->transform, armature.binding(), armature.shownAt(0),
            armature.shownAt(static_cast<int>(k)));
        fla::setShapeContents(*made.shapeAt(static_cast<int>(k)), *bent);
    }

    // The hand: a graphic of five keyframes.
    auto* symbol = new fla::Symbol(document->symbolList);
    symbol->name = "Hand";
    symbol->href = "Hand.xml";
    symbol->symbolType = fla::SymbolType::Graphic;
    auto* inside = new fla::Timeline(symbol);
    inside->name = "Hand";
    symbol->timelines.push_back(inside);
    auto* drawing = new fla::Layer(inside);
    drawing->name = "Layer 1";
    inside->layers.push_back(drawing);
    for (int i = 0; i < 5; ++i)
    {
        auto* key = new fla::Frame(drawing);
        key->index = i;
        drawing->frames.push_back(key);
    }
    document->symbolList->symbols.push_back(symbol);
    document->symbolList->link(symbol);
    auto* handLayer = new fla::Layer(scene);
    handLayer->name = "hand";
    auto* handFrame = new fla::Frame(handLayer);
    handFrame->duration = 30;
    auto* hand = new fla::SymbolInstance(handFrame);
    hand->libraryItemName = "Hand";
    hand->symbol = symbol;
    hand->loopType = fla::LoopType::SingleFrame;
    handFrame->elements.push_back(hand);
    handLayer->frames.push_back(handFrame);
    scene->layers.push_back(handLayer);
    made.hand = hand;
    return true;
}

/// A correction on the joint at the second bone's head, from its rest
/// angle (0) to its pose at frame 29 (60 degrees).
corrections::Correction turning(corrections::Property property)
{
    corrections::Correction correction;
    correction.armature = "Armature_1";
    correction.joint = "ikNode_2";
    correction.property = property;
    correction.from = 0.0;
    correction.to = 60.0;
    return correction;
}

} // namespace

TEST(bone_corrections_keep_their_settings_as_text)
{
    corrections::Correction shape = turning(corrections::Property::Shape);
    shape.armature = "Arm|ature 1";
    shape.shifts.push_back({0, 1, 2, 1.5, -2.25});
    shape.shifts.push_back({1, 0, 7, 0.0, 3.0});
    corrections::Correction frame = turning(corrections::Property::Frame);
    frame.valueFrom = 1.0;
    frame.valueTo = 5.0;
    const std::vector<corrections::Correction> list = {shape, frame};
    const std::vector<corrections::Correction> back = corrections::decode(corrections::encode(list));
    CHECK(back.size() == 2 && back[0] == shape && back[1] == frame);

    fla::SymbolInstance instance(nullptr);
    CHECK(!corrections::any(instance));
    instance.extraAttributes = corrections::withCorrections(instance, list);
    CHECK(corrections::any(instance) && corrections::of(instance).size() == 2);
    instance.extraAttributes = corrections::withCorrections(instance, {});
    CHECK(instance.extraAttributes.empty());
}

TEST(bone_corrections_blend_in_by_the_bones_angle)
{
    const corrections::Correction correction = turning(corrections::Property::Alpha);
    CHECK(corrections::weight(correction, -10.0) == 0.0);
    CHECK(std::fabs(corrections::weight(correction, 30.0) - 0.5) < 1.0e-9);
    CHECK(corrections::weight(correction, 75.0) == 1.0);

    // A joint's turn is from its parent's, or its own at the top.
    std::vector<fla::IKJoint> joints(2);
    joints[0].angle = kPi / 2.0;
    joints[1].angle = kPi / 2.0 + kPi / 6.0;
    joints[1].parent = 0;
    CHECK(std::fabs(corrections::jointAngle(joints, 0) - 90.0) < 1.0e-9);
    CHECK(std::fabs(corrections::jointAngle(joints, 1) - 30.0) < 1.0e-9);
    joints[1].angle = -kPi + 0.1;
    CHECK(corrections::jointAngle(joints, 1) > 0.0);
}

TEST(a_shape_correction_moves_points_as_the_bone_turns)
{
    Rigged made;
    CHECK(rig(made));
    if (!made.container)
        return;
    const fla::IKArmature& armature = made.armature;
    const int joint = corrections::jointNamed(armature, "ikNode_2");
    CHECK(joint == 1);
    CHECK(std::fabs(corrections::jointAngle(armature.shownAt(0), joint)) < 1.0e-6);
    // The armature shows its last pose eased in, a hair short of it.
    const double end = corrections::jointAngle(armature.shownAt(29), joint);
    CHECK(std::fabs(end - 60.0) < 1.0);

    // At frame 29 the far top corner (point 2, on the last bone) pulled 10
    // pixels down in the shape's own space, as the correction in full.
    fla::Shape* rest = made.shapeAt(0);
    fla::Shape* bent = made.shapeAt(29);
    std::unique_ptr<fla::Shape> edited(fla::cloneShape(*bent, nullptr));
    edited->transform = bent->transform;
    const std::vector<fla::Point> before = corrections::points(*bent);
    CHECK(before.size() == 7);
    std::vector<fla::Point> wanted = before;
    wanted[2].y += 10.0;
    edited->edges.front()->paths.front()->segments[2]->points.back().y += 10.0;
    CHECK(near(corrections::points(*edited)[2], wanted[2].x, wanted[2].y));
    corrections::Correction correction = turning(corrections::Property::Shape);
    correction.to = end;
    CHECK(corrections::recordShifts(*bent, *edited, *rest, *made.container, armature, 29, 0, 0, correction.shifts));
    CHECK(correction.shifts.size() == 1 && correction.shifts.front().point == 2);
    // Kept turned back to the rest pose, as the bone turned.
    const corrections::PointShift& shift = correction.shifts.front();
    CHECK(std::fabs(std::hypot(shift.dx, shift.dy) - 10.0) < 1.0e-6);
    const double turned = end * kPi / 180.0;
    CHECK(std::fabs(shift.dx - 10.0 * std::sin(turned)) < 1.0e-6 && std::fabs(shift.dy - 10.0 * std::cos(turned)) < 1.0e-6);

    // In full at frame 29: the point where it was put.
    const std::vector<corrections::Correction> list = {correction};
    std::unique_ptr<fla::Shape> shown(fla::cloneShape(*bent, nullptr));
    shown->transform = bent->transform;
    CHECK(corrections::correctShape(*shown, *rest, *made.container, armature, 29, 0, 0, list));
    const std::vector<fla::Point> after = corrections::points(*shown);
    CHECK(near(after[2], wanted[2].x, wanted[2].y));
    CHECK(near(after[3], before[3].x, before[3].y));

    // At rest nothing moves; part way, part of the way, turned with the bone.
    std::unique_ptr<fla::Shape> still(fla::cloneShape(*rest, nullptr));
    CHECK(!corrections::correctShape(*still, *rest, *made.container, armature, 0, 0, 0, list));
    for (int k : {8, 15, 22})
    {
        fla::Shape* own = made.shapeAt(k);
        std::unique_ptr<fla::Shape> part(fla::cloneShape(*own, nullptr));
        part->transform = own->transform;
        const double degrees = corrections::jointAngle(armature.shownAt(k), joint);
        const double t = corrections::weight(correction, degrees);
        CHECK(t > 0.0 && t < 1.0);
        CHECK(corrections::correctShape(*part, *rest, *made.container, armature, k, 0, 0, list));
        const fla::Point was = corrections::points(*own)[2];
        const fla::Point now = corrections::points(*part)[2];
        CHECK(std::fabs(std::hypot(now.x - was.x, now.y - was.y) - 10.0 * t) < 1.0e-6);
        // The rest pose's shift turned by the bone's turn there.
        const double turn = degrees * kPi / 180.0;
        const double c = std::cos(turn);
        const double s = std::sin(turn);
        CHECK(near(fla::Point(now.x - was.x, now.y - was.y), t * (c * shift.dx - s * shift.dy),
            t * (s * shift.dx + c * shift.dy), 1.0e-6));
    }
    // Another joint's correction is passed over when asked.
    std::unique_ptr<fla::Shape> left(fla::cloneShape(*bent, nullptr));
    CHECK(!corrections::correctShape(*left, *rest, *made.container, armature, 29, 0, 0, list, "ikNode_2"));
}

TEST(a_bone_swaps_a_graphics_drawing_and_fades_it)
{
    Rigged made;
    CHECK(rig(made));
    if (!made.hand)
        return;
    corrections::Correction swap = turning(corrections::Property::Frame);
    swap.valueFrom = 1.0;
    swap.valueTo = 5.0;
    corrections::Correction fade = turning(corrections::Property::Alpha);
    fade.valueFrom = 100.0;
    fade.valueTo = 40.0;
    made.hand->extraAttributes = corrections::withCorrections(*made.hand, {swap, fade});

    corrections::Rig rig;
    const corrections::Effect start = rig.effect(*made.hand, 0);
    CHECK(start.frame == 0 && std::fabs(start.alpha - 1.0) < 1.0e-9);
    const corrections::Effect end = rig.effect(*made.hand, 29);
    CHECK(end.frame == 4 && std::fabs(end.alpha - 0.4) < 1.0e-9);
    // Past the span the last pose holds.
    CHECK(rig.effect(*made.hand, 40).frame == 4);
    // In between, the frame follows the angle.
    double degrees = 0.0;
    CHECK(rig.angleAt(*made.scene, 15, swap, degrees));
    const int expected = static_cast<int>(std::lround(1.0 + 4.0 * degrees / 60.0)) - 1;
    CHECK(rig.effect(*made.hand, 15).frame == expected);
    // A bone that is not there drives nothing.
    corrections::Correction lost = swap;
    lost.joint = "ikNode_9";
    made.hand->extraAttributes = corrections::withCorrections(*made.hand, {lost});
    CHECK(rig.effect(*made.hand, 29).frame == -1);

    std::set<std::string> driving = corrections::drivingJoints(*made.scene, "Armature_1");
    CHECK(driving.size() == 1 && driving.count("ikNode_9"));
}

TEST(bone_corrections_save_and_bake)
{
    Rigged made;
    CHECK(rig(made));
    if (!made.hand)
        return;
    corrections::Correction shape = turning(corrections::Property::Shape);
    shape.shifts.push_back({0, 0, 2, 5.0, 8.0});
    made.container->extraAttributes = corrections::withCorrections(*made.container, {shape});
    corrections::Correction swap = turning(corrections::Property::Frame);
    swap.valueFrom = 1.0;
    swap.valueTo = 5.0;
    made.hand->extraAttributes = corrections::withCorrections(*made.hand, {swap});
    CHECK(corrections::anyIn(*made.fla->document));

    // Phoenix's own, kept where other apps leave it.
    bool listed = false;
    for (const fla::features::Use& use : fla::features::used(*made.fla->document))
        listed = listed || std::string(use.feature->id) == "bone-corrections";
    CHECK(listed);

    const std::filesystem::path out = std::filesystem::temp_directory_path() / "phoenix_bone_corrections" / "rigged";
    std::filesystem::remove_all(out);
    fla::XFLFolderWriter writer;
    CHECK(writer.write(*made.fla->document, out.string(), "rigged"));
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> back(parser.parse((out / "rigged.xfl").string()));
    CHECK(back && back->document && !back->document->timelines.empty());
    if (!back || !back->document || back->document->timelines.empty())
        return;
    fla::Timeline* scene = back->document->timelines.front();
    fla::Frame* pose = scene->layers.front()->frames.front();
    fla::SymbolInstance* container = corrections::containerOn(*pose);
    fla::SymbolInstance* hand = nullptr;
    for (fla::Layer* layer : scene->layers)
        if (layer->name == "hand")
            hand = static_cast<fla::SymbolInstance*>(layer->frames.front()->elements.front());
    CHECK(container && hand);
    if (!container || !hand)
        return;
    const std::vector<corrections::Correction> shapes = corrections::of(*container);
    CHECK(shapes.size() == 1 && shapes.front() == shape);
    const std::vector<corrections::Correction> swaps = corrections::of(*hand);
    CHECK(swaps.size() == 1 && swaps.front() == swap);

    // Worked out as the stage shows it, before baking.
    fla::IKArmature armature;
    CHECK(armature.parse(*corrections::treeOf(*pose)));
    std::vector<fla::Frame*>& frames = container->symbol->timelines.front()->layers.front()->frames;
    const auto shapeAt = [&](int k) { return static_cast<fla::Shape*>(frames[static_cast<size_t>(k)]->elements.front()); };
    std::unique_ptr<fla::Shape> expected(fla::cloneShape(*shapeAt(29), nullptr));
    expected->transform = shapeAt(29)->transform;
    CHECK(corrections::correctShape(*expected, *shapeAt(0), *container, armature, 29, 0, 0, shapes));
    const fla::Point wanted = corrections::points(*expected)[2];
    const fla::Point restPoint = corrections::points(*shapeAt(0))[2];

    std::vector<std::string> notes;
    CHECK(corrections::bake(*back->document, notes) == 2);
    CHECK(notes.size() == 2);
    CHECK(!corrections::anyIn(*back->document));
    // The boned shape's frames carry the correction; its first is at rest.
    CHECK(near(corrections::points(*shapeAt(29))[2], wanted.x, wanted.y));
    CHECK(near(corrections::points(*shapeAt(0))[2], restPoint.x, restPoint.y));
    // The hand shows a graphic of its own, its drawings keyed as the bone
    // turns: frame 1 to begin with, frame 5 at the end.
    CHECK(hand->symbol && hand->symbol->name == "Hand corrected" && hand->loopType == fla::LoopType::Loop);
    const std::vector<fla::Frame*>& keys = hand->symbol->timelines.front()->layers.front()->frames;
    CHECK(keys.size() >= 5);
    const auto* firstShown = static_cast<const fla::SymbolInstance*>(keys.front()->elements.front());
    const auto* lastShown = static_cast<const fla::SymbolInstance*>(keys.back()->elements.front());
    CHECK(firstShown->symbol && firstShown->symbol->name == "Hand" && firstShown->firstFrame == 0);
    CHECK(lastShown->firstFrame == 4 && keys.back()->index + keys.back()->duration == 30);
    for (size_t i = 1; i < keys.size(); ++i)
        CHECK(static_cast<const fla::SymbolInstance*>(keys[i]->elements.front())->firstFrame >=
            static_cast<const fla::SymbolInstance*>(keys[i - 1]->elements.front())->firstFrame);
}
