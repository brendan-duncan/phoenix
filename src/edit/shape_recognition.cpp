#include "shape_recognition.h"

#include <algorithm>
#include <cmath>

namespace fla {

namespace {

double distance(const Point& a, const Point& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// The ideal oval through the stroke's extent, as four smooth anchors.
void makeOval(EditablePath& path, double left, double top, double right, double bottom)
{
    const double cx = (left + right) * 0.5;
    const double cy = (top + bottom) * 0.5;
    const double rx = (right - left) * 0.5;
    const double ry = (bottom - top) * 0.5;
    // The handle length that makes four cubics into a circle.
    const double k = 0.5522847498;

    path.anchors.clear();
    Anchor topAnchor(Point(cx, cy - ry));
    topAnchor.inHandle = Point(cx - k * rx, cy - ry);
    topAnchor.outHandle = Point(cx + k * rx, cy - ry);
    Anchor rightAnchor(Point(cx + rx, cy));
    rightAnchor.inHandle = Point(cx + rx, cy - k * ry);
    rightAnchor.outHandle = Point(cx + rx, cy + k * ry);
    Anchor bottomAnchor(Point(cx, cy + ry));
    bottomAnchor.inHandle = Point(cx + k * rx, cy + ry);
    bottomAnchor.outHandle = Point(cx - k * rx, cy + ry);
    Anchor leftAnchor(Point(cx - rx, cy));
    leftAnchor.inHandle = Point(cx - rx, cy + k * ry);
    leftAnchor.outHandle = Point(cx - rx, cy - k * ry);
    for (Anchor* anchor : {&topAnchor, &rightAnchor, &bottomAnchor, &leftAnchor})
    {
        anchor->smooth = true;
        path.anchors.push_back(*anchor);
    }
    path.closed = true;
}

/// Whether a closed four-cornered stroke is a rectangle, and whether it sits
/// square to the axes.
bool isRectangle(const EditablePath& path, bool& axisAligned)
{
    if (path.anchors.size() != 4)
        return false;

    axisAligned = true;
    for (size_t i = 0; i < 4; ++i)
    {
        const Point& a = path.anchors[i].position;
        const Point& b = path.anchors[(i + 1) % 4].position;
        const Point& c = path.anchors[(i + 2) % 4].position;
        const double ux = b.x - a.x;
        const double uy = b.y - a.y;
        const double vx = c.x - b.x;
        const double vy = c.y - b.y;
        const double lu = std::hypot(ux, uy);
        const double lv = std::hypot(vx, vy);
        if (lu < 1.0e-9 || lv < 1.0e-9)
            return false;

        // Corners within twenty degrees of square.
        const double cosine = (ux * vx + uy * vy) / (lu * lv);
        if (std::fabs(cosine) > 0.35)
            return false;

        const double tilt = std::fabs(std::atan2(uy, ux));
        const double offAxis = std::min(std::fmod(tilt, 3.14159265358979323846 / 2.0),
            3.14159265358979323846 / 2.0 - std::fmod(tilt, 3.14159265358979323846 / 2.0));
        if (offAxis > 10.0 * 3.14159265358979323846 / 180.0)
            axisAligned = false;
    }
    return true;
}

} // namespace

void tidyStraightenedStroke(EditablePath& path, const std::vector<Point>& raw,
    const StrokeTidyOptions& options)
{
    if (path.anchors.size() < 3)
        return;

    // A stroke that comes back to where it started is a closed outline, with
    // the last straight run going to the first anchor rather than to a loose
    // end beside it.
    // The fitter may already have closed a stroke that came back to its
    // start; either way a last anchor sitting on the first is the seam, not
    // a corner, and goes.
    if (options.connectLines || path.closed)
    {
        const Point& first = path.anchors.front().position;
        const Point& last = path.anchors.back().position;
        if (distance(first, last) <= options.closeTolerance && path.anchors.size() > 3)
        {
            path.anchors.pop_back();
            path.closed = true;
        }
        else if (options.connectLines && distance(first, last) <= options.closeTolerance)
        {
            path.closed = true;
        }
        if (path.closed && path.anchors.size() < 3)
            return;
    }

    if (!options.recognizeShapes || !path.closed)
        return;

    // Round: many short sides, every raw point about as far from the middle.
    if (path.anchors.size() >= 5 && raw.size() >= 12)
    {
        double sx = 0.0;
        double sy = 0.0;
        for (const Point& p : raw)
        {
            sx += p.x;
            sy += p.y;
        }
        const Point centre(sx / raw.size(), sy / raw.size());

        double mean = 0.0;
        for (const Point& p : raw)
            mean += distance(p, centre);
        mean /= raw.size();
        if (mean < 1.0e-9)
            return;

        double variance = 0.0;
        for (const Point& p : raw)
        {
            const double d = distance(p, centre) - mean;
            variance += d * d;
        }
        const double spread = std::sqrt(variance / raw.size()) / mean;

        if (spread < 0.2)
        {
            double left = raw[0].x;
            double top = raw[0].y;
            double right = left;
            double bottom = top;
            for (const Point& p : raw)
            {
                left = std::min(left, p.x);
                top = std::min(top, p.y);
                right = std::max(right, p.x);
                bottom = std::max(bottom, p.y);
            }
            makeOval(path, left, top, right, bottom);
        }
        return;
    }

    // Four square corners: a rectangle, squared up to the axes when it was
    // nearly there.
    bool axisAligned = false;
    if (isRectangle(path, axisAligned) && axisAligned)
    {
        double left = path.anchors[0].position.x;
        double top = path.anchors[0].position.y;
        double right = left;
        double bottom = top;
        for (const Anchor& anchor : path.anchors)
        {
            left = std::min(left, anchor.position.x);
            top = std::min(top, anchor.position.y);
            right = std::max(right, anchor.position.x);
            bottom = std::max(bottom, anchor.position.y);
        }
        path.anchors.clear();
        path.anchors.push_back(Anchor(Point(left, top)));
        path.anchors.push_back(Anchor(Point(right, top)));
        path.anchors.push_back(Anchor(Point(right, bottom)));
        path.anchors.push_back(Anchor(Point(left, bottom)));
        path.closed = true;
    }
}

} // namespace fla
