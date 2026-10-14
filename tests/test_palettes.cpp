#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/stroke_style.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/palettes.h"

#include <memory>

TEST(linked_palettes_recolor_what_is_linked_and_swap_whole)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    fla::Timeline* scene = new fla::Timeline(document.get());
    fla::Layer* layer = new fla::Layer(scene);
    fla::Frame* frame = new fla::Frame(layer);
    fla::Shape* shape = new fla::Shape(frame);
    // A skin-colored fill and a black outline; another fill of the same skin.
    fla::SolidColor* skin = new fla::SolidColor(shape);
    skin->color[0] = 240;
    skin->color[1] = 200;
    skin->color[2] = 160;
    fla::SolidColor* other = new fla::SolidColor(shape);
    other->color[0] = 240;
    other->color[1] = 200;
    other->color[2] = 160;
    other->color[3] = 128;
    shape->fills = {skin, other};
    fla::StrokeStyle* stroke = new fla::SolidStroke(shape);
    fla::SolidColor* line = new fla::SolidColor(stroke);
    stroke->fill = line;
    shape->strokes = {stroke};
    frame->elements.push_back(shape);
    layer->frames.push_back(frame);
    scene->layers.push_back(layer);
    document->timelines.push_back(scene);

    fla::palettes::Palettes palettes;
    fla::palettes::Palette day{"Day", {{"Skin", {240, 200, 160, 255}}, {"Line", {0, 0, 0, 255}}}};
    fla::palettes::Palette night{"Night", {{"Skin", {90, 80, 140, 255}}, {"Line", {20, 20, 60, 255}}}};
    palettes.list = {day, night};
    palettes.active = "Day";
    document->extraAttributes = fla::palettes::saved(*document, palettes);
    const fla::palettes::Palettes loaded = fla::palettes::load(*document);
    CHECK(loaded.list.size() == 2 && loaded.active == "Day");
    CHECK(loaded.list[1].colors[0].name == "Skin" && loaded.list[1].colors[0].rgba[2] == 140);

    fla::CommandStack stack;
    // Both skin fills link, by color; the outline is painted and linked.
    stack.push(fla::palettes::linkMatching(*document, day.colors[0]));
    stack.push(fla::palettes::paint({shape}, day.colors[1], false, true));
    const std::vector<std::pair<std::string, int>> uses = fla::palettes::usage(*document);
    CHECK(uses.size() == 2 && uses[0].first == "Line" && uses[0].second == 1 && uses[1].second == 2);

    // Night: everything linked takes its colors.
    palettes.active = "Night";
    stack.push(fla::palettes::recolor(*document, palettes));
    CHECK(skin->color[0] == 90 && skin->color[2] == 140 && other->color[0] == 90);
    CHECK(line->color[2] == 60);
    stack.undo();
    CHECK(skin->color[0] == 240 && line->color[2] == 0);

    stack.push(fla::palettes::unlink({shape}));
    CHECK(fla::palettes::usage(*document).empty());
    CHECK(!fla::palettes::recolor(*document, palettes));
}

TEST(linked_palettes_keep_their_file_and_read_and_write_it)
{
    std::unique_ptr<fla::Document> document(new fla::Document(nullptr));
    fla::palettes::Palettes palettes;
    fla::palettes::Palette day;
    day.name = "Day";
    day.file = "../show palettes/Day; main=1.phxpal";
    fla::palettes::Color skin;
    skin.name = "skin tone";
    skin.rgba[0] = 240;
    skin.rgba[1] = 200;
    skin.rgba[2] = 160;
    day.colors.push_back(skin);
    palettes.list.push_back(day);
    palettes.active = "Day";
    document->extraAttributes = fla::palettes::saved(*document, palettes);
    // The link comes back with the palette, odd characters and all.
    const fla::palettes::Palettes back = fla::palettes::load(*document);
    CHECK(back.list.size() == 1 && back.list[0].file == day.file && back.list[0].colors.size() == 1);

    const std::string text = fla::palettes::toText(day);
    CHECK(text == "Phoenix Palette\nName: Day\n#F0C8A0FF skin tone\n");
    fla::palettes::Palette read;
    read.file = "kept";
    // Windows line endings read the same.
    std::string crlf;
    for (char c : text)
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    CHECK(fla::palettes::fromText(crlf, read));
    CHECK(read.name == "Day" && read.file == "kept" && read.colors.size() == 1);
    CHECK(read.colors[0].name == "skin tone" && read.colors[0].rgba[0] == 240 && read.colors[0].rgba[3] == 255);
    CHECK(!fla::palettes::fromText("GIMP Palette\n", read));
}
