#include "test_util.h"

#include "../src/edit/drawing_aids.h"

#include <cmath>
#include <set>
#include <utility>

namespace aids = fla::aids;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) < tolerance;
}

bool nearPoint(const fla::Point& p, double x, double y, double tolerance = 1.0e-6)
{
    return near(p.x, x, tolerance) && near(p.y, y, tolerance);
}

/// Where \a point lands under each of \a copies, rounded, as a set.
std::set<std::pair<long, long>> images(const std::vector<fla::Transform>& copies, const fla::Point& point)
{
    std::set<std::pair<long, long>> found;
    for (const fla::Transform& copy : copies)
    {
        const fla::Point p = aids::apply(copy, point);
        found.insert({std::lround(p.x * 1000.0), std::lround(p.y * 1000.0)});
    }
    return found;
}

} // namespace

TEST(symmetry_off_makes_no_copies)
{
    aids::Symmetry symmetry;
    CHECK(aids::symmetryCopies(symmetry).empty());
    symmetry.on = true;
    CHECK(aids::symmetryCopies(symmetry).size() == 1);
}

TEST(symmetry_mirrors_across_its_axes)
{
    aids::Symmetry symmetry;
    symmetry.on = true;
    symmetry.x = 100.0;
    symmetry.y = 50.0;

    // Upright axis: left to right.
    std::vector<fla::Transform> copies = aids::symmetryCopies(symmetry);
    CHECK(copies.size() == 1 && aids::mirrors(copies[0]));
    CHECK(nearPoint(aids::apply(copies[0], fla::Point(70, 20)), 130, 20));

    // Level axis: top to bottom.
    symmetry.mode = aids::SymmetryMode::Horizontal;
    copies = aids::symmetryCopies(symmetry);
    CHECK(copies.size() == 1 && nearPoint(aids::apply(copies[0], fla::Point(70, 20)), 70, 80));

    // Both: the three other quarters.
    symmetry.mode = aids::SymmetryMode::Both;
    copies = aids::symmetryCopies(symmetry);
    CHECK(copies.size() == 3);
    CHECK((images(copies, fla::Point(70, 20)) ==
           std::set<std::pair<long, long>>{{130000, 20000}, {70000, 80000}, {130000, 80000}}));

    // The axis turned 90 degrees mirrors top to bottom instead.
    symmetry.mode = aids::SymmetryMode::Vertical;
    symmetry.angle = 90.0;
    copies = aids::symmetryCopies(symmetry);
    CHECK(nearPoint(aids::apply(copies[0], fla::Point(70, 20)), 70, 80));
}

TEST(radial_symmetry_turns_n_copies)
{
    aids::Symmetry symmetry;
    symmetry.on = true;
    symmetry.mode = aids::SymmetryMode::Radial;
    symmetry.copies = 4;
    std::vector<fla::Transform> copies = aids::symmetryCopies(symmetry);
    // Three more: the stroke itself is the fourth.
    CHECK(copies.size() == 3);
    for (const fla::Transform& copy : copies)
        CHECK(!aids::mirrors(copy));
    CHECK((images(copies, fla::Point(10, 0)) ==
           std::set<std::pair<long, long>>{{0, 10000}, {-10000, 0}, {0, -10000}}));

    // Six copies: each a sixth of a turn on, all at the same distance.
    symmetry.copies = 6;
    symmetry.x = 5.0;
    symmetry.y = 5.0;
    copies = aids::symmetryCopies(symmetry);
    CHECK(copies.size() == 5);
    for (const fla::Transform& copy : copies)
    {
        const fla::Point p = aids::apply(copy, fla::Point(15, 5));
        CHECK(near(std::hypot(p.x - 5.0, p.y - 5.0), 10.0));
    }
    CHECK(nearPoint(aids::apply(copies[0], fla::Point(15, 5)), 5 + 10 * std::cos(kPi / 3), 5 + 10 * std::sin(kPi / 3)));

    // Mirrored: twice as many, half of them mirrored.
    symmetry.mirror = true;
    copies = aids::symmetryCopies(symmetry);
    CHECK(copies.size() == 11);
    int mirrored = 0;
    for (const fla::Transform& copy : copies)
        mirrored += aids::mirrors(copy) ? 1 : 0;
    CHECK(mirrored == 6);
    // The first mirror is across the upright axis.
    CHECK(nearPoint(aids::apply(copies[5], fla::Point(15, 7)), -5, 7));

    // The count is kept to 2..64.
    symmetry.mirror = false;
    symmetry.copies = 1;
    CHECK(aids::symmetryCopies(symmetry).size() == 1);
    symmetry.copies = 500;
    CHECK(aids::symmetryCopies(symmetry).size() == 63);
}

