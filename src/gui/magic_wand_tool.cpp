#include "magic_wand_tool.h"

#include "phoenix_view.h"

#include "../data/bitmap_fill.h"
#include "../data/shape.h"
#include "../edit/drawing_style.h"
#include "../edit/magic_wand.h"
#include "../edit/region_split.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

/// More corners than this make cutting the fill slow, so a busier area is
/// tidied roughly instead.
constexpr size_t kMostCorners = 4000;

} // namespace

bool MagicWandTool::mousePress(PhoenixView& view, QMouseEvent* event, const QPointF& documentPos)
{
    if (event->button() != Qt::LeftButton)
        return false;
    const bool additive = (event->modifiers() & Qt::ShiftModifier) != 0;

    // The topmost fill under the click; the wand works only when it is a
    // bitmap's.
    const double tolerance = view.pickTolerance();
    const QRectF probe(documentPos.x() - tolerance, documentPos.y() - tolerance, 2.0 * tolerance, 2.0 * tolerance);
    std::vector<fla::Element*> under = view.elementsIn(probe);
    fla::Shape* shape = nullptr;
    const fla::BitmapFill* fill = nullptr;
    QTransform shapeToDocument;
    QPointF local;
    int wandFill = -1;
    for (auto it = under.rbegin(); it != under.rend() && !shape; ++it)
    {
        if (!*it || (*it)->elementType() != fla::Element::Type::Shape)
            continue;
        fla::Shape* candidate = static_cast<fla::Shape*>(*it);
        const fla::Transform& t = candidate->transform;
        const QTransform toDocument(t.m11, t.m12, t.m21, t.m22, t.tx, t.ty);
        bool invertible = false;
        const QPointF at = toDocument.inverted(&invertible).map(documentPos);
        if (!invertible)
            continue;
        fla::RegionSplitter regions(*candidate);
        const int face = regions.faceAt(fla::Point(at.x(), at.y()));
        if (face < 0)
            continue;
        const int fillIndex = regions.faceFill(face);
        if (fillIndex < 0)
            continue;
        const fla::FillStyle* style = candidate->getFillStyleByIndex(fillIndex);
        if (!style)
            continue;
        // The first fill found is the one showing; a plain fill stops it.
        if (style->type() != fla::FillStyle::Type::BitmapFill)
            break;
        shape = candidate;
        wandFill = fillIndex;
        fill = static_cast<const fla::BitmapFill*>(style);
        shapeToDocument = toDocument;
        local = at;
    }
    if (!shape || !fill)
    {
        if (!additive)
            view.selectRegionsIn(QPainterPath(), false);
        return true;
    }

    const QImage image = view.libraryBitmap(QString::fromStdString(fill->bitmapPath)).toImage()
        .convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        return true;
    const int width = image.width();
    const int height = image.height();

    // The fill's matrix takes the bitmap's pixels to the shape, in twips.
    const fla::Transform& m = fill->transform;
    const double s = 1.0 / 20.0;
    const QTransform pixelToShape(m.m11 * s, m.m12 * s, m.m21 * s, m.m22 * s, m.tx, m.ty);
    bool invertible = false;
    const QPointF pixel = pixelToShape.inverted(&invertible).map(local);
    if (!invertible)
        return true;
    // A tiled fill repeats the bitmap; the wand works in the tile clicked.
    const double tileX = fill->bitmapIsClipped ? 0.0 : std::floor(pixel.x() / width);
    const double tileY = fill->bitmapIsClipped ? 0.0 : std::floor(pixel.y() / height);
    const int x = static_cast<int>(std::floor(pixel.x() - tileX * width));
    const int y = static_cast<int>(std::floor(pixel.y() - tileY * height));
    if (x < 0 || y < 0 || x >= width || y >= height)
        return true;

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    for (int row = 0; row < height; ++row)
        std::memcpy(pixels.data() + static_cast<size_t>(row) * width, image.constScanLine(row),
            static_cast<size_t>(width) * 4);
    const int threshold = std::max(0, std::min(200, _style.wandThreshold));
    const auto smoothing = static_cast<fla::MagicWand::Smoothing>(std::max(0, std::min(3, _style.wandSmoothing)));
    std::vector<uint8_t> mask = fla::MagicWand::region(pixels.data(), width, height, x, y, threshold);
    // Smoothing takes the strands and gaps of a pixel or two out of the area
    // first, as well as rounding its outline.
    const int strand = smoothing == fla::MagicWand::Smoothing::Smooth ? 2
        : smoothing == fla::MagicWand::Smoothing::Normal ? 1 : 0;
    if (strand > 0)
    {
        std::vector<uint8_t> tidied = fla::MagicWand::tidy(mask, width, height, strand);
        // Unless that takes the very pixel clicked.
        if (tidied[static_cast<size_t>(y) * width + x])
            mask = std::move(tidied);
    }
    std::vector<std::vector<fla::Point>> loops = fla::MagicWand::outline(mask, width, height);

    const auto tidy = [&loops](fla::MagicWand::Smoothing how) {
        std::vector<std::vector<fla::Point>> tidied;
        size_t corners = 0;
        for (const auto& loop : loops)
        {
            tidied.push_back(fla::MagicWand::smooth(loop, how));
            corners += tidied.back().size();
        }
        return std::make_pair(tidied, corners);
    };
    auto result = tidy(smoothing);
    if (result.second > kMostCorners && smoothing != fla::MagicWand::Smoothing::Rough)
        result = tidy(fla::MagicWand::Smoothing::Rough);

    // Where the area runs along the bitmap's edge and the fill ends there
    // too, its outline would lie on the shape's own edge, which the cut
    // cannot tell apart; there it is taken a little way past the edge.
    // Between tiles, inside the fill, it stays on the line.
    {
        const fla::RegionSplitter regions(*shape);
        const double margin = 2.0;
        const auto paints = [&](double px, double py) {
            const QPointF at = pixelToShape.map(QPointF(px + tileX * width, py + tileY * height));
            const int face = regions.faceAt(fla::Point(at.x(), at.y()));
            return face >= 0 && regions.faceFill(face) == wandFill;
        };
        for (auto& loop : result.first)
        {
            for (fla::Point& corner : loop)
            {
                double dx = 0.0;
                double dy = 0.0;
                if (corner.x <= 1.0e-6)
                    dx = -margin;
                else if (corner.x >= width - 1.0e-6)
                    dx = margin;
                if (corner.y <= 1.0e-6)
                    dy = -margin;
                else if (corner.y >= height - 1.0e-6)
                    dy = margin;
                if ((dx != 0.0 || dy != 0.0) && !paints(corner.x + dx, corner.y + dy))
                {
                    corner.x += dx;
                    corner.y += dy;
                }
            }
        }
    }

    // The area in the space the tools work in, holes and all.
    const QTransform toDocument = pixelToShape * shapeToDocument;
    QPainterPath area;
    area.setFillRule(Qt::OddEvenFill);
    for (const auto& loop : result.first)
    {
        if (loop.size() < 3)
            continue;
        QPolygonF polygon;
        for (const fla::Point& corner : loop)
            polygon << toDocument.map(QPointF(corner.x + tileX * width, corner.y + tileY * height));
        area.addPolygon(polygon);
        area.closeSubpath();
    }
    if (area.isEmpty())
        return true;
    view.selectRegionsIn(area, additive, shape);
    return true;
}
