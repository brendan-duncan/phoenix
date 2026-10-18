#include "ik_poser.h"

#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/command_stack.h"
#include "../edit/element_commands.h"
#include "../edit/property_command.h"
#include "../edit/shape_merge.h"

#include <cmath>

namespace {

/// \a first, then \a second.
fla::Transform then(const fla::Transform& first, const fla::Transform& second)
{
    return fla::Transform(first.m11 * second.m11 + first.m12 * second.m21,
        first.m11 * second.m12 + first.m12 * second.m22,
        first.m21 * second.m11 + first.m22 * second.m21,
        first.m21 * second.m12 + first.m22 * second.m22,
        first.tx * second.m11 + first.ty * second.m21 + second.tx,
        first.tx * second.m12 + first.ty * second.m22 + second.ty);
}

bool moved(const std::vector<fla::IKJoint>& a, const std::vector<fla::IKJoint>& b)
{
    if (a.size() != b.size())
        return true;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::fabs(a[i].position.x - b[i].position.x) > 1.0e-6 ||
            std::fabs(a[i].position.y - b[i].position.y) > 1.0e-6 ||
            std::fabs(a[i].angle - b[i].angle) > 1.0e-9)
        {
            return true;
        }
    }
    return false;
}

} // namespace

std::string IKPoser::posesOf(const fla::IKArmature& armature) const
{
    std::string text;
    for (int pose : armature.poses())
        text += (text.empty() ? "" : ",") + std::to_string(pose);
    return text;
}

fla::Transform IKPoser::toArmature(const fla::Shape& shape) const
{
    return _instance ? then(shape.transform, _instance->transform) : shape.transform;
}

void IKPoser::carry(const std::vector<fla::IKJoint>& before, const std::vector<fla::IKJoint>& after)
{
    for (Carried& carried : _carried)
    {
        if (carried.joint < 0 || static_cast<size_t>(carried.joint) >= before.size() ||
            static_cast<size_t>(carried.joint) >= after.size())
        {
            continue;
        }
        // Turned with the joint, the registration point keeping its place
        // from it, and scaled with the bone below when that stretches.
        carried.instance->transform = then(carried.original, fla::boneMove(before, after, carried.joint));
    }
}

void IKPoser::reset()
{
    _frame = nullptr;
    _instance = nullptr;
    _bent.clear();
    _carried.clear();
    _binding.clear();
    _rest = nullptr;
    _moved = false;
    _joint = -1;
    _name = "Pose Armature";
}

bool IKPoser::begin(PhoenixView& view, const QPointF& documentPos, bool alone)
{
    reset();
    const PhoenixView::IKBoneHit hit = view.ikBoneAt(documentPos);
    if (!hit || hit.locked)
        return false;
    if (!setUp(view, hit.frame, hit.child))
        return false;
    _joint = hit.joint;
    _into = hit.into;
    _offset = hit.tail - documentPos;
    _mode = alone ? Mode::Turn : Mode::Reach;
    // Pressed on the joint at the root, the whole armature moves.
    const std::vector<fla::IKJoint> shown = _original.shownAt(_into);
    const int head = _joint >= 0 && static_cast<size_t>(_joint) < shown.size() ? shown[_joint].parent : -1;
    if (!alone && head >= 0 && shown[head].parent < 0)
    {
        const QPointF root(shown[head].position.x, shown[head].position.y);
        if (std::hypot(documentPos.x() - root.x(), documentPos.y() - root.y()) <= 7.0 / std::max(view.zoom(), 1.0e-6))
        {
            _mode = Mode::Move;
            _joint = head;
            _offset = root - documentPos;
        }
    }
    return true;
}

bool IKPoser::change(PhoenixView& view, fla::CommandStack& commandStack, fla::Frame* frame, size_t child,
    const std::function<bool(fla::IKArmature&)>& edit, const std::string& name)
{
    reset();
    if (!setUp(view, frame, child))
        return false;
    _posed = _original;
    if (!edit(_posed))
    {
        reset();
        return false;
    }
    _moved = true;
    _name = name;
    fla::PoseArmatureCommand::apply(_frame, _child, _posed.xml(), posesOf(_posed));
    carry(_original.shownAt(0), _posed.shownAt(0));
    finish(view, commandStack);
    return true;
}

