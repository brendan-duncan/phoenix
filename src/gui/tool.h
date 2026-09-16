#pragma once

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
    /// back to its own behaviour, which is how panning stays available.
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

    /// Whether the view should draw its selection bounding box while this tool
    /// is active. Tools that show their own handles turn it off, so the box does
    /// not sit on top of what the user is trying to grab.
    virtual bool showsSelectionBounds() const { return true; }

    /// Called when the tool stops being the active one, so it can abandon any
    /// gesture in progress.
    virtual void deactivate(PhoenixView& view) { (void)view; }
};
