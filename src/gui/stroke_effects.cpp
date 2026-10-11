#include "stroke_effects.h"

#include "../data/stroke_style.h"

#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace StrokeEffects {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// A small deterministic generator, so a stroke wavers the same way on
/// every paint.
class Random
{
public:
    explicit Random(uint32_t seed) : _state(seed ? seed : 0x9E3779B9u) {}

    /// Uniform in [0, 1).
    double next()
    {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        return (_state & 0xFFFFFF) / static_cast<double>(0x1000000);
    }

    /// Uniform in [-1, 1).
    double signedNext() { return next() * 2.0 - 1.0; }

private:
    uint32_t _state;
};

uint32_t seedOf(const QPainterPath& path)
{
    const QRectF bounds = path.boundingRect();
    const uint32_t a = static_cast<uint32_t>(std::lround(bounds.x() * 20.0));
    const uint32_t b = static_cast<uint32_t>(std::lround(bounds.y() * 20.0));
    const uint32_t c = static_cast<uint32_t>(path.elementCount());
    return a * 73856093u ^ b * 19349663u ^ c * 83492791u;
}

std::string attribute(const fla::StrokeStyle& style, const char* name, const char* fallback)
{
    const auto found = style.styleAttributes.find(name);
    return found == style.styleAttributes.end() ? std::string(fallback) : found->second;
}

/// Picks a value by one of Animate's named settings, in the order given.
double pick(const std::string& value, std::initializer_list<const char*> names,
    std::initializer_list<double> values, double fallback)
{
    auto name = names.begin();
    auto number = values.begin();
    for (; name != names.end() && number != values.end(); ++name, ++number)
    {
        if (value == *name)
            return *number;
    }
    return fallback;
}

/// A point and the direction of travel, every \a step along each subpath.
struct Sample
{
    QPointF point;
    QPointF tangent;
    bool start;
};

std::vector<Sample> samples(const QPainterPath& path, double step)
{
    std::vector<Sample> result;
    for (const QPolygonF& polygon : path.toSubpathPolygons())
    {
        if (polygon.size() < 2)
            continue;
        double carried = 0.0;
        bool first = true;
        for (int i = 1; i < polygon.size(); ++i)
        {
            const QPointF a = polygon[i - 1];
            const QPointF b = polygon[i];
            const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (length < 1.0e-9)
                continue;
            const QPointF tangent((b.x() - a.x()) / length, (b.y() - a.y()) / length);
            double at = first ? 0.0 : step - carried;
            while (at <= length)
            {
                result.push_back({a + (b - a) * (at / length), tangent, first});
                first = false;
                at += step;
            }
            carried = length - (at - step);
        }
        // The end of the subpath, so the line reaches it.
        const QPointF a = polygon[polygon.size() - 2];
        const QPointF b = polygon.back();
        const double length = std::max(std::hypot(b.x() - a.x(), b.y() - a.y()), 1.0e-9);
        result.push_back({b, QPointF((b.x() - a.x()) / length, (b.y() - a.y()) / length), false});
    }
    return result;
}

QPainterPath ragged(const QPainterPath& centerline, const fla::StrokeStyle& style)
{
    const double weight = std::max(style.weight, 0.5);
    const double height = pick(attribute(style, "waveHeight", "wavy"),
        {"flat", "wavy", "very wavy", "wild"}, {0.0, 0.6, 1.2, 2.2}, 0.6) * std::max(weight, 1.0);
    const double wave = pick(attribute(style, "waveLength", "short"),
        {"very short", "short", "medium", "long"}, {1.5, 3.0, 6.0, 12.0}, 3.0) * std::max(weight, 1.0);
    const std::string pattern = attribute(style, "pattern", "simple");
    const bool dotted = pattern.find("dotted") != std::string::npos;
    const bool random = pattern.find("random") != std::string::npos;

    Random rng(seedOf(centerline));
    QPainterPath line;
    double phase = 0.0;
    for (const Sample& sample : samples(centerline, std::max(wave / 2.0, 0.5)))
    {
        const QPointF normal(-sample.tangent.y(), sample.tangent.x());
        const double swing = height * (random ? rng.signedNext() : std::sin(phase) * (0.6 + 0.4 * rng.next()));
        phase += kPi / 2.0;
        const QPointF point = sample.point + normal * swing;
        if (sample.start || line.elementCount() == 0)
            line.moveTo(point);
        else
            line.lineTo(point);
    }

    QPainterPathStroker stroker;
    stroker.setWidth(weight * (dotted ? 1.0 : 0.6));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    if (dotted)
        stroker.setDashPattern(QList<qreal>{0.1, 2.5});
    QPainterPath outline = stroker.createStroke(line);
    outline.setFillRule(Qt::WindingFill);
    return outline;
}

