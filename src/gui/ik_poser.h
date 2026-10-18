#pragma once

#include "../edit/ik_pose.h"

#include <QPointF>

#include <functional>
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

/// Drags an IK armature into a new pose, as the selection tool does on
/// an armature layer: the bone under the cursor has its tail pulled after
/// the cursor and the joints above it turn to follow.
///
/// The frame under the playhead becomes a pose (one is added when there is
/// none), every frame of the span has its joints worked out again, and for
/// an armature bending a shape, the shape the FLA keeps for each frame in the
/// armature's ik container symbol is bent from its old bones to its new ones.
/// While dragging only the frame on show is bent; letting go bends the rest
/// and makes it all one undo step.
///
/// An armature of symbols turns and moves the instances its joints name, each
/// with its joint: posing the span's first frame moves their saved matrices,
/// and any other frame shows them where its joints put them.
class IKPoser
{
public:
    /// Starts on the bone under \a documentPos; false when there is none.
    bool begin(PhoenixView& view, const QPointF& documentPos);

    bool active() const { return _frame != nullptr; }

    void drag(PhoenixView& view, const QPointF& documentPos);

    void finish(PhoenixView& view, fla::CommandStack& commandStack);

    /// Changes the armature kept on \a frame (its IKTree, extraChildren
    /// \a child) by \a edit, as a drag would: every frame's joints worked
    /// out again, the shapes it bends bent and the instances it carries
    /// moved, all one undo step named \a name. False when \a edit changes
    /// nothing it can make.
    bool change(PhoenixView& view, fla::CommandStack& commandStack, fla::Frame* frame, size_t child,
        const std::function<bool(fla::IKArmature&)>& edit, const std::string& name);

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

    /// Sets \a shape to \a bent's frame's shape bent to the new pose: from
    /// the first frame's shape by the binding, when the shape has one
    /// Phoenix can follow, or else from the frame's own by nearness.
    void bend(Bent& bent, fla::Shape& shape);

    std::string posesOf(const fla::IKArmature& armature) const;
    /// Gathers what posing \a frame's armature changes; false when it has
    /// none there.
    bool setUp(PhoenixView& view, fla::Frame* frame, size_t child);
    fla::Transform toArmature(const fla::Shape& shape) const;
    void reset();

    fla::Frame* _frame = nullptr;
    size_t _child = 0;
    int _joint = -1;
    int _into = 0;
    QPointF _offset;
    bool _moved = false;
    std::string _name = "Pose Armature";
    std::string _xml;
    std::string _poses;
    fla::IKArmature _original;
    fla::IKArmature _posed;
    fla::SymbolInstance* _instance = nullptr;
    std::vector<Bent> _bent;
    std::vector<Carried> _carried;
    /// The shape's binding, and its first frame's shape as it was, when
    /// every frame bends from that.
    fla::ShapeBinding _binding;
    const fla::Shape* _rest = nullptr;
};
