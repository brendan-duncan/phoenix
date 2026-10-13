#include "bone_tool.h"

#include "phoenix_view.h"

#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/symbol.h"
#include "../data/symbol_instance.h"
#include "../data/timeline.h"
#include "../edit/ik_pose.h"

#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

QTransform toQ(const fla::Transform& t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
}

} // namespace

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

bool BoneTool::shapeJointAt(PhoenixView& view, const QPointF& documentPos, fla::Frame*& frame, size_t& child,
    int& joint, QPointF& at)
{
    double best = 8.0 / std::max(view.zoom(), 1.0e-6);
    bool found = false;
    for (const auto& entry : view.armatureFrames())
    {
        for (size_t i = 0; i < entry.first->extraChildren.size(); ++i)
        {
            fla::IKArmature armature;
            if (!armature.parse(entry.first->extraChildren[i]) || !armature.bendsShape())
                continue;
            const std::vector<fla::IKJoint> joints = armature.shownAt(entry.second);
            for (size_t j = 0; j < joints.size(); ++j)
            {
                const double distance = std::hypot(documentPos.x() - joints[j].position.x,
                    documentPos.y() - joints[j].position.y);
                if (distance <= best)
                {
                    best = distance;
                    found = true;
                    frame = entry.first;
                    child = i;
                    joint = static_cast<int>(j);
                    at = QPointF(joints[j].position.x, joints[j].position.y);
                }
            }
        }
    }
    return found;
}

fla::SymbolInstance* BoneTool::instanceAt(PhoenixView& view, const QPointF& documentPos, fla::Frame*& frame)
{
    const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
    if (!hit || hit.element->elementType() != fla::Element::Type::SymbolInstance)
        return nullptr;
    fla::SymbolInstance* instance = static_cast<fla::SymbolInstance*>(hit.element);
    // A boned shape's symbol is not a part to join.
    if (instance->symbol && instance->symbol->otherSymbolType == "ik container")
        return nullptr;
    frame = hit.frame;
    return instance;
}

