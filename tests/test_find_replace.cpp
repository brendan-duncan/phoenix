#include "test_util.h"

#include "../src/data/bitmap.h"
#include "../src/data/bitmap_fill.h"
#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/linear_gradient.h"
#include "../src/data/shape.h"
#include "../src/data/solid_color.h"
#include "../src/data/static_text.h"
#include "../src/data/stroke_style.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/find_replace.h"

#include <memory>

namespace {

/// A scene with a text field ("Hello cat, Cat!" in Arial), a red shape
/// with a red-to-blue gradient stroke inside a group, and an instance of
/// "Ball"; the library also has "Box".
struct Scene
{
    std::unique_ptr<fla::Document> document;
    fla::StaticText* text = nullptr;
    fla::SolidColor* fill = nullptr;
    fla::LinearGradient* gradient = nullptr;
    fla::SymbolInstance* instance = nullptr;
    fla::Symbol* box = nullptr;
};

Scene makeScene()
{
    Scene scene;
    scene.document.reset(new fla::Document(nullptr));
    fla::Document& document = *scene.document;
    document.symbolList = new fla::SymbolList(&document);
    for (const char* name : {"Ball", "Box"})
    {
        fla::Symbol* symbol = new fla::Symbol(document.symbolList);
        symbol->name = name;
        document.symbolList->symbols.push_back(symbol);
        document.symbolList->symbolMap[name] = symbol;
    }
    scene.box = document.symbolList->symbolMap["Box"];

    fla::Timeline* main = new fla::Timeline(&document);
    main->name = "Scene 1";
    fla::Layer* layer = new fla::Layer(main);
    layer->name = "Layer 1";
    fla::Frame* frame = new fla::Frame(layer);
    frame->index = 4;

    scene.text = new fla::StaticText(frame);
    fla::TextRun run;
    run.text = "Hello cat, Cat! concat";
    run.face = "ArialMT";
    scene.text->runs.push_back(run);
    frame->elements.push_back(scene.text);

    fla::Group* group = new fla::Group(frame);
    fla::Shape* shape = new fla::Shape(group);
    scene.fill = new fla::SolidColor(shape);
    scene.fill->color[0] = 255;
    scene.fill->color[3] = 128;
    shape->fills.push_back(scene.fill);
    fla::StrokeStyle* stroke = new fla::SolidStroke(shape);
    scene.gradient = new fla::LinearGradient(stroke);
    scene.gradient->entries.push_back({0.0, {255, 0, 0, 255}});
    scene.gradient->entries.push_back({1.0, {0, 0, 255, 255}});
    stroke->fill = scene.gradient;
    shape->strokes.push_back(stroke);
    group->members.push_back(shape);
    frame->elements.push_back(group);

    scene.instance = new fla::SymbolInstance(frame);
    scene.instance->libraryItemName = "Ball";
    scene.instance->symbol = document.symbolList->symbolMap["Ball"];
    frame->elements.push_back(scene.instance);

    layer->frames.push_back(frame);
    main->layers.push_back(layer);
    document.timelines.push_back(main);
    return scene;
}

} // namespace

TEST(find_and_replace_text_matches_case_and_whole_words_as_asked)
{
    Scene scene = makeScene();
    fla::find::Query query;
    query.kind = fla::find::Kind::Text;
    query.find = "cat";
    query.replace = "dog";

    std::vector<fla::find::Match> matches = fla::find::findAll(*scene.document, query);
    CHECK(matches.size() == 1 && matches[0].count == 3);
    CHECK(matches[0].where == "Scene 1 > Layer 1 > Frame 5");

    query.wholeWord = true;
    CHECK(fla::find::findAll(*scene.document, query)[0].count == 2);
    query.matchCase = true;
    fla::CommandStack stack;
    int replaced = 0;
    stack.push(fla::find::replaceAll(*scene.document, query, &replaced));
    CHECK(replaced == 1);
    CHECK(scene.text->runs[0].text == "Hello dog, Cat! concat");
    stack.undo();
    CHECK(scene.text->runs[0].text == "Hello cat, Cat! concat");
}

TEST(find_and_replace_fonts_colors_and_symbols_with_undo)
{
    Scene scene = makeScene();
    fla::CommandStack stack;

    CHECK(fla::find::fontsUsed(*scene.document) == std::vector<std::string>{"ArialMT"});
    fla::find::Query font;
    font.kind = fla::find::Kind::Font;
    font.find = "arialmt";
    font.replace = "Georgia";
    stack.push(fla::find::replaceAll(*scene.document, font));
    CHECK(scene.text->runs[0].face == "Georgia");

    // Red is in the fill and the gradient's first color, inside a group;
    // each keeps its alpha.
    fla::find::Query color;
    color.kind = fla::find::Kind::Color;
    color.color[0] = 255;
    color.replaceColor[1] = 200;
    const std::vector<fla::find::Match> reds = fla::find::findAll(*scene.document, color);
    CHECK(reds.size() == 1 && reds[0].count == 2 && reds[0].element != reds[0].topElement);
    stack.push(fla::find::replaceAll(*scene.document, color));
    CHECK(scene.fill->color[0] == 0 && scene.fill->color[1] == 200 && scene.fill->color[3] == 128);
    CHECK(scene.gradient->entries[0].color[1] == 200 && scene.gradient->entries[1].color[2] == 255);

    fla::find::Query symbol;
    symbol.kind = fla::find::Kind::Symbol;
    symbol.find = "Ball";
    symbol.replace = "Nothing";
    CHECK(!fla::find::replaceAll(*scene.document, symbol));
    symbol.replace = "Box";
    stack.push(fla::find::replaceAll(*scene.document, symbol));
    CHECK(scene.instance->libraryItemName == "Box" && scene.instance->symbol == scene.box);

    stack.undo();
    stack.undo();
    stack.undo();
    CHECK(scene.instance->libraryItemName == "Ball");
    CHECK(scene.fill->color[0] == 255 && scene.fill->color[1] == 0);
    CHECK(scene.text->runs[0].face == "ArialMT");
}

