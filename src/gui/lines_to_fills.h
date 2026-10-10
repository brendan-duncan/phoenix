#pragma once

namespace fla {
class Shape;
}

/// Animate's Modify > Shape > Convert Lines to Fills: every stroke on the
/// shape becomes a filled region of the stroke's color, the shape's
/// strokes are dropped, and the new fills are merged over the shape's own
/// as a drawn fill would be. Works on the shape in place; false when it had
/// no strokes to convert.
bool convertLinesToFills(fla::Shape& shape);
