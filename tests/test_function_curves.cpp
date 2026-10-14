#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol_instance.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/function_curves.h"

#include <cmath>
#include <cstdlib>

TEST(function_curves_read_through_tweens_and_set_keys)
{
    // Two keys, x 0 then 100, a classic tween between.
    fla::Layer layer(nullptr);
    for (int at : {0, 10})
    {
        fla::Frame* frame = new fla::Frame(&layer);
        frame->index = at;
        frame->duration = 10;
        frame->tweenType = at == 0 ? fla::TweenType::Motion : fla::TweenType::None;
        fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
        instance->libraryItemName = "Ball";
        instance->transform.tx = at * 10.0;
        frame->elements.push_back(instance);
        layer.frames.push_back(frame);
    }
    const fla::curves::Context context;
    CHECK(fla::curves::propertiesOf(layer).size() == 10);

    double value = -1.0;
    CHECK(fla::curves::valueAt(layer, 0, "x", context, value) && value == 0.0);
    CHECK(fla::curves::valueAt(layer, 5, "x", context, value) && std::abs(value - 50.0) < 1.0e-6);
    CHECK(fla::curves::valueAt(layer, 15, "x", context, value) && value == 100.0);
    CHECK(!fla::curves::valueAt(layer, 25, "x", context, value));
    CHECK(fla::curves::valueAt(layer, 0, "alpha", context, value) && value == 100.0);

    const std::vector<fla::curves::Key> keys = fla::curves::keys(layer, "x", context);
    CHECK(keys.size() == 2 && keys[1].frame == 10 && keys[1].value == 100.0);

    fla::CommandStack stack;
    CHECK(fla::curves::setKey(layer, 5, "x", 1.0, context) == nullptr);
    stack.push(fla::curves::setKey(layer, 10, "x", 200.0, context));
    CHECK(fla::curves::valueAt(layer, 5, "x", context, value) && std::abs(value - 100.0) < 1.0e-6);
    stack.push(fla::curves::setKey(layer, 0, "rotation", 90.0, context));
    CHECK(fla::curves::valueAt(layer, 0, "rotation", context, value) && std::abs(value - 90.0) < 1.0e-6);
    stack.undo();
    stack.undo();
    CHECK(fla::curves::keys(layer, "x", context)[1].value == 100.0);
    CHECK(fla::curves::valueAt(layer, 0, "rotation", context, value) && std::abs(value) < 1.0e-6);
}