bool IKPoser::setUp(PhoenixView& view, fla::Frame* frame, size_t child)
{
    if (!frame || child >= frame->extraChildren.size() || !_original.parse(frame->extraChildren[child]))
        return false;
    // Springs swing at the document's own pace.
    if (view.flaDocument() && view.flaDocument()->document)
        _original.setFrameRate(view.flaDocument()->document->frameRate);
    _frame = frame;
    _child = child;
    _xml = frame->extraChildren[child];
    _poses = posesOf(_original);
    for (const auto& attribute : _frame->extraAttributes)
    {
        if (attribute.first == "poseLocations")
            _poses = attribute.second;
    }
    _posed = _original;

    // An armature of symbols carries its instances: their matrices are
    // where the span's first frame has them, and every other frame places
    // them from its joints (IKArmature::carried).
    if (!_original.bendsShape())
    {
        // In the order the frame lists them, as the betweenFrameMatrixList
        // keeps them.
        for (fla::Element* element : _frame->elements)
        {
            if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
                continue;
            fla::SymbolInstance* instance = static_cast<fla::SymbolInstance*>(element);
            for (int joint = 0; joint < static_cast<int>(_original.jointsAt(0).size()); ++joint)
            {
                const std::string reference = _original.referenceOf(joint);
                for (const auto& attribute : instance->extraAttributes)
                {
                    if (!reference.empty() && attribute.first == "referenceID" && attribute.second == reference)
                        _carried.push_back({instance, instance->transform, joint});
                }
            }
        }
    }

    // The shapes the armature bends: one a frame in its ik container
    // symbol, which the pose frame shows playing once from its first frame.
    if (_original.bendsShape())
    {
        for (fla::Element* element : _frame->elements)
        {
            if (element && element->elementType() == fla::Element::Type::SymbolInstance)
            {
                fla::SymbolInstance* instance = static_cast<fla::SymbolInstance*>(element);
                if (instance->symbol && !instance->symbol->timelines.empty())
                {
                    _instance = instance;
                    break;
                }
            }
        }
    }
    if (_instance)
    {
        const fla::Timeline* timeline = _instance->symbol->timelines.front();
        for (int frame = 0; frame < _original.frames(); ++frame)
        {
            const int index = _instance->firstFrame + frame;
            for (fla::Layer* layer : timeline->layers)
            {
                for (fla::Frame* symbolFrame : layer ? layer->frames : std::vector<fla::Frame*>())
                {
                    // Only a frame of its own: one held over several would
                    // have to bend differently on each.
                    if (!symbolFrame || symbolFrame->index != index || symbolFrame->duration > 1)
                        continue;
                    for (fla::Element* element : symbolFrame->elements)
                    {
                        if (element && element->elementType() == fla::Element::Type::Shape)
                        {
                            fla::Shape* shape = static_cast<fla::Shape*>(element);
                            _bent.push_back({frame, shape, std::unique_ptr<fla::Shape>(fla::cloneShape(*shape, nullptr))});
                        }
                    }
                }
            }
        }
    }

    // A shape bound to its bones point by point, as the Bone tool binds it,
    // bends every frame from the span's first: its points follow their
    // bones from the first frame's joints to the frame's own.
    // So does one the reference app bound, its points found by where they
    // were bound.
    _binding.clear();
    _rest = nullptr;
    if (_original.bendsShape())
    {
        const Bent* first = nullptr;
        for (const Bent& bent : _bent)
        {
            if (bent.frame == 0 && !first)
                first = &bent;
        }
        if (first && _original.bindsAnchors())
            _binding = _original.binding();
        else if (first)
        {
            std::vector<fla::Point> anchors = fla::shapeAnchors(*first->original);
            const fla::Transform placed = toArmature(*first->shape);
            for (fla::Point& anchor : anchors)
                anchor = anchor.transformed(placed);
            _binding = _original.bindingFor(anchors);
        }
        _rest = first ? first->original.get() : nullptr;
        if (!_rest || fla::shapeAnchors(*_rest).size() != _binding.size())
        {
            _binding.clear();
            _rest = nullptr;
        }
    }
    return true;
}

void IKPoser::bend(Bent& bent, fla::Shape& shape)
{
    fla::setShapeContents(shape, *bent.original);
    if (_rest)
    {
        // From the first frame's shape, by the binding.
        fla::setShapeContents(shape, *_rest);
        fla::bindShape(shape, toArmature(*bent.shape), _binding, _original.shownAt(0), _posed.shownAt(bent.frame));
        return;
    }
    fla::reskinShape(shape, toArmature(*bent.shape), _original.shownAt(bent.frame), _posed.shownAt(bent.frame));
}

