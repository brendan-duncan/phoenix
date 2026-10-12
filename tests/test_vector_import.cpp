#include "test_util.h"

#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/edit/vector_import.h"

#include <cmath>

namespace {

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::fabs(a - b) <= tolerance;
}

} // namespace

TEST(svg_paths_shapes_and_styles_are_read)
{
    const std::string svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 100 50">
  <g transform="translate(10,5)" fill="#f00">
    <rect x="0" y="0" width="10" height="20"/>
    <circle cx="30" cy="10" r="5" style="fill:rgb(0,0,255);stroke:black;stroke-width:2"/>
  </g>
  <path d="M0 0 h10 v10 z m20 0 l5 5" fill="none" stroke="#0f0"/>
  <line x1="0" y1="0" x2="4" y2="0" stroke="blue"/>
  <polygon points="0,0 4,0 4,4" fill="none"/>
  <text>left out</text>
</svg>)svg";
    const fla::vectors::Artwork art = fla::vectors::readSvg(svg);
    CHECK(near(art.width, 200.0) && near(art.height, 100.0));
    // The polygon paints nothing: three paths remain, then the line.
    CHECK(art.paths.size() == 4);
    CHECK(art.notes.size() == 1);

    // The rect: the group's translate, then the view box's doubling.
    const fla::vectors::Painted& rect = art.paths[0];
    CHECK(rect.fill && (*rect.fill)[0] == 255 && (*rect.fill)[2] == 0 && !rect.stroke);
    CHECK(near(rect.subpaths[0].segments[0].points[0].x, 20.0) && near(rect.subpaths[0].segments[0].points[0].y, 10.0));
    CHECK(near(rect.subpaths[0].segments[2].points[0].x, 40.0) && near(rect.subpaths[0].segments[2].points[0].y, 50.0));

    const fla::vectors::Painted& circle = art.paths[1];
    CHECK(circle.fill && (*circle.fill)[2] == 255 && circle.stroke && near(circle.strokeWidth, 4.0));
    CHECK(circle.subpaths[0].closed && circle.subpaths[0].segments.size() == 5);

    // Two subpaths; the relative move starts from the closed one's start.
    const fla::vectors::Painted& path = art.paths[2];
    CHECK(!path.fill && path.stroke && (*path.stroke)[1] == 255);
    CHECK(path.subpaths.size() == 2 && path.subpaths[0].closed && !path.subpaths[1].closed);
    CHECK(near(path.subpaths[1].segments[0].points[0].x, 40.0) && near(path.subpaths[1].segments[1].points[0].x, 50.0));

    CHECK(!art.paths[3].fill && art.paths[3].stroke);
}

TEST(svg_arcs_land_on_their_end_and_stay_on_the_circle)
{
    const fla::Point from(10.0, 0.0);
    const fla::Point to(-10.0, 0.0);
    const std::vector<fla::vectors::Segment> half = fla::vectors::arcToCubics(from, 10.0, 10.0, 0.0, false, true, to);
    CHECK(half.size() == 2);
    CHECK(near(half.back().points[2].x, -10.0) && near(half.back().points[2].y, 0.0));
    // Sweeping from (10,0) toward positive angles: through (0,10).
    CHECK(near(half[0].points[2].x, 0.0, 1.0e-6) && near(half[0].points[2].y, 10.0, 1.0e-6));

    // Radii too small to reach are scaled up to fit.
    const std::vector<fla::vectors::Segment> scaled = fla::vectors::arcToCubics(from, 1.0, 1.0, 0.0, false, false, to);
    CHECK(!scaled.empty() && near(scaled.back().points[2].x, -10.0));
    CHECK(near(scaled[0].points[2].y, -10.0, 1.0e-6));
}

TEST(pdf_page_paths_are_read_upright)
{
    const std::string content = "q 1 0 0 1 10 0 cm 1 0 0 rg 0 0 50 20 re f Q 0 0 1 RG 3 w 0 100 m 50 100 l S";
    std::string pdf = "%PDF-1.4\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                      "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                      "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R >> endobj\n"
                      "4 0 obj << /Length " +
        std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream\nendobj\n%%EOF\n";
    const fla::vectors::Artwork art = fla::vectors::readPdf(std::vector<uint8_t>(pdf.begin(), pdf.end()));
    CHECK(near(art.width, 200.0) && near(art.height, 100.0));
    CHECK(art.paths.size() == 2);
    const fla::vectors::Painted& rect = art.paths[0];
    CHECK(rect.fill && (*rect.fill)[0] == 255 && !rect.stroke);
    // The page's bottom left is the stage's (0, 100).
    CHECK(near(rect.subpaths[0].segments[0].points[0].x, 10.0) && near(rect.subpaths[0].segments[0].points[0].y, 100.0));
    CHECK(near(rect.subpaths[0].segments[2].points[0].y, 80.0));
    const fla::vectors::Painted& line = art.paths[1];
    CHECK(!line.fill && line.stroke && (*line.stroke)[2] == 255 && near(line.strokeWidth, 3.0));
    CHECK(near(line.subpaths[0].segments[0].points[0].y, 0.0));

    const fla::vectors::Artwork none = fla::vectors::readPdf(std::vector<uint8_t>{'x', 'y'});
    CHECK(none.paths.empty() && !none.notes.empty());
}

TEST(painted_paths_become_drawing_objects_with_holes)
{
    fla::vectors::Painted ring;
    const auto square = [](double l, double t, double r, double b, bool clockwise) {
        fla::vectors::Subpath s;
        s.closed = true;
        s.segments.push_back({fla::vectors::Segment::Kind::Move, {fla::Point(l, t)}});
        if (clockwise)
            s.segments.insert(s.segments.end(), {{fla::vectors::Segment::Kind::Line, {fla::Point(r, t)}},
                {fla::vectors::Segment::Kind::Line, {fla::Point(r, b)}}, {fla::vectors::Segment::Kind::Line, {fla::Point(l, b)}}});
        else
            s.segments.insert(s.segments.end(), {{fla::vectors::Segment::Kind::Line, {fla::Point(l, b)}},
                {fla::vectors::Segment::Kind::Line, {fla::Point(r, b)}}, {fla::vectors::Segment::Kind::Line, {fla::Point(r, t)}}});
        return s;
    };
    // Both wound the same way: the inner one is still a hole.
    ring.subpaths = {square(0, 0, 100, 100, true), square(25, 25, 75, 75, true)};
    ring.fill = fla::vectors::Color{10, 20, 30, 255};
    ring.stroke = fla::vectors::Color{0, 0, 0, 255};
    ring.strokeWidth = 2.0;
    fla::Shape* shape = fla::vectors::toShape(ring);
    CHECK(shape != nullptr);
    CHECK(shape->isDrawingObject && shape->edges.size() == 2);
    CHECK(shape->fillsMap.count(1) == 1 && shape->strokesMap.count(1) == 1);
    const fla::Edge* outer = shape->edges[0];
    const fla::Edge* inner = shape->edges[1];
    // Opposite sides: the fill is between the two.
    CHECK((outer->fillStyle1 == 1) == (inner->fillStyle0 == 1));
    CHECK(outer->strokeStyle == 1 && inner->strokeStyle == 1);
    // Closed with a line back to the start.
    CHECK(outer->paths[0]->segments.size() == 5);
    CHECK(near(shape->localBounds.bottomRight.x, 100.0) && near(shape->localBounds.bottomRight.y, 100.0));
    delete shape;

    fla::vectors::Painted nothing;
    nothing.subpaths = {square(0, 0, 1, 1, true)};
    CHECK(fla::vectors::toShape(nothing) == nullptr);
}