QPainterPath stipple(const QPainterPath& centerline, const fla::StrokeStyle& style)
{
    const double weight = std::max(style.weight, 1.0);
    const double dot = pick(attribute(style, "dotSize", "small"),
        {"tiny", "small", "medium", "large"}, {0.3, 0.6, 1.1, 1.8}, 0.6);
    const double variation = pick(attribute(style, "variation", "varied sizes"),
        {"one size", "small variation", "varied sizes", "random sizes"}, {0.0, 0.25, 0.5, 0.9}, 0.5);
    const double spacing = pick(attribute(style, "density", "dense"),
        {"very dense", "dense", "sparse", "very sparse"}, {0.6, 1.0, 2.0, 3.5}, 1.0) * dot * 2.5;

    Random rng(seedOf(centerline));
    QPainterPath dots;
    dots.setFillRule(Qt::WindingFill);
    for (const Sample& sample : samples(centerline, std::max(spacing, 0.3)))
    {
        const QPointF normal(-sample.tangent.y(), sample.tangent.x());
        const QPointF along = sample.tangent * (rng.signedNext() * spacing * 0.5);
        const QPointF across = normal * (rng.signedNext() * weight * 0.5);
        const double radius = dot * (1.0 + variation * rng.signedNext());
        if (radius > 0.05)
            dots.addEllipse(sample.point + along + across, radius, radius);
    }
    return dots;
}

QPainterPath hatched(const QPainterPath& centerline, const fla::StrokeStyle& style)
{
    const double weight = std::max(style.weight, 1.0);
    const double thickness = pick(attribute(style, "hatchThickness", "thin"),
        {"hairline", "thin", "medium", "thick"}, {0.25, 0.5, 1.0, 1.6}, 0.5);
    const double space = pick(attribute(style, "space", "distant"),
        {"very close", "close", "distant", "very distant"}, {1.0, 2.0, 3.5, 6.0}, 3.5);
    const double jiggle = pick(attribute(style, "jiggle", "none"),
        {"none", "bounce", "loose", "wild"}, {0.0, 0.1, 0.25, 0.5}, 0.0);
    const double rotate = pick(attribute(style, "rotate", "none"),
        {"none", "slight", "medium", "free"}, {0.0, 10.0, 30.0, 90.0}, 0.0) * kPi / 180.0;
    const double lengthVariation = pick(attribute(style, "length", "equal"),
        {"equal", "slight variation", "medium variation", "random"}, {0.0, 0.15, 0.35, 0.6}, 0.0);

    Random rng(seedOf(centerline));
    QPainterPath hatches;
    hatches.setFillRule(Qt::WindingFill);
    for (const Sample& sample : samples(centerline, space))
    {
        // A mark across the line, turned and shifted a little at random.
        const double angle = std::atan2(sample.tangent.y(), sample.tangent.x()) + kPi / 2.0 +
            rotate * rng.signedNext();
        const QPointF direction(std::cos(angle), std::sin(angle));
        const QPointF side(-direction.y(), direction.x());
        const double half = weight * 0.5 * (1.0 + lengthVariation * rng.signedNext());
        const QPointF center = sample.point + sample.tangent * (jiggle * space * rng.signedNext()) +
            direction * (jiggle * weight * 0.5 * rng.signedNext());
        const double w = thickness * 0.5;
        // Always wound the same way, so overlapping marks fill as one.
        QPolygonF mark;
        mark << center - direction * half - side * w << center + direction * half - side * w
             << center + direction * half + side * w << center - direction * half + side * w;
        hatches.addPolygon(mark);
        hatches.closeSubpath();
    }
    return hatches;
}

} // namespace

