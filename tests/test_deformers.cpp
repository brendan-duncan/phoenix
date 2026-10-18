#include "test_util.h"

#include "../src/data/symbol_instance.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/deformers.h"

#include <cmath>
#include <cstdlib>
#include <optional>

namespace {

bool near(const fla::Point& a, double x, double y)
{
    return std::abs(a.x - x) < 1.0e-6 && std::abs(a.y - y) < 1.0e-6;
}

} // namespace

TEST(deformers_leave_art_alone_at_rest_and_bend_it_posed)
{
    // At rest nothing moves, inside or out.
    fla::deform::Deformer curve = fla::deform::curveAround(0.0, -10.0, 90.0, 10.0);
    fla::deform::Deformer envelope = fla::deform::envelopeAround(0.0, 0.0, 90.0, 60.0);
    CHECK(curve.valid() && envelope.valid() && curve.rest.size() == 4 && envelope.rest.size() == 16);
    CHECK(fla::deform::atRest(curve) && fla::deform::atRest(envelope));
    for (const fla::Point& point : {fla::Point(10.0, 5.0), fla::Point(45.0, -8.0), fla::Point(120.0, 3.0)})
    {
        CHECK(near(fla::deform::apply(curve, point), point.x, point.y));
        CHECK(near(fla::deform::apply(envelope, point), point.x, point.y));
    }

    // A curve moved whole moves the art with it; the start held, the far
    // end swung down, a point at the end turns with it.
    fla::deform::Deformer shifted = curve;
    for (fla::Point& point : shifted.posed)
        point.y += 20.0;
    CHECK(near(fla::deform::apply(shifted, fla::Point(30.0, 4.0)), 30.0, 24.0));
    fla::deform::Deformer bent = curve;
    bent.posed[3] = fla::Point(0.0, 90.0);
    bent.posed[2] = fla::Point(0.0, 60.0);
    bent.posed[1] = fla::Point(0.0, 30.0);
    // Now pointing straight down: the end's left side is toward +x.
    const fla::Point end = fla::deform::apply(bent, fla::Point(90.0, 5.0));
    CHECK(std::abs(end.x + 5.0) < 1.0e-3 && std::abs(end.y - 90.0) < 1.0e-3);

    // An envelope's corner dragged drags the art near it most.
    fla::deform::Deformer pulled = envelope;
    pulled.posed[15].x += 30.0;
    const fla::Point corner = fla::deform::apply(pulled, fla::Point(90.0, 60.0));
    const fla::Point opposite = fla::deform::apply(pulled, fla::Point(0.0, 0.0));
    CHECK(near(corner, 120.0, 60.0) && near(opposite, 0.0, 0.0));

    // Written and read back, and between two poses.
    const std::optional<fla::deform::Deformer> read = fla::deform::parse(fla::deform::text(pulled));
    CHECK(read && read->kind == fla::deform::Kind::Envelope && near(read->posed[15], 120.0, 60.0));
    CHECK(!fla::deform::parse("curve 1 2 3"));
    const fla::deform::Deformer half = fla::deform::blend(envelope, pulled, 0.5);
    CHECK(near(half.posed[15], 105.0, 60.0));

    // On an instance, undoably.
    fla::SymbolInstance instance(nullptr);
    instance.extraAttributes.emplace_back("other", "kept");
    fla::CommandStack stack;
    stack.push(fla::deform::setDeformer(instance, pulled, "Deform"));
    CHECK(fla::deform::of(instance) && instance.extraAttributes.size() == 2);
    stack.push(fla::deform::setDeformer(instance, std::nullopt, "Remove"));
    CHECK(!fla::deform::of(instance) && instance.extraAttributes.size() == 1);
    stack.undo();
    stack.undo();
    CHECK(!fla::deform::of(instance) && instance.extraAttributes.size() == 1);
}

