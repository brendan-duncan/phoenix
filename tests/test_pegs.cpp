#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/pegs.h"

#include <memory>

TEST(a_peg_is_an_empty_marked_symbol_on_a_layer_of_its_own)
{
    fla::SymbolList library(nullptr);
    CHECK(fla::pegs::pegSymbol(library) == nullptr);
    fla::Symbol* peg = fla::pegs::newPegSymbol("Peg");
    CHECK(fla::pegs::isPeg(*peg) && peg->symbolType == fla::SymbolType::Graphic);
    CHECK(peg->timelines.size() == 1 && peg->timelines[0]->layers[0]->frames[0]->elements.empty());
    library.symbols.push_back(peg);
    library.symbolMap["Peg"] = peg;
    CHECK(fla::pegs::pegSymbol(library) == peg);

    fla::Timeline scene(nullptr);
    fla::Layer* art = new fla::Layer(&scene);
    art->name = "Peg 1";
    fla::Frame* frame = new fla::Frame(art);
    frame->duration = 30;
    art->frames.push_back(frame);
    scene.layers.push_back(art);

    std::unique_ptr<fla::Layer> layer(fla::pegs::newPegLayer(scene, peg, 640, 360));
    // The next free name, as long as the timeline, the peg where asked.
    CHECK(layer->name == "Peg 2");
    CHECK(layer->frames.size() == 1 && layer->frames[0]->duration == 30);
    const auto* instance = dynamic_cast<const fla::SymbolInstance*>(layer->frames[0]->elements.front());
    CHECK(instance && instance->symbol == peg && instance->transform.tx == 640 && instance->transform.ty == 360);
}
