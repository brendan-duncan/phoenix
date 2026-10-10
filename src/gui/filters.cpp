#include "filters.h"

#include "../edit/image_blur.h"

#include <QRegion>

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace Filters {

namespace {

constexpr double kPi = 3.14159265358979323846;

int passesOf(const fla::Filter& filter)
{
    return std::max(1, std::min(15, filter.quality));
}

/// The silhouette of \a image in one color: every pixel's alpha, scaled by
/// \a alpha, carrying the color premultiplied. With \a inverted, the
/// silhouette of what is not covered.
QImage silhouette(const QImage& image, const uint8_t rgb[3], double alpha, bool inverted)
{
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* in = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            int a = qAlpha(in[x]);
            if (inverted)
                a = 255 - a;
            const double coverage = a / 255.0 * alpha;
            line[x] = qRgba(static_cast<int>(rgb[0] * coverage + 0.5),
                            static_cast<int>(rgb[1] * coverage + 0.5),
                            static_cast<int>(rgb[2] * coverage + 0.5),
                            static_cast<int>(255.0 * coverage + 0.5));
        }
    }
    return out;
}

void blur(QImage& image, const fla::Filter& filter, double scale)
{
    fla::boxBlur(reinterpret_cast<uint32_t*>(image.bits()), image.width(), image.height(),
        filter.blurX * scale, filter.blurY * scale, passesOf(filter));
}

/// Multiplies every pixel's coverage by \a strength, clamped, which is what
/// a filter's strength does to its blurred silhouette.
void strengthen(QImage& image, double strength)
{
    if (strength == 1.0)
        return;
    for (int y = 0; y < image.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb straight = qUnpremultiply(line[x]);
            const int a = std::max(0, std::min(255, static_cast<int>(qAlpha(straight) * strength + 0.5)));
            line[x] = qPremultiply(qRgba(qRed(straight), qGreen(straight), qBlue(straight), a));
        }
    }
}

QImage shifted(const QImage& image, int dx, int dy)
{
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(dx, dy, image);
    return out;
}

/// \a image kept only where \a mask is covered (or, inverted, where it is
/// not).
void maskBy(QImage& image, const QImage& mask, bool inverted)
{
    const uint8_t white[3] = {255, 255, 255};
    const QImage coverage = silhouette(mask, white, 1.0, inverted);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, coverage);
}

QImage dropShadow(const QImage& image, const fla::Filter& filter, double scale, bool glow)
{
    const double radians = filter.angle * kPi / 180.0;
    const int dx = glow ? 0 : static_cast<int>(std::lround(std::cos(radians) * filter.distance * scale));
    const int dy = glow ? 0 : static_cast<int>(std::lround(std::sin(radians) * filter.distance * scale));

    // The shadow is the object's silhouette (or, for an inner shadow, of
    // everything outside it), moved, blurred and strengthened.
    QImage shadow = silhouette(image, filter.color, filter.color[3] / 255.0, filter.inner);
    shadow = shifted(shadow, dx, dy);
    blur(shadow, filter, scale);
    strengthen(shadow, filter.strength);

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);

    if (filter.inner)
    {
        // An inner shadow lies on the object, inside its edge.
        maskBy(shadow, image, false);
        if (!filter.knockout && !filter.hideObject)
            painter.drawImage(0, 0, image);
        painter.drawImage(0, 0, shadow);
        return out;
    }

    if (filter.knockout)
        maskBy(shadow, image, true);
    painter.drawImage(0, 0, shadow);
    if (!filter.knockout && !filter.hideObject)
        painter.drawImage(0, 0, image);
    return out;
}

