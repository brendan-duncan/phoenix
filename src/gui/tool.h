#pragma once

#include <QVariant>

#include <QInputMethodEvent>

#include <QCursor>
#include <QPainter>
#include <QString>
#include <QTransform>

class QMouseEvent;
class QKeyEvent;
class PhoenixView;

namespace fla {
class Element;
class Frame;
}

/// What a mouse press landed on, in document space.
struct HitResult
{
    enum class Part
    {
        None,
        /// Inside a filled region.
        Fill,
        /// On a stroked edge.
        Stroke,
        /// Within grabbing distance of an anchor or control point.
        Anchor
    };

    /// The element that was hit, or null for empty stage.
    fla::Element* element = nullptr;

    /// The frame the element was found in, which is what an edit has to modify.
    fla::Frame* frame = nullptr;

    Part part = Part::None;

    /// Maps the element's own coordinates to document coordinates. An edit
    /// expressed in document space needs the inverse of this.
    QTransform elementToDocument;

    bool isEmpty() const { return element == nullptr; }

    explicit operator bool() const { return element != nullptr; }
};

/// A stage tool: the thing that decides what mouse input means.
///
/// Exactly one tool is active at a time. PhoenixView routes its mouse and key
/// events here rather than acting on them itself, so adding a tool does not mean
/// touching the view.
///
/// A tool may draw on top of the stage through paintOverlay(), which runs with
/// the painter already in document coordinates.
class Tool
{
public:
    virtual ~Tool() = default;

    /// Name shown in the toolbar and used for undo entries.
    virtual QString name() const = 0;

    /// Cursor to show while this tool is active and not mid-gesture.
    virtual QCursor cursor() const { return Qt::ArrowCursor; }

    /// Handles a press at \a documentPos. Returning false lets the view fall
    /// back to its own behavior, which is how panning stays available.
    virtual bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
    {
        (void)view; (void)event; (void)documentPos;
        return false;
    }

    virtual bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
    {
        (void)view; (void)event; (void)documentPos;
        return false;
    }

    virtual bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
    {
        (void)view; (void)event; (void)documentPos;
        return false;
    }

    /// Handles a double click. Returning false leaves it to be treated as an
    /// ordinary press.
    virtual bool mouseDoubleClick(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
    {
        (void)view; (void)event; (void)documentPos;
        return false;
    }

    virtual bool keyPress(PhoenixView& view, QKeyEvent* event)
    {
        (void)view; (void)event;
        return false;
    }

    /// Draws whatever the tool needs on top of the stage. The painter is in
    /// document coordinates; \a scale is how many document units a screen pixel
    /// covers, for keeping handles a constant size on screen.
    virtual void paintOverlay(PhoenixView& view, QPainter& painter, double scale)
    {
        (void)view; (void)painter; (void)scale;
    }

    /// Whether ctrl belongs to the tool's own gestures, as it does for the
    /// free transform tool's Distort, rather than calling up the temporary
    /// arrow tool while it is held.
    virtual bool usesControlKey() const { return false; }

    /// Whether the tool is taking typed text, so that single-key tool
    /// shortcuts must not fire. The text tool says yes while a box is open.
    virtual bool capturesKeyboard() const { return false; }

    /// Text from an input method (Chinese, Japanese, Korean...): the text
    /// being composed and the text committed. False when the tool takes no
    /// text.
    virtual bool inputMethod(PhoenixView& view, QInputMethodEvent* event)
    {
        (void)view;
        (void)event;
        return false;
    }

    /// What an input method asks of the tool, such as where the caret is in
    /// the view's coordinates; empty when the tool takes no text.
    virtual QVariant inputMethodQuery(const PhoenixView& view, Qt::InputMethodQuery query) const
    {
        (void)view;
        (void)query;
        return QVariant();
    }

    /// Whether Enter finishes something the tool has under way (a pen path,
    /// a polygon lasso), so that it does not play the timeline.
    virtual bool wantsEnter() const { return false; }

    /// Whether the view should draw its selection bounding box while this tool
    /// is active. Tools that show their own handles turn it off, so the box does
    /// not sit on top of what the user is trying to grab.
    virtual bool showsSelectionBounds() const { return true; }

    /// Whether the view should draw the dot screen over selected merge-drawn
    /// shapes. The subselection tool shows anchors instead.
    virtual bool showsFillHighlight() const { return true; }

    /// Called when the tool stops being the active one, so it can abandon any
    /// gesture in progress.
    virtual void deactivate(PhoenixView& view) { (void)view; }
};
