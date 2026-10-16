#include "graphics.h"

#include "display_object.h"

#include <algorithm>
#include <cmath>

namespace fla {
namespace display {

namespace {

double clamp(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

double distanceToSegment(double px, double py, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    double t = length2 > 0.0 ? ((px - a.x) * dx + (py - a.y) * dy) / length2 : 0.0;
    t = clamp(t, 0.0, 1.0);
    const double cx = a.x + t * dx - px;
    const double cy = a.y + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

/// Whether (x, y) is inside \a polygons: crossings counted even-odd, or
/// windings summed non-zero.
bool inside(const std::vector<std::vector<Point>>& polygons, double x, double y, bool nonZero)
{
    int winding = 0;
    int crossings = 0;
    for (const std::vector<Point>& polygon : polygons)
    {
        const size_t n = polygon.size();
        if (n < 3)
            continue;
        for (size_t i = 0, j = n - 1; i < n; j = i++)
        {
            const Point& a = polygon[j];
            const Point& b = polygon[i];
            if ((a.y <= y) != (b.y <= y))
            {
                const double at = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x < at)
                {
                    ++crossings;
                    winding += b.y > a.y ? 1 : -1;
                }
            }
        }
    }
    return nonZero ? winding != 0 : (crossings & 1) != 0;
}

} // namespace

std::vector<Gradient::Stop> Gradient::stopsOf(const std::vector<uint32_t>& colors, const std::vector<double>& alphas,
    const std::vector<double>& ratios)
{
    std::vector<Stop> stops;
    const size_t count = std::min({colors.size(), alphas.size(), ratios.size(), static_cast<size_t>(15)});
    for (size_t i = 0; i < count; ++i)
    {
        Stop stop;
        stop.color = colors[i] & 0xffffff;
        stop.alpha = std::isnan(alphas[i]) ? 0.0 : clamp(alphas[i], 0.0, 1.0);
        stop.ratio = std::isnan(ratios[i]) ? 0 : static_cast<int>(clamp(ratios[i], 0.0, 255.0));
        stops.push_back(stop);
    }
    return stops;
}

LineStyle::ScaleMode LineStyle::scaleModeNamed(const std::string& name)
{
    if (name == "none")
        return ScaleMode::None;
    if (name == "vertical")
        return ScaleMode::Vertical;
    if (name == "horizontal")
        return ScaleMode::Horizontal;
    return ScaleMode::Normal;
}

LineStyle::Caps LineStyle::capsNamed(const std::string& name)
{
    if (name == "none")
        return Caps::None;
    if (name == "square")
        return Caps::Square;
    return Caps::Round;
}

LineStyle::Joints LineStyle::jointsNamed(const std::string& name)
{
    if (name == "bevel")
        return Joints::Bevel;
    if (name == "miter")
        return Joints::Miter;
    return Joints::Round;
}

void Graphics::changed()
{
    ++_revision;
    _shownValid = false;
    if (_owner)
        _owner->invalidate();
}

void Graphics::clear()
{
    _paths.clear();
    _filling = false;
    _fill = GraphicsPath();
    _pendingLines.clear();
    _line = LineStyle();
    _lineOpen = false;
    _stroke = GraphicsPath();
    _cursor = Point();
    _fillStart = Point();
    changed();
}

void Graphics::startLine()
{
    // What was drawn in the old style is done: it waits for the fill when
    // one is open, else it is drawn in turn.
    bool drew = false;
    for (const PathStep& step : _stroke.steps)
        drew = drew || step.op != PathStep::Op::Move;
    if (_lineOpen && drew)
        (_filling ? _pendingLines : _paths).push_back(_stroke);
    _stroke = GraphicsPath();
    _stroke.kind = GraphicsPath::Kind::Line;
    _stroke.line = _line;
    _lineOpen = _line.on();
    if (_lineOpen)
    {
        PathStep move;
        move.op = PathStep::Op::Move;
        move.points[0] = _cursor;
        _stroke.steps.push_back(move);
    }
}

void Graphics::lineStyle(double thickness, uint32_t color, double alpha, bool pixelHinting, LineStyle::ScaleMode scaleMode,
    LineStyle::Caps caps, LineStyle::Joints joints, double miterLimit)
{
    _line = LineStyle();
    _line.thickness = std::isnan(thickness) ? thickness : clamp(thickness, 0.0, 255.0);
    _line.pixelHinting = pixelHinting;
    _line.scaleMode = scaleMode;
    _line.caps = caps;
    _line.joints = joints;
    _line.miterLimit = std::isnan(miterLimit) ? 3.0 : clamp(miterLimit, 1.0, 255.0);
    _line.fill.type = FillStyle::Type::Solid;
    _line.fill.color = color & 0xffffff;
    _line.fill.alpha = std::isnan(alpha) ? 1.0 : clamp(alpha, 0.0, 1.0);
    startLine();
    changed();
}

void Graphics::lineGradientStyle(const Gradient& gradient)
{
    if (!_line.on())
        return;
    _line.fill.type = FillStyle::Type::Gradient;
    _line.fill.gradient = gradient;
    startLine();
    changed();
}

void Graphics::lineBitmapStyle(std::shared_ptr<BitmapData> bitmap, const Matrix& matrix, bool repeat, bool smooth)
{
    if (!_line.on() || !bitmap)
        return;
    _line.fill.type = FillStyle::Type::Bitmap;
    _line.fill.bitmap = std::move(bitmap);
    _line.fill.bitmapMatrix = matrix;
    _line.fill.repeat = repeat;
    _line.fill.smooth = smooth;
    startLine();
    changed();
}

void Graphics::flushFill()
{
    if (!_filling)
        return;
    // The stroke drawn with it so far goes with it, the rest of it after.
    if (_lineOpen)
        startLine();
    bool drew = false;
    for (const PathStep& step : _fill.steps)
        drew = drew || step.op != PathStep::Op::Move;
    if (drew)
        _paths.push_back(_fill);
    for (GraphicsPath& line : _pendingLines)
        _paths.push_back(std::move(line));
    _pendingLines.clear();
    _fill = GraphicsPath();
    _filling = false;
}

void Graphics::beginFill(uint32_t color, double alpha)
{
    FillStyle fill;
    fill.type = FillStyle::Type::Solid;
    fill.color = color & 0xffffff;
    fill.alpha = std::isnan(alpha) ? 1.0 : clamp(alpha, 0.0, 1.0);
    flushFill();
    _filling = true;
    _fill.kind = GraphicsPath::Kind::Fill;
    _fill.fill = fill;
    _fillStart = _cursor;
    PathStep move;
    move.points[0] = _cursor;
    _fill.steps.push_back(move);
    changed();
}

void Graphics::beginGradientFill(const Gradient& gradient)
{
    beginFill(0, 1.0);
    _fill.fill.type = FillStyle::Type::Gradient;
    _fill.fill.gradient = gradient;
}

void Graphics::beginBitmapFill(std::shared_ptr<BitmapData> bitmap, const Matrix& matrix, bool repeat, bool smooth)
{
    beginFill(0, 1.0);
    if (!bitmap)
    {
        _fill.fill.type = FillStyle::Type::None;
        return;
    }
    _fill.fill.type = FillStyle::Type::Bitmap;
    _fill.fill.bitmap = std::move(bitmap);
    _fill.fill.bitmapMatrix = matrix;
    _fill.fill.repeat = repeat;
    _fill.fill.smooth = smooth;
}

void Graphics::endFill()
{
    if (!_filling)
        return;
    flushFill();
    changed();
}

void Graphics::addStep(const PathStep& step)
{
    if (_filling)
        _fill.steps.push_back(step);
    if (_lineOpen)
        _stroke.steps.push_back(step);
    _shownValid = false;
}

void Graphics::moveTo(double x, double y)
{
    _cursor = {x, y};
    PathStep step;
    step.op = PathStep::Op::Move;
    step.points[0] = _cursor;
    if (_filling)
        _fillStart = _cursor;
    addStep(step);
    changed();
}

void Graphics::lineTo(double x, double y)
{
    PathStep step;
    step.op = PathStep::Op::Line;
    step.points[0] = {x, y};
    _cursor = {x, y};
    addStep(step);
    changed();
}

void Graphics::curveTo(double controlX, double controlY, double anchorX, double anchorY)
{
    PathStep step;
    step.op = PathStep::Op::Quad;
    step.points[0] = {controlX, controlY};
    step.points[1] = {anchorX, anchorY};
    _cursor = {anchorX, anchorY};
    addStep(step);
    changed();
}

void Graphics::cubicCurveTo(double c1x, double c1y, double c2x, double c2y, double anchorX, double anchorY)
{
    PathStep step;
    step.op = PathStep::Op::Cubic;
    step.points[0] = {c1x, c1y};
    step.points[1] = {c2x, c2y};
    step.points[2] = {anchorX, anchorY};
    _cursor = {anchorX, anchorY};
    addStep(step);
    changed();
}

void Graphics::drawRect(double x, double y, double width, double height)
{
    moveTo(x, y);
    lineTo(x + width, y);
    lineTo(x + width, y + height);
    lineTo(x, y + height);
    lineTo(x, y);
}

void Graphics::drawRoundRect(double x, double y, double width, double height, double ellipseWidth, double ellipseHeight)
{
    if (std::isnan(ellipseHeight))
        ellipseHeight = ellipseWidth;
    if (!(ellipseWidth > 0.0) || !(ellipseHeight > 0.0))
    {
        drawRect(x, y, width, height);
        return;
    }
    // Each corner a quarter ellipse of two curves, as Flash Player draws it.
    const double rx = std::min(ellipseWidth / 2.0, std::fabs(width) / 2.0);
    const double ry = std::min(ellipseHeight / 2.0, std::fabs(height) / 2.0);
    const double right = x + width;
    const double bottom = y + height;
    const double k = 1.0 - std::sqrt(2.0) / 2.0;   // where the corner's middle is, from its box's corner
    const double c = std::tan(kPi / 8.0);          // the control points' reach, along the sides
    moveTo(right, bottom - ry);
    curveTo(right, bottom - ry + ry * c, right - rx * k, bottom - ry * k);
    curveTo(right - rx + rx * c, bottom, right - rx, bottom);
    lineTo(x + rx, bottom);
    curveTo(x + rx - rx * c, bottom, x + rx * k, bottom - ry * k);
    curveTo(x, bottom - ry + ry * c, x, bottom - ry);
    lineTo(x, y + ry);
    curveTo(x, y + ry - ry * c, x + rx * k, y + ry * k);
    curveTo(x + rx - rx * c, y, x + rx, y);
    lineTo(right - rx, y);
    curveTo(right - rx + rx * c, y, right - rx * k, y + ry * k);
    curveTo(right, y + ry - ry * c, right, y + ry);
    lineTo(right, bottom - ry);
}

void Graphics::drawEllipse(double x, double y, double width, double height)
{
    // Eight curves of an eighth of the way round each, from three o'clock.
    const double rx = width / 2.0;
    const double ry = height / 2.0;
    const double cx = x + rx;
    const double cy = y + ry;
    const double reach = 1.0 / std::cos(kPi / 8.0);
    moveTo(cx + rx, cy);
    for (int i = 1; i <= 8; ++i)
    {
        const double mid = (i - 0.5) * kPi / 4.0;
        const double end = i * kPi / 4.0;
        curveTo(cx + rx * reach * std::cos(mid), cy + ry * reach * std::sin(mid), cx + rx * std::cos(end),
            cy + ry * std::sin(end));
    }
}

void Graphics::drawCircle(double x, double y, double radius)
{
    drawEllipse(x - radius, y - radius, radius * 2.0, radius * 2.0);
}

void Graphics::drawPath(const std::vector<int>& commands, const std::vector<double>& data, bool nonZero)
{
    size_t at = 0;
    const auto take = [&](double& value) {
        if (at >= data.size())
            return false;
        value = data[at++];
        return true;
    };
    if (_filling)
        _fill.nonZero = nonZero;
    for (int command : commands)
    {
        double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        switch (command)
        {
        case 1: // MOVE_TO
            if (take(a) && take(b))
                moveTo(a, b);
            break;
        case 2: // LINE_TO
            if (take(a) && take(b))
                lineTo(a, b);
            break;
        case 3: // CURVE_TO
            if (take(a) && take(b) && take(c) && take(d))
                curveTo(a, b, c, d);
            break;
        case 4: // WIDE_MOVE_TO: a point to skip, then the move
            if (take(a) && take(b) && take(c) && take(d))
                moveTo(c, d);
            break;
        case 5: // WIDE_LINE_TO
            if (take(a) && take(b) && take(c) && take(d))
                lineTo(c, d);
            break;
        case 6: // CUBIC_CURVE_TO
            if (take(a) && take(b) && take(c) && take(d) && take(e) && take(f))
                cubicCurveTo(a, b, c, d, e, f);
            break;
        default:
            break;
        }
    }
}

void Graphics::drawTriangles(const std::vector<double>& vertices, const std::vector<int>& indices, int culling)
{
    const size_t count = vertices.size() / 2;
    std::vector<int> order = indices;
    if (order.empty())
        for (size_t i = 0; i < count; ++i)
            order.push_back(static_cast<int>(i));
    for (size_t t = 0; t + 2 < order.size(); t += 3)
    {
        const int i0 = order[t], i1 = order[t + 1], i2 = order[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(std::max({i0, i1, i2})) >= count)
            continue;
        const Point p0{vertices[i0 * 2], vertices[i0 * 2 + 1]};
        const Point p1{vertices[i1 * 2], vertices[i1 * 2 + 1]};
        const Point p2{vertices[i2 * 2], vertices[i2 * 2 + 1]};
        const double cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if ((culling == 1 && cross > 0.0) || (culling == 2 && cross < 0.0))
            continue;
        moveTo(p0.x, p0.y);
        lineTo(p1.x, p1.y);
        lineTo(p2.x, p2.y);
        lineTo(p0.x, p0.y);
    }
}

void Graphics::copyFrom(const Graphics& other)
{
    _paths = other._paths;
    _filling = other._filling;
    _fill = other._fill;
    _fillStart = other._fillStart;
    _pendingLines = other._pendingLines;
    _line = other._line;
    _lineOpen = other._lineOpen;
    _stroke = other._stroke;
    _cursor = other._cursor;
    changed();
}

const std::vector<GraphicsPath>& Graphics::paths() const
{
    if (_shownValid)
        return _shown;
    _shown = _paths;
    const auto drawn = [](const GraphicsPath& path) {
        for (const PathStep& step : path.steps)
            if (step.op != PathStep::Op::Move)
                return true;
        return false;
    };
    if (_filling)
    {
        if (drawn(_fill))
            _shown.push_back(_fill);
        for (const GraphicsPath& line : _pendingLines)
            _shown.push_back(line);
    }
    if (_lineOpen && drawn(_stroke))
        _shown.push_back(_stroke);
    _shownValid = true;
    return _shown;
}

Rect Graphics::bounds(bool withStrokes) const
{
    double l = 0, t = 0, r = 0, b = 0;
    bool any = false;
    for (const GraphicsPath& path : paths())
    {
        double grow = 0.0;
        if (path.kind == GraphicsPath::Kind::Line && withStrokes)
            grow = std::max(path.line.thickness, 0.0) / 2.0;
        for (const std::vector<Point>& polygon : flatten(path.steps, 0.5))
        {
            // A lone move draws nothing.
            if (polygon.size() < 2)
                continue;
            for (const Point& p : polygon)
            {
                if (!any)
                {
                    l = r = p.x;
                    t = b = p.y;
                    any = true;
                }
                l = std::min(l, p.x - grow);
                r = std::max(r, p.x + grow);
                t = std::min(t, p.y - grow);
                b = std::max(b, p.y + grow);
            }
        }
    }
    if (!any)
        return Rect();
    return Rect(l, t, r - l, b - t);
}

bool Graphics::hits(double x, double y) const
{
    for (const GraphicsPath& path : paths())
    {
        const std::vector<std::vector<Point>> polygons = flatten(path.steps, 0.25);
        if (path.kind == GraphicsPath::Kind::Fill)
        {
            if (path.fill.type != FillStyle::Type::None && inside(polygons, x, y, path.nonZero))
                return true;
            continue;
        }
        const double reach = std::max(path.line.thickness / 2.0, 0.5);
        for (const std::vector<Point>& polygon : polygons)
            for (size_t i = 1; i < polygon.size(); ++i)
                if (distanceToSegment(x, y, polygon[i - 1], polygon[i]) <= reach)
                    return true;
    }
    return false;
}

std::vector<std::vector<Point>> flatten(const std::vector<PathStep>& steps, double tolerance)
{
    std::vector<std::vector<Point>> polygons;
    Point at;
    for (const PathStep& step : steps)
    {
        if (step.op == PathStep::Op::Move)
        {
            at = step.points[0];
            polygons.push_back({at});
            continue;
        }
        if (polygons.empty())
            polygons.push_back({at});
        std::vector<Point>& polygon = polygons.back();
        switch (step.op)
        {
        case PathStep::Op::Line:
            at = step.points[0];
            polygon.push_back(at);
            break;
        case PathStep::Op::Quad:
        {
            const Point p0 = at, p1 = step.points[0], p2 = step.points[1];
            const double dx = p0.x - 2.0 * p1.x + p2.x;
            const double dy = p0.y - 2.0 * p1.y + p2.y;
            const double deviation = std::sqrt(dx * dx + dy * dy) / 4.0;
            const int n = static_cast<int>(clamp(std::ceil(std::sqrt(deviation / tolerance)), 1.0, 100.0));
            for (int i = 1; i <= n; ++i)
            {
                const double t = static_cast<double>(i) / n;
                const double u = 1.0 - t;
                polygon.push_back(
                    {u * u * p0.x + 2.0 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2.0 * u * t * p1.y + t * t * p2.y});
            }
            at = p2;
            break;
        }
        case PathStep::Op::Cubic:
        {
            const Point p0 = at, p1 = step.points[0], p2 = step.points[1], p3 = step.points[2];
            const double ax = p0.x - 2.0 * p1.x + p2.x, ay = p0.y - 2.0 * p1.y + p2.y;
            const double bx = p1.x - 2.0 * p2.x + p3.x, by = p1.y - 2.0 * p2.y + p3.y;
            const double deviation = 0.75 * std::max(std::sqrt(ax * ax + ay * ay), std::sqrt(bx * bx + by * by));
            const int n = static_cast<int>(clamp(std::ceil(std::sqrt(deviation / tolerance)), 1.0, 100.0));
            for (int i = 1; i <= n; ++i)
            {
                const double t = static_cast<double>(i) / n;
                const double u = 1.0 - t;
                const double a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
                polygon.push_back({a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y});
            }
            at = p3;
            break;
        }
        default:
            break;
        }
    }
    return polygons;
}

} // namespace display
} // namespace fla
