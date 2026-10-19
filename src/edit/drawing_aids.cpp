#include "drawing_aids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace fla::aids {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string number(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

double toNumber(const std::string& text, double otherwise)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end && end != text.c_str() && std::isfinite(value) ? value : otherwise;
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
    {
        if (!part.empty())
            parts.push_back(part);
    }
    return parts;
}

/// The "key=value" words of \a text, in order.
std::vector<std::pair<std::string, std::string>> pairs(const std::string& text)
{
    std::vector<std::pair<std::string, std::string>> found;
    for (const std::string& word : split(text, ' '))
    {
        const size_t equals = word.find('=');
        if (equals == std::string::npos)
            found.emplace_back(word, std::string());
        else
            found.emplace_back(word.substr(0, equals), word.substr(equals + 1));
    }
    return found;
}

std::string pointText(const Point& point)
{
    return number(point.x) + "," + number(point.y);
}

Point pointOf(const std::string& text, const Point& otherwise)
{
    const size_t comma = text.find(',');
    if (comma == std::string::npos)
        return otherwise;
    return Point(toNumber(text.substr(0, comma), otherwise.x), toNumber(text.substr(comma + 1), otherwise.y));
}

/// The reflection across the line through \a cx, \a cy at \a lineAngle
/// radians from the x axis.
Transform reflection(double cx, double cy, double lineAngle)
{
    const double ux = std::cos(lineAngle);
    const double uy = std::sin(lineAngle);
    Transform t(ux * ux - uy * uy, 2.0 * ux * uy, 2.0 * ux * uy, uy * uy - ux * ux, 0.0, 0.0);
    t.tx = cx - (t.m11 * cx + t.m21 * cy);
    t.ty = cy - (t.m12 * cx + t.m22 * cy);
    return t;
}

/// The turn by \a angle radians about \a cx, \a cy.
Transform rotation(double cx, double cy, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    Transform t(c, s, -s, c, 0.0, 0.0);
    t.tx = cx - (t.m11 * cx + t.m21 * cy);
    t.ty = cy - (t.m12 * cx + t.m22 * cy);
    return t;
}

double length(double x, double y)
{
    return std::hypot(x, y);
}

/// \a x, \a y one unit long; false when it has no length to speak of.
bool normalize(double& x, double& y)
{
    const double l = length(x, y);
    if (!(l > 1.0e-9) || !std::isfinite(l))
        return false;
    x /= l;
    y /= l;
    return true;
}

Constraint line(const Point& origin, double dx, double dy)
{
    Constraint constraint;
    if (!normalize(dx, dy))
        return constraint;
    constraint.kind = Constraint::Kind::Line;
    constraint.origin = origin;
    constraint.direction = Point(dx, dy);
    return constraint;
}

Constraint ellipseOf(const DrawingGuide& guide)
{
    Constraint constraint;
    const double rx = length(guide.b.x - guide.a.x, guide.b.y - guide.a.y);
    const double ry = std::fabs(guide.minor);
    if (!(rx > 1.0e-9) || !(ry > 1.0e-9))
        return constraint;
    constraint.kind = Constraint::Kind::Ellipse;
    constraint.center = guide.a;
    constraint.rx = rx;
    constraint.ry = ry;
    constraint.angle = std::atan2(guide.b.y - guide.a.y, guide.b.x - guide.a.x);
    return constraint;
}

/// \a point in the ellipse's own frame: along its first radius and its
/// second.
void ellipseLocal(const Constraint& ellipse, const Point& point, double& u, double& v)
{
    const double dx = point.x - ellipse.center.x;
    const double dy = point.y - ellipse.center.y;
    const double c = std::cos(ellipse.angle);
    const double s = std::sin(ellipse.angle);
    u = dx * c + dy * s;
    v = -dx * s + dy * c;
}

double distance(const Point& a, const Point& b)
{
    return length(b.x - a.x, b.y - a.y);
}

