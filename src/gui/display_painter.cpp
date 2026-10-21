#include "display_painter.h"

#include "filters.h"
#include "text_layout.h"

#include "../data/filter.h"
#include "../data/static_text.h"
#include "../play/display/bitmap_data.h"
#include "../play/display/display_object.h"
#include "../play/display/stage.h"
#include "../play/display/text_field.h"

#include <QLinearGradient>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

using namespace fla::display;

namespace {

QTransform toQ(const Matrix& m)
{
    return QTransform(m.a, m.b, m.c, m.d, m.tx, m.ty);
}

Matrix fromQ(const QTransform& t)
{
    return Matrix(t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy());
}

QColor colorOf(uint32_t rgb, double alpha)
{
    return QColor(static_cast<int>((rgb >> 16) & 0xff), static_cast<int>((rgb >> 8) & 0xff),
        static_cast<int>(rgb & 0xff), static_cast<int>(std::lround(std::max(0.0, std::min(1.0, alpha)) * 255.0)));
}

void setColor(uint8_t out[4], uint32_t rgb, double alpha)
{
    out[0] = static_cast<uint8_t>((rgb >> 16) & 0xff);
    out[1] = static_cast<uint8_t>((rgb >> 8) & 0xff);
    out[2] = static_cast<uint8_t>(rgb & 0xff);
    out[3] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(1.0, alpha)) * 255.0));
}

/// The filter as Phoenix's filters (src/gui/filters.cpp) take it, for
/// those it works.
fla::Filter toFla(const BitmapFilter& f)
{
    fla::Filter out;
    out.blurX = f.blurX;
    out.blurY = f.blurY;
    out.quality = std::max(1, f.quality);
    out.angle = f.angle;
    out.distance = f.distance;
    out.strength = f.strength;
    out.inner = f.inner;
    out.knockout = f.knockout;
    out.hideObject = f.hideObject;
    setColor(out.color, f.color, f.alpha);
    setColor(out.highlightColor, f.highlightColor, f.highlightAlpha);
    setColor(out.shadowColor, f.shadowColor, f.shadowAlpha);
    out.bevelType = f.bevelType;
    for (const Gradient::Stop& stop : f.stops)
    {
        fla::GradientEntry entry;
        entry.ratio = stop.ratio / 255.0;
        setColor(entry.color, stop.color, stop.alpha);
        out.entries.push_back(entry);
    }
    switch (f.type)
    {
    case BitmapFilter::Type::DropShadow: out.type = fla::Filter::Type::DropShadow; break;
    case BitmapFilter::Type::Glow: out.type = fla::Filter::Type::Glow; break;
    case BitmapFilter::Type::Bevel: out.type = fla::Filter::Type::Bevel; break;
    case BitmapFilter::Type::GradientGlow: out.type = fla::Filter::Type::GradientGlow; break;
    case BitmapFilter::Type::GradientBevel: out.type = fla::Filter::Type::GradientBevel; break;
    default: out.type = fla::Filter::Type::Blur; break;
    }
    return out;
}

QGradient::Spread spreadOf(Gradient::Spread spread)
{
    switch (spread)
    {
    case Gradient::Spread::Reflect: return QGradient::ReflectSpread;
    case Gradient::Spread::Repeat: return QGradient::RepeatSpread;
    default: return QGradient::PadSpread;
    }
}

/// The text field's lines: TextLayout's, as the engine numbers them.
std::vector<TextLine> linesOf(const fla::StaticText& text, const std::u32string& shown)
{
    std::vector<TextLine> lines;
    const TextLayout::Layout layout = TextLayout::layout(text);
    // TextLayout counts UTF-16 units; the engine counts code points.
    std::vector<int> unitToPoint;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        unitToPoint.push_back(static_cast<int>(i));
        if (shown[i] >= 0x10000)
            unitToPoint.push_back(static_cast<int>(i));
    }
    unitToPoint.push_back(static_cast<int>(shown.size()));
    const auto point = [&unitToPoint](int unit) {
        return unitToPoint[static_cast<size_t>(std::max(0, std::min(unit, static_cast<int>(unitToPoint.size()) - 1)))];
    };
    for (const TextLayout::Line& line : layout.lines)
    {
        TextLine out;
        out.start = point(line.start);
        const int end = point(line.start + line.length);
        out.length = end - out.start;
        // The return ending it is the line's.
        if (end < static_cast<int>(shown.size()) && shown[static_cast<size_t>(end)] == U'\r')
            ++out.length;
        out.x = line.x;
        out.y = line.top;
        out.width = line.width;
        out.height = line.height;
        out.ascent = line.ascent;
        out.descent = std::max(0.0, line.height - line.ascent);
        lines.push_back(out);
    }
    return lines;
}

