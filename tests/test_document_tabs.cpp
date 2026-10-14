#include "test_util.h"

#include "../src/data/bitmap.h"
#include "../src/data/bitmap_instance.h"
#include "../src/data/document.h"
#include "../src/data/frame.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/command_stack.h"
#include "../src/edit/document_session.h"
#include "../src/edit/element_commands.h"
#include "../src/edit/selection.h"
#include "../src/edit/library_media.h"
#include "../src/edit/library_transfer.h"

#include <memory>
#include <cstdint>

// What several documents open at once rely on: the files reopened next
// time, media let go by documents waiting in tabs, and copies between
// documents bringing the library items they use.

namespace {

fla::Symbol* addSymbol(fla::Document& document, const std::string& name)
{
    if (!document.symbolList)
        document.symbolList = new fla::SymbolList(&document);
    fla::Symbol* symbol = new fla::Symbol(document.symbolList);
    symbol->name = name;
    fla::Timeline* inner = new fla::Timeline(symbol);
    fla::Layer* layer = new fla::Layer(inner);
    fla::Frame* frame = new fla::Frame(layer);
    frame->elements.push_back(new fla::Shape(frame));
    layer->frames.push_back(frame);
    inner->layers.push_back(layer);
    symbol->timelines.push_back(inner);
    document.symbolList->symbols.push_back(symbol);
    document.symbolList->symbolMap[name] = symbol;
    return symbol;
}

fla::Bitmap* addBitmap(fla::Document& document, const std::string& name)
{
    fla::Bitmap* bitmap = new fla::Bitmap(&document);
    bitmap->name = name;
    bitmap->bitmapDataHRef = "M 1 1.dat";
    document.resources.push_back(bitmap);
    return bitmap;
}

/// Bytes that lie "on disk": read by a loader that counts its reads.
fla::LazyBytes onDisk(int& reads)
{
    auto location = std::make_shared<fla::ByteLocation>();
    location->size = 3;
    location->load = [&reads](const fla::ByteLocation&, std::vector<uint8_t>& bytes) {
        ++reads;
        bytes = {7, 8, 9};
        return true;
    };
    return fla::LazyBytes(location);
}

} // namespace

TEST(reopened_documents_are_the_saved_ones_with_the_shown_one_last)
{
    using fla::session::OpenDocument;
    // Two windows: the first shows b, the second shows d.
    const std::vector<std::vector<OpenDocument>> windows = {
        {{"a.fla", false}, {"b.fla", true}, {"", false}, {"c.fla", false}},
        {{"d.fla", true}, {"a.fla", false}},
    };
    const std::vector<std::string> list = fla::session::reopenList(windows);
    CHECK((list == std::vector<std::string>{"a.fla", "c.fla", "d.fla", "b.fla"}));
    CHECK(fla::session::reopenList({}).empty());
    // Nothing saved: nothing to reopen.
    CHECK(fla::session::reopenList({{{"", true}}}).empty());
}

TEST(media_read_from_disk_is_let_go_and_read_again)
{
    int reads = 0;
    fla::LazyBytes bytes = onDisk(reads);
    // Not read yet: nothing to let go.
    CHECK(!bytes.loaded() && !bytes.unload());
    CHECK(bytes.get().size() == 3 && reads == 1);
    CHECK(bytes.unload() && !bytes.loaded());
    // Read again when next asked for.
    CHECK(bytes.get().size() == 3 && bytes.get()[0] == 7 && reads == 2);

    // Bytes made in memory have nowhere to be read back from, and stay.
    fla::LazyBytes made(std::vector<uint8_t>{1, 2});
    CHECK(!made.unload() && made.loaded() && made.size() == 2);

    // Bytes another holder shares stay too: it may be reading them.
    fla::LazyBytes shared = bytes;
    CHECK(bytes.get().size() == 3 && !bytes.unload() && bytes.loaded());
}

TEST(a_document_put_aside_lets_go_of_its_media)
{
    int reads = 0;
    fla::Document document(nullptr);
    fla::Bitmap* bitmap = addBitmap(document, "face.png");
    bitmap->datData = onDisk(reads);
    document.extraFiles.emplace_back("bin/M 2 1.dat", onDisk(reads));
    // Media never shown are not read for nothing.
    CHECK(fla::media::releaseLoadedMedia(document) == 0 && reads == 0);
    CHECK(bitmap->datData.get().size() == 3 && document.extraFiles[0].second.get().size() == 3);
    CHECK(fla::media::releaseLoadedMedia(document) == 6);
    CHECK(!bitmap->datData.loaded() && !document.extraFiles[0].second.loaded());
    CHECK(bitmap->datData.get().size() == 3 && reads == 3);
}