/// How far apart two directions are, either way along them: the sine of the
/// angle between them.
double across(double ax, double ay, double bx, double by)
{
    return std::fabs(ax * by - ay * bx);
}

} // namespace

// ------------------------------------------------------------- symmetry

bool Symmetry::operator==(const Symmetry& other) const
{
    return on == other.on && mode == other.mode && x == other.x && y == other.y && angle == other.angle &&
           copies == other.copies && mirror == other.mirror;
}

std::vector<Transform> symmetryCopies(const Symmetry& symmetry)
{
    std::vector<Transform> copies;
    if (!symmetry.on)
        return copies;
    const double turn = symmetry.angle * kPi / 180.0;
    // The upright axis, turned with the rest.
    const double upright = turn + kPi / 2.0;
    switch (symmetry.mode)
    {
    case SymmetryMode::Vertical:
        copies.push_back(reflection(symmetry.x, symmetry.y, upright));
        break;
    case SymmetryMode::Horizontal:
        copies.push_back(reflection(symmetry.x, symmetry.y, turn));
        break;
    case SymmetryMode::Both:
        copies.push_back(reflection(symmetry.x, symmetry.y, upright));
        copies.push_back(reflection(symmetry.x, symmetry.y, turn));
        copies.push_back(rotation(symmetry.x, symmetry.y, kPi));
        break;
    case SymmetryMode::Radial:
    {
        const int n = std::clamp(symmetry.copies, 2, 64);
        for (int k = 1; k < n; ++k)
            copies.push_back(rotation(symmetry.x, symmetry.y, 2.0 * kPi * k / n));
        // Mirrored, the stroke is reflected across each of n axes as well,
        // the first the upright one.
        if (symmetry.mirror)
        {
            for (int k = 0; k < n; ++k)
                copies.push_back(reflection(symmetry.x, symmetry.y, upright + kPi * k / n));
        }
        break;
    }
    }
    return copies;
}

bool mirrors(const Transform& transform)
{
    return transform.m11 * transform.m22 - transform.m12 * transform.m21 < 0.0;
}

Point apply(const Transform& transform, const Point& point)
{
    return Point(transform.m11 * point.x + transform.m21 * point.y + transform.tx,
        transform.m12 * point.x + transform.m22 * point.y + transform.ty);
}

namespace {

const char* modeCode(SymmetryMode mode)
{
    switch (mode)
    {
    case SymmetryMode::Horizontal: return "horizontal";
    case SymmetryMode::Both: return "both";
    case SymmetryMode::Radial: return "radial";
    case SymmetryMode::Vertical: break;
    }
    return "vertical";
}

} // namespace

std::string toText(const Symmetry& symmetry)
{
    std::string text = std::string("on=") + (symmetry.on ? "1" : "0");
    text += std::string(" mode=") + modeCode(symmetry.mode);
    text += " x=" + number(symmetry.x) + " y=" + number(symmetry.y);
    text += " angle=" + number(symmetry.angle);
    text += " copies=" + std::to_string(symmetry.copies);
    text += std::string(" mirror=") + (symmetry.mirror ? "1" : "0");
    return text;
}

Symmetry symmetryFromText(const std::string& text)
{
    Symmetry symmetry;
    for (const auto& [key, value] : pairs(text))
    {
        if (key == "on")
            symmetry.on = value == "1";
        else if (key == "mode")
        {
            for (SymmetryMode mode :
                {SymmetryMode::Vertical, SymmetryMode::Horizontal, SymmetryMode::Both, SymmetryMode::Radial})
            {
                if (value == modeCode(mode))
                    symmetry.mode = mode;
            }
        }
        else if (key == "x")
            symmetry.x = toNumber(value, symmetry.x);
        else if (key == "y")
            symmetry.y = toNumber(value, symmetry.y);
        else if (key == "angle")
            symmetry.angle = toNumber(value, symmetry.angle);
        else if (key == "copies")
            symmetry.copies = std::clamp(static_cast<int>(toNumber(value, symmetry.copies)), 2, 64);
        else if (key == "mirror")
            symmetry.mirror = value == "1";
    }
    return symmetry;
}