/// The utf-16 offset of code point \a index in \a shown.
int unitOf(const std::u32string& shown, int index)
{
    int unit = 0;
    for (int i = 0; i < index && i < static_cast<int>(shown.size()); ++i)
        unit += shown[static_cast<size_t>(i)] >= 0x10000 ? 2 : 1;
    return unit;
}

/// How far down the field's text is scrolled, in its text space.
double scrollTop(const TextField& field)
{
    const std::vector<TextLine>& lines = field.lines();
    if (lines.empty())
        return 0.0;
    const int first = std::max(0, std::min(field.scrollV() - 1, static_cast<int>(lines.size()) - 1));
    return lines[static_cast<size_t>(first)].y - lines.front().y;
}

} // namespace

DisplayPainter::DisplayPainter() = default;
DisplayPainter::~DisplayPainter() = default;

QImage DisplayPainter::imageOf(const BitmapData& data)
{
    if (data.disposed())
        return QImage();
    return QImage(reinterpret_cast<const uchar*>(data.bits()), data.width(), data.height(), data.width() * 4,
        QImage::Format_ARGB32_Premultiplied);
}

void DisplayPainter::applyColorTransform(QImage& image, const ColorTransform& transform)
{
    if (transform.isIdentity())
        return;
    for (int y = 0; y < image.height(); ++y)
    {
        auto* line = reinterpret_cast<uint32_t*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = premultiplied(transform.apply(unpremultiplied(line[x])));
    }
}

double DisplayPainter::filterMargin(const std::vector<BitmapFilter>& filters, double scale)
{
    double total = 0.0;
    for (const BitmapFilter& filter : filters)
    {
        double l, t, r, b;
        filter.reach(l, t, r, b);
        total += std::max({l, t, r, b});
    }
    return std::ceil(total * scale);
}

void DisplayPainter::applyFilters(QImage& image, const std::vector<BitmapFilter>& filters, double scale)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (const BitmapFilter& filter : filters)
    {
        if (filter.type == BitmapFilter::Type::ColorMatrix)
            applyColorMatrix(reinterpret_cast<uint32_t*>(image.bits()), image.width(), image.height(),
                static_cast<int>(image.bytesPerLine() / 4), filter.matrix);
        else if (filter.type == BitmapFilter::Type::Convolution)
            applyConvolution(reinterpret_cast<uint32_t*>(image.bits()), image.width(), image.height(),
                static_cast<int>(image.bytesPerLine() / 4), filter);
        else if (filter.type == BitmapFilter::Type::DisplacementMap)
            applyDisplacementMap(reinterpret_cast<uint32_t*>(image.bits()), image.width(), image.height(),
                static_cast<int>(image.bytesPerLine() / 4), filter, scale);
        else
            image = Filters::apply(image, {toFla(filter)}, scale);
    }
}

void DisplayPainter::draw(QPainter& painter, const DisplayObject& object)
{
    // A mask shows only through what it masks.
    if (!object.visible() || object.maskedObject())
        return;
    if (_depth > 96)
        return;
    struct Depth
    {
        int& depth;
        Depth(int& d)
            : depth(d)
        {
            ++depth;
        }
        ~Depth() { --depth; }
    } depth(_depth);

    const QTransform toDevice = toQ(object.localToParent()) * painter.transform();
    const ColorTransform color = object.colorTransform();
    const bool isolate = !object.filters().empty() || !color.alphaOnly() || object.blendMode() != BlendMode::Normal ||
        object.mask() != nullptr;
    if (isolate)
    {
        drawIsolated(painter, object, toDevice);
        return;
    }
    const double alpha = std::max(0.0, std::min(1.0, color.alphaMultiplier));
    if (alpha <= 0.0)
        return;
    painter.save();
    painter.setTransform(toDevice);
    painter.setOpacity(painter.opacity() * alpha);
    if (const std::optional<Rect> scroll = object.scrollRect())
        painter.setClipRect(QRectF(scroll->x, scroll->y, scroll->width, scroll->height), Qt::IntersectClip);
    drawContent(painter, object);
    painter.restore();
}

