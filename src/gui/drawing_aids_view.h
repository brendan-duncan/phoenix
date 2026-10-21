#pragma once

#include "../edit/brush_stroke.h"
#include "../edit/drawing_aids.h"

#include <QPainterPath>
#include <QPointF>
#include <QTransform>

#include <vector>

class PhoenixView;
class QPainter;

/// The drawing aids on a view: the symmetry axis and the drawing guides, as
/// the freehand tools use them and the stage shows them. Both are kept in the
/// document (fla::aids has the details) in the stage's space; the tools draw
/// in the space of the symbol open, so everything here maps between the two.
namespace drawaids {

/// The document's symmetry: off, centered on the stage, when it has none.
fla::aids::Symmetry symmetry(const PhoenixView& view);
void setSymmetry(PhoenixView& view, const fla::aids::Symmetry& symmetry);

fla::aids::DrawingGuides guides(const PhoenixView& view);
void setGuides(PhoenixView& view, const fla::aids::DrawingGuides& guides);

/// The guide the Drawing Guides tool has picked, -1 for none.
int selectedGuide(const PhoenixView& view);
void setSelectedGuide(PhoenixView& view, int index);

QTransform toQt(const fla::Transform& transform);

/// The maps that copy a stroke across the symmetry axis, in the space the
/// tools draw in; none while symmetry is off.
std::vector<QTransform> copiesInScope(const PhoenixView& view);

/// The area \a path and its copies cover, as one path.
QPainterPath withCopies(const QPainterPath& path, const std::vector<QTransform>& copies);

/// Draws \a path and its copies, for a stroke's preview.
void drawWithCopies(QPainter& painter, const QPainterPath& path, const std::vector<QTransform>& copies);

/// A stroke's points, in the tools' space, pulled onto the drawing guide
/// it keeps to.
std::vector<fla::Point> snapped(const PhoenixView& view, const std::vector<fla::Point>& points);
std::vector<fla::BrushSample> snapped(const PhoenixView& view, const std::vector<fla::BrushSample>& samples);

/// While a stabilizer trails the pen: the circle the pen moves in freely
/// round the end of the line, and the string from the line to the pen.
/// \a radius is in the painter's units.
void paintStabilizer(QPainter& painter, const QPointF& pen, const QPointF& line, double radius, double scale);

/// The stage's units for one screen pixel.
double stagePixel(const PhoenixView& view);

/// What the Drawing Guides tool can take hold of.
struct Handle
{
    enum class Part
    {
        None,
        /// A guide's point a, b or c, or its second radius.
        A,
        B,
        C,
        Minor,
        /// The guide as a whole.
        Body,
        /// The symmetry axis's center, and the handle that turns it.
        SymmetryCenter,
        SymmetryTurn
    };
    Part part = Part::None;
    int guide = -1;
};

/// The symmetry axis's handle that turns it, on the stage.
fla::Point symmetryTurnHandle(const PhoenixView& view, const fla::aids::Symmetry& symmetry);

/// A guide's second-radius handle, on the stage.
fla::Point minorHandle(const fla::aids::DrawingGuide& guide);

/// What is under \a stagePos, handles before bodies, the symmetry axis
/// before the guides.
Handle handleAt(const PhoenixView& view, const QPointF& stagePos);

/// Has \a view show the aids, painted with its frame.
void attach(PhoenixView* view);

/// Shows a change to the aids.
void refresh(PhoenixView& view);

} // namespace drawaids