// --------------------------------------------------------------- guides

bool DrawingGuide::operator==(const DrawingGuide& other) const
{
    return kind == other.kind && a.x == other.a.x && a.y == other.a.y && b.x == other.b.x && b.y == other.b.y &&
           c.x == other.c.x && c.y == other.c.y && minor == other.minor && grid == other.grid;
}

bool DrawingGuides::operator==(const DrawingGuides& other) const
{
    return guides == other.guides && snap == other.snap && hidden == other.hidden && magnetism == other.magnetism;
}

namespace {

const char* kindCode(GuideKind kind)
{
    switch (kind)
    {
    case GuideKind::ParallelRuler: return "parallel";
    case GuideKind::Ellipse: return "ellipse";
    case GuideKind::ConcentricEllipse: return "concentric";
    case GuideKind::Perspective1: return "perspective1";
    case GuideKind::Perspective2: return "perspective2";
    case GuideKind::Perspective3: return "perspective3";
    case GuideKind::Ruler: break;
    }
    return "ruler";
}

const GuideKind kKinds[] = {GuideKind::Ruler, GuideKind::ParallelRuler, GuideKind::Ellipse,
    GuideKind::ConcentricEllipse, GuideKind::Perspective1, GuideKind::Perspective2, GuideKind::Perspective3};

} // namespace

const char* kindName(GuideKind kind)
{
    switch (kind)
    {
    case GuideKind::ParallelRuler: return "Parallel Ruler";
    case GuideKind::Ellipse: return "Ellipse";
    case GuideKind::ConcentricEllipse: return "Concentric Ellipses";
    case GuideKind::Perspective1: return "1-Point Perspective";
    case GuideKind::Perspective2: return "2-Point Perspective";
    case GuideKind::Perspective3: return "3-Point Perspective";
    case GuideKind::Ruler: break;
    }
    return "Ruler";
}

DrawingGuide makeGuide(GuideKind kind, const Point& center, double size)
{
    DrawingGuide guide;
    guide.kind = kind;
    const double half = size / 2.0;
    switch (kind)
    {
    case GuideKind::Ruler:
    case GuideKind::ParallelRuler:
        guide.a = Point(center.x - half, center.y);
        guide.b = Point(center.x + half, center.y);
        break;
    case GuideKind::Ellipse:
    case GuideKind::ConcentricEllipse:
        guide.a = center;
        guide.b = Point(center.x + half, center.y);
        guide.minor = half / 2.0;
        break;
    case GuideKind::Perspective1:
        guide.a = center;
        break;
    case GuideKind::Perspective2:
        guide.a = Point(center.x - size, center.y);
        guide.b = Point(center.x + size, center.y);
        break;
    case GuideKind::Perspective3:
        guide.a = Point(center.x - size, center.y - half);
        guide.b = Point(center.x + size, center.y - half);
        guide.c = Point(center.x, center.y + size * 1.5);
        break;
    }
    return guide;
}

DrawingGuide moved(const DrawingGuide& guide, double dx, double dy)
{
    DrawingGuide out = guide;
    out.a.translate(dx, dy);
    out.b.translate(dx, dy);
    out.c.translate(dx, dy);
    return out;
}

std::string toText(const DrawingGuides& guides)
{
    std::string text = std::string("snap=") + (guides.snap ? "1" : "0");
    text += std::string(" hidden=") + (guides.hidden ? "1" : "0");
    text += " magnetism=" + std::to_string(guides.magnetism);
    for (const DrawingGuide& guide : guides.guides)
    {
        text += ";";
        text += kindCode(guide.kind);
        text += " a=" + pointText(guide.a) + " b=" + pointText(guide.b) + " c=" + pointText(guide.c);
        text += " minor=" + number(guide.minor);
        text += std::string(" grid=") + (guide.grid ? "1" : "0");
    }
    return text;
}