void DisplayPainter::drawIsolated(QPainter& painter, const DisplayObject& object, const QTransform& toDevice)
{
    const Rect box = object.boundsThrough(fromQ(toDevice), true);
    if (box.width <= 0.0 && box.height <= 0.0)
        return;
    const double scale = std::sqrt(std::fabs(stageToDevice.determinant()));
    const double margin = filterMargin(object.filters(), scale > 0.0 ? scale : 1.0) + 2.0;
    QRect crop = QRectF(box.x - margin, box.y - margin, box.width + 2.0 * margin, box.height + 2.0 * margin).toAlignedRect();
    if (painter.device())
    {
        const QRect device(-static_cast<int>(margin), -static_cast<int>(margin),
            painter.device()->width() + 2 * static_cast<int>(margin),
            painter.device()->height() + 2 * static_cast<int>(margin));
        crop = crop.intersected(device);
    }
    if (crop.isEmpty() || static_cast<qint64>(crop.width()) * crop.height() > 64 * 1024 * 1024)
        return;

    QImage image(crop.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter inner(&image);
        inner.setRenderHints(painter.renderHints());
        inner.setTransform(toDevice * QTransform::fromTranslate(-crop.x(), -crop.y()));
        if (const std::optional<Rect> scroll = object.scrollRect())
            inner.setClipRect(QRectF(scroll->x, scroll->y, scroll->width, scroll->height));
        // A Layer group's picture is what alpha and erase objects inside it
        // cut into; any other isolated object starts a group of its own.
        const int outer = _layerDepth;
        _layerDepth = object.blendMode() == BlendMode::Layer ? outer + 1 : 0;
        drawContent(inner, object);
        _layerDepth = outer;
    }
    const ColorTransform color = object.colorTransform();
    applyColorTransform(image, color);
    if (!object.filters().empty())
        applyFilters(image, object.filters(), scale > 0.0 ? scale : 1.0);
    if (const DisplayObject* mask = object.mask())
    {
        // The mask where it is on the stage: its shape, all or nothing,
        // unless both cache as bitmaps (then its alpha by degrees).
        QImage coverage(crop.size(), QImage::Format_ARGB32_Premultiplied);
        coverage.fill(Qt::transparent);
        {
            QPainter cover(&coverage);
            cover.setRenderHint(QPainter::Antialiasing, true);
            cover.setTransform(toQ(mask->concatenatedMatrix()) * stageToDevice *
                QTransform::fromTranslate(-crop.x(), -crop.y()));
            drawContent(cover, *mask);
        }
        if (!(object.cacheAsBitmap() && mask->cacheAsBitmap()))
            for (int y = 0; y < coverage.height(); ++y)
            {
                auto* line = reinterpret_cast<uint32_t*>(coverage.scanLine(y));
                for (int x = 0; x < coverage.width(); ++x)
                    line[x] = (line[x] >> 24) >= 128 ? 0xffffffffu : 0u;
            }
        QPainter cut(&image);
        cut.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        cut.drawImage(0, 0, coverage);
    }
    painter.save();
    painter.resetTransform();
    Filters::composite(painter, crop.topLeft(), image, blendModeName(object.blendMode()), _layerDepth > 0);
    painter.restore();
}