bool BoneTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    _link = Link();
    _bone = view.ikBoneAt(documentPos);
    _press = documentPos;
    _cursor = documentPos;

    // From a joint of a boned shape, a bone more.
    QPointF joint;
    if (shapeJointAt(view, documentPos, _link.armature, _link.child, _link.joint, joint))
    {
        _link.head = joint;
        _dragging = true;
        view.update();
        return true;
    }

    fla::Frame* frame = nullptr;
    fla::SymbolInstance* instance = instanceAt(view, documentPos, frame);
    if (!instance)
    {
        // In a shape, its first bone.
        const HitResult hit = view.hitTest(documentPos, view.pickTolerance());
        if (hit && hit.element->elementType() == fla::Element::Type::Shape && !_bone)
        {
            _link.shapeFrame = hit.frame;
            _link.shape = static_cast<fla::Shape*>(hit.element);
            _link.head = documentPos;
            _dragging = true;
            view.update();
            return true;
        }
        // A click on a bone sets its joint's limits.
        const bool onBone = static_cast<bool>(_bone);
        if (onBone && !_bone.locked)
            editJoint(view);
        return onBone;
    }
    _link.fromFrame = frame;
    _link.from = instance;
    // From an instance in an armature, the bone starts at its joint.
    _link.head = jointOf(frame, instance, joint) ? joint : documentPos;
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
    const bool click = std::hypot(documentPos.x() - _press.x(), documentPos.y() - _press.y()) < 3.0 * view.pickTolerance();
    // A click on a bone, not a drag, sets its joint's limits.
    if (_bone && !_bone.locked && click)
    {
        _link = Link();
        editJoint(view);
        return true;
    }
    if (_link.inShape())
    {
        // A bone in a shape ends wherever it is let go.
        if (!click && std::hypot(documentPos.x() - _link.head.x(), documentPos.y() - _link.head.y()) >= 1.0)
        {
            _link.tail = documentPos;
            if (_addBone)
                _addBone(view, _link);
        }
        _link = Link();
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

// ---------------------------------------------------------------------------

bool BoundShape::find(fla::Frame* frame, size_t child, int into, BoundShape& found)
{
    if (!frame || child >= frame->extraChildren.size() || !found.armature.parse(frame->extraChildren[child]) ||
        !found.armature.bendsShape())
    {
        return false;
    }
    found.frame = frame;
    found.child = child;
    found.into = into;
    found.instance = nullptr;
    for (fla::Element* element : frame->elements)
    {
        fla::SymbolInstance* instance = dynamic_cast<fla::SymbolInstance*>(element);
        if (instance && instance->symbol && !instance->symbol->timelines.empty())
        {
            found.instance = instance;
            break;
        }
    }
    if (!found.instance)
        return false;
    // The container's shape at a frame of the span.
    const auto shapeAt = [&found](int index) -> fla::Shape* {
        const fla::Timeline* timeline = found.instance->symbol->timelines.front();
        const int at = found.instance->firstFrame + index;
        for (const fla::Layer* layer : timeline->layers)
        {
            for (const fla::Frame* symbolFrame : layer ? layer->frames : std::vector<fla::Frame*>())
            {
                if (!symbolFrame || at < symbolFrame->index || at >= symbolFrame->index + std::max(1, symbolFrame->duration))
                    continue;
                for (fla::Element* element : symbolFrame->elements)
                {
                    if (element && element->elementType() == fla::Element::Type::Shape)
                        return static_cast<fla::Shape*>(element);
                }
            }
        }
        return nullptr;
    };
    found.rest = shapeAt(0);
    fla::Shape* now = shapeAt(std::max(0, std::min(into, found.armature.frames() - 1)));
    if (!found.rest || !now)
        return false;
    const QTransform placed = toQ(found.instance->transform);
    found.anchors.clear();
    for (const fla::Point& p : fla::shapeAnchors(*found.rest))
    {
        const QPointF at = (toQ(found.rest->transform) * placed).map(QPointF(p.x, p.y));
        found.anchors.emplace_back(at.x(), at.y());
    }
    found.shown.clear();
    for (const fla::Point& p : fla::shapeAnchors(*now))
        found.shown.push_back((toQ(now->transform) * placed).map(QPointF(p.x, p.y)));
    if (found.shown.size() != found.anchors.size())
        return false;
    found.binding = found.armature.binding();
    if (!found.armature.bindsAnchors() || found.binding.size() != found.anchors.size())
        found.binding = fla::bindToNearestBones(found.anchors, found.armature.jointsAt(0));
    return true;
}

BindTool::BindTool(std::function<void(PhoenixView&, fla::Frame*, size_t, const fla::ShapeBinding&)> rebind)
    : _rebind(std::move(rebind))
{
}

std::vector<BoundShape> BindTool::shapesOnShow(PhoenixView& view) const
{
    std::vector<BoundShape> shapes;
    for (const auto& entry : view.armatureFrames())
    {
        for (size_t child = 0; child < entry.first->extraChildren.size(); ++child)
        {
            BoundShape found;
            if (BoundShape::find(entry.first, child, entry.second, found))
                shapes.push_back(found);
        }
    }
    return shapes;
}

bool BindTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    const double reach = 5.0 / std::max(view.zoom(), 1.0e-6);

    // A point of the boned shape whose bone is chosen.
    if (_frame && _bone >= 0)
    {
        for (const BoundShape& shape : shapesOnShow(view))
        {
            if (shape.frame != _frame || shape.child != _child)
                continue;
            int nearest = -1;
            double best = reach;
            for (size_t i = 0; i < shape.shown.size(); ++i)
            {
                const double distance = std::hypot(shape.shown[i].x() - documentPos.x(), shape.shown[i].y() - documentPos.y());
                if (distance <= best)
                {
                    best = distance;
                    nearest = static_cast<int>(i);
                }
            }
            if (nearest < 0)
                break;
            const fla::Layer* layer = dynamic_cast<const fla::Layer*>(shape.frame->parent);
            if (layer && layer->isLocked())
                return true;
            fla::ShapeBinding binding = shape.binding;
            std::vector<fla::BoneWeight>& bones = binding[static_cast<size_t>(nearest)];
            const auto on = std::find_if(bones.begin(), bones.end(), [this](const fla::BoneWeight& bound) {
                return bound.bone == _bone;
            });
            const bool bound = on != bones.end();
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            // Ctrl unbinds, Shift switches, a plain click binds.
            const bool unbind = (modifiers & Qt::ControlModifier) || ((modifiers & Qt::ShiftModifier) && bound);
            if (unbind && bound)
                bones.erase(on);
            else if (!unbind && !bound)
                bones.push_back({_bone, 1.0});
            else
                return true;
            for (fla::BoneWeight& weight : bones)
                weight.weight = 1.0 / static_cast<double>(bones.size());
            if (_rebind)
                _rebind(view, shape.frame, shape.child, binding);
            view.update();
            return true;
        }
    }

    // A bone of a boned shape to bind points to.
    const PhoenixView::IKBoneHit hit = view.ikBoneAt(documentPos);
    fla::IKArmature armature;
    if (hit && armature.parse(hit.frame->extraChildren[hit.child]) && armature.bendsShape())
    {
        _frame = hit.frame;
        _child = hit.child;
        _bone = hit.joint;
    }
    else
    {
        _frame = nullptr;
        _bone = -1;
    }
    view.update();
    return true;
}

