#pragma once

#include "tool.h"

#include "../data/point.h"
#include "../edit/curve_fit.h"
#include "../edit/editable_path.h"

#include <QPointF>
#include <vector>

namespace fla {
class CommandStack;
class DrawingStyle;
class Edge;
class Element;
class Frame;
class Selection;
class Shape;
}

/// Animate's pencil: draw freehand and get a tidy path.
///
/// The raw cursor positions are collected during the drag and fitted on release,
/// so a stroke that arrives as hundreds of points becomes a handful of anchors
/// placed where the line actually changes direction. Which kind of tidying
/// happens -- smoothing, straightening, or none at all -- comes from the pencil
/// mode in the properties panel.
class PencilTool : public Tool
{
public:
    PencilTool(fla::Selection& selection, fla::CommandStack& commandStack,
        const fla::DrawingStyle& style)
        : _selection(selection)
        , _commandStack(commandStack)
        , _style(style)
    {}

    QString name() const override { return "Pencil"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseMove(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool mouseRelease(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

    bool keyPress(PhoenixView& view, QKeyEvent* event) override;

    void paintOverlay(PhoenixView& view, QPainter& painter, double scale) override;

    bool showsSelectionBounds() const override { return false; }

    void deactivate(PhoenixView& view) override;

private:
    /// Fits the collected points and puts the result in the document.
    void finish(PhoenixView& view);

    /// The fitter settings for the pencil mode and smoothing, with the
    /// tolerance in screen pixels brought to document units at this zoom.
    fla::CurveFit::Options fitOptions(PhoenixView& view) const;

    /// The fitted stroke as it stands, for the live preview and the finish.
    fla::EditablePath fitted(PhoenixView& view) const;

    /// Builds the shape for a fitted stroke. The caller owns the result.
    fla::Element* createShape(fla::Frame* frame, const fla::EditablePath& path) const;

    fla::Selection& _selection;
    fla::CommandStack& _commandStack;
    const fla::DrawingStyle& _style;

    /// Raw cursor positions, in document space, in the order they arrived.
    std::vector<fla::Point> _points;

    bool _drawing = false;

    /// A stroke that began on an end of the selected shape's open path
    /// carries that path on instead of making a new shape: the path is
    /// remembered here and the stroke is joined to it on release.
    bool _extending = false;
    fla::Shape* _extendShape = nullptr;
    fla::Edge* _extendEdge = nullptr;
    size_t _extendPathIndex = 0;
    bool _extendAtStart = false;
    fla::EditablePath _extendBefore;

    /// Looks for an end of an open path on the selected shape under the
    /// press and, finding one, sets up the extension.
    void beginExtension(PhoenixView& view, const QPointF& documentPos);
};