void DisplayPainter::drawContent(QPainter& painter, const DisplayObject& object)
{
    if (const std::optional<uint32_t> back = object.opaqueBackground())
    {
        const Rect box = object.boundsThrough(Matrix(), true);
        painter.fillRect(QRectF(box.x, box.y, box.width, box.height), colorOf(*back, 1.0));
    }
    switch (object.kind())
    {
    case ObjectKind::Shape:
        drawGraphics(painter, static_cast<const Shape&>(object).graphics);
        break;
    case ObjectKind::Sprite:
    case ObjectKind::MovieClip:
    {
        const auto& sprite = static_cast<const Sprite&>(object);
        // A clip a timeline plays: its drawing, its frame and what scripts
        // added in it are drawn by what draws timelines.
        if (object.clip && drawTimeline)
        {
            drawTimeline(painter, object);
            break;
        }
        drawGraphics(painter, sprite.graphics);
        const std::vector<std::shared_ptr<DisplayObject>> children = sprite.children();
        for (const std::shared_ptr<DisplayObject>& child : children)
            if (child)
                draw(painter, *child);
        break;
    }
    case ObjectKind::Stage:
    {
        const std::vector<std::shared_ptr<DisplayObject>> children =
            static_cast<const DisplayObjectContainer&>(object).children();
        for (const std::shared_ptr<DisplayObject>& child : children)
            if (child)
                draw(painter, *child);
        break;
    }
    case ObjectKind::SimpleButton:
        if (const DisplayObject* state = static_cast<const SimpleButton&>(object).shownState())
            draw(painter, *state);
        break;
    case ObjectKind::Bitmap:
        drawBitmap(painter, static_cast<const Bitmap&>(object));
        break;
    case ObjectKind::TextField:
        drawText(painter, static_cast<const TextField&>(object));
        break;
    case ObjectKind::TimelineArt:
        if (drawTimeline)
            drawTimeline(painter, object);
        break;
    }
}

void DisplayPainter::drawBitmap(QPainter& painter, const Bitmap& bitmap)
{
    const std::shared_ptr<BitmapData> data = bitmap.bitmapData();
    if (!data || data->disposed())
        return;
    const QImage image = imageOf(*data);
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, bitmap.smoothing);
    // Snapped to whole device pixels when it is only moved.
    const QTransform t = painter.transform();
    if (bitmap.pixelSnapping != PixelSnapping::Never && t.type() <= QTransform::TxTranslate)
        painter.setTransform(QTransform::fromTranslate(std::round(t.dx()), std::round(t.dy())));
    painter.drawImage(QPointF(0, 0), image);
    painter.restore();
}

QBrush DisplayPainter::brushOf(const FillStyle& fill) const
{
    switch (fill.type)
    {
    case FillStyle::Type::Solid:
        return QBrush(colorOf(fill.color, fill.alpha));
    case FillStyle::Type::Gradient:
    {
        const Gradient& g = fill.gradient;
        QGradient gradient;
        if (g.radial)
            gradient = QRadialGradient(QPointF(0, 0), 819.2,
                QPointF(std::max(-1.0, std::min(1.0, g.focalPointRatio)) * 819.2, 0));
        else
            gradient = QLinearGradient(-819.2, 0, 819.2, 0);
        QGradientStops stops;
        double last = -1.0;
        for (const Gradient::Stop& stop : g.stops)
        {
            // Two stops at one ratio make a hard edge; Qt keeps only one
            // at a place, so the second goes a hair after.
            double at = stop.ratio / 255.0;
            if (at <= last)
                at = std::min(1.0, last + 1e-6);
            stops.append({at, colorOf(stop.color, stop.alpha)});
            last = at;
        }
        if (stops.isEmpty())
            return QBrush(Qt::NoBrush);
        gradient.setStops(stops);
        gradient.setSpread(spreadOf(g.spread));
        // Each channel blended on its own before alpha, as the player does.
        gradient.setInterpolationMode(QGradient::ComponentInterpolation);
        QBrush brush(gradient);
        brush.setTransform(toQ(g.matrix));
        return brush;
    }
    case FillStyle::Type::Bitmap:
    {
        if (!fill.bitmap || fill.bitmap->disposed())
            return QBrush(Qt::NoBrush);
        // A copy: the brush outlives this paint's look at the pixels.
        QBrush brush(imageOf(*fill.bitmap).copy());
        brush.setTransform(toQ(fill.bitmapMatrix));
        return brush;
    }
    default:
        return QBrush(Qt::NoBrush);
    }
}

QPainterPath DisplayPainter::pathOf(const GraphicsPath& path, bool fill)
{
    QPainterPath out;
    out.setFillRule(path.nonZero ? Qt::WindingFill : Qt::OddEvenFill);
    QPointF start;
    bool open = false;
    const auto closeIfBack = [&]() {
        // A stroke back where it started joins up there.
        if (open && !fill && out.elementCount() > 1 && QLineF(out.currentPosition(), start).length() < 1e-9)
            out.closeSubpath();
    };
    for (const PathStep& step : path.steps)
    {
        switch (step.op)
        {
        case PathStep::Op::Move:
            closeIfBack();
            start = QPointF(step.points[0].x, step.points[0].y);
            out.moveTo(start);
            open = true;
            break;
        case PathStep::Op::Line:
            out.lineTo(step.points[0].x, step.points[0].y);
            break;
        case PathStep::Op::Quad:
            out.quadTo(step.points[0].x, step.points[0].y, step.points[1].x, step.points[1].y);
            break;
        case PathStep::Op::Cubic:
            out.cubicTo(step.points[0].x, step.points[0].y, step.points[1].x, step.points[1].y, step.points[2].x,
                step.points[2].y);
            break;
        }
    }
    closeIfBack();
    return out;
}