QImage bevel(const QImage& image, const fla::Filter& filter, double scale)
{
    const double radians = filter.angle * kPi / 180.0;
    const int dx = static_cast<int>(std::lround(std::cos(radians) * filter.distance * scale));
    const int dy = static_cast<int>(std::lround(std::sin(radians) * filter.distance * scale));

    // The object's coverage moved towards the light and away from it, each
    // blurred: where the one away from the light has more coverage than the
    // one towards it, the edge faces the light and is highlighted; the other
    // way round it is in shadow.
    const uint8_t white[3] = {255, 255, 255};
    QImage towards = shifted(silhouette(image, white, 1.0, false), dx, dy);
    QImage away = shifted(silhouette(image, white, 1.0, false), -dx, -dy);
    blur(towards, filter, scale);
    blur(away, filter, scale);

    QImage highlight(image.size(), QImage::Format_ARGB32_Premultiplied);
    QImage shadow(image.size(), QImage::Format_ARGB32_Premultiplied);
    const double highlightAlpha = filter.highlightColor[3] / 255.0 * filter.strength;
    const double shadowAlpha = filter.shadowColor[3] / 255.0 * filter.strength;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* t = reinterpret_cast<const QRgb*>(towards.constScanLine(y));
        const QRgb* a = reinterpret_cast<const QRgb*>(away.constScanLine(y));
        QRgb* h = reinterpret_cast<QRgb*>(highlight.scanLine(y));
        QRgb* s = reinterpret_cast<QRgb*>(shadow.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const int difference = qAlpha(a[x]) - qAlpha(t[x]);
            const double lit = std::max(0.0, std::min(1.0, difference / 255.0 * highlightAlpha));
            const double dark = std::max(0.0, std::min(1.0, -difference / 255.0 * shadowAlpha));
            h[x] = qRgba(static_cast<int>(filter.highlightColor[0] * lit + 0.5),
                         static_cast<int>(filter.highlightColor[1] * lit + 0.5),
                         static_cast<int>(filter.highlightColor[2] * lit + 0.5),
                         static_cast<int>(255.0 * lit + 0.5));
            s[x] = qRgba(static_cast<int>(filter.shadowColor[0] * dark + 0.5),
                         static_cast<int>(filter.shadowColor[1] * dark + 0.5),
                         static_cast<int>(filter.shadowColor[2] * dark + 0.5),
                         static_cast<int>(255.0 * dark + 0.5));
        }
    }

    const bool inner = filter.bevelType.empty() || filter.bevelType == "inner";
    const bool outer = filter.bevelType == "outer";
    if (inner)
    {
        maskBy(highlight, image, false);
        maskBy(shadow, image, false);
    }
    else if (outer)
    {
        maskBy(highlight, image, true);
        maskBy(shadow, image, true);
    }

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    if (!inner)
    {
        painter.drawImage(0, 0, highlight);
        painter.drawImage(0, 0, shadow);
    }
    if (!filter.knockout)
        painter.drawImage(0, 0, image);
    if (!outer)
    {
        painter.drawImage(0, 0, highlight);
        painter.drawImage(0, 0, shadow);
    }
    return out;
}

/// The gradient's color at  t, 0 to 1, interpolated between its entries
/// in straight (unpremultiplied) RGBA, as Flash's gradient filters read it.
QRgb gradientAt(const std::vector<fla::GradientEntry>& entries, double t)
{
    if (entries.empty())
        return qRgba(0, 0, 0, 0);
    t = std::max(0.0, std::min(1.0, t));
    const fla::GradientEntry* before = &entries.front();
    const fla::GradientEntry* after = &entries.back();
    for (const fla::GradientEntry& entry : entries)
    {
        if (entry.ratio <= t && entry.ratio >= before->ratio)
            before = &entry;
    }
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
    {
        if (it->ratio >= t && it->ratio <= after->ratio)
            after = &*it;
    }
    const double span = after->ratio - before->ratio;
    const double f = span > 1.0e-9 ? (t - before->ratio) / span : 0.0;
    int rgba[4];
    for (int i = 0; i < 4; ++i)
        rgba[i] = static_cast<int>(before->color[i] + (after->color[i] - before->color[i]) * f + 0.5);
    return qRgba(rgba[0], rgba[1], rgba[2], rgba[3]);
}

/// Every pixel's coverage (its alpha) mapped through the gradient: the
/// pixel takes the gradient's color and alpha at that coverage. A glow's
/// gradient runs from its outer edge (ratio 0) in to the object (ratio 1).
void mapThroughGradient(QImage& image, const std::vector<fla::GradientEntry>& entries)
{
    QRgb table[256];
    for (int i = 0; i < 256; ++i)
        table[i] = qPremultiply(gradientAt(entries, i / 255.0));
    for (int y = 0; y < image.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = table[qAlpha(line[x])];
    }
}

/// Gradient glow: the glow's blurred coverage colored through the gradient
/// rather than in one color, placed as a glow is.
QImage gradientGlow(const QImage& image, const fla::Filter& filter, double scale)
{
    const double radians = filter.angle * kPi / 180.0;
    const int dx = static_cast<int>(std::lround(std::cos(radians) * filter.distance * scale));
    const int dy = static_cast<int>(std::lround(std::sin(radians) * filter.distance * scale));

    const uint8_t white[3] = {255, 255, 255};
    QImage glow = silhouette(image, white, 1.0, filter.inner);
    glow = shifted(glow, dx, dy);
    blur(glow, filter, scale);
    strengthen(glow, filter.strength);
    mapThroughGradient(glow, filter.entries);

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    if (filter.inner)
    {
        maskBy(glow, image, false);
        if (!filter.knockout && !filter.hideObject)
            painter.drawImage(0, 0, image);
        painter.drawImage(0, 0, glow);
        return out;
    }
    if (filter.knockout)
        maskBy(glow, image, true);
    painter.drawImage(0, 0, glow);
    if (!filter.knockout && !filter.hideObject)
        painter.drawImage(0, 0, image);
    return out;
}

