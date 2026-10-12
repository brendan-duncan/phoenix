#include "bone_tool.h"

#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/symbol_instance.h"
#include "../edit/ik_pose.h"

#include <QMouseEvent>

#include <cmath>

BoneTool::BoneTool(std::function<void(PhoenixView&, const Link&)> addBone,
    std::function<void(PhoenixView&, fla::Frame*, size_t, int)> editJoint)
    : _addBone(std::move(addBone))
    , _editJoint(std::move(editJoint))
{
}

bool BoneTool::jointOf(const fla::Frame* frame, const fla::SymbolInstance* instance, QPointF& joint)
{
    if (!frame || !instance)
        return false;
    std::string reference;
    for (const auto& attribute : instance->extraAttributes)
    {
        if (attribute.first == "referenceID")
            reference = attribute.second;
    }
    if (reference.empty())
        return false;
    for (const std::string& child : frame->extraChildren)
    {
        if (child.find("<IKTree") == std::string::npos)
            continue;
        fla::IKArmature armature;
        if (!armature.parse(child) || armature.bendsShape())
            continue;
        for (const fla::ArmatureJoint& found : armature.symbolJoints())
        {
            if (found.reference == reference)
            {
                joint = QPointF(found.position.x, found.position.y);
                return true;
            }
        }
    }
    return false;
}

fla::SymbolInstance* BoneTool::instanceAt(PhoenixView& view, const QPointF& documentPos, fla::Frame*& frame)
{
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (!hit || hit.element->elementType() != fla::Element::Type::SymbolInstance)
        return nullptr;
    frame = hit.frame;
    return static_cast<fla::SymbolInstance*>(hit.element);
}

bool BoneTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    _link = Link();
    _bone = view.ikBoneAt(documentPos);
    _press = documentPos;
    fla::Frame* frame = nullptr;
    fla::SymbolInstance* instance = instanceAt(view, documentPos, frame);
    if (!instance)
    {
        // A click on a bone sets its joint's limits.
        const bool onBone = static_cast<bool>(_bone);
        if (onBone && !_bone.locked)
            editJoint(view);
        return onBone;
    }
    _link.fromFrame = frame;
    _link.from = instance;
    // From an instance in an armature, the bone starts at its joint.
    QPointF joint;
    _link.head = jointOf(frame, instance, joint) ? joint : documentPos;
    _cursor = documentPos;
    _dragging = true;
    view.update();
    return true;
}

bool BoneTool::mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (!_dragging)
        return false;
    _cursor = documentPos;
    view.update();
    return true;
}

bool BoneTool::mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    (void)event;
    if (!_dragging)
        return false;
    _dragging = false;
    view.update();
    // A click on a bone, not a drag, sets its joint's limits.
    if (_bone && !_bone.locked &&
        std::hypot(documentPos.x() - _press.x(), documentPos.y() - _press.y()) < 3.0 * view.pickTolerance())
    {
        _link = Link();
        editJoint(view);
        return true;
    }
    fla::Frame* frame = nullptr;
    fla::SymbolInstance* instance = instanceAt(view, documentPos, frame);
    if (!instance || instance == _link.from ||
        std::hypot(documentPos.x() - _link.head.x(), documentPos.y() - _link.head.y()) < 1.0)
    {
        return true;
    }
    _link.toFrame = frame;
    _link.to = instance;
    _link.tail = documentPos;
    if (_addBone)
        _addBone(view, _link);
    _link = Link();
    return true;
}

void BoneTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    (void)view;
    if (!_dragging)
        return;
    const QPointF head = _link.head;
    const QPointF tail = _cursor;
    const double length = std::hypot(tail.x() - head.x(), tail.y() - head.y());
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(153, 51, 204);
    painter.setPen(QPen(ink.darker(160), 1.0 * scale));
    QColor fill = ink;
    fill.setAlpha(160);
    painter.setBrush(fill);
    if (length > 1.0e-9)
    {
        // The kite the bone will be drawn as.
        const QPointF along = (tail - head) / length;
        const QPointF across(-along.y(), along.x());
        const double width = std::min(6.0 * scale, length / 5.0);
        const QPointF wide = head + along * (length / 5.0);
        QPolygonF kite;
        kite << head << wide + across * width << tail << wide - across * width;
        painter.drawPolygon(kite);
    }
    painter.drawEllipse(head, 6.0 * scale, 6.0 * scale);
    painter.drawEllipse(tail, 3.5 * scale, 3.5 * scale);
    painter.restore();
}

void BoneTool::editJoint(PhoenixView& view)
{
    const PhoenixView::IKBoneHit bone = _bone;
    _bone = PhoenixView::IKBoneHit();
    fla::IKArmature armature;
    if (!_editJoint || !bone || !armature.parse(bone.frame->extraChildren[bone.child]))
        return;
    // The bone turns about its head: the joint above its tail.
    const std::vector<fla::IKJoint> joints = armature.jointsAt(0);
    const int pivot = bone.joint >= 0 && static_cast<size_t>(bone.joint) < joints.size() ? joints[bone.joint].parent : -1;
    if (pivot >= 0)
        _editJoint(view, bone.frame, bone.child, pivot);
}

void BoneTool::deactivate(PhoenixView& view)
{
    _dragging = false;
    _link = Link();
    view.update();
}