void DisplayPainter::drawGraphics(QPainter& painter, const Graphics& graphics)
{
    const std::vector<GraphicsPath>& paths = graphics.paths();
    if (paths.empty())
        return;
    if (_paths.size() > 4096)
        _paths.clear();
    PathCache& cache = _paths[&graphics];
    if (cache.revision != graphics.revision() || cache.paths.size() != paths.size())
    {
        cache.paths.clear();
        for (const GraphicsPath& path : paths)
            cache.paths.push_back(pathOf(path, path.kind == GraphicsPath::Kind::Fill));
        cache.revision = graphics.revision();
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const GraphicsPath& path = paths[i];
        const QPainterPath& shape = cache.paths[i];
        if (path.kind == GraphicsPath::Kind::Fill)
        {
            if (path.fill.type == FillStyle::Type::None)
                continue;
            painter.setRenderHint(QPainter::SmoothPixmapTransform, path.fill.smooth);
            painter.fillPath(shape, brushOf(path.fill));
            continue;
        }
        const LineStyle& line = path.line;
        QPen pen(brushOf(line.fill), line.thickness);
        // A hairline, and a stroke that keeps its width however it is
        // scaled, are as wide on the device as asked.
        if (line.thickness <= 0.0)
            pen = QPen(brushOf(line.fill), 0.0);
        else if (line.scaleMode == LineStyle::ScaleMode::None)
            pen.setCosmetic(true);
        switch (line.caps)
        {
        case LineStyle::Caps::None: pen.setCapStyle(Qt::FlatCap); break;
        case LineStyle::Caps::Square: pen.setCapStyle(Qt::SquareCap); break;
        default: pen.setCapStyle(Qt::RoundCap); break;
        }
        switch (line.joints)
        {
        case LineStyle::Joints::Bevel: pen.setJoinStyle(Qt::BevelJoin); break;
        case LineStyle::Joints::Miter:
            pen.setJoinStyle(Qt::MiterJoin);
            pen.setMiterLimit(line.miterLimit);
            break;
        default: pen.setJoinStyle(Qt::RoundJoin); break;
        }
        painter.strokePath(shape, pen);
    }
    painter.restore();
}

