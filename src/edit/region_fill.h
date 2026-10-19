#pragma once

#include "../data/point.h"
#include "../data/transform.h"

#include <memory>
#include <vector>

namespace fla {

class DrawingStyle;
class Frame;
class Layer;
class RegionSplitter;
class Shape;
class Timeline;

/// The paint bucket's bigger strokes: filling every region a scribble
/// passes over or a lasso goes round, and filling one region on every
/// drawing of a run of frames, finding it again on each by where it is or
/// by following it as it moves. Free of Qt.
namespace regionfill {

/// The index of a fill in \a shape matching the current fill color, adding
/// one when there is none. -1 when fills are switched off.
int ensureFill(Shape& shape, const DrawingStyle& style);

/// \a t undone. False when it squashes everything flat.
bool inverse(const Transform& t, Transform& result);

/// Every enclosed region of \a regions the path passes over, each once, in
/// the order it reaches them, the path sampled every \a step units.
std::vector<int> facesAlong(const RegionSplitter& regions, const std::vector<Point>& path, double step);

/// Every enclosed region of \a regions lying inside the closed \a loop.
std::vector<int> facesInside(const RegionSplitter& regions, const std::vector<Point>& loop);

/// Whether \a point is inside the closed polygon \a loop (even-odd).
bool insideLoop(const std::vector<Point>& loop, const Point& point);

/// Where a filled region lies on the stage: the middle of its area and how
/// much area it covers, to find it again on the next drawing.
struct Footprint
{
    Point centroid;
    double area = 0.0;
    bool valid = false;
};

/// The footprint of region \a face of a shape placed on the stage by
/// \a shapeToStage.
Footprint footprintOf(const RegionSplitter& regions, int face, const Transform& shapeToStage);

/// How well a region with footprint \a candidate stands for \a was on the
/// next drawing: lower is better; negative when it is too far off or too
/// different in size to be the same region. \a holdsCentroid says whether
/// the candidate covers the point \a was was centered on.
double matchScore(const Footprint& was, const Footprint& candidate, bool holdsCentroid);

/// What Fill Across Frames is asked to do.
struct AcrossSettings
{
    /// The fill, which each drawing's shape gains when it lacks it.
    const DrawingStyle* style = nullptr;
    /// Where the click was, on the stage.
    Point stagePoint;
    /// How wide a gap in an outline may be closed, in stage units; 0 for none.
    double gap = 0.0;
    /// Follow the region as it moves from drawing to drawing, rather than
    /// filling whatever region is under the same point.
    bool follow = false;
};

/// One drawing's new paint.
struct DrawingFill
{
    /// The frame number the drawing starts on.
    int frameIndex = 0;
    /// The keyframe painted. Null when a keyframe has to be made there
    /// first, as on a color art layer whose paint did not change with the
    /// lines: the new keyframe copies the span's, and the paint goes on
    /// that copy.
    Frame* frame = nullptr;
    /// Which of the keyframe's elements is the shape painted, -1 for a new
    /// shape added to it.
    int elementIndex = -1;
    /// The shape painted, when \a frame is given and it is not new.
    Shape* target = nullptr;
    /// Its new contents.
    std::unique_ptr<Shape> after;
};

struct AcrossResult
{
    std::vector<DrawingFill> fills;
    /// The drawings on which no region matched, by frame number.
    std::vector<int> missed;
    /// Drawings whose region already had the fill.
    int unchanged = 0;
};

/// The frame numbers where the drawings of \a layer seen between frames
/// \a first and \a last begin, in order: its keyframes, or on a color art
/// layer the keyframes of its line layer.
std::vector<int> drawingsIn(const Timeline& timeline, Layer& layer, int first, int last);

/// Fills the region at the click on each of \a drawings (frame numbers from
/// drawingsIn), starting with \a startDrawing, the drawing clicked, and
/// going out from it both ways so each drawing can follow from its
/// neighbor. Nothing is changed: the caller applies the fills.
AcrossResult fillAcross(const Timeline& timeline, Layer& layer, const std::vector<int>& drawings,
    int startDrawing, const AcrossSettings& settings);

} // namespace regionfill

} // namespace fla
