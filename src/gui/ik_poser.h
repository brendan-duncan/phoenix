#pragma once

#include "../edit/ik_pose.h"

#include <QPointF>

#include <memory>
#include <string>
#include <vector>

namespace fla {
class CommandStack;
class Frame;
class Shape;
class SymbolInstance;
}

class PhoenixView;

/// Drags an IK armature into a new pose, as Animate's selection tool does on
/// an armature layer: the bone under the cursor has its tail pulled after
/// the cursor and the joints above it turn to follow.
///
/// The frame under the playhead becomes a pose (one is added when there is
/// none), every frame of the span has its joints worked out again, and for
/// an armature bending a shape, the shape Animate keeps for each frame in the
/// armature's ik container symbol is bent from its old bones to its new ones.
/// While dragging only the frame on show is bent; letting go bends the rest
/// and makes it all one undo step.
///
/// An armature of symbols turns and moves the instances its joints name, each
/// with its joint. Phoenix does not yet play such an armature between poses,
/// so only one posed on a single frame can be dragged.
class IKPoser
{
public:
    /// Starts on the bone under \a documentPos; false when there is none.
    bool begin(PhoenixView& view, const QPointF& documentPos);

    bool active() const { return _frame != nullptr; }

    void drag(PhoenixView& view, const QPointF& documentPos);

    void finish(PhoenixView& view, fla::CommandStack& commandStack);

    /// Puts everything back as it was.
    void cancel(PhoenixView& view);

private:
    struct Bent
    {
        int frame = 0;
        fla::Shape* shape = nullptr;
        std::unique_ptr<fla::Shape> original;
    };

    /// An instance an armature of symbols moves, with the joint it follows.
    struct Carried
    {
        fla::SymbolInstance* instance = nullptr;
        fla::Transform original;
        int joint = -1;
    };

    /// Turns and moves the carried instances with their joints.
    void carry(const std::vector<fla::IKJoint>& before, const std::vector<fla::IKJoint>& after);

    std::string posesOf(const fla::IKArmature& armature) const;
    fla::Transform toArmature(const fla::Shape& shape) const;
    void reset();

    fla::Frame* _frame = nullptr;
    size_t _child = 0;
    int _joint = -1;
    int _into = 0;
    QPointF _offset;
    bool _moved = false;
    std::string _xml;
    std::string _poses;
    fla::IKArmature _original;
    fla::IKArmature _posed;
    fla::SymbolInstance* _instance = nullptr;
    std::vector<Bent> _bent;
    std::vector<Carried> _carried;
};
