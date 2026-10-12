#include "test_util.h"

#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/timeline.h"
#include "../src/edit/review_notes.h"

TEST(review_notes_live_on_their_own_guide_layer)
{
    fla::Timeline scene(nullptr);
    fla::Layer* art = new fla::Layer(&scene);
    fla::Frame* frame = new fla::Frame(art);
    frame->duration = 24;
    art->frames.push_back(frame);
    scene.layers.push_back(art);
    CHECK(fla::review::notesLayer(scene) == nullptr);

    fla::Layer* notes = fla::review::newNotesLayer(scene);
    CHECK(notes->layerType == fla::Layer::Type::Guide);
    CHECK(notes->frames.size() == 1 && notes->frames[0]->duration == 24);
    scene.layers.insert(scene.layers.begin(), notes);
    CHECK(fla::review::notesLayer(scene) == notes);

    fla::StaticText* note = fla::review::makeNote("Brendan", "Hold this pose two frames longer", 20, 20);
    CHECK(fla::review::isNote(*note) && fla::review::authorOf(*note) == "Brendan");
    notes->frames[0]->elements.push_back(note);
    std::vector<fla::review::Note> listed = fla::review::notes(scene);
    CHECK(listed.size() == 1 && listed[0].text == note && !listed[0].done);
    CHECK(listed[0].body == "Brendan: Hold this pose two frames longer");

    note->extraAttributes = fla::review::withDone(*note, true);
    CHECK(fla::review::notes(scene)[0].done);
    note->extraAttributes = fla::review::withDone(*note, false);
    CHECK(!fla::review::isDone(*note));
}
