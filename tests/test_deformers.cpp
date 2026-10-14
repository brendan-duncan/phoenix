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
