#pragma once

#include "phoenix_view.h"
#include "tool.h"

#include <QPointF>

#include <functional>

namespace fla {
class Frame;
class SymbolInstance;
}

/// The Bone tool (M): drag from one symbol instance to another to join them
/// with a bone. The first bone makes an armature: the two instances move to
/// a new pose layer, each turning about a joint where the bone starts or
/// ends. Dragging from an instance already in an armature adds a bone from
/// its joint. Clicking a bone sets how far its joint may turn.
class BoneTool : public Tool
{
public:
    /// A bone to add, from where it starts on one instance to where it ends
    /// on another.
    struct Link
    {
        fla::Frame* fromFrame = nullptr;
        fla::SymbolInstance* from = nullptr;
        QPointF head;
        fla::Frame* toFrame = nullptr;
        fla::SymbolInstance* to = nullptr;
        QPointF tail;
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
