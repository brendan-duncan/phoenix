#pragma once

#include "tool.h"

namespace fla {
class DrawingStyle;
}

/// The Magic Wand, in the lasso's slot: click a bitmap that has been
/// broken apart into a fill, and the area of like color around the click is
/// cut out of the fill and selected, as the lasso would cut it. Shift adds
/// to the selection. The Properties panel sets the threshold (how far a
/// color may be from the clicked one, 0 to 200) and the smoothing of the
/// area's outline (Pixels, Rough, Normal, Smooth).
class MagicWandTool : public Tool
{
public:
    explicit MagicWandTool(const fla::DrawingStyle& style)
        : _style(style)
    {}

    QString name() const override { return "Magic Wand"; }

    QCursor cursor() const override { return Qt::CrossCursor; }

    bool mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos) override;

private:
    const fla::DrawingStyle& _style;
};
