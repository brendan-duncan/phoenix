#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/document_settings.h"

TEST(document_settings_resize_scale_and_units)
{
    fla::Document document(nullptr);
    document.width = 400;
    document.height = 300;
    fla::Timeline* scene = new fla::Timeline(&document);
    document.timelines.push_back(scene);
    fla::Layer* layer = new fla::Layer(scene);
    scene->layers.push_back(layer);
    fla::Frame* frame = new fla::Frame(layer);
    layer->frames.push_back(frame);
    fla::SymbolInstance* instance = new fla::SymbolInstance(frame);
    instance->transform.tx = 100.0;
    instance->transform.ty = 50.0;
    frame->elements.push_back(instance);
    fla::Group* group = new fla::Group(frame);
    fla::SymbolInstance* member = new fla::SymbolInstance(group);
    member->transform.tx = 10.0;
    group->members.push_back(member);
    frame->elements.push_back(group);

    // A group counts by its members.
    const std::vector<fla::Element*> elements = fla::docsettings::sceneElements(document);
    CHECK(elements.size() == 2 && elements[1] == member);

    fla::CommandStack stack;
    // Twice the size, the content scaled with it, then moved.
    stack.push(fla::docsettings::resize(document, 800, 600, true, 5.0, 0.0));
    CHECK(document.width == 800 && document.height == 600);
    CHECK(instance->transform.tx == 205.0 && instance->transform.ty == 100.0 && instance->transform.m11 == 2.0);
    CHECK(member->transform.tx == 25.0);
    stack.undo();
    CHECK(document.width == 400 && instance->transform.tx == 100.0 && instance->transform.m11 == 1.0);
    // Without scaling, only the stage changes.
    stack.push(fla::docsettings::resize(document, 500, 300, false, 0.0, 0.0));
    CHECK(document.width == 500 && instance->transform.tx == 100.0);

    using fla::docsettings::Unit;
    CHECK(fla::docsettings::unitOf(document) == Unit::Pixels);
    document.extraAttributes = fla::docsettings::withUnit(document, Unit::Centimeters);
    CHECK(fla::docsettings::unitOf(document) == Unit::Centimeters && document.extraAttributes.back().second == "centimeters");
    CHECK(std::abs(fla::docsettings::pixelsPer(Unit::Inches) - 72.0) < 1e-9);
    document.extraAttributes = fla::docsettings::withUnit(document, Unit::Pixels);
    CHECK(document.extraAttributes.empty());
}
