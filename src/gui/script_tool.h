#pragma once

#include "tool.h"

#include <QIcon>

#include <memory>

namespace jsfl {
class Host;
class ToolScript;
} // namespace jsfl

/// A JSFL tool from the Tools folder, as a stage tool: mouse and keys go to
/// its script's handlers, and what it draws on fl.drawingLayer shows over
/// the stage while it works.
class ScriptTool : public Tool
{
public:
    /// The tool scripts are loaded with \a host, which their errors are
    /// traced through to the Output panel.
    ScriptTool(jsfl::Host& host, std::unique_ptr<jsfl::ToolScript> script);
    ~ScriptTool() override;

    QString name() const override;
    QString toolTip() const;
    QIcon icon() const;
    QCursor cursor() const override;

    /// Picked from the toolbox: the script's activate().
    void activate();
    void deactivate(PhoenixView& view) override;

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;
    bool keyPress(PhoenixView& view, QKeyEvent* event) override;
    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;
    bool showsSelectionBounds() const override { return false; }

    /// Whether the tool has settings of its own (an options file), and the
    /// dialog that sets them.
    bool hasOptions() const;
    void showOptions(QWidget* parent);

private:
    void call(PhoenixView* view, const char* function, const QPointF& at, Qt::KeyboardModifiers modifiers, int key = 0);

    jsfl::Host& _host;
    std::unique_ptr<jsfl::ToolScript> _script;
    QPointF _down;
    bool _pressed = false;
    bool _active = false;
};
