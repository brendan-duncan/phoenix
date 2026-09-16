#include "test_util.h"

#include "../src/edit/editable_path.h"
#include "../src/parser/path_parser.h"
#include "../src/writer/edge_writer.h"

#include <memory>
#include <string>

using fla::Anchor;
using fla::EditablePath;
using fla::Point;

namespace {

std::unique_ptr<fla::Edge> parseEdge(const std::string& data)
{
    PathParser parser;
    return std::unique_ptr<fla::Edge>(parser.parse(data, nullptr));
}

bool near(double a, double b)
{
    return std::fabs(a - b) < 1.0e-6;
}

bool samePoint(const Point& a, const Point& b)
{
    return near(a.x, b.x) && near(a.y, b.y);
}

} // namespace

TEST(anchor_starts_straight)
{
    Anchor anchor(Point(10.0, 20.0));

    CHECK(!anchor.hasInCurve());
    CHECK(!anchor.hasOutCurve());
    CHECK(samePoint(anchor.inHandle, anchor.position));
}

TEST(anchor_translate_moves_handles_too)
{
    Anchor anchor(Point(10.0, 10.0));
    anchor.outHandle = Point(20.0, 10.0);
    anchor.inHandle = Point(0.0, 10.0);

    anchor.translate(5.0, -3.0);

    CHECK(samePoint(anchor.position, Point(15.0, 7.0)));
    CHECK(samePoint(anchor.outHandle, Point(25.0, 7.0)));
    CHECK(samePoint(anchor.inHandle, Point(5.0, 7.0)));
}

TEST(smooth_anchor_mirrors_its_handles)
{
    Anchor anchor(Point(0.0, 0.0));
    anchor.smooth = true;
    anchor.inHandle = Point(-10.0, 0.0);

    // Dragging the out handle up swings the in handle to stay opposite, keeping
    // its own length.
    anchor.setOutHandle(Point(0.0, 5.0));

    CHECK(samePoint(anchor.outHandle, Point(0.0, 5.0)));
    CHECK(samePoint(anchor.inHandle, Point(0.0, -10.0)));
}

TEST(corner_anchor_leaves_the_other_handle_alone)
{
    Anchor anchor(Point(0.0, 0.0));
    anchor.smooth = false;
    anchor.inHandle = Point(-10.0, 0.0);

    anchor.setOutHandle(Point(0.0, 5.0));

    CHECK(samePoint(anchor.outHandle, Point(0.0, 5.0)));
    // The two sides are independent, which is what makes it a corner.
    CHECK(samePoint(anchor.inHandle, Point(-10.0, 0.0)));
}

TEST(path_of_lines_becomes_straight_anchors)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0|100 0|100 100");
    CHECK(edge != nullptr);
    if (!edge || edge->paths.empty())
        return;

    const EditablePath path = EditablePath::fromPath(*edge->paths[0]);

    CHECK(path.anchors.size() == 3);
    CHECK(!path.closed);
    if (path.anchors.size() != 3)
        return;

    for (const Anchor& anchor : path.anchors)
    {
        CHECK(!anchor.hasInCurve());
        CHECK(!anchor.hasOutCurve());
    }
    CHECK(samePoint(path.anchors[1].position, Point(5.0, 0.0)));
}

TEST(cubic_splits_into_two_anchor_handles)
{
    // One cubic carries the first anchor's out handle and the second's in
    // handle; the model puts each on the anchor it belongs to.
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0(100 0 200 100 300 100)");
    CHECK(edge != nullptr);
    if (!edge || edge->paths.empty())
        return;

    const EditablePath path = EditablePath::fromPath(*edge->paths[0]);

    CHECK(path.anchors.size() == 2);
    if (path.anchors.size() != 2)
        return;

    CHECK(samePoint(path.anchors[0].position, Point(0.0, 0.0)));
    CHECK(samePoint(path.anchors[0].outHandle, Point(5.0, 0.0)));
    CHECK(samePoint(path.anchors[1].inHandle, Point(10.0, 5.0)));
    CHECK(samePoint(path.anchors[1].position, Point(15.0, 5.0)));
}

