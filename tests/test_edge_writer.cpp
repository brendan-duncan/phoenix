#include "test_util.h"

#include "../src/parser/path_parser.h"
#include "../src/writer/edge_writer.h"

#include <memory>
#include <string>

using fla::EdgeWriter;

namespace {

/// Parses an edge string and hands back an owning pointer, so tests do not leak
/// when a check fails.
std::unique_ptr<fla::Edge> parseEdge(const std::string& data)
{
    PathParser parser;
    return std::unique_ptr<fla::Edge>(parser.parse(data, nullptr));
}

/// Reads \a data back as a coordinate, the way PathParser would.
double readCoordinate(const std::string& data)
{
    int pos = 0;
    return PathParser::parseNumber(data, pos) / 20.0;
}

/// Writes a coordinate and reads it straight back.
double roundTripCoordinate(double pixels)
{
    return readCoordinate(EdgeWriter::formatCoordinate(pixels));
}

} // namespace

TEST(coordinate_whole_twips_use_decimal)
{
    // 1 pixel is 20 twips, and whole twips take the shorter decimal form.
    CHECK(EdgeWriter::formatCoordinate(0.0) == "0");
    CHECK(EdgeWriter::formatCoordinate(1.0) == "20");
    CHECK(EdgeWriter::formatCoordinate(5.0) == "100");
    CHECK(EdgeWriter::formatCoordinate(-5.0) == "-100");
    CHECK(EdgeWriter::formatCoordinate(0.05) == "1");
}

TEST(coordinate_fractions_use_hex_fixed_point)
{
    // Half a twip is 0x80 in the 8-bit fraction.
    CHECK(EdgeWriter::formatCoordinate(100.5 / 20.0) == "#64.80");

    // A quarter twip is 0x40.
    CHECK(EdgeWriter::formatCoordinate(0.25 / 20.0) == "#0.40");
}

TEST(coordinate_negative_fractions_use_twos_complement)
{
    // -100.5 twips floors to -101 (0xFFFF9B over 24 bits) with a +0.5 fraction,
    // which is how the reader recombines them.
    const std::string text = EdgeWriter::formatCoordinate(-100.5 / 20.0);
    CHECK(text == "#FFFF9B.80");
    CHECK_NEAR(readCoordinate(text), -100.5 / 20.0);
}

TEST(coordinate_round_trips)
{
    const double values[] = {
        0.0, 1.0, -1.0, 0.05, -0.05, 5.25, -5.25, 123.456, -123.456,
        0.0125, -0.0125, 1000.0, -1000.0, 0.001953125
    };

    for (double value : values)
    {
        // Everything survives to within the 1/256-twip grid the format stores.
        const double read = roundTripCoordinate(value);
        CHECK(std::fabs(read - value) <= 1.0 / (20.0 * 256.0) + 1e-12);
    }
}

TEST(coordinate_snaps_to_the_storable_grid)
{
    // A value finer than 1/256 of a twip cannot be represented, so it snaps to
    // the nearest step rather than being written with false precision.
    const double tooFine = 1.0 / (20.0 * 256.0 * 4.0);
    const double read = roundTripCoordinate(tooFine);
    CHECK(read == 0.0);
}

TEST(writes_a_simple_closed_rectangle)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0|1000 0|1000 1000|0 1000|0 0");
    CHECK(edge != nullptr);
    if (!edge)
        return;

    CHECK(EdgeWriter::writeEdge(*edge) == "!0 0|1000 0|1000 1000|0 1000|0 0");
}

TEST(writes_quadratic_segments)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0[100 200 300 0");
    CHECK(edge != nullptr);
    if (!edge)
        return;

    CHECK(EdgeWriter::writeEdge(*edge) == "!0 0[100 200 300 0");
}

TEST(writes_style_selects)
{
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0S1|100 0");
    CHECK(edge != nullptr);
    if (!edge)
        return;

    // A style select attaches to the path opened by the preceding move, so it
    // has to come after that move. Leading with it would give the reader nothing
    // to attach the style to.
    CHECK(EdgeWriter::writeEdge(*edge) == "!0 0S1|100 0");
}

TEST(style_selects_survive_a_round_trip)
{
    std::unique_ptr<fla::Edge> first = parseEdge("!0 0S1FS2LS3|100 0");
    CHECK(first != nullptr);
    if (!first || first->paths.empty())
        return;

    CHECK(first->paths[0]->styleIndex == 1);
    CHECK(first->paths[0]->fillStyleIndex == 2);
    CHECK(first->paths[0]->lineStyleIndex == 3);

    std::unique_ptr<fla::Edge> second = parseEdge(EdgeWriter::writeEdge(*first));
    CHECK(second != nullptr);
    if (!second || second->paths.empty())
        return;

    CHECK(second->paths[0]->styleIndex == 1);
    CHECK(second->paths[0]->fillStyleIndex == 2);
    CHECK(second->paths[0]->lineStyleIndex == 3);
}