TEST(corner_pin_maps_corners_exactly_and_keeps_lines_straight)
{
    // At rest nothing moves.
    fla::deform::Deformer pin = fla::deform::cornerPinAround(0.0, 0.0, 200.0, 100.0);
    CHECK(pin.valid() && pin.kind == fla::deform::Kind::CornerPin && pin.rest.size() == 4);
    CHECK(fla::deform::atRest(pin));
    CHECK(near(fla::deform::apply(pin, fla::Point(37.0, 81.0)), 37.0, 81.0));

    // A sign seen from the side: the right edge shorter, the top sloping.
    pin.posed = {fla::Point(10.0, 0.0), fla::Point(180.0, 30.0), fla::Point(170.0, 80.0), fla::Point(20.0, 120.0)};
    for (size_t i = 0; i < 4; ++i)
        CHECK(near(fla::deform::apply(pin, pin.rest[i]), pin.posed[i].x, pin.posed[i].y));
    std::array<double, 9> map;
    CHECK(fla::deform::cornerPinMap(pin, map) && std::abs(map[6]) + std::abs(map[7]) > 1.0e-6);

    // Points on a straight line stay on one, and the middle of the art is
    // where the diagonals of the new corners cross, not halfway along them.
    const auto cross = [](fla::Point a, fla::Point b, fla::Point c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    const fla::Point from = fla::deform::apply(pin, fla::Point(-30.0, 10.0));
    const fla::Point to = fla::deform::apply(pin, fla::Point(230.0, 90.0));
    for (double t = 0.1; t < 1.0; t += 0.2)
    {
        const fla::Point on = fla::deform::apply(pin, fla::Point(-30.0 + 260.0 * t, 10.0 + 80.0 * t));
        CHECK(std::abs(cross(from, to, on)) < 1.0e-6 * std::hypot(to.x - from.x, to.y - from.y) * 100.0);
    }
    const fla::Point middle = fla::deform::apply(pin, fla::Point(100.0, 50.0));
    CHECK(std::abs(cross(pin.posed[0], pin.posed[2], middle)) < 1.0e-6);
    CHECK(std::abs(cross(pin.posed[1], pin.posed[3], middle)) < 1.0e-6);
    CHECK(!near(middle, (pin.posed[0].x + pin.posed[2].x) / 2.0, (pin.posed[0].y + pin.posed[2].y) / 2.0));

    // Any four corners to any four.
    const fla::Point a[4] = {fla::Point(5.0, 5.0), fla::Point(50.0, -3.0), fla::Point(60.0, 40.0), fla::Point(-2.0, 30.0)};
    const fla::Point b[4] = {fla::Point(100.0, 100.0), fla::Point(300.0, 120.0), fla::Point(260.0, 300.0), fla::Point(90.0, 210.0)};
    CHECK(fla::deform::homography(a, b, map));
    for (int i = 0; i < 4; ++i)
    {
        const double w = map[6] * a[i].x + map[7] * a[i].y + map[8];
        CHECK(std::abs((map[0] * a[i].x + map[1] * a[i].y + map[2]) / w - b[i].x) < 1.0e-6);
        CHECK(std::abs((map[3] * a[i].x + map[4] * a[i].y + map[5]) / w - b[i].y) < 1.0e-6);
    }
    // Corners with no area have no map.
    const fla::Point flat[4] = {fla::Point(0.0, 0.0), fla::Point(10.0, 0.0), fla::Point(20.0, 0.0), fla::Point(30.0, 0.0)};
    CHECK(!fla::deform::homography(flat, b, map));

    // Written and read back, and tweened corner by corner.
    const std::optional<fla::deform::Deformer> read = fla::deform::parse(fla::deform::text(pin));
    CHECK(read && read->kind == fla::deform::Kind::CornerPin && near(read->posed[2], 170.0, 80.0));
    const fla::deform::Deformer half = fla::deform::blend(fla::deform::cornerPinAround(0.0, 0.0, 200.0, 100.0), pin, 0.5);
    CHECK(near(half.posed[1], 190.0, 15.0));
}
