#pragma once

#include <cstdint>

namespace fla {

class DOMElement;
class FillStyle;
class StrokeStyle;

/// The fill and stroke a new object is drawn with, plus the settings the shape
/// tools need.
///
/// One instance lives for the session and is what the properties panel edits, so
/// the next rectangle picks up whatever the last one was given.
///
/// Free of Qt: it holds document colours, not GUI ones.
class DrawingStyle
{
public:
    bool hasFill = true;
    uint8_t fillColor[4] = {0, 153, 255, 255};

    bool hasStroke = true;
    uint8_t strokeColor[4] = {0, 0, 0, 255};
    double strokeWeight = 1.0;

    /// How the pencil tidies a freehand stroke.
    enum class PencilMode
    {
        /// Fit smooth curves through the stroke.
        Smooth,
        /// Fit straight runs, keeping the corners.
        Straighten,
        /// Keep the points as drawn.
        Ink
    };

    PencilMode pencilMode = PencilMode::Smooth;

    /// How far, in document units, the fitted stroke may stray from what was
    /// drawn. Larger means smoother and fewer anchors.
    double pencilTolerance = 2.0;

    /// PolyStar settings. Sides is the point count in star mode.
    int sides = 5;
    bool star = false;

    /// How far in the inner points of a star sit, as a fraction of the radius.
    double starInnerRatio = 0.5;

    /// Builds the fill for a new object. Returns null when fills are switched
    /// off. The caller owns the result.
    FillStyle* createFill(DOMElement* parent) const;

    /// Builds the stroke for a new object, or null when strokes are switched
    /// off. The caller owns the result.
    StrokeStyle* createStroke(DOMElement* parent) const;
};

} // namespace fla