TEST(find_and_replace_sounds_and_bitmaps)
{
    Scene scene = makeScene();
    fla::Document& document = *scene.document;
    document.sounds.push_back({"beat.wav", "", ""});
    document.sounds.push_back({"drum.wav", "", ""});
    for (const char* name : {"sky.png", "sea.png"})
    {
        fla::Bitmap* bitmap = new fla::Bitmap(&document);
        bitmap->name = name;
        document.resources.push_back(bitmap);
    }
    fla::Frame* frame = document.timelines[0]->layers[0]->frames[0];
    frame->extraAttributes.emplace_back("soundName", "beat.wav");
    fla::BitmapInstance* picture = new fla::BitmapInstance(frame);
    picture->libraryItemName = "sky.png";
    frame->elements.push_back(picture);
    fla::Shape* shape = new fla::Shape(frame);
    fla::BitmapFill* fill = new fla::BitmapFill(shape);
    fill->bitmapPath = "sky.png";
    shape->fills.push_back(fill);
    frame->elements.push_back(shape);

    fla::find::Query sound;
    sound.kind = fla::find::Kind::Sound;
    sound.find = "beat.wav";
    sound.replace = "drum.wav";
    const std::vector<fla::find::Match> heard = fla::find::findAll(document, sound);
    CHECK(heard.size() == 1 && heard[0].frame == 4 && heard[0].element == nullptr);

    fla::find::Query bitmap;
    bitmap.kind = fla::find::Kind::Bitmap;
    bitmap.find = "sky.png";
    bitmap.replace = "sea.png";
    CHECK(fla::find::findAll(document, bitmap).size() == 2);

    fla::CommandStack stack;
    stack.push(fla::find::replaceAll(document, sound));
    stack.push(fla::find::replaceAll(document, bitmap));
    CHECK(frame->extraAttributes.back().second == "drum.wav");
    CHECK(picture->libraryItemName == "sea.png" && fill->bitmapPath == "sea.png");
    stack.undo();
    stack.undo();
    CHECK(frame->extraAttributes.back().second == "beat.wav" && picture->libraryItemName == "sky.png");

    // Only to an item the library has.
    bitmap.replace = "missing.png";
    CHECK(fla::find::replaceAll(document, bitmap) == nullptr);
}

namespace {

/// Rebuilds a shape's solid fills as new objects each way, as undoing or
/// redoing a merge does (setShapeContents).
class RebuildFills : public fla::Command
{
public:
    explicit RebuildFills(fla::Shape* shape)
        : _shape(shape)
    {}
    void redo() override { rebuild(); }
    void undo() override { rebuild(); }
    std::string name() const override { return "Rebuild"; }

private:
    void rebuild()
    {
        for (fla::FillStyle*& fill : _shape->fills)
        {
            auto* solid = static_cast<fla::SolidColor*>(fill);
            auto* copy = new fla::SolidColor(_shape);
            std::copy(solid->color, solid->color + 4, copy->color);
            delete solid;
            fill = copy;
        }
    }
    fla::Shape* _shape;
};

} // namespace

// A replacement finds its fields again each time rather than keeping
// pointers to fills that a later edit, undone and redone, rebuilds.
TEST(a_replacement_survives_the_fills_it_changed_being_rebuilt)
{
    Scene scene = makeScene();
    fla::Shape* shape = static_cast<fla::Shape*>(scene.fill->parent);
    fla::CommandStack stack;
    fla::find::Query color;
    color.kind = fla::find::Kind::Color;
    color.color[0] = 255;
    color.replaceColor[1] = 200;
    stack.push(fla::find::replaceAll(*scene.document, color));
    stack.push(fla::CommandPtr(new RebuildFills(shape)));
    CHECK(static_cast<fla::SolidColor*>(shape->fills[0])->color[1] == 200);

    stack.undo();
    stack.redo();
    stack.undo();
    stack.undo();
    const uint8_t* now = static_cast<fla::SolidColor*>(shape->fills[0])->color;
    CHECK(now[0] == 255 && now[1] == 0);
    stack.redo();
    CHECK(static_cast<fla::SolidColor*>(shape->fills[0])->color[1] == 200);
    stack.redo();
    CHECK(static_cast<fla::SolidColor*>(shape->fills[0])->color[1] == 200);
}
