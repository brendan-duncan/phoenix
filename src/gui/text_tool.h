#pragma once

#include "tool.h"

#include "../data/static_text.h"

#include <QPointF>
#include <QRectF>
#include <QTransform>

#include <cstdint>
#include <string>

namespace fla {
class CommandStack;
class DrawingStyle;
class Frame;
class Selection;
}

/// Animate's text tool.
///
/// A click on empty stage opens an auto-expanding text box there; a drag
/// opens a fixed-width one that wraps. Typing fills the box, Return starts a
/// new line, and clicking an existing text with the tool puts the caret in
/// it. Dragging across the text, shift with the arrow keys, a double-click on
/// a word or ctrl+A select a range, which typing replaces and which the font,
/// size and fill color in Properties restyle. Dragging the box's handle sets
/// a fixed width; double-clicking it lets the box grow again. Escape,
/// clicking elsewhere or changing tool finishes the edit: a new box with
/// nothing typed vanishes, otherwise it becomes a text element in the current
/// frame, selected, as one undo step.
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

    bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    bool capturesKeyboard() const override { return _text != nullptr; }

    /// Composed text from an input method shows underlined at the caret
    /// until it is committed, and is typed in when it is.
    bool inputMethod(PhoenixView& view, QInputMethodEvent* event) override;
    QVariant inputMethodQuery(const PhoenixView& view, Qt::InputMethodQuery query) const override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    void deactivate(PhoenixView& view) override;

    /// Whether a text box is open for typing.
    bool isEditing() const { return _text != nullptr; }

    /// Restyle the selected range of the open box. Each returns false when
    /// no box is open or nothing is selected, so the caller can treat the
    /// change as a setting for new text instead.
    bool applyFace(const std::string& face);
    bool applySize(double size);
    bool applyColor(const uint8_t color[4]);

    /// Changes the open box's kind: static, dynamic or input text.
    bool applyTextType(fla::StaticText::TextType type);

    /// Any text setting by name (see fla::textstyle): a character setting
    /// on the selected characters, a paragraph setting on the paragraphs
    /// the selection or caret is in, a field setting on the box. False when
    /// no box is open, or a character setting finds nothing selected.
    bool applyStyle(const std::string& key, const std::string& value);

    /// The open box, for Properties to show its settings; null when none.
    const fla::StaticText* editedText() const { return _text; }
    /// Where the caret is, or the selection starts, in the joined text.
    int caretIndex() const;

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

    /// The selected range, in joined-text offsets, or false when there is
    /// only a caret.
    bool selectedRange(int& from, int& to) const;

    /// Removes the selected range, leaving the caret where it began.
    void deleteSelection();

    /// Removes a range of the joined text.
    void removeRange(int from, int to);

    /// Splits the runs at an offset so a run boundary falls there.
    void splitRunAt(int offset);

    /// Restyles the runs covering the selection with \a change.
    template <typename Change>
    bool restyleSelection(Change change);

    /// Joins neighboring runs that have come to share one style.
    void mergeRuns();

    /// Where the box's handle sits, in the text's own coordinates.
    QPointF handlePosition() const;

    /// Whether a point in the text's coordinates is on the box handle.
    bool onHandle(PhoenixView& view, const QPointF& local) const;

    /// Writes the measured box back into the text, which is what gets saved.
    void measure();

    void refresh(PhoenixView& view);

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    fla::StaticText* _text = nullptr;
    fla::StaticText* _before = nullptr;
    fla::Frame* _frame = nullptr;
    PhoenixView* _view = nullptr;
    /// Whether the box is new and still the tool's, not yet in any frame.
    bool _owned = false;
    QTransform _toDocument;
    int _caret = 0;
    /// The other end of the selection, or -1 for a caret alone.
    int _anchor = -1;
    bool _selecting = false;
    bool _resizing = false;

    /// What an input method is composing, not yet typed in.
    QString _preedit;

    bool _pressed = false;
    QPointF _pressPos;
    QPointF _dragPos;
};