TEST(quadratic_is_raised_to_a_cubic)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0[60 60 120 0");
    CHECK(edge != nullptr);
    if (!edge || edge->paths.empty())
        return;

    const EditablePath path = EditablePath::fromPath(*edge->paths[0]);
    CHECK(path.anchors.size() == 2);
    if (path.anchors.size() != 2)
        return;

    // Control at (3,3) between (0,0) and (6,0): the cubic controls sit two
    // thirds of the way from each end toward it.
    CHECK(samePoint(path.anchors[0].outHandle, Point(2.0, 2.0)));
    CHECK(samePoint(path.anchors[1].inHandle, Point(4.0, 2.0)));
}

TEST(a_path_ending_where_it_started_is_closed)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0|100 0|100 100|0 100|0 0");
    CHECK(edge != nullptr);
    if (!edge || edge->paths.empty())
        return;

    const EditablePath path = EditablePath::fromPath(*edge->paths[0]);

    CHECK(path.closed);
    // The repeated final point is the first anchor, not a fifth one.
    CHECK(path.anchors.size() == 4);
}

TEST(anchors_round_trip_back_to_a_path)
{
    const char* sources[] = {
        "!0 0|100 0|100 100",
        "!0 0(100 0 200 100 300 100)",
        "!0 0|100 0|100 100|0 100|0 0",
        "!0 0(100 0 200 100 300 100)|400 100",
    };

    for (const char* source : sources)
    {
        std::unique_ptr<fla::Edge> first = parseEdge(source);
        CHECK(first != nullptr);
        if (!first || first->paths.empty())
            continue;

        const EditablePath path = EditablePath::fromPath(*first->paths[0]);

        fla::Edge rebuilt(nullptr);
        path.applyTo(rebuilt);

        // Reading the rebuilt geometry back must give the same anchors, so an
        // edit that touches nothing changes nothing.
        CHECK(rebuilt.paths.size() == 1);
        if (rebuilt.paths.size() != 1)
            continue;

        const EditablePath again = EditablePath::fromPath(*rebuilt.paths[0]);

        CHECK(again.anchors.size() == path.anchors.size());
        CHECK(again.closed == path.closed);
        if (again.anchors.size() != path.anchors.size())
        {
            std::printf("    source: %s (%d anchors vs %d)\n", source,
                static_cast<int>(path.anchors.size()),
                static_cast<int>(again.anchors.size()));
            continue;
        }

        for (size_t i = 0; i < path.anchors.size(); ++i)
        {
            CHECK(samePoint(again.anchors[i].position, path.anchors[i].position));
            CHECK(samePoint(again.anchors[i].inHandle, path.anchors[i].inHandle));
            CHECK(samePoint(again.anchors[i].outHandle, path.anchors[i].outHandle));
        }
    }
}

TEST(applying_anchors_replaces_what_was_there)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0|100 0");
    CHECK(edge != nullptr);
    if (!edge)
        return;

    EditablePath path;
    path.anchors.push_back(Anchor(Point(1.0, 2.0)));
    path.anchors.push_back(Anchor(Point(3.0, 4.0)));
    path.applyTo(*edge);

    // One path, not two: the old geometry is gone rather than appended to.
    CHECK(edge->paths.size() == 1);
    if (edge->paths.empty())
        return;
    CHECK(edge->paths[0]->segments.size() == 2);
}

TEST(a_closed_path_writes_its_final_segment_home)
{
    EditablePath path;
    path.anchors.push_back(Anchor(Point(0.0, 0.0)));
    path.anchors.push_back(Anchor(Point(10.0, 0.0)));
    path.anchors.push_back(Anchor(Point(10.0, 10.0)));
    path.closed = true;

    fla::Edge edge(nullptr);
    path.applyTo(edge);

    CHECK(edge.paths.size() == 1);
    if (edge.paths.empty())
        return;

    // Move plus three lines: the last one returns to the start so the outline
    // encloses an area.
    CHECK(edge.paths[0]->segments.size() == 4);
    CHECK(fla::EdgeWriter::writeEdge(edge) == "!0 0|200 0|200 200|0 0");
}

TEST(an_empty_path_clears_the_edge)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0|100 0");
    CHECK(edge != nullptr);
    if (!edge)
        return;

    EditablePath().applyTo(*edge);
    CHECK(edge->paths.empty());
}