void BindTool::paintOverlay(PhoenixView& view, QPainter& painter, double scale)
{
    const std::vector<BoundShape> shapes = shapesOnShow(view);
    if (shapes.empty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double half = 3.5 * scale;
    for (const BoundShape& shape : shapes)
    {
        const bool chosen = shape.frame == _frame && shape.child == _child && _bone >= 0;
        if (chosen)
        {
            // The chosen bone, outlined.
            const std::vector<fla::IKJoint> joints = shape.armature.shownAt(shape.into);
            if (static_cast<size_t>(_bone) < joints.size() && joints[_bone].parent >= 0)
            {
                const fla::Point& head = joints[joints[_bone].parent].position;
                const fla::Point& tail = joints[_bone].position;
                painter.setPen(QPen(QColor(0, 200, 90), 3.0 * scale, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(QPointF(head.x, head.y), QPointF(tail.x, tail.y));
            }
        }
        // Each point: yellow when it follows the chosen bone, a triangle
        // when it follows more than one bone, red when it follows none.
        for (size_t i = 0; i < shape.shown.size(); ++i)
        {
            const std::vector<fla::BoneWeight>& bones = shape.binding[i];
            const bool onBone = chosen && std::any_of(bones.begin(), bones.end(),
                [this](const fla::BoneWeight& bound) { return bound.bone == _bone; });
            const QPointF at = shape.shown[i];
            painter.setPen(QPen(bones.empty() ? QColor(220, 40, 40) : QColor(40, 90, 220), 1.0 * scale));
            painter.setBrush(onBone ? QColor(255, 220, 0) : QColor(255, 255, 255));
            if (bones.size() > 1)
            {
                painter.drawPolygon(QPolygonF({at + QPointF(0.0, -half * 1.2), at + QPointF(half * 1.1, half),
                    at + QPointF(-half * 1.1, half)}));
            }
            else
                painter.drawRect(QRectF(at.x() - half, at.y() - half, 2.0 * half, 2.0 * half));
        }
    }
    painter.restore();
}

void BindTool::deactivate(PhoenixView& view)
{
    _frame = nullptr;
    _bone = -1;
    view.update();
}