void IKPoser::drag(PhoenixView& view, const QPointF& documentPos)
{
    if (!_frame)
        return;
    const QPointF target = documentPos + _offset;
    const fla::Point to(target.x(), target.y());
    const std::vector<fla::IKJoint> start = _original.shownAt(_into);
    if (_joint < 0 || static_cast<size_t>(_joint) >= start.size())
        return;
    // Locked bones, targets and independent angles kept as it moves.
    const fla::PoseGoals goals = _original.goalsAt(_into, start, _mode == Mode::Move ? -1 : _joint);
    std::vector<fla::IKJoint> solved = start;
    if (_mode == Mode::Move)
    {
        // Every joint of the root's armature moves with it.
        const fla::Point by(to.x - start[_joint].position.x, to.y - start[_joint].position.y);
        for (fla::IKJoint& joint : solved)
        {
            int root = static_cast<int>(&joint - solved.data());
            while (solved[root].parent >= 0)
                root = solved[root].parent;
            if (root == _joint)
                joint.position = fla::Point(joint.position.x + by.x, joint.position.y + by.y);
        }
        solved = fla::solvePose(solved, goals, -1, fla::Point(), _original.limits());
    }
    else if (_mode == Mode::Turn)
        solved = fla::stretchBone(start, _joint, to, goals, _original.limits());
    else
        solved = fla::solvePose(start, goals, _joint, to, _original.limits());

    _posed = _original;
    if (!_posed.setPlacedPose(_into, solved))
        return;
    // A bone that reaches for a point takes the point where it is dragged.
    if (_mode == Mode::Reach)
    {
        fla::BoneTarget aim = _original.target(_joint, _into);
        if (aim.point && aim.joint < 0)
        {
            aim.at = to;
            _posed.setTarget(_joint, _into, aim, false);
        }
    }
    _moved = true;
    fla::PoseArmatureCommand::apply(_frame, _child, _posed.xml(), posesOf(_posed));

    // The frame on show bends now; the rest when the drag ends. Instances
    // are saved where the span's first frame has them, so only posing that
    // frame moves them; the others follow their joints as drawn.
    if (_into == 0)
        carry(_original.shownAt(0), _posed.shownAt(0));
    if (!_carried.empty())
        view.invalidateBounds();
    for (Bent& bent : _bent)
    {
        if (bent.frame != _into)
            continue;
        bend(bent, *bent.shape);
        view.forgetShape(bent.shape);
    }
    view.update();
}

void IKPoser::finish(PhoenixView& view, fla::CommandStack& commandStack)
{
    if (!_frame)
        return;
    if (!_moved)
    {
        reset();
        return;
    }

    std::unique_ptr<fla::CompoundCommand> compound(new fla::CompoundCommand(_name));
    compound->addCommand(fla::CommandPtr(new fla::PoseArmatureCommand(_frame, _child, _xml, _posed.xml(),
        _poses, posesOf(_posed))));
    for (Bent& bent : _bent)
    {
        // Each shape as it was, then bent from its frame's old bones to its new.
        fla::setShapeContents(*bent.shape, *bent.original);
        const std::vector<fla::IKJoint> before = _original.shownAt(bent.frame);
        const std::vector<fla::IKJoint> after = _posed.shownAt(bent.frame);
        if (!moved(before, after))
            continue;
        std::unique_ptr<fla::Shape> reshaped(fla::cloneShape(*bent.original, nullptr));
        reshaped->transform = bent.shape->transform;
        bend(bent, *reshaped);
        compound->addCommand(fla::CommandPtr(new fla::SetShapeContentsCommand(bent.shape, *reshaped, "Pose Armature")));
    }
    // How the instances move from the first frame, for every frame.
    std::vector<std::string> children = _frame->extraChildren;
    if (!_carried.empty())
    {
        std::vector<std::pair<int, const fla::Transform*>> placed;
        for (const Carried& carried : _carried)
            placed.emplace_back(carried.joint, &carried.instance->transform);
        fla::setBetweenFrames(children, _posed.betweenFrameMatrixList(placed));
    }
    for (Carried& carried : _carried)
    {
        compound->addCommand(fla::CommandPtr(new fla::SetElementTransformCommand(carried.instance, carried.original,
            carried.instance->transform, "Pose Armature")));
        carried.instance->transform = carried.original;
    }
    // The pose text goes back too, so the stack applies it all as one.
    fla::PoseArmatureCommand::apply(_frame, _child, _xml, _poses);
    if (!_carried.empty() && children != _frame->extraChildren)
    {
        compound->addCommand(fla::CommandPtr(new fla::SetPropertyCommand<std::vector<std::string>>(
            _frame->extraChildren, _frame->extraChildren, children, "Pose Armature")));
    }
    commandStack.push(std::move(compound));
    commandStack.breakMergeChain();
    for (Bent& bent : _bent)
        view.forgetShape(bent.shape);
    reset();
    view.update();
}

void IKPoser::cancel(PhoenixView& view)
{
    if (!_frame)
        return;
    fla::PoseArmatureCommand::apply(_frame, _child, _xml, _poses);
    for (Bent& bent : _bent)
    {
        fla::setShapeContents(*bent.shape, *bent.original);
        view.forgetShape(bent.shape);
    }
    for (Carried& carried : _carried)
        carried.instance->transform = carried.original;
    reset();
    view.update();
}
