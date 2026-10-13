#include "snapping.h"

#include <algorithm>
#include <cmath>

namespace fla {

void Snapper::clearCandidates()
{
    _candidatesX.clear();
    _candidatesY.clear();
    _candidatePoints.clear();
    _candidateSegments.clear();
}

void Snapper::addCandidateX(double x)
{
    _candidatesX.push_back(x);
}

void Snapper::addCandidateY(double y)
{
    _candidatesY.push_back(y);
}

void Snapper::addCandidatePoint(double x, double y)
{
    _candidatePoints.push_back({x, y});
}

void Snapper::addCandidateSegment(double x1, double y1, double x2, double y2)
{
    _candidateSegments.push_back({x1, y1, x2, y2});
}

namespace {

/// The smallest scale a snap may leave: anything less would flip the box
/// over or squash it flat.
constexpr double kSmallestScale = 1.0e-3;

/// Guides are placed on purpose, so they win a tie; objects come before the
/// grid, as for a move.
int rankOf(SnapKind kind)
{
    switch (kind)
    {
    case SnapKind::Guide:
        return 0;
    case SnapKind::Object:
        return 1;
    case SnapKind::Grid:
        return 2;
    case SnapKind::Pixel:
        return 3;
    case SnapKind::None:
        break;
    }
    return 4;
}

double distanceToSegment(double px, double py, double x1, double y1, double x2, double y2,
    double& nearestX, double& nearestY)
{
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 0.0)
        t = std::clamp(((px - x1) * dx + (py - y1) * dy) / lengthSquared, 0.0, 1.0);
    nearestX = x1 + t * dx;
    nearestY = y1 + t * dy;
    return std::hypot(px - nearestX, py - nearestY);
}

} // namespace

