#include "nine_slice.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace nineslice {

AxisScale axisScale(const Transform& matrix)
{
    return AxisScale{std::hypot(matrix.m11, matrix.m12), std::hypot(matrix.m21, matrix.m22)};
}

bool stretches(const Transform& matrix)
{
    const AxisScale scale = axisScale(matrix);
    if (scale.x <= 1.0e-6 || scale.y <= 1.0e-6)
        return false;
    return std::fabs(scale.x - 1.0) > 1.0e-6 || std::fabs(scale.y - 1.0) > 1.0e-6;
}

namespace {

/// The edges of the strips along one axis from \a low to \a high cut at
/// \a guides, where they come from and where they go for a stretch of
/// \a scale (in the symbol's space before it).
void cut(double low, double high, const std::vector<double>& guides, double scale, std::vector<double>& from,
    std::vector<double>& to)
{
    from.assign(1, low);
    for (const double guide : guides)
        from.push_back(std::clamp(guide, low, high));
    from.push_back(high);
    // The kept strips at their size once the stretch is applied: in the
    // symbol's space, their width over the stretch.
    double kept = 0.0;
    double stretching = 0.0;
    for (size_t i = 0; i + 1 < from.size(); ++i)
        (i % 2 == 0 ? kept : stretching) += from[i + 1] - from[i];
    const double whole = high - low;
    double keep = 1.0 / scale;
    double room = whole - kept * keep;
    if (room < 0.0)
    {
        keep = kept > 0.0 ? whole / kept : 0.0;
        room = 0.0;
    }
    to.assign(1, low);
    for (size_t i = 0; i + 1 < from.size(); ++i)
    {
        const double width = from[i + 1] - from[i];
        const double placed = i % 2 == 0 ? width * keep : (stretching > 0.0 ? width / stretching * room : 0.0);
        to.push_back(to.back() + placed);
    }
    // Rounding aside, the last edge is the art's.
    if (stretching > 0.0)
        to.back() = high;
}

} // namespace

std::vector<Cell> cells(const Rect& bounds, double left, double top, double right, double bottom,
    const AxisScale& scale)
{
    return cells(bounds, std::vector<double>{left, right}, std::vector<double>{top, bottom}, scale);
}

std::vector<Cell> cells(const Rect& bounds, const std::vector<double>& across, const std::vector<double>& down,
    const AxisScale& scale)
{
    std::vector<Cell> result;
    if (scale.x <= 1.0e-6 || scale.y <= 1.0e-6)
        return result;

    std::vector<double> srcX, dstX, srcY, dstY;
    cut(bounds.topLeft.x, bounds.bottomRight.x, across, scale.x, srcX, dstX);
    cut(bounds.topLeft.y, bounds.bottomRight.y, down, scale.y, srcY, dstY);
    for (size_t row = 0; row + 1 < srcY.size(); ++row)
    {
        for (size_t column = 0; column + 1 < srcX.size(); ++column)
        {
            Cell cell;
            cell.source = Rect(Point(srcX[column], srcY[row]), Point(srcX[column + 1], srcY[row + 1]));
            cell.target = Rect(Point(dstX[column], dstY[row]), Point(dstX[column + 1], dstY[row + 1]));
            if (cell.source.width() <= 1.0e-6 || cell.source.height() <= 1.0e-6 ||
                cell.target.width() <= 1.0e-6 || cell.target.height() <= 1.0e-6)
                continue;
            result.push_back(cell);
        }
    }
    return result;
}

} // namespace nineslice

} // namespace fla