fla::StaticText DisplayPainter::textOf(const TextField& field)
{
    fla::StaticText text(nullptr);
    const Rect box = field.box();
    text.left = 0.0;
    text.top = 0.0;
    text.width = box.width;
    text.height = box.height;
    text.autoExpand = !field.wordWrap();
    text.textType = field.type() == TextField::Type::Input ? fla::StaticText::TextType::Input
                                                           : fla::StaticText::TextType::Dynamic;
    text.lineType = field.multiline() ? (field.wordWrap() ? "multiline" : "multiline no wrap") : "single line";
    const std::u32string shown = field.shownCharacters();
    const int n = static_cast<int>(shown.size());
    int start = 0;
    while (start < n || (n == 0 && start == 0))
    {
        int end = start + 1;
        while (end < n && field.formatAt(end) == field.formatAt(start))
            ++end;
        const TextFormat& format = field.formatAt(start);
        fla::TextRun run;
        run.text = n == 0 ? std::string() : toUtf8(shown.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
        run.face = format.font.value_or("Times New Roman");
        run.size = format.size.value_or(12.0);
        const uint32_t color = format.color.value_or(0);
        run.fillColor[0] = static_cast<uint8_t>((color >> 16) & 0xff);
        run.fillColor[1] = static_cast<uint8_t>((color >> 8) & 0xff);
        run.fillColor[2] = static_cast<uint8_t>(color & 0xff);
        run.fillColor[3] = 255;
        run.letterSpacing = format.letterSpacing.value_or(0.0);
        run.autoKern = format.kerning.value_or(false);
        const std::string align = format.align.value_or("left");
        run.alignment = align == "center" ? fla::TextRun::Alignment::Center
            : align == "right"            ? fla::TextRun::Alignment::Right
            : align == "justify"          ? fla::TextRun::Alignment::Justify
                                          : fla::TextRun::Alignment::Left;
        if (format.bold.value_or(false))
            run.extraAttributes.emplace_back("bold", "true");
        if (format.italic.value_or(false))
            run.extraAttributes.emplace_back("italic", "true");
        if (format.leading.value_or(0.0) != 0.0)
            run.extraAttributes.emplace_back("lineSpacing", std::to_string(*format.leading));
        if (format.leftMargin.value_or(0.0) != 0.0)
            run.extraAttributes.emplace_back("leftMargin", std::to_string(*format.leftMargin));
        if (format.rightMargin.value_or(0.0) != 0.0)
            run.extraAttributes.emplace_back("rightMargin", std::to_string(*format.rightMargin));
        if (format.indent.value_or(0.0) != 0.0)
            run.extraAttributes.emplace_back("indent", std::to_string(*format.indent));
        text.runs.push_back(run);
        if (n == 0)
            break;
        start = end;
    }
    return text;
}

void DisplayPainter::drawText(QPainter& painter, const TextField& field)
{
    const Rect box = field.box();
    const QRectF area(box.x, box.y, box.width, box.height);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (field.background())
        painter.fillRect(area, colorOf(field.backgroundColor(), 1.0));
    if (field.border())
    {
        painter.setPen(QPen(colorOf(field.borderColor(), 1.0), 0.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area);
    }
    painter.setClipRect(area, Qt::IntersectClip);
    painter.translate(box.x - field.scrollH(), box.y - scrollTop(field));
    const fla::StaticText text = textOf(field);
    const std::u32string shown = field.shownCharacters();
    // The selection under the words, when the field has the focus (or
    // always shows it).
    const int from = field.selectionBeginIndex();
    const int to = field.selectionEndIndex();
    if (from != to && (field.showsCaret() || field.alwaysShowSelection || field.stage() && field.stage()->focus() == &field))
        for (const QRectF& band : TextLayout::selectionRects(text, unitOf(shown, from), unitOf(shown, to)))
            painter.fillRect(band, QColor(51, 153, 255, 110));
    struct Outlines
    {
        bool was = TextLayout::fieldOutlines();
        ~Outlines() { TextLayout::setFieldOutlines(was); }
    } outlines;
    TextLayout::setFieldOutlines(false);
    TextLayout::draw(painter, text);
    // Underlines, which the layout does not draw.
    for (int i = 0; i < field.length();)
    {
        if (!field.formatAt(i).underline.value_or(false))
        {
            ++i;
            continue;
        }
        int end = i + 1;
        while (end < field.length() && field.formatAt(end).underline.value_or(false))
            ++end;
        painter.setPen(QPen(colorOf(field.formatAt(i).color.value_or(0), 1.0), std::max(1.0, field.formatAt(i).size.value_or(12) / 16.0)));
        for (const QRectF& band : TextLayout::selectionRects(text, unitOf(shown, i), unitOf(shown, end)))
        {
            const double y = band.bottom() - band.height() * 0.18;
            painter.drawLine(QPointF(band.left(), y), QPointF(band.right(), y));
        }
        i = end;
    }
    if (field.showsCaret())
    {
        const QRectF caret = TextLayout::caretRect(text, unitOf(shown, field.caretIndex()));
        painter.setPen(QPen(colorOf(field.formatAt(std::max(0, field.caretIndex() - 1)).color.value_or(0), 1.0), 0.0));
        painter.drawLine(caret.topLeft(), caret.bottomLeft());
    }
    painter.restore();
}

QImage DisplayPainter::render(const DisplayObject& object, const QTransform& parentToImage, const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setTransform(parentToImage);
    const QTransform outer = stageToDevice;
    if (const DisplayObject* parent = object.parent())
        stageToDevice = toQ(parent->concatenatedMatrix().inverted()) * parentToImage;
    else
        stageToDevice = parentToImage;
    draw(painter, object);
    stageToDevice = outer;
    return image;
}

void DisplayPainter::draw(BitmapData& target, const DisplayObject& source, const Matrix& matrix,
    const ColorTransform* colorTransform, BlendMode blendMode, const Rect* clipRect, bool smoothing)
{
    if (target.disposed())
        return;
    QImage pixels(reinterpret_cast<uchar*>(target.bits()), target.width(), target.height(), target.width() * 4,
        QImage::Format_ARGB32_Premultiplied);
    // The source as it is in itself: its children, art and filters, not its
    // own transform, color or blend, through the matrix given.
    QImage layer(pixels.size(), QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    const QTransform outer = stageToDevice;
    stageToDevice = toQ(source.concatenatedMatrix().inverted().then(matrix));
    {
        QPainter painter(&layer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothing);
        painter.setTransform(toQ(matrix));
        if (const std::optional<Rect> scroll = source.scrollRect())
            painter.setClipRect(QRectF(scroll->x, scroll->y, scroll->width, scroll->height));
        const int was = _depth;
        drawContent(painter, source);
        _depth = was;
    }
    stageToDevice = outer;
    if (!source.filters().empty())
        applyFilters(layer, source.filters(), 1.0);
    if (colorTransform)
        applyColorTransform(layer, *colorTransform);
    {
        QPainter onto(&pixels);
        if (clipRect)
            onto.setClipRect(QRectF(clipRect->x, clipRect->y, clipRect->width, clipRect->height));
        Filters::composite(onto, QPoint(0, 0), layer, blendModeName(blendMode), true);
    }
    // An opaque BitmapData stays opaque.
    if (!target.transparent())
    {
        uint32_t* bits = target.bits();
        const size_t count = static_cast<size_t>(target.width()) * target.height();
        for (size_t i = 0; i < count; ++i)
            bits[i] = unpremultiplied(bits[i]) | 0xff000000u;
    }
}

bool DisplayPainter::applyFilter(BitmapData& target, const BitmapData& source, const Rect& sourceRect,
    const Point& destPoint, const BitmapFilter& filter)
{
    const QRect area = QRectF(sourceRect.x, sourceRect.y, sourceRect.width, sourceRect.height).toAlignedRect().intersected(
        QRect(0, 0, source.width(), source.height()));
    if (area.isEmpty() || target.disposed())
        return false;
    QImage image = imageOf(source).copy(area);
    applyFilters(image, {filter}, 1.0);
    QImage pixels(reinterpret_cast<uchar*>(target.bits()), target.width(), target.height(), target.width() * 4,
        QImage::Format_ARGB32_Premultiplied);
    QPainter onto(&pixels);
    onto.setCompositionMode(QPainter::CompositionMode_Source);
    onto.drawImage(QPointF(std::floor(destPoint.x), std::floor(destPoint.y)), image);
    return true;
}

Rect DisplayPainter::artBounds(const TimelineArt& art, bool strokes)
{
    if (!timelineArtBounds)
        return Rect();
    const QRectF box = timelineArtBounds(art, strokes);
    if (!box.isValid())
        return Rect();
    return Rect(box.x(), box.y(), box.width(), box.height());
}

bool DisplayPainter::artHits(const TimelineArt& art, double x, double y)
{
    return timelineArtHits && timelineArtHits(art, x, y);
}

std::vector<TextLine> DisplayPainter::layoutText(const TextField& field)
{
    return linesOf(textOf(field), field.shownCharacters());
}

int DisplayPainter::textIndexAt(const TextField& field, double x, double y)
{
    const std::u32string shown = field.shownCharacters();
    const int unit = TextLayout::indexAt(textOf(field), QPointF(x, y));
    // Back from UTF-16 units to code points.
    int units = 0;
    for (int i = 0; i < static_cast<int>(shown.size()); ++i)
    {
        if (units >= unit)
            return i;
        units += shown[static_cast<size_t>(i)] >= 0x10000 ? 2 : 1;
    }
    return static_cast<int>(shown.size());
}

Rect DisplayPainter::textCharBox(const TextField& field, int index)
{
    const fla::StaticText text = textOf(field);
    const std::u32string shown = field.shownCharacters();
    const QRectF left = TextLayout::caretRect(text, unitOf(shown, index));
    const QRectF right = TextLayout::caretRect(text, unitOf(shown, index + 1));
    double width = right.x() - left.x();
    if (std::fabs(right.y() - left.y()) > 0.5 || width < 0.0)
        width = field.formatAt(index).size.value_or(12.0) * 0.5;
    return Rect(left.x(), left.y(), width, left.height());
}
