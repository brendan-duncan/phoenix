#include "test_util.h"

#include "../src/edit/asset_warp.h"

#include <cmath>
#include <string>

namespace {

bool near(const fla::Point& p, double x, double y, double tolerance = 1.0e-6)
{
    return std::fabs(p.x - x) < tolerance && std::fabs(p.y - y) < tolerance;
}

} // namespace

TEST(asset_warp_pins_are_read_from_puppet_handles)
{
    // As template_bike_girl.fla writes them: the dragged place in twips.
    const std::string handles =
        "{\"envelope_bz\":{\"deformed_bz\":[],\"original_bz\":[]},\"puppetMeshDensity\":7,\"puppetPoints\":["
        "{\"ID\":1,\"boneHandleEnable\":true,\"newEndControlPt\":{\"x\":0,\"y\":0},\"newLocation\":{\"x\":-295,\"y\":396},"
        "\"orgLocation\":{\"x\":-14.75,\"y\":19.8},\"rotationAngle\":0},"
        "{\"ID\":0,\"newLocation\":{\"x\":200,\"y\":-692},\"orgLocation\":{\"x\":6.95,\"y\":-34.6}}],"
        "\"rigTree\":[{\"ID\":1,\"children\":[]}],\"type\":1}";
    const std::vector<fla::WarpPin> pins = fla::AssetWarp::pins(handles);
    CHECK(pins.size() == 2);
    if (pins.size() == 2)
    {
        CHECK(pins[0].id == 1 && near(pins[0].from, -14.75, 19.8) && near(pins[0].to, -14.75, 19.8));
        CHECK(pins[1].id == 0 && near(pins[1].from, 6.95, -34.6) && near(pins[1].to, 10.0, -34.6));
    }
    CHECK(fla::AssetWarp::pins("{}").empty());
}

TEST(asset_warp_moves_the_art_with_its_pins)
{
    fla::WarpPin a;
    a.id = 0;
    a.from = fla::Point(0, 0);
    a.to = fla::Point(0, 0);
    fla::WarpPin b;
    b.id = 1;
    b.from = fla::Point(100, 0);
    b.to = fla::Point(100, 0);

    // Pins where they were leave the art where it was.
    CHECK(near(fla::AssetWarp::warp({a, b}, fla::Point(50, 20)), 50, 20));
    // A point on a pin goes where the pin went.
    b.to = fla::Point(0, 100);
    CHECK(near(fla::AssetWarp::warp({a, b}, fla::Point(100, 0)), 0, 100));
    // Turned a quarter about the first pin, the art between turns with it.
    CHECK(near(fla::AssetWarp::warp({a, b}, fla::Point(50, 0)), 0, 50, 1.0e-6));
    // One pin drags everything along.
    a.to = fla::Point(10, -5);
    CHECK(near(fla::AssetWarp::warp({a}, fla::Point(40, 40)), 50, 35));
    CHECK(near(fla::AssetWarp::warp({}, fla::Point(40, 40)), 40, 40));

    // Half way through a tween, each pin is half way.
    fla::WarpPin later = a;
    later.to = fla::Point(30, 15);
    const std::vector<fla::WarpPin> blended = fla::AssetWarp::blend({a}, {later}, 0.5);
    CHECK(blended.size() == 1 && near(blended[0].to, 20, 5));
}
