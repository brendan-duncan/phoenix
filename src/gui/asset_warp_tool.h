#pragma once

#include "tool.h"

#include "../edit/asset_warp.h"

#include <QPointF>
#include <QTransform>

#include <functional>

#include <string>
#include <utility>
#include <vector>

namespace fla {
class CommandStack;
class Element;
class Frame;
class SymbolInstance;
}

/// The Asset Warp tool (Shift+W), for puppets: click the art to place a pin, drag
/// a pin to bend the art around the others, Alt-click a pin (or select it
/// and press Delete) to take it away. Each change is one undo step, kept on
/// the instance as FLA files keep a puppet's pins.
class AssetWarpTool : public Tool
{
public:
    /// \a status shows a message in the status bar. \a makePuppet turns art
    /// (a shape, a bitmap, a group or a graphic) into a puppet in its place,
    /// as one undo step, and gives the puppet's instance; null when it
    /// cannot.
    AssetWarpTool(fla::CommandStack& commands, std::function<void(const QString&)> status,
        std::function<fla::SymbolInstance*(fla::Element* art, fla::Frame* frame)> makePuppet = {});

    QString name() const override { return "Asset Warp"; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool keyPress(PhoenixView& view, QKeyEvent* event) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }
    /// Ctrl-click joins two pins with a bone, so ctrl does not call up the arrow.
    bool usesControlKey() const override { return true; }
    void deactivate(PhoenixView& view) override;

private:
    using Attributes = std::vector<std::pair<std::string, std::string>>;

    std::vector<fla::WarpPin> pinsOf() const;
    /// The pin nearest \a documentPos within reach, by index, or -1.
    int pinAt(PhoenixView& view, const QPointF& documentPos) const;
    /// Writes \a pins onto the puppet, live.
    void apply(PhoenixView& view, const std::vector<fla::WarpPin>& pins);
    /// Makes what changed since \a before one undo step.
    void commit(PhoenixView& view, const Attributes& before, const char* name);

    fla::CommandStack& _commands;
    std::function<void(const QString&)> _status;
    std::function<fla::SymbolInstance*(fla::Element*, fla::Frame*)> _makePuppet;
    fla::SymbolInstance* _puppet = nullptr;
    /// The puppet's own space to the document's, from when it was picked.
    QTransform _toDocument;
    int _selected = -1;
    bool _dragging = false;
    /// A Shift-drag turns the pin; the angle the cursor started at, less
    /// the pin's turn then.
    bool _turning = false;
    double _turnStart = 0.0;
    Attributes _before;
};
