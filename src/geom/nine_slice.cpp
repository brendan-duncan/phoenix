#include "nine_slice.h"

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

std::vector<Cell> cells(const Rect& bounds, double left, double top, double right, double bottom,
    const AxisScale& scale)
{
    std::vector<Cell> result;
    if (scale.x <= 1.0e-6 || scale.y <= 1.0e-6)
        return result;

    const double x0 = bounds.topLeft.x;
    const double x1 = bounds.bottomRight.x;
    const double y0 = bounds.topLeft.y;
    const double y1 = bounds.bottomRight.y;
    const double srcX[4] = {x0, left, right, x1};
    const double srcY[4] = {y0, top, bottom, y1};
    const double dstX[4] = {x0, x0 + (left - x0) / scale.x, x1 - (x1 - right) / scale.x, x1};
    const double dstY[4] = {y0, y0 + (top - y0) / scale.y, y1 - (y1 - bottom) / scale.y, y1};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
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