TEST(mid_path_style_changes_are_lost_by_the_reader)
{
    // Documents a reader limitation, not a writer one: Path holds a single
    // styleIndex, so a second style select part-way through simply overwrites
    // the first and the change point is gone before the writer ever sees it.
    // Anything relying on mid-path style changes needs Path to carry styles per
    // segment first.
    std::unique_ptr<fla::Edge> edge = parseEdge("!0 0S1|100 0S2|100 100");
    CHECK(edge != nullptr);
    if (!edge || edge->paths.empty())
        return;

    CHECK(edge->paths[0]->styleIndex == 2);
    CHECK(EdgeWriter::writeEdge(*edge) == "!0 0S2|100 0|100 100");
}

TEST(round_trip_is_stable)
{
    // The reader normalises as it goes -- it infers closes, and merges a move
    // that lands where the previous path ended. So the test is that writing is a
    // fixed point: once a string has been through the reader, writing and
    // re-reading it must not keep changing it.
    const char* sources[] = {
        "!0 0|1000 0|1000 1000|0 1000|0 0",
        "!0 0[100 200 300 0[400 -200 600 0",
        "!0 0|100 0|100 100|0 100|0 0!200 200|300 200|300 300|200 300|200 200",
        "!#64.80 #C8.40|1000 0",
        "!0 0S1|100 0",
        "!0 0S1FS2LS3|100 0|100 100",
        "!-100 -100|100 -100|100 100|-100 100|-100 -100",
        "!0 0(100 0 200 100 300 100)",
    };

    for (const char* source : sources)
    {
        std::unique_ptr<fla::Edge> first = parseEdge(source);
        CHECK(first != nullptr);
        if (!first)
            continue;

        const std::string written = EdgeWriter::writeEdge(*first);

        std::unique_ptr<fla::Edge> second = parseEdge(written);
        CHECK(second != nullptr);
        if (!second)
            continue;

        const std::string rewritten = EdgeWriter::writeEdge(*second);

        if (written != rewritten)
            std::printf("    source: %s\n      1st: %s\n      2nd: %s\n",
                source, written.c_str(), rewritten.c_str());
        CHECK(written == rewritten);
    }
}

TEST(round_trip_preserves_geometry)
{
    // Beyond textual stability, the points themselves must survive.
    std::unique_ptr<fla::Edge> first = parseEdge("!0 0[100 200 300 0|300 400");
    CHECK(first != nullptr);
    if (!first)
        return;

    std::unique_ptr<fla::Edge> second = parseEdge(EdgeWriter::writeEdge(*first));
    CHECK(second != nullptr);
    if (!second)
        return;

    CHECK(first->paths.size() == second->paths.size());
    if (first->paths.size() != second->paths.size())
        return;

    for (size_t p = 0; p < first->paths.size(); ++p)
    {
        const fla::Path* a = first->paths[p];
        const fla::Path* b = second->paths[p];
        CHECK(a->segments.size() == b->segments.size());
        if (a->segments.size() != b->segments.size())
            continue;

        for (size_t s = 0; s < a->segments.size(); ++s)
        {
            CHECK(a->segments[s]->command == b->segments[s]->command);
            CHECK(a->segments[s]->points.size() == b->segments[s]->points.size());
            if (a->segments[s]->points.size() != b->segments[s]->points.size())
                continue;

            for (size_t i = 0; i < a->segments[s]->points.size(); ++i)
            {
                CHECK_NEAR(a->segments[s]->points[i].x, b->segments[s]->points[i].x);
                CHECK_NEAR(a->segments[s]->points[i].y, b->segments[s]->points[i].y);
            }
        }
    }
}

TEST(cubic_segments_survive_a_round_trip)
{
    std::unique_ptr<fla::Edge> first = parseEdge("!0 0(100 0 200 100 300 100)");
    CHECK(first != nullptr);
    if (!first)
        return;

    CHECK(first->paths.size() == 1);
    if (first->paths.empty())
        return;

    // Move plus one cubic.
    CHECK(first->paths[0]->segments.size() == 2);
    if (first->paths[0]->segments.size() != 2)
        return;
    CHECK(first->paths[0]->segments[1]->command == fla::PathSegment::Command::Cubic);

    std::unique_ptr<fla::Edge> second = parseEdge(EdgeWriter::writeEdge(*first));
    CHECK(second != nullptr);
    if (!second || second->paths.empty())
        return;

    CHECK(second->paths[0]->segments.size() == 2);
    if (second->paths[0]->segments.size() != 2)
        return;
    CHECK(second->paths[0]->segments[1]->command == fla::PathSegment::Command::Cubic);

    const fla::PathSegment* a = first->paths[0]->segments[1];
    const fla::PathSegment* b = second->paths[0]->segments[1];
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK_NEAR(a->points[i].x, b->points[i].x);
        CHECK_NEAR(a->points[i].y, b->points[i].y);
    }
}