TEST(symmetry_and_guides_round_trip_as_text)
{
    aids::Symmetry symmetry;
    symmetry.on = true;
    symmetry.mode = aids::SymmetryMode::Radial;
    symmetry.x = 275.5;
    symmetry.y = -12.25;
    symmetry.angle = 30.0;
    symmetry.copies = 8;
    symmetry.mirror = true;
    CHECK(aids::symmetryFromText(aids::toText(symmetry)) == symmetry);
    CHECK(aids::symmetryFromText("") == aids::Symmetry());

    aids::DrawingGuides guides;
    guides.magnetism = 65;
    guides.hidden = true;
    guides.snap = false;
    for (aids::GuideKind kind : {aids::GuideKind::Ruler, aids::GuideKind::ParallelRuler, aids::GuideKind::Ellipse,
             aids::GuideKind::ConcentricEllipse, aids::GuideKind::Perspective1, aids::GuideKind::Perspective2,
             aids::GuideKind::Perspective3})
        guides.guides.push_back(aids::makeGuide(kind, fla::Point(100.125, 200), 300));
    guides.guides[4].grid = false;
    CHECK(aids::guidesFromText(aids::toText(guides)) == guides);
    // A kind from a later version is passed over; the rest still load.
    const aids::DrawingGuides later = aids::guidesFromText("snap=1;spiral a=1,2;ruler a=0,0 b=10,0");
    CHECK(later.guides.size() == 1 && later.guides[0].kind == aids::GuideKind::Ruler);
    CHECK(aids::guidesFromText("") == aids::DrawingGuides());
}

TEST(a_stroke_begun_near_a_ruler_runs_along_it)
{
    aids::DrawingGuides guides;
    guides.guides.push_back(aids::makeGuide(aids::GuideKind::Ruler, fla::Point(0, 0), 200));
    // Begun 10 pixels off the ruler and wandering: it lands on the ruler.
    const std::vector<fla::Point> stroke = {{-50, 10}, {-20, 14}, {10, 6}, {40, 12}};
    std::vector<fla::Point> snapped = aids::snapStroke(guides, stroke, 1.0);
    CHECK(snapped.size() == stroke.size());
    for (size_t i = 0; i < stroke.size(); ++i)
        CHECK(nearPoint(snapped[i], stroke[i].x, 0));

    // Half magnetism: half way there.
    guides.magnetism = 50;
    snapped = aids::snapStroke(guides, stroke, 1.0);
    CHECK(nearPoint(snapped[1], -20, 7));

    // Out of reach, or with snapping off or the guides hidden: untouched.
    guides.magnetism = 100;
    const std::vector<fla::Point> far = {{0, 100}, {30, 110}};
    CHECK(aids::snapStroke(guides, far, 1.0)[1].y == 110);
    // The reach is in screen pixels: zoomed out, the same stroke is near.
    CHECK(near(aids::snapStroke(guides, far, 4.0)[1].y, 0));
    guides.snap = false;
    CHECK(aids::snapStroke(guides, stroke, 1.0)[1].y == 14);
    guides.snap = true;
    guides.hidden = true;
    CHECK(aids::snapStroke(guides, stroke, 1.0)[1].y == 14);
}

TEST(a_parallel_ruler_keeps_strokes_to_its_direction)
{
    aids::DrawingGuides guides;
    aids::DrawingGuide ruler;
    ruler.kind = aids::GuideKind::ParallelRuler;
    ruler.a = fla::Point(0, 0);
    ruler.b = fla::Point(10, 10);
    guides.guides.push_back(ruler);
    // Anywhere on the stage, a stroke runs at 45 degrees from where it began.
    const std::vector<fla::Point> stroke = {{500, 300}, {510, 312}, {540, 338}};
    const std::vector<fla::Point> snapped = aids::snapStroke(guides, stroke, 1.0);
    CHECK(nearPoint(snapped[0], 500, 300));
    for (const fla::Point& p : snapped)
        CHECK(near(p.x - 500, p.y - 300));
    // Too short to have a direction yet: left as it is.
    const std::vector<fla::Point> tap = {{500, 300}, {501, 302}};
    CHECK(aids::snapStroke(guides, tap, 1.0)[1].y == 302);
}