DrawingGuides guidesFromText(const std::string& text)
{
    DrawingGuides guides;
    const std::vector<std::string> parts = split(text, ';');
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto words = pairs(parts[i]);
        if (i == 0)
        {
            for (const auto& [key, value] : words)
            {
                if (key == "snap")
                    guides.snap = value != "0";
                else if (key == "hidden")
                    guides.hidden = value == "1";
                else if (key == "magnetism")
                    guides.magnetism = std::clamp(static_cast<int>(toNumber(value, guides.magnetism)), 0, 100);
            }
            continue;
        }
        if (words.empty())
            continue;
        // A kind this version does not know is passed over.
        bool known = false;
        DrawingGuide guide;
        for (GuideKind kind : kKinds)
        {
            if (words.front().first == kindCode(kind))
            {
                guide.kind = kind;
                known = true;
            }
        }
        if (!known)
            continue;
        for (const auto& [key, value] : words)
        {
            if (key == "a")
                guide.a = pointOf(value, guide.a);
            else if (key == "b")
                guide.b = pointOf(value, guide.b);
            else if (key == "c")
                guide.c = pointOf(value, guide.c);
            else if (key == "minor")
                guide.minor = toNumber(value, guide.minor);
            else if (key == "grid")
                guide.grid = value != "0";
        }
        guides.guides.push_back(guide);
    }
    return guides;
}

// ------------------------------------------------------------- snapping

Point Constraint::project(const Point& point) const
{
    switch (kind)
    {
    case Kind::Line:
    {
        const double t = (point.x - origin.x) * direction.x + (point.y - origin.y) * direction.y;
        return Point(origin.x + direction.x * t, origin.y + direction.y * t);
    }
    case Kind::Ellipse:
    {
        double u = 0.0;
        double v = 0.0;
        ellipseLocal(*this, point, u, v);
        // In the space where the ellipse is a unit circle, the point is
        // taken out (or in) to the circle along its own direction.
        double nu = u / rx;
        double nv = v / ry;
        if (!normalize(nu, nv))
        {
            nu = 1.0;
            nv = 0.0;
        }
        u = nu * rx;
        v = nv * ry;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return Point(center.x + u * c - v * s, center.y + u * s + v * c);
    }
    case Kind::None: break;
    }
    return point;
}