/// Gradient bevel: the relief (how much an edge faces the light, -1 to 1)
/// mapped through the gradient, whose middle is the flat surface. Flash's
/// default gradient has a transparent middle, so flat areas show nothing.
QImage gradientBevel(const QImage& image, const fla::Filter& filter, double scale)
{
    const double radians = filter.angle * kPi / 180.0;
    const int dx = static_cast<int>(std::lround(std::cos(radians) * filter.distance * scale));
    const int dy = static_cast<int>(std::lround(std::sin(radians) * filter.distance * scale));

    const uint8_t white[3] = {255, 255, 255};
    QImage towards = shifted(silhouette(image, white, 1.0, false), dx, dy);
    QImage away = shifted(silhouette(image, white, 1.0, false), -dx, -dy);
    blur(towards, filter, scale);
    blur(away, filter, scale);

    QRgb table[256];
    for (int i = 0; i < 256; ++i)
        table[i] = qPremultiply(gradientAt(filter.entries, i / 255.0));

    QImage relief(image.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* t = reinterpret_cast<const QRgb*>(towards.constScanLine(y));
        const QRgb* a = reinterpret_cast<const QRgb*>(away.constScanLine(y));
        QRgb* r = reinterpret_cast<QRgb*>(relief.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const double difference = (qAlpha(a[x]) - qAlpha(t[x])) / 255.0 * filter.strength;
            const double position = std::max(0.0, std::min(1.0, 0.5 + difference / 2.0));
            r[x] = table[static_cast<int>(position * 255.0 + 0.5)];
        }
    }

    const bool inner = filter.bevelType.empty() || filter.bevelType == "inner";
    const bool outer = filter.bevelType == "outer";
    if (inner)
        maskBy(relief, image, false);
    else if (outer)
        maskBy(relief, image, true);

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    if (!inner)
        painter.drawImage(0, 0, relief);
    if (!filter.knockout)
        painter.drawImage(0, 0, image);
    if (!outer)
        painter.drawImage(0, 0, relief);
    return out;
}

/// Brightness, contrast, saturation and hue as a 3x3 matrix plus offset on
/// straight RGB, the way Flash's Adjust Color builds its color matrix.
QImage adjustColor(const QImage& image, const fla::Filter& filter)
{
    const double brightness = std::max(-100.0, std::min(100.0, filter.brightness)) * 2.55;
    const double contrast = 1.0 + std::max(-100.0, std::min(100.0, filter.contrast)) / 100.0;
    const double saturation = 1.0 + std::max(-100.0, std::min(100.0, filter.saturation)) / 100.0;
    const double hue = std::max(-180.0, std::min(180.0, filter.hue)) * kPi / 180.0;

    // Saturation about the luminance axis, then the hue turned about it.
    constexpr double lr = 0.3086;
    constexpr double lg = 0.6094;
    constexpr double lb = 0.0820;
    double m[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r][c] = (c == 0 ? lr : c == 1 ? lg : lb) * (1.0 - saturation) + (r == c ? saturation : 0.0);

    const double cosH = std::cos(hue);
    const double sinH = std::sin(hue);
    const double w = 1.0 / 3.0;
    const double s3 = std::sqrt(1.0 / 3.0);
    double h[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            h[r][c] = w + cosH * ((r == c ? 1.0 : 0.0) - w) + sinH * s3 * (((c - r + 3) % 3 == 1) ? 1.0 : ((c - r + 3) % 3 == 2) ? -1.0 : 0.0);

    double full[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            full[r][c] = h[r][0] * m[0][c] + h[r][1] * m[1][c] + h[r][2] * m[2][c];

    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* in = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb straight = qUnpremultiply(in[x]);
            const double rgb[3] = {static_cast<double>(qRed(straight)),
                static_cast<double>(qGreen(straight)), static_cast<double>(qBlue(straight))};
            int result[3];
            for (int r = 0; r < 3; ++r)
            {
                double v = full[r][0] * rgb[0] + full[r][1] * rgb[1] + full[r][2] * rgb[2];
                v = (v - 128.0) * contrast + 128.0 + brightness;
                result[r] = std::max(0, std::min(255, static_cast<int>(v + 0.5)));
            }
            line[x] = qPremultiply(qRgba(result[0], result[1], result[2], qAlpha(straight)));
        }
    }
    return out;
}

} // namespace