TEST(copied_elements_name_the_library_items_they_use)
{
    fla::Document source(nullptr);
    fla::Symbol* hero = addSymbol(source, "Hero");
    addBitmap(source, "face.png");
    fla::Frame frame(nullptr);
    fla::SymbolInstance* instance = new fla::SymbolInstance(&frame);
    instance->libraryItemName = "Hero";
    instance->symbol = hero;
    fla::Group* group = new fla::Group(&frame);
    fla::BitmapInstance* picture = new fla::BitmapInstance(group);
    picture->libraryItemName = "face.png";
    group->members.push_back(picture);
    frame.elements = {instance, group};
    frame.extraAttributes.emplace_back("soundName", "hello.wav");

    CHECK((fla::transfer::usedBy({instance, group}) == std::vector<std::string>{"Hero", "face.png"}));
    CHECK((fla::transfer::usedByFrames({&frame}) == std::vector<std::string>{"Hero", "face.png", "hello.wav"}));

    // Pasted where names clash and both are kept: the copies use the new
    // names.
    fla::transfer::renameUses(frame, {{"Hero", "Hero copy"}, {"hello.wav", "hello 2.wav"}});
    CHECK(instance->libraryItemName == "Hero copy" && picture->libraryItemName == "face.png");
    CHECK(frame.extraAttributes[0].second == "hello 2.wav");
}

TEST(pasting_into_another_document_brings_symbols_under_their_new_names)
{
    fla::Document source(nullptr);
    fla::Symbol* hero = addSymbol(source, "Hero");
    fla::Symbol* head = addSymbol(source, "Head");
    fla::Frame* inHero = hero->timelines[0]->layers[0]->frames[0];
    fla::SymbolInstance* nested = new fla::SymbolInstance(inHero);
    nested->libraryItemName = "Head";
    nested->symbol = head;
    inHero->elements.push_back(nested);

    fla::Document target(nullptr);
    addSymbol(target, "Hero");

    // What a copied instance of Hero needs, with what Hero uses.
    fla::SymbolInstance copied(nullptr);
    copied.libraryItemName = "Hero";
    const std::vector<std::string> needed = fla::transfer::usedBy({&copied});
    CHECK((fla::transfer::withDependencies(source, needed) == std::vector<std::string>{"Head", "Hero"}));
    CHECK((fla::transfer::conflicts(target, source, needed) == std::vector<std::string>{"Hero"}));

    fla::CommandStack stack;
    std::map<std::string, std::string> renamed;
    stack.push(fla::transfer::copyItems(target, source, needed, fla::transfer::Resolution::KeepBoth, nullptr, &renamed));
    CHECK(renamed["Hero"] == "Hero copy" && renamed["Head"] == "Head");
    fla::transfer::renameUses(copied, renamed);
    CHECK(copied.libraryItemName == "Hero copy" && target.symbolList->symbolMap.count("Hero copy") == 1);
    CHECK(target.symbolList->symbolMap.count("Head") == 1);

    // Skipped: the target's own Hero is used, under its own name.
    stack.undo();
    std::map<std::string, std::string> kept;
    stack.push(fla::transfer::copyItems(target, source, needed, fla::transfer::Resolution::Skip, nullptr, &kept));
    CHECK(kept["Hero"] == "Hero" && target.symbolList->symbolMap.count("Hero copy") == 0);
}

TEST(a_history_moved_to_another_window_keeps_to_that_windows_selection)
{
    fla::Frame frame(nullptr);
    fla::Selection first;
    fla::Selection second;
    fla::CommandStack stack;
    fla::Shape* shape = new fla::Shape(&frame);
    stack.beginMacro("Paste");
    stack.push(fla::CommandPtr(new fla::AddElementCommand(&frame, shape, "Paste", &first)));
    stack.endMacro();
    // The document and its history went to the window whose selection is
    // the second.
    stack.retargetSelection(&first, &second);
    first.select(shape);
    second.select(shape);
    stack.undo();
    CHECK(frame.elements.empty());
    CHECK(second.isEmpty() && !first.isEmpty());
    first.clear();
    stack.redo();
    CHECK(frame.elements.size() == 1);
}