HandleSnapResult Snapper::snapHandle(const HandleDrag& drag) const
{
    HandleSnapResult best;
    best.scaleX = drag.scaleX;
    best.scaleY = drag.scaleY;

    // Where the pointer alone would put the handle.
    const double hx = drag.anchorX + drag.scaleX * drag.axisXx + drag.scaleY * drag.axisYx;
    const double hy = drag.anchorY + drag.scaleX * drag.axisXy + drag.scaleY * drag.axisYy;
    best.x = hx;
    best.y = hy;

    const double reach = 2.0 * _tolerance;
    const bool freeX = drag.scalesX && std::hypot(drag.axisXx, drag.axisXy) > 1.0e-9;
    const bool freeY = drag.scalesY && std::hypot(drag.axisYx, drag.axisYy) > 1.0e-9;
    if (!freeX && !freeY)
        return best;

    double bestMoved = 0.0;
    const auto offer = [&](double x, double y, double sx, double sy, SnapKind kind, bool onPoint, bool anyDistance) {
        // Snapping never turns the box over or flattens it.
        const double signX = drag.scaleX < 0.0 ? -1.0 : 1.0;
        const double signY = drag.scaleY < 0.0 ? -1.0 : 1.0;
        if ((freeX || drag.uniform) && sx * signX <= kSmallestScale)
            return;
        if ((freeY || drag.uniform) && sy * signY <= kSmallestScale)
            return;

        const double moved = std::hypot(x - hx, y - hy);
        if (!anyDistance && moved > reach)
            return;

        if (best.snapped())
        {
            if (best.onPoint != onPoint)
            {
                if (best.onPoint)
                    return;
            }
            else if (moved > bestMoved + 1.0e-9)
            {
                return;
            }
            else if (moved >= bestMoved - 1.0e-9 && rankOf(kind) >= rankOf(best.kind))
            {
                return;
            }
        }

        best.scaleX = sx;
        best.scaleY = sy;
        best.kind = kind;
        best.onPoint = onPoint;
        best.x = x;
        best.y = y;
        bestMoved = moved;
    };

    // The lines in reach on each axis: guides, the edges and centers of other
    // objects, and the nearest grid line.
    struct Line
    {
        double value;
        SnapKind kind;
    };
    std::vector<Line> linesX;
    std::vector<Line> linesY;
    const auto gather = [&](std::vector<Line>& lines, double at, double spacing,
                            const std::vector<double>& candidates, const std::vector<double>& guides) {
        if (_guideSnapEnabled)
        {
            for (double guide : guides)
            {
                if (std::fabs(guide - at) <= _tolerance)
                    lines.push_back({guide, SnapKind::Guide});
            }
        }
        if (_objectSnapEnabled)
        {
            for (double candidate : candidates)
            {
                if (std::fabs(candidate - at) <= _tolerance)
                    lines.push_back({candidate, SnapKind::Object});
            }
        }
        if (_gridEnabled && spacing > 0.0)
        {
            const double grid = std::round(at / spacing) * spacing;
            if (std::fabs(grid - at) <= _tolerance)
                lines.push_back({grid, SnapKind::Grid});
        }
    };
    gather(linesX, hx, _gridSpacingX, _candidatesX, _guidesX);
    gather(linesY, hy, _gridSpacingY, _candidatesY, _guidesY);

    std::vector<CandidatePoint> points;
    std::vector<CandidateSegment> segments;
    if (_objectSnapEnabled)
    {
        for (const CandidatePoint& point : _candidatePoints)
        {
            if (std::hypot(point.x - hx, point.y - hy) <= _tolerance)
                points.push_back(point);
        }
        for (const CandidateSegment& segment : _candidateSegments)
        {
            double nx = 0.0;
            double ny = 0.0;
            if (distanceToSegment(hx, hy, segment.x1, segment.y1, segment.x2, segment.y2, nx, ny) <= _tolerance)
                segments.push_back(segment);
        }
    }

    if (freeX && freeY && !drag.uniform)
    {
        // Both scales are free, so any spot can be reached: each target gives
        // the spot on it nearest the handle, and the scales follow from that.
        const double det = drag.axisXx * drag.axisYy - drag.axisXy * drag.axisYx;
        if (std::fabs(det) <= 1.0e-12)
            return best;

        const auto land = [&](double x, double y, SnapKind kind, bool onPoint, bool anyDistance) {
            const double rx = x - drag.anchorX;
            const double ry = y - drag.anchorY;
            const double sx = (rx * drag.axisYy - ry * drag.axisYx) / det;
            const double sy = (drag.axisXx * ry - drag.axisXy * rx) / det;
            offer(x, y, sx, sy, kind, onPoint, anyDistance);
        };

        for (const CandidatePoint& point : points)
            land(point.x, point.y, SnapKind::Object, true, false);
        if (_gridEnabled && _gridSpacingX > 0.0 && _gridSpacingY > 0.0)
        {
            const double gx = std::round(hx / _gridSpacingX) * _gridSpacingX;
            const double gy = std::round(hy / _gridSpacingY) * _gridSpacingY;
            if (std::hypot(gx - hx, gy - hy) <= _tolerance)
                land(gx, gy, SnapKind::Grid, true, false);
        }

        // A line on each axis at once pins the handle to where they cross.
        for (const Line& lineX : linesX)
        {
            for (const Line& lineY : linesY)
                land(lineX.value, lineY.value, rankOf(lineX.kind) <= rankOf(lineY.kind) ? lineX.kind : lineY.kind, true, false);
        }

        for (const Line& line : linesX)
            land(line.value, hy, line.kind, false, false);
        for (const Line& line : linesY)
            land(hx, line.value, line.kind, false, false);
        for (const CandidateSegment& segment : segments)
        {
            double nx = 0.0;
            double ny = 0.0;
            distanceToSegment(hx, hy, segment.x1, segment.y1, segment.x2, segment.y2, nx, ny);
            land(nx, ny, SnapKind::Object, false, false);
        }

        if (!best.snapped() && _pixelSnapEnabled)
            land(std::round(hx), std::round(hy), SnapKind::Pixel, true, true);
        return best;
    }

    // One scale moves the handle: along a line from a base point. Shift's
    // single scale is the same, with the direction set by the scales' signs.
    double baseX = drag.anchorX;
    double baseY = drag.anchorY;
    double dirX = 0.0;
    double dirY = 0.0;
    if (drag.uniform)
    {
        const double signX = drag.scaleX < 0.0 ? -1.0 : 1.0;
        const double signY = drag.scaleY < 0.0 ? -1.0 : 1.0;
        dirX = signX * drag.axisXx + signY * drag.axisYx;
        dirY = signX * drag.axisXy + signY * drag.axisYy;
    }
    else if (freeX)
    {
        baseX += drag.scaleY * drag.axisYx;
        baseY += drag.scaleY * drag.axisYy;
        dirX = drag.axisXx;
        dirY = drag.axisXy;
    }
    else
    {
        baseX += drag.scaleX * drag.axisXx;
        baseY += drag.scaleX * drag.axisXy;
        dirX = drag.axisYx;
        dirY = drag.axisYy;
    }
    const double lengthSquared = dirX * dirX + dirY * dirY;
    if (lengthSquared <= 1.0e-18)
        return best;

    const auto along = [&](double s, SnapKind kind, bool onPoint, bool anyDistance) {
        double sx = drag.scaleX;
        double sy = drag.scaleY;
        if (drag.uniform)
        {
            sx = drag.scaleX < 0.0 ? -s : s;
            sy = drag.scaleY < 0.0 ? -s : s;
        }
        else if (freeX)
        {
            sx = s;
        }
        else
        {
            sy = s;
        }
        offer(baseX + s * dirX, baseY + s * dirY, sx, sy, kind, onPoint, anyDistance);
    };

    // A point lands only if the handle's path runs through it; one just off
    // the path still pulls the handle level with it, as a line would.
    for (const CandidatePoint& point : points)
    {
        const double s = ((point.x - baseX) * dirX + (point.y - baseY) * dirY) / lengthSquared;
        const double miss = std::hypot(baseX + s * dirX - point.x, baseY + s * dirY - point.y);
        along(s, SnapKind::Object, miss <= 1.0e-9 * (1.0 + std::sqrt(lengthSquared)), false);
    }

    if (std::fabs(dirX) > 1.0e-9)
    {
        for (const Line& line : linesX)
            along((line.value - baseX) / dirX, line.kind, false, false);
    }
    if (std::fabs(dirY) > 1.0e-9)
    {
        for (const Line& line : linesY)
            along((line.value - baseY) / dirY, line.kind, false, false);
    }

    for (const CandidateSegment& segment : segments)
    {
        // Where the handle's path crosses the segment.
        const double ex = segment.x2 - segment.x1;
        const double ey = segment.y2 - segment.y1;
        const double det = ex * dirY - dirX * ey;
        if (std::fabs(det) <= 1.0e-12 * (1.0 + lengthSquared + ex * ex + ey * ey))
            continue;
        const double rx = segment.x1 - baseX;
        const double ry = segment.y1 - baseY;
        const double s = (ex * ry - rx * ey) / det;
        const double u = (dirX * ry - dirY * rx) / det;
        if (u >= -1.0e-9 && u <= 1.0 + 1.0e-9)
            along(s, SnapKind::Object, false, false);
    }

    if (!best.snapped() && _pixelSnapEnabled)
    {
        // The handle's main direction goes to a whole pixel.
        if (std::fabs(dirX) >= std::fabs(dirY))
            along((std::round(hx) - baseX) / dirX, SnapKind::Pixel, false, true);
        else
            along((std::round(hy) - baseY) / dirY, SnapKind::Pixel, false, true);
    }

    return best;
}

