#pragma once

#include "phoenix_view.h"
#include "tool.h"

#include "../edit/ik_pose.h"

#include <QPointF>

#include <functional>
#include <string>
#include <vector>

namespace fla {
class Frame;
class Shape;
class SymbolInstance;
}

/// The Bone tool (M): drag from one symbol instance to another to join them
/// with a bone. The first bone makes an armature: the two instances move to
/// a new pose layer, each turning about a joint where the bone starts or
/// ends. Dragging from an instance already in an armature adds a bone from
/// its joint. Dragging inside a shape bones the shape: it moves to a pose
/// layer in a symbol of its own, its points bound to their nearest bones,
/// and a drag from one of its joints adds a bone there. Clicking a bone sets
/// how far its joint may turn.
class BoneTool : public Tool
{
public:
    /// A bone to add, from where it starts on one instance to where it ends
    /// on another; or, in a shape, from where it starts in the shape (or at
    /// a joint of the shape's armature) to where it ends.
    struct Link
    {
        fla::Frame* fromFrame = nullptr;
        fla::SymbolInstance* from = nullptr;
        QPointF head;
        fla::Frame* toFrame = nullptr;
        fla::SymbolInstance* to = nullptr;
        QPointF tail;

        /// The shape a first bone is dragged in.
        fla::Frame* shapeFrame = nullptr;
        fla::Shape* shape = nullptr;
        /// The pose frame, its IKTree child and the joint of a boned
        /// shape's armature a bone more is dragged from.
        fla::Frame* armature = nullptr;
        size_t child = 0;
        int joint = -1;

        bool inShape() const { return shape || armature; }
    };

    BoneTool(std::function<void(PhoenixView&, const Link&)> addBone,
        std::function<void(PhoenixView&, fla::Frame*, size_t, int)> editJoint);

    QString name() const override { return "Bone"; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }
    void deactivate(PhoenixView& view) override;

    /// The joint of \a instance in the armature on \a frame, when it is in
    /// one.
    static bool jointOf(const fla::Frame* frame, const fla::SymbolInstance* instance, QPointF& joint);

    /// The joint of a boned shape's armature on show nearest \a documentPos,
    /// within a few pixels; false when there is none.
    static bool shapeJointAt(PhoenixView& view, const QPointF& documentPos, fla::Frame*& frame, size_t& child,
        int& joint, QPointF& at);

private:
    /// The symbol instance under \a documentPos and the frame it is in.
    static fla::SymbolInstance* instanceAt(PhoenixView& view, const QPointF& documentPos, fla::Frame*& frame);

    void editJoint(PhoenixView& view);

    std::function<void(PhoenixView&, const Link&)> _addBone;
    std::function<void(PhoenixView&, fla::Frame*, size_t, int)> _editJoint;
    bool _dragging = false;
    /// The bone pressed on, which a click without a drag edits.
    PhoenixView::IKBoneHit _bone;
    QPointF _press;
    Link _link;
    QPointF _cursor;
};

/// A boned shape on show, as the Bind tool works on it: its armature, the
/// shape's control points and the bones each follows.
struct BoundShape
{
    fla::Frame* frame = nullptr;
    size_t child = 0;
    fla::IKArmature armature;
    /// The ik container instance the shape is drawn by.
    fla::SymbolInstance* instance = nullptr;
    /// The container's shape on the span's first frame, which every frame
    /// bends from.
    fla::Shape* rest = nullptr;
    /// Its points where the frame on show has them, in document space.
    std::vector<QPointF> shown;
    /// Its points on the first frame, in the armature's space.
    std::vector<fla::Point> anchors;
    fla::ShapeBinding binding;
    int into = 0;

    /// Finds the boned shape of the armature in \a frame's \a child, \a
    /// into frames into its span. A binding Phoenix cannot follow point by
    /// point (one made elsewhere) is shown as binding to the nearest bones
    /// would make it.
    static bool find(fla::Frame* frame, size_t child, int into, BoundShape& found);
};

/// The Bind tool, in the Bone tool's slot: click a bone of a boned shape to
/// see the points that follow it, then click a point to bind it to the bone,
/// Shift-click to bind or unbind it, Ctrl-click to unbind it. A point bound
/// to several bones follows them all, evenly; Alt-click gives the chosen
/// bone a quarter more of it, Alt+Shift-click a quarter less.
class BindTool : public Tool
{
public:
    explicit BindTool(std::function<void(PhoenixView&, fla::Frame*, size_t, const fla::ShapeBinding&)> rebind);

    QString name() const override { return "Bind"; }
    QCursor cursor() const override { return Qt::PointingHandCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }
    void deactivate(PhoenixView& view) override;

    /// The bone chosen, by the joint at its tail, with its armature's frame
    /// and child; -1 when there is none.
    int chosenBone(fla::Frame*& frame, size_t& child) const
    {
        frame = _frame;
        child = _child;
        return _frame ? _bone : -1;
    }

private:
    std::vector<BoundShape> shapesOnShow(PhoenixView& view) const;

    std::function<void(PhoenixView&, fla::Frame*, size_t, const fla::ShapeBinding&)> _rebind;
    /// The bone chosen: its armature's frame and child, and the joint at
    /// its tail.
    fla::Frame* _frame = nullptr;
    size_t _child = 0;
    int _bone = -1;
};
