#pragma once

#include "../play/display/backend.h"
#include "../play/display/bitmap_filter.h"
#include "../play/display/graphics.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace fla {
class StaticText;
namespace display {
class Bitmap;
class DisplayObject;
class DisplayObjectContainer;
class TextField;
class TimelineArt;
} // namespace display
} // namespace fla

/// Draws the display engine's objects (src/play/display) with QPainter, as
/// Test Movie shows a movie's display list: each object with its transform,
/// alpha and color transform, filters, blend mode, mask and scroll rect,
/// its Graphics, bitmap or text, and its children in order. What a timeline
/// placed (timeline art, a movie clip's frame) is drawn by whatever draws
/// the movie's timelines, through drawTimeline. It is the engine's Backend
/// too: BitmapData.draw, the filters the engine does not work itself, and
/// text laid out in real fonts by TextLayout.
class DisplayPainter : public fla::display::Backend
{
public:
    DisplayPainter();
    ~DisplayPainter() override;

    /// Draws what a timeline placed, in the object's own space: timeline
    /// art, or a timeline-backed movie clip's current frame.
    std::function<void(QPainter&, const fla::display::DisplayObject&)> drawTimeline;
    /// The box timeline art covers, and whether it is at a point, in its
    /// own space.
    std::function<QRectF(const fla::display::TimelineArt&, bool strokes)> timelineArtBounds;
    std::function<bool(const fla::display::TimelineArt&, double x, double y)> timelineArtHits;

    /// From the stage to the device being drawn on, for masks, which are
    /// drawn where they are on the stage.
    QTransform stageToDevice;

    /// Draws \a object over \a painter, whose transform is its parent's
    /// space on the device.
    void draw(QPainter& painter, const fla::display::DisplayObject& object);
    /// Its own art and children, in its own space (no transform, alpha or
    /// effects of its own).
    void drawContent(QPainter& painter, const fla::display::DisplayObject& object);
    /// A text field's box, text, selection and caret, in its own space.
    void drawText(QPainter& painter, const fla::display::TextField& field);
    /// A Graphics' fills and strokes.
    void drawGraphics(QPainter& painter, const fla::display::Graphics& graphics);

    /// Renders \a object into an image the size of \a area (in the space
    /// \a toArea maps from the object's parent), transparent around it.
    QImage render(const fla::display::DisplayObject& object, const QTransform& parentToImage, const QSize& size);

    /// The pixels of a BitmapData as an image, without copying them.
    static QImage imageOf(const fla::display::BitmapData& data);
    /// \a filters applied in turn to \a image (premultiplied, with room
    /// round it): those the core works (color matrix, convolution) here,
    /// the rest by Filters::apply. \a scale is device pixels per stage
    /// pixel.
    static void applyFilters(QImage& image, const std::vector<fla::display::BitmapFilter>& filters, double scale);
    /// How far \a filters reach past an object, in device pixels.
    static double filterMargin(const std::vector<fla::display::BitmapFilter>& filters, double scale);
    static void applyColorTransform(QImage& image, const fla::display::ColorTransform& transform);
    /// A text field as the static text TextLayout lays out: one run per
    /// stretch of one format, its box at (0, 0).
    static fla::StaticText textOf(const fla::display::TextField& field);

    // Backend.
    void draw(fla::display::BitmapData& target, const fla::display::DisplayObject& source,
        const fla::display::Matrix& matrix, const fla::display::ColorTransform* colorTransform,
        fla::display::BlendMode blendMode, const fla::display::Rect* clipRect, bool smoothing) override;
    bool applyFilter(fla::display::BitmapData& target, const fla::display::BitmapData& source,
        const fla::display::Rect& sourceRect, const fla::display::Point& destPoint,
        const fla::display::BitmapFilter& filter) override;
    fla::display::Rect artBounds(const fla::display::TimelineArt& art, bool strokes) override;
    bool artHits(const fla::display::TimelineArt& art, double x, double y) override;
    std::vector<fla::display::TextLine> layoutText(const fla::display::TextField& field) override;
    int textIndexAt(const fla::display::TextField& field, double x, double y) override;
    fla::display::Rect textCharBox(const fla::display::TextField& field, int index) override;

private:
    /// Draws \a object on its own into an image, then its color transform,
    /// filters and mask on that, then the image over \a painter with its
    /// blend mode.
    void drawIsolated(QPainter& painter, const fla::display::DisplayObject& object, const QTransform& toDevice);
    void drawBitmap(QPainter& painter, const fla::display::Bitmap& bitmap);
    QBrush brushOf(const fla::display::FillStyle& fill) const;
    /// A path's steps as a QPainterPath; strokes that end where they start
    /// are closed.
    static QPainterPath pathOf(const fla::display::GraphicsPath& path, bool fill);

    /// Each Graphics' paths, made again when its revision moves on.
    struct PathCache
    {
        uint64_t revision = 0;
        std::vector<QPainterPath> paths;
    };
    std::map<const fla::display::Graphics*, PathCache> _paths;
    /// How deep the drawing is inside objects with the Layer blend mode.
    int _layerDepth = 0;
    /// How deep draw has called itself, against cycles a script makes.
    int _depth = 0;
};
