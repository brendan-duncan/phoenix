#pragma once

#include "tool.h"

#include "../edit/three_d.h"

#include <QPointF>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fla {
class CommandStack;
class SymbolInstance;
}

/// The 3D Rotation tool (W) and the 3D Translation tool (G), for movie
/// clips. Rotation: drag across to turn the clip about its vertical axis,
/// up and down to tip it about its horizontal one; Alt-click sets it flat
/// again. Translation: drag up to push the clip into the stage, down to
/// bring it nearer. Each drag is one undo step.
class ThreeDTool : public Tool
{
public:
    enum class Mode
    {
        Rotate,
        Translate
    };

    /// \a status shows a message in the status bar.
    ThreeDTool(Mode mode, fla::CommandStack& commands, std::function<void(const QString&)> status);

    QString name() const override { return _mode == Mode::Rotate ? "3D Rotation" : "3D Translation"; }
    QCursor cursor() const override { return _mode == Mode::Rotate ? Qt::SizeAllCursor : Qt::SizeVerCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    void deactivate(PhoenixView& view) override;

private:
    using Attributes = std::vector<std::pair<std::string, std::string>>;

    void apply(PhoenixView& view, const fla::threed::Pose& pose);
    void commit(PhoenixView& view);

    Mode _mode;
    fla::CommandStack& _commands;
    std::function<void(const QString&)> _status;
    fla::SymbolInstance* _clip = nullptr;
    bool _dragging = false;
    QPointF _grab;
    fla::threed::Pose _start;
    Attributes _before;
    /// Where the clip turns, on the stage, for the overlay.
    QPointF _center;
};
