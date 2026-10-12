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
        const fla::IKJoint& from = before[carried.joint];
        const fla::IKJoint& to = after[carried.joint];
        const double turn = to.angle - from.angle;
        const double c = std::cos(turn);
        const double s = std::sin(turn);
        const fla::Transform rotation(c, s, -s, c, 0.0, 0.0);
        fla::Transform linear = carried.original;
        linear.tx = 0.0;
        linear.ty = 0.0;
        fla::Transform moved = then(linear, rotation);
        // The registration point keeps its place relative to the joint.
        const double dx = carried.original.tx - from.position.x;
        const double dy = carried.original.ty - from.position.y;
        moved.tx = to.position.x + c * dx - s * dy;
        moved.ty = to.position.y + s * dx + c * dy;
        carried.instance->transform = moved;
    }
}

void IKPoser::reset()
{
    _frame = nullptr;
    _instance = nullptr;
    _bent.clear();
    _carried.clear();
    _moved = false;
    _joint = -1;
}

bool IKPoser::begin(PhoenixView& view, const QPointF& documentPos)
{
    reset();
    const PhoenixView::IKBoneHit hit = view.ikBoneAt(documentPos);
    if (!hit || hit.locked)
        return false;
    if (!_original.parse(hit.frame->extraChildren[hit.child]))
        return false;
    _frame = hit.frame;
    _child = hit.child;
    _joint = hit.joint;
    _into = hit.into;
    _offset = hit.tail - documentPos;
    _xml = hit.frame->extraChildren[hit.child];
    _poses = posesOf(_original);
    for (const auto& attribute : _frame->extraAttributes)
    {
        if (attribute.first == "poseLocations")
            _poses = attribute.second;
    }
    _posed = _original;

    // An armature of symbols carries its instances, which only a single
    // pose leaves where they are drawn; that pose is the span's first frame.
    if (!_original.bendsShape())
    {
        if (_original.poses().size() != 1)
        {
            reset();
            return false;
        }
        _into = 0;
        for (int joint = 0; joint < static_cast<int>(_original.jointsAt(0).size()); ++joint)
        {
            const std::string reference = _original.referenceOf(joint);
            if (reference.empty())
                continue;
            for (fla::Element* element : _frame->elements)
            {
                if (!element || element->elementType() != fla::Element::Type::SymbolInstance)
                    continue;
                fla::SymbolInstance* instance = static_cast<fla::SymbolInstance*>(element);
                for (const auto& attribute : instance->extraAttributes)
                {
                    if (attribute.first == "referenceID" && attribute.second == reference)
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
    return true;
}

void IKPoser::drag(PhoenixView& view, const QPointF& documentPos)
{
    if (!_frame)
        return;
    const QPointF target = documentPos + _offset;
    const std::vector<fla::IKJoint> start = _original.shownAt(_into);
    const std::vector<fla::IKJoint> solved = fla::solveIK(start, _joint, fla::Point(target.x(), target.y()), 16, _original.limits());
    std::vector<double> angles;
    for (const fla::IKJoint& joint : solved)
        angles.push_back(joint.angle);

    _posed = _original;
    if (!_posed.setPose(_into, angles))
        return;
    _moved = true;
    fla::PoseArmatureCommand::apply(_frame, _child, _posed.xml(), posesOf(_posed));

    // The frame on show bends now; the rest when the drag ends.
    const std::vector<fla::IKJoint> before = _original.shownAt(_into);
    const std::vector<fla::IKJoint> after = _posed.shownAt(_into);
    carry(before, after);
    if (!_carried.empty())
        view.invalidateBounds();
    for (Bent& bent : _bent)
    {
        if (bent.frame != _into)
            continue;
        fla::setShapeContents(*bent.shape, *bent.original);
        fla::reskinShape(*bent.shape, toArmature(*bent.shape), before, after);
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

    std::unique_ptr<fla::CompoundCommand> compound(new fla::CompoundCommand("Pose Armature"));
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
        fla::reskinShape(*reshaped, toArmature(*bent.shape), before, after);
        compound->addCommand(fla::CommandPtr(new fla::SetShapeContentsCommand(bent.shape, *reshaped, "Pose Armature")));
    }
    for (Carried& carried : _carried)
    {
        compound->addCommand(fla::CommandPtr(new fla::SetElementTransformCommand(carried.instance, carried.original,
            carried.instance->transform, "Pose Armature")));
        carried.instance->transform = carried.original;
    }
    // The pose text goes back too, so the stack applies it all as one.
    fla::PoseArmatureCommand::apply(_frame, _child, _xml, _poses);
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