double margin(const std::vector<fla::Filter>& filters, double scale)
{
    double total = 0.0;
    for (const fla::Filter& filter : filters)
    {
        if (!filter.enabled)
            continue;
        const double reach = std::max(filter.blurX, filter.blurY) * scale / 2.0 * passesOf(filter);
        double offset = 0.0;
        if (filter.type == fla::Filter::Type::DropShadow || filter.type == fla::Filter::Type::Bevel ||
            filter.type == fla::Filter::Type::GradientBevel)
        {
            offset = std::fabs(filter.distance) * scale;
        }
        if (filter.type != fla::Filter::Type::AdjustColor)
            total += reach + offset + 1.0;
    }
    return std::ceil(total);
}

QImage apply(const QImage& source, const std::vector<fla::Filter>& filters, double scale)
{
    QImage image = source.format() == QImage::Format_ARGB32_Premultiplied
        ? source : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    for (const fla::Filter& filter : filters)
    {
        if (!filter.enabled)
            continue;
        switch (filter.type)
        {
        case fla::Filter::Type::Blur:
            blur(image, filter, scale);
            break;
        case fla::Filter::Type::DropShadow:
            image = dropShadow(image, filter, scale, false);
            break;
        case fla::Filter::Type::Glow:
            image = dropShadow(image, filter, scale, true);
            break;
        case fla::Filter::Type::Bevel:
            image = bevel(image, filter, scale);
            break;
        case fla::Filter::Type::GradientGlow:
            image = gradientGlow(image, filter, scale);
            break;
        case fla::Filter::Type::GradientBevel:
            image = gradientBevel(image, filter, scale);
            break;
        case fla::Filter::Type::AdjustColor:
            image = adjustColor(image, filter);
            break;
        }
    }
    return image;
}

QPainter::CompositionMode blendMode(const std::string& name, bool& supported)
{
    supported = true;
    if (name == "multiply") return QPainter::CompositionMode_Multiply;
    if (name == "screen") return QPainter::CompositionMode_Screen;
    if (name == "lighten") return QPainter::CompositionMode_Lighten;
    if (name == "darken") return QPainter::CompositionMode_Darken;
    if (name == "difference") return QPainter::CompositionMode_Difference;
    if (name == "add") return QPainter::CompositionMode_Plus;
    if (name == "overlay") return QPainter::CompositionMode_Overlay;
    if (name == "hardlight") return QPainter::CompositionMode_HardLight;
    if (name == "subtract" || name == "invert" || name == "alpha" || name == "erase")
        supported = false;
    return QPainter::CompositionMode_SourceOver;
}

bool needsIsolation(const std::string& name)
{
    return !name.empty() && name != "normal";
}

void composite(QPainter& painter, const QPoint& at, const QImage& image, const std::string& name,
    bool insideLayerGroup)
{
    if (name == "alpha" || name == "erase")
    {
        if (!insideLayerGroup)
            return;
        if (name == "erase")
        {
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.drawImage(at, image);
            return;
        }
        // Alpha keeps the group only where the instance covers it, which
        // clears everything outside the instance's image as well.
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(at, image);
        QRegion outside(QRect(0, 0, painter.device()->width(), painter.device()->height()));
        outside -= QRegion(QRect(at, image.size()));
        painter.save();
        painter.setClipRegion(outside);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(QRect(0, 0, painter.device()->width(), painter.device()->height()), Qt::transparent);
        painter.restore();
        return;
    }
    if (name == "invert" || name == "subtract")
    {
        // White under the instance's coverage, differenced with the
        // backdrop, inverts it there and leaves the rest alone.
        const uint8_t white[3] = {255, 255, 255};
        const QImage cover = silhouette(image, white, 1.0, false);
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.drawImage(at, cover);
        if (name == "subtract")
        {
            // backdrop - source = invert(invert(backdrop) + source), the
            // sum clamped at white and so the difference at black.
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
            painter.drawImage(at, image);
            painter.setCompositionMode(QPainter::CompositionMode_Difference);
            painter.drawImage(at, cover);
        }
        return;
    }
    bool supported = true;
    painter.setCompositionMode(blendMode(name, supported));
    painter.drawImage(at, image);
}

} // namespace Filters