Constraint constraintFor(const DrawingGuides& guides, const std::vector<Point>& stroke, double pixel)
{
    Constraint none;
    if (guides.hidden || !guides.snap || guides.guides.empty() || stroke.empty())
        return none;
    pixel = pixel > 0.0 && std::isfinite(pixel) ? pixel : 1.0;
    const Point& start = stroke.front();

    // A ruler or an ellipse is a straight edge laid on the paper: a stroke
    // begun within reach of one runs along it, the nearest if several.
    const double reach = 32.0 * pixel;
    Constraint fixed;
    double nearest = reach;
    for (const DrawingGuide& guide : guides.guides)
    {
        Constraint candidate;
        if (guide.kind == GuideKind::Ruler)
            candidate = line(guide.a, guide.b.x - guide.a.x, guide.b.y - guide.a.y);
        else if (guide.kind == GuideKind::Ellipse)
            candidate = ellipseOf(guide);
        if (candidate.kind == Constraint::Kind::None)
            continue;
        const double d = distance(start, candidate.project(start));
        if (d <= nearest)
        {
            nearest = d;
            fixed = candidate;
        }
    }
    if (fixed.kind != Constraint::Kind::None)
        return fixed;

    // The rest give a direction wherever the stroke begins; which one is
    // up to how the stroke sets off.
    Point ahead;
    bool moved = false;
    for (const Point& point : stroke)
    {
        if (distance(start, point) >= 4.0 * pixel)
        {
            ahead = point;
            moved = true;
            break;
        }
    }
    if (!moved)
        return none;
    double dx = ahead.x - start.x;
    double dy = ahead.y - start.y;
    normalize(dx, dy);

    Constraint best;
    double bestScore = 2.0;
    const auto consider = [&](const Constraint& candidate, double tx, double ty) {
        if (candidate.kind == Constraint::Kind::None || !normalize(tx, ty))
            return;
        const double score = across(dx, dy, tx, ty);
        if (score < bestScore)
        {
            bestScore = score;
            best = candidate;
        }
    };
    const auto lineTo = [&](double tx, double ty) { consider(line(start, tx, ty), tx, ty); };
    const auto toward = [&](const Point& vanishing) { lineTo(vanishing.x - start.x, vanishing.y - start.y); };

    for (const DrawingGuide& guide : guides.guides)
    {
        switch (guide.kind)
        {
        case GuideKind::ParallelRuler:
            lineTo(guide.b.x - guide.a.x, guide.b.y - guide.a.y);
            break;
        case GuideKind::ConcentricEllipse:
        {
            Constraint ellipse = ellipseOf(guide);
            if (ellipse.kind == Constraint::Kind::None)
                break;
            double u = 0.0;
            double v = 0.0;
            ellipseLocal(ellipse, start, u, v);
            const double scale = std::hypot(u / ellipse.rx, v / ellipse.ry);
            if (!(scale > 1.0e-9))
                break;
            ellipse.rx *= scale;
            ellipse.ry *= scale;
            // The tangent there, in the ellipse's frame and then the stage's.
            const double tu = -v * ellipse.rx / ellipse.ry;
            const double tv = u * ellipse.ry / ellipse.rx;
            const double c = std::cos(ellipse.angle);
            const double s = std::sin(ellipse.angle);
            consider(ellipse, tu * c - tv * s, tu * s + tv * c);
            break;
        }
        case GuideKind::Perspective1:
            toward(guide.a);
            lineTo(1.0, 0.0);
            lineTo(0.0, 1.0);
            break;
        case GuideKind::Perspective2:
        {
            toward(guide.a);
            toward(guide.b);
            // Upright is square to the horizon through the two points.
            double hx = guide.b.x - guide.a.x;
            double hy = guide.b.y - guide.a.y;
            if (!normalize(hx, hy))
            {
                hx = 1.0;
                hy = 0.0;
            }
            lineTo(-hy, hx);
            break;
        }
        case GuideKind::Perspective3:
            toward(guide.a);
            toward(guide.b);
            toward(guide.c);
            break;
        case GuideKind::Ruler:
        case GuideKind::Ellipse:
            break;
        }
    }
    return best;
}

std::vector<Point> snapStroke(const DrawingGuides& guides, const std::vector<Point>& stroke, double pixel)
{
    const Constraint constraint = constraintFor(guides, stroke, pixel);
    if (constraint.kind == Constraint::Kind::None)
        return stroke;
    const double pull = std::clamp(guides.magnetism, 0, 100) / 100.0;
    std::vector<Point> out;
    out.reserve(stroke.size());
    for (const Point& point : stroke)
    {
        const Point on = constraint.project(point);
        out.emplace_back(point.x + (on.x - point.x) * pull, point.y + (on.y - point.y) * pull);
    }
    return out;
}

std::vector<std::pair<Point, Point>> fanLines(const Point& vanishing, int count, double length)
{
    std::vector<std::pair<Point, Point>> lines;
    count = std::max(count, 1);
    for (int i = 0; i < count; ++i)
    {
        const double angle = kPi * i / count;
        const double dx = std::cos(angle) * length;
        const double dy = std::sin(angle) * length;
        lines.emplace_back(Point(vanishing.x - dx, vanishing.y - dy), Point(vanishing.x + dx, vanishing.y + dy));
    }
    return lines;
}

} // namespace fla::aids
