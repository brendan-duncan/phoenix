#pragma once

#include "../data/point.h"

#include <vector>

namespace fla {

class Shape;

/// A motion guide's path: the stroked lines of a guide layer's shapes,
/// flattened and joined end to end, which a classic tween on a guided layer
/// carries its object along. Places on it are distances along it, from its
/// start. Free of Qt.
class GuidePath
{
public:
    /// Reads the strokes of \a shapes, each placed by its own transform.
    /// Where they do not join up into one line, the longest is kept.
    void build(const std::vector<const Shape*>& shapes);

    /// Builds from lines already flattened, joining them as build does.
    void build(std::vector<std::vector<Point>> lines);

    bool empty() const { return _points.size() < 2; }

    double length() const { return _lengths.empty() ? 0.0 : _lengths.back(); }

    /// How far along the path its nearest point to \a point is.
    double nearest(const Point& point) const;

    /// The point \a distance along the path, held to its ends.
    Point at(double distance) const;

    /// The path's direction \a distance along it, in degrees.
    double angleAt(double distance) const;

    const std::vector<Point>& points() const { return _points; }

private:
    size_t segmentAt(double distance) const;

    std::vector<Point> _points;
    /// How far along the path each point is.
    std::vector<double> _lengths;
};

} // namespace fla