SnapResult Snapper::snap(const std::vector<double>& values, double gridSpacing,
    const std::vector<double>& candidates, const std::vector<double>& guides) const
{
    SnapResult best;
    double bestDistance = _tolerance;

    for (double value : values)
    {
        // A guide is placed on purpose, so it is considered first and holds
        // against anything no closer.
        if (_guideSnapEnabled)
        {
            for (double guide : guides)
            {
                const double distance = std::fabs(guide - value);
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    best.adjustment = guide - value;
                    best.kind = SnapKind::Guide;
                    best.target = guide;
                }
            }
        }

        // Lining up with another object beats the grid, so objects are
        // considered first and the grid only wins if it is strictly closer.
        if (_objectSnapEnabled)
        {
            for (double candidate : candidates)
            {
                const double distance = std::fabs(candidate - value);
                if (distance < bestDistance || (distance <= bestDistance && best.kind != SnapKind::Guide))
                {
                    bestDistance = distance;
                    best.adjustment = candidate - value;
                    best.kind = SnapKind::Object;
                    best.target = candidate;
                }
            }
        }

        if (_gridEnabled && gridSpacing > 0.0)
        {
            const double target = std::round(value / gridSpacing) * gridSpacing;
            const double distance = std::fabs(target - value);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best.adjustment = target - value;
                best.kind = SnapKind::Grid;
                best.target = target;
            }
        }
    }

    // Nothing else in reach: the first value (an edge, or the point dragged)
    // goes to the nearest whole pixel.
    if (_pixelSnapEnabled && best.kind == SnapKind::None && !values.empty())
    {
        const double target = std::round(values.front());
        best.adjustment = target - values.front();
        best.kind = SnapKind::Pixel;
        best.target = target;
    }

    return best;
}

SnapResult Snapper::snapX(const std::vector<double>& values) const
{
    return snap(values, _gridSpacingX, _candidatesX, _guidesX);
}

SnapResult Snapper::snapY(const std::vector<double>& values) const
{
    return snap(values, _gridSpacingY, _candidatesY, _guidesY);
}

} // namespace fla
