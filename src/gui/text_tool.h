#pragma once

#include "tool.h"

#include <QPointF>
#include <QRectF>
#include <QTransform>

namespace fla {
class CommandStack;
class DrawingStyle;
class Frame;
class Selection;
class StaticText;
}

/// Animate's text tool.
///
/// A click on empty stage opens an auto-expanding text box there; a drag
/// opens a fixed-width one that wraps. Typing fills the box, Return starts a
/// new line, and clicking an existing text with the tool puts the caret in
/// it. Escape, clicking elsewhere or changing tool finishes the edit: a new
/// box with nothing typed vanishes, otherwise it becomes a static text
/// element in the current frame, selected, as one undo step.
class TextTool : public Tool
{
public:
    TextTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    ~TextTool() override;

    QString name() const override { return "Text"; }

    QCursor cursor() const override { return Qt::IBeamCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    bool capturesKeyboard() const override { return _text != nullptr; }

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

    /// Whether a text box is open for typing.
    bool isEditing() const { return _text != nullptr; }

private:
    /// Opens a new box at a point, fixed to a width when one is given.
    void beginNew(PhoenixView& view, const QPointF& documentPos, double width);

    /// Opens an existing text for editing with the caret nearest a point.
    void beginExisting(PhoenixView& view, fla::StaticText* text, fla::Frame* frame,
        const QTransform& toDocument, const QPointF& documentPos);

    /// Ends the edit: adds a new text, records a change to an old one, or
    /// throws away an empty new box.
    void commit(PhoenixView& view);

    void insertText(const QString& text);
    void deleteBackward();
    void deleteForward();

    /// Writes the measured box back into the text, which is what gets saved.
    void measure();

    void refresh(PhoenixView& view);

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    fla::StaticText* _text = nullptr;
    fla::StaticText* _before = nullptr;
    fla::Frame* _frame = nullptr;
    /// Whether the box is new and still the tool's, not yet in any frame.
    bool _owned = false;
    QTransform _toDocument;
    int _caret = 0;

    bool _pressed = false;
    QPointF _pressPos;
    QPointF _dragPos;
};