/// The stroke's reach to the left and the right, in pixels, a fraction \a t
/// of the way along it: its profile's points joined smoothly, or straight
/// into and out of a corner point.
void widthAt(const fla::StrokeStyle& style, double t, double& left, double& right)
{
    const std::vector<fla::StrokeStyle::WidthMarker>& profile = style.widthProfile;
    const double weight = std::max(style.weight, 0.1);
    if (t <= profile.front().position)
    {
        left = profile.front().left * weight;
        right = profile.front().right * weight;
        return;
    }
    for (size_t i = 1; i < profile.size(); ++i)
    {
        const fla::StrokeStyle::WidthMarker& a = profile[i - 1];
        const fla::StrokeStyle::WidthMarker& b = profile[i];
        if (t > b.position)
            continue;
        double u = b.position > a.position ? (t - a.position) / (b.position - a.position) : 1.0;
        if (!a.corner || !b.corner)
            u = u * u * (3.0 - 2.0 * u);
        left = (a.left + (b.left - a.left) * u) * weight;
        right = (a.right + (b.right - a.right) * u) * weight;
        return;
    }
    left = profile.back().left * weight;
    right = profile.back().right * weight;
}

/// A variable-width stroke as the area it covers: each subpath walked by
/// its length, the profile setting how far out to either side each point
/// lies, the left side out and the right side back.
QPainterPath variableWidth(const QPainterPath& centerline, const fla::StrokeStyle& style)
{
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    const double step = std::max(0.25, std::min(2.0, style.weight / 4.0));
    const std::vector<Sample> all = samples(centerline, step);
    size_t begin = 0;
    while (begin < all.size())
    {
        size_t end = begin + 1;
        while (end < all.size() && !all[end].start)
            ++end;
        if (end - begin >= 2)
        {
            std::vector<double> along(end - begin, 0.0);
            for (size_t i = begin + 1; i < end; ++i)
            {
                const QPointF d = all[i].point - all[i - 1].point;
                along[i - begin] = along[i - begin - 1] + std::hypot(d.x(), d.y());
            }
            const double length = std::max(along.back(), 1.0e-9);
            QPolygonF leftSide;
            QPolygonF rightSide;
            for (size_t i = begin; i < end; ++i)
            {
                double left = 0.0;
                double right = 0.0;
                widthAt(style, along[i - begin] / length, left, right);
                // Left of the way along, on a page whose y runs down.
                const QPointF normal(all[i].tangent.y(), -all[i].tangent.x());
                leftSide << all[i].point + normal * left;
                rightSide << all[i].point - normal * right;
            }
            const QPointF first = all[begin].point;
            const QPointF last = all[end - 1].point;
            const bool closed = std::hypot(last.x() - first.x(), last.y() - first.y()) < 1.0e-6;
            if (closed)
            {
                // A ring: the two sides as loops the opposite ways round.
                area.addPolygon(leftSide);
                area.closeSubpath();
                std::reverse(rightSide.begin(), rightSide.end());
                area.addPolygon(rightSide);
                area.closeSubpath();
            }
            else
            {
                std::reverse(rightSide.begin(), rightSide.end());
                area.addPolygon(leftSide + rightSide);
                area.closeSubpath();
            }
        }
        begin = end;
    }
    return area;
}

bool isProcedural(const fla::StrokeStyle& style)
{
    const fla::StrokeStyle::Style kind = style.style();
    return kind == fla::StrokeStyle::Style::Ragged || kind == fla::StrokeStyle::Style::Stipple ||
        kind == fla::StrokeStyle::Style::Hatched || !style.widthProfile.empty();
}

QPainterPath outline(const QPainterPath& centerline, const fla::StrokeStyle& style)
{
    if (!style.widthProfile.empty() && style.style() == fla::StrokeStyle::Style::Solid)
        return variableWidth(centerline, style);
    switch (style.style())
    {
    case fla::StrokeStyle::Style::Ragged: return ragged(centerline, style);
    case fla::StrokeStyle::Style::Stipple: return stipple(centerline, style);
    case fla::StrokeStyle::Style::Hatched: return hatched(centerline, style);
    default: return QPainterPath();
    }
}

} // namespace StrokeEffects
