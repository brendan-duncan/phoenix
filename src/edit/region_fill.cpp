#include "region_fill.h"

#include "color_art.h"
#include "drawing_style.h"
#include "region_split.h"
#include "shape_merge.h"
#include "timeline_edit.h"

#include "../data/element.h"
#include "../data/fill_style.h"
#include "../data/frame.h"
#include "../data/layer.h"
#include "../data/shape.h"
#include "../data/solid_color.h"
#include "../data/timeline.h"
#include "../geom/curve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace fla {

namespace regionfill {

namespace {

bool sameColor(const uint8_t a[4], const uint8_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

double determinant(const Transform& t)
{
    return t.m11 * t.m22 - t.m12 * t.m21;
}

} // namespace

int ensureFill(Shape& shape, const DrawingStyle& style)
{
    if (!style.hasFill)
        return -1;

    for (const auto& entry : shape.fillsMap)
    {
        const FillStyle* fill = entry.second;
        if (fill && style.fillKind == DrawingStyle::FillKind::Solid && fill->type() == FillStyle::Type::SolidColor &&
            sameColor(static_cast<const SolidColor*>(fill)->color, style.fillColor))
        {
            return entry.first;
        }
    }

    FillStyle* fill = style.createFill(&shape);
    if (!fill)
        return -1;

    const int index = shape.fillsMap.empty() ? 1 : shape.fillsMap.rbegin()->first + 1;
    shape.fills.push_back(fill);
    shape.fillsMap[index] = fill;
    return index;
}

bool inverse(const Transform& t, Transform& result)
{
    const double det = determinant(t);
    if (std::fabs(det) < 1.0e-12)
        return false;
    const double m11 = t.m22 / det;
    const double m12 = -t.m12 / det;
    const double m21 = -t.m21 / det;
    const double m22 = t.m11 / det;
    result = Transform(m11, m12, m21, m22, -(t.tx * m11 + t.ty * m21), -(t.tx * m12 + t.ty * m22));
    return true;
}

std::vector<int> facesAlong(const RegionSplitter& regions, const std::vector<Point>& path, double step)
{
    std::vector<int> faces;
    std::set<int> seen;
    const auto visit = [&](const Point& point) {
        const int face = regions.faceAt(point);
        if (face >= 0 && seen.insert(face).second)
            faces.push_back(face);
    };
    if (path.empty())
        return faces;
    step = std::max(step, 1.0e-3);
    visit(path.front());
    for (size_t i = 1; i < path.size(); ++i)
    {
        const Point& a = path[i - 1];
        const Point& b = path[i];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const int count = std::max(1, static_cast<int>(std::ceil(length / step)));
        for (int k = 1; k <= count; ++k)
        {
            const double t = static_cast<double>(k) / count;
            visit(Point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
        }
    }
    return faces;
}

bool insideLoop(const std::vector<Point>& loop, const Point& point)
{
    bool inside = false;
    for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
    {
        const Point& a = loop[i];
        const Point& b = loop[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }
    return inside;
}

bool regionMiddle(const RegionSplitter& regions, int face, Point& point)
{
    point = regions.faceCentroid(face);
    if (regions.faceAt(point) == face)
        return true;
    return regions.faceInterior(face, point);
}

std::vector<int> facesInside(const RegionSplitter& regions, const std::vector<Point>& loop)
{
    std::vector<int> faces;
    if (loop.size() < 3)
        return faces;
    for (const int face : regions.boundedFaces())
    {
        Point point;
        if (regionMiddle(regions, face, point) && insideLoop(loop, point))
            faces.push_back(face);
    }
    return faces;
}

std::string paintKey(const Shape& shape, int fillIndex)
{
    const FillStyle* fill = fillIndex > 0 ? shape.getFillStyleByIndex(fillIndex) : nullptr;
    if (!fill)
        return std::string();
    if (fill->type() != FillStyle::Type::SolidColor)
        return "other";
    const uint8_t* color = static_cast<const SolidColor*>(fill)->color;
    char text[16];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", color[0], color[1], color[2], color[3]);
    return text;
}

Footprint footprintOf(const RegionSplitter& regions, int face, const Transform& shapeToStage)
{
    Footprint footprint;
    if (face < 0)
        return footprint;
    footprint.centroid = regions.faceCentroid(face).transformed(shapeToStage);
    footprint.area = regions.faceArea(face) * std::fabs(determinant(shapeToStage));
    footprint.valid = footprint.area > 0.0;
    return footprint;
}

double matchScore(const Footprint& was, const Footprint& candidate, bool holdsCentroid)
{
    if (!was.valid || !candidate.valid)
        return -1.0;
    // Much bigger or much smaller is another region.
    const double ratio = candidate.area / was.area;
    if (ratio < 1.0 / 3.0 || ratio > 3.0)
        return -1.0;
    // How far it moved, against how big it is: a region rarely moves more
    // than its own size from one drawing to the next.
    const double size = std::sqrt(was.area);
    const double moved = std::hypot(candidate.centroid.x - was.centroid.x, candidate.centroid.y - was.centroid.y) / size;
    if (moved > 1.5)
        return -1.0;
    double score = moved + 0.5 * std::fabs(std::log(ratio));
    if (holdsCentroid)
        score *= 0.5;
    return score;
}

std::vector<int> drawingsIn(const Timeline& timeline, Layer& layer, int first, int last)
{
    std::vector<int> drawings;
    const Layer* source = &layer;
    if (colorart::isColorArt(layer))
    {
        if (const Layer* line = colorart::lineLayerOf(timeline, layer))
            source = line;
    }
    for (const Frame* frame : source->frames)
    {
        if (!frame)
            continue;
        const int end = frame->index + std::max(1, frame->duration) - 1;
        if (frame->index <= last && end >= first && !frame->elements.empty())
            drawings.push_back(frame->index);
    }
    std::sort(drawings.begin(), drawings.end());
    drawings.erase(std::unique(drawings.begin(), drawings.end()), drawings.end());
    return drawings;
}

namespace {

/// One shape of a drawing, divided into its regions.
struct Candidate
{
    Shape* shape = nullptr;
    int elementIndex = -1;
    Transform toStage;
    Transform fromStage;
    std::unique_ptr<Shape> working;
    std::unique_ptr<RegionSplitter> open;
    std::unique_ptr<RegionSplitter> closed;
};

class Filler
{
public:
    Filler(const Timeline& timeline, Layer& layer, const AcrossSettings& settings)
        : _layer(layer)
        , _settings(settings)
    {
        if (colorart::isColorArt(layer))
            _line = colorart::lineLayerOf(timeline, layer);
    }

    /// Paints the region on the drawing at \a index: the one under the click,
    /// or with \a follow the one most like it. False when none matched.
    bool fill(int index, const Footprint* follow, Footprint& found, AcrossResult& result)
    {
        Frame* frame = nullptr;
        bool exact = true;
        if (_line)
        {
            frame = timeline::spanAt(_layer, index);
            exact = frame && frame->index == index;
        }
        else
        {
            for (Frame* keyframe : _layer.frames)
                if (keyframe && keyframe->index == index)
                    frame = keyframe;
        }
        if (!frame && !_line)
            return false;

        std::vector<std::unique_ptr<Candidate>> candidates;
        if (frame)
        {
            for (int i = static_cast<int>(frame->elements.size()) - 1; i >= 0; --i)
            {
                Element* element = frame->elements[static_cast<size_t>(i)];
                if (!element || element->elementType() != Element::Type::Shape)
                    continue;
                auto candidate = make(index, static_cast<Shape*>(element), i);
                if (candidate)
                    candidates.push_back(std::move(candidate));
            }
        }
        // Under another layer's lines, paint can go where there was none.
        if (_line && candidates.empty())
        {
            if (auto candidate = make(index, nullptr, -1))
                candidates.push_back(std::move(candidate));
        }

        Candidate* chosen = nullptr;
        const RegionSplitter* regions = nullptr;
        int face = -1;
        if (follow && follow->valid)
        {
            double best = 1.0e300;
            for (const auto& candidate : candidates)
            {
                const RegionSplitter& divided = candidate->closed ? *candidate->closed : *candidate->open;
                const int holding = divided.faceAt(follow->centroid.transformed(candidate->fromStage));
                // Painted as the region was, or already in the new paint.
                const int already = ensureFill(*candidate->working, *_settings.style);
                for (const int each : divided.boundedFaces())
                {
                    const int fill = divided.faceFill(each);
                    if (fill != already && paintKey(*candidate->working, fill) != follow->paint)
                        continue;
                    const double score = matchScore(*follow, footprintOf(divided, each, candidate->toStage), each == holding);
                    if (score >= 0.0 && score < best)
                    {
                        best = score;
                        chosen = candidate.get();
                        regions = &divided;
                        face = each;
                    }
                }
            }
        }
        else
        {
            for (const auto& candidate : candidates)
            {
                const Point local = _settings.stagePoint.transformed(candidate->fromStage);
                face = candidate->open->faceAt(local);
                regions = candidate->open.get();
                if (face < 0 && candidate->closed)
                {
                    face = candidate->closed->faceAt(local);
                    regions = candidate->closed.get();
                }
                if (face >= 0)
                {
                    chosen = candidate.get();
                    break;
                }
            }
        }
        if (!chosen || face < 0)
            return false;

        found = footprintOf(*regions, face, chosen->toStage);
        const int fill = ensureFill(*chosen->working, *_settings.style);
        // Followed from here as what it was, or for a drawing painted
        // already, as what the first one was.
        found.paint = follow && follow->valid ? follow->paint : paintKey(*chosen->working, regions->faceFill(face));
        if (fill < 0)
            return false;
        if (regions->faceFill(face) == fill)
        {
            ++result.unchanged;
            return true;
        }
        std::unique_ptr<Shape> after(regions->withFaceFill(face, fill));
        if (!after)
            return false;
        DrawingStyle::fitFills(*after);
        if (!chosen->shape)
            after->transform = Transform();

        DrawingFill painted;
        painted.frameIndex = index;
        painted.frame = exact ? frame : nullptr;
        painted.elementIndex = chosen->elementIndex;
        painted.target = exact ? chosen->shape : nullptr;
        painted.after = std::move(after);
        result.fills.push_back(std::move(painted));
        return true;
    }

private:
    std::unique_ptr<Candidate> make(int index, Shape* shape, int elementIndex)
    {
        auto candidate = std::make_unique<Candidate>();
        candidate->shape = shape;
        candidate->elementIndex = elementIndex;
        candidate->toStage = shape ? shape->transform : Transform();
        if (!inverse(candidate->toStage, candidate->fromStage))
            return nullptr;
        candidate->working.reset(shape ? cloneShape(*shape, nullptr) : new Shape(nullptr));

        std::vector<Curve> cutters;
        if (_line)
        {
            cutters = colorart::withoutShared(colorart::lineCutters(*_line, index, candidate->fromStage), *candidate->working);
            if (cutters.empty() && !shape)
                return nullptr;
            candidate->open.reset(new RegionSplitter(*candidate->working, cutters));
        }
        else
        {
            candidate->open.reset(new RegionSplitter(*candidate->working));
        }
        if (!candidate->open->valid())
            return nullptr;
        if (_settings.gap > 0.0)
        {
            const std::vector<Curve> joins = candidate->open->gapJoins(_settings.gap);
            if (!joins.empty())
            {
                cutters.insert(cutters.end(), joins.begin(), joins.end());
                candidate->closed.reset(new RegionSplitter(*candidate->working, cutters));
            }
        }
        return candidate;
    }

    Layer& _layer;
    const AcrossSettings& _settings;
    const Layer* _line = nullptr;
};

} // namespace

AcrossResult fillAcross(const Timeline& timeline, Layer& layer, const std::vector<int>& drawings,
    int startDrawing, const AcrossSettings& settings)
{
    AcrossResult result;
    if (!settings.style)
        return result;
    Filler filler(timeline, layer, settings);

    std::vector<int> order(drawings);
    if (std::find(order.begin(), order.end(), startDrawing) == order.end())
        order.push_back(startDrawing);
    std::sort(order.begin(), order.end());
    const auto start = std::find(order.begin(), order.end(), startDrawing);

    // The drawing clicked, by the click itself.
    Footprint clicked;
    AcrossSettings atClick = settings;
    atClick.follow = false;
    Filler first(timeline, layer, atClick);
    if (!first.fill(startDrawing, nullptr, clicked, result))
        result.missed.push_back(startDrawing);

    // Then out from it each way, each drawing following the last one found.
    const auto walk = [&](auto begin, auto end) {
        Footprint last = clicked;
        for (auto it = begin; it != end; ++it)
        {
            Footprint found;
            const bool following = settings.follow && last.valid;
            if (filler.fill(*it, following ? &last : nullptr, found, result))
                last = found;
            else
                result.missed.push_back(*it);
        }
    };
    walk(std::next(start), order.end());
    walk(std::make_reverse_iterator(start), order.rend());
    std::sort(result.missed.begin(), result.missed.end());
    return result;
}

} // namespace regionfill

} // namespace fla