TEST(ellipse_guides_keep_strokes_on_the_ellipse)
{
    aids::DrawingGuides guides;
    aids::DrawingGuide ellipse = aids::makeGuide(aids::GuideKind::Ellipse, fla::Point(0, 0), 200);
    // 100 across its first radius, 50 down its second.
    CHECK(near(ellipse.minor, 50.0));
    guides.guides.push_back(ellipse);
    const std::vector<fla::Point> stroke = {{105, 0}, {90, 30}, {0, 60}};
    std::vector<fla::Point> snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.x * p.x / 10000.0 + p.y * p.y / 2500.0, 1.0));
    CHECK(nearPoint(snapped[0], 100, 0));
    CHECK(nearPoint(snapped[2], 0, 50));

    // Concentric: an ellipse of the same shape through where it begins.
    guides.guides[0].kind = aids::GuideKind::ConcentricEllipse;
    const std::vector<fla::Point> outer = {{200, 0}, {199, 8}, {150, 70}};
    snapped = aids::snapStroke(guides, outer, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.x * p.x / 40000.0 + p.y * p.y / 10000.0, 1.0));
}

TEST(perspective_guides_pick_the_direction_the_stroke_sets_off_in)
{
    aids::DrawingGuides guides;
    guides.guides.push_back(aids::makeGuide(aids::GuideKind::Perspective1, fla::Point(0, 0), 100));
    // Heading roughly toward the vanishing point: on the line to it.
    std::vector<fla::Point> stroke = {{100, 100}, {90, 92}, {50, 45}};
    aids::Constraint constraint = aids::constraintFor(guides, stroke, 1.0);
    CHECK(constraint.kind == aids::Constraint::Kind::Line);
    std::vector<fla::Point> snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.x, p.y));
    // Setting off nearly level: level.
    stroke = {{100, 100}, {110, 102}, {150, 97}};
    snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.y, 100));
    // Nearly upright: upright.
    stroke = {{100, 100}, {101, 120}, {97, 150}};
    snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.x, 100));

    // Two points: toward either, or square to the horizon between them.
    guides.guides[0] = aids::makeGuide(aids::GuideKind::Perspective2, fla::Point(0, 0), 100);
    stroke = {{0, 100}, {-10, 92}, {-30, 75}};
    snapped = aids::snapStroke(guides, stroke, 1.0);
    // The line from (0, 100) to the left point (-100, 0).
    for (const fla::Point& p : snapped)
        CHECK(near(p.y - 100, p.x));
    stroke = {{0, 100}, {1, 120}};
    CHECK(near(aids::snapStroke(guides, stroke, 1.0)[1].x, 0));

    // Three points: toward the third, below.
    guides.guides[0] = aids::makeGuide(aids::GuideKind::Perspective3, fla::Point(0, 0), 100);
    const fla::Point third = guides.guides[0].c;
    stroke = {{20, 0}, {21, 10}, {19, 30}};
    snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
    {
        // On the line from the start to the third point.
        const double cross = (p.x - 20) * (third.y - 0) - (p.y - 0) * (third.x - 20);
        CHECK(near(cross, 0, 1.0e-6));
    }
}

TEST(a_nearby_ruler_wins_over_perspective)
{
    aids::DrawingGuides guides;
    guides.guides.push_back(aids::makeGuide(aids::GuideKind::Perspective1, fla::Point(0, 0), 100));
    guides.guides.push_back(aids::makeGuide(aids::GuideKind::Ruler, fla::Point(0, 200), 200));
    const std::vector<fla::Point> stroke = {{-20, 205}, {-10, 230}, {0, 260}};
    const std::vector<fla::Point> snapped = aids::snapStroke(guides, stroke, 1.0);
    for (const fla::Point& p : snapped)
        CHECK(near(p.y, 200));
}

TEST(perspective_grid_fans_out_from_the_point)
{
    const auto lines = aids::fanLines(fla::Point(10, 20), 4, 100);
    CHECK(lines.size() == 4);
    for (const auto& [from, to] : lines)
    {
        CHECK(near((from.x + to.x) / 2, 10) && near((from.y + to.y) / 2, 20));
        CHECK(near(std::hypot(to.x - from.x, to.y - from.y), 200));
    }
}
