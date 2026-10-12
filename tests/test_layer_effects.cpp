#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/timeline.h"
#include "../src/edit/layer_effects.h"

#include <memory>

TEST(an_effect_layer_keeps_its_effect_and_is_a_guide)
{
    fla::Timeline scene(nullptr);
    fla::Layer* hair = new fla::Layer(&scene);
    hair->name = "Hair";
    fla::Frame* frame = new fla::Frame(hair);
    frame->duration = 48;
    hair->frames.push_back(frame);
    scene.layers.push_back(hair);
    CHECK(!fla::effects::isEffect(*hair));

    fla::effects::Effect tone = fla::effects::defaults(fla::effects::Kind::Tone);
    std::unique_ptr<fla::Layer> layer(fla::effects::newEffectLayer(scene, *hair, tone));
    CHECK(layer->name == "Hair Tone" && layer->layerType == fla::Layer::Type::Guide);
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 48);
    CHECK(fla::effects::isEffect(*layer));

    fla::effects::Effect glow = fla::effects::defaults(fla::effects::Kind::Glow);
    glow.blur = 12.5;
    glow.opacity = 0.25;
    glow.color[0] = 10;
    layer->extraAttributes = fla::effects::withEffect(*layer, glow);
    const fla::effects::Effect read = fla::effects::effectOf(*layer);
    CHECK(read.kind == fla::effects::Kind::Glow && read.blur == 12.5 && read.opacity == 0.25);
    CHECK(read.color[0] == 10 && !read.clip);
    CHECK(fla::effects::defaults(fla::effects::Kind::Tone).clip);
}
