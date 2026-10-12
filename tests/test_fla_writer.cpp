#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/symbol_list.h"
#include "../src/edit/new_document.h"
#include "../src/edit/library_media.h"
#include "../src/edit/command_stack.h"
#include "../src/data/bitmap.h"
#include "../src/data/bitmap_decoder.h"
#include "../src/parser/fla_parser.h"
#include "../src/parser/zip_reader.h"
#include "../src/util/zip_writer.h"
#include "../src/writer/fla_writer.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// A scratch directory that cleans itself up, so a failing test does not leave
/// files behind for the next run to trip over.
class ScratchDirectory
{
public:
    explicit ScratchDirectory(const std::string& name)
        : _path(fs::temp_directory_path() / ("phoenix_test_" + name))
    {
        std::error_code ignored;
        fs::remove_all(_path, ignored);
        fs::create_directories(_path, ignored);
    }

    ~ScratchDirectory()
    {
        // Set PHOENIX_TEST_KEEP_SCRATCH to leave the output in place, which is
        // how you get at a written .fla to open in another tool.
        if (std::getenv("PHOENIX_TEST_KEEP_SCRATCH"))
        {
            std::printf("    kept scratch: %s\n", _path.string().c_str());
            return;
        }

        std::error_code ignored;
        fs::remove_all(_path, ignored);
    }

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    const fs::path& path() const { return _path; }

    std::string file(const std::string& name) const { return (_path / name).string(); }

private:
    fs::path _path;
};

std::vector<uint8_t> readAll(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

TEST(zip_writer_round_trips_through_the_reader)
{
    ScratchDirectory scratch("zip");
    const std::string archive = scratch.file("test.zip");

    // Highly compressible, so this exercises the deflate path.
    const std::string big(4096, 'a');
    const std::string small = "hi";
    const std::vector<uint8_t> binary = {0x00, 0x01, 0xFF, 0x7F, 0x00, 0x80};

    {
        ZipWriter writer;
        CHECK(writer.open(archive));
        CHECK(writer.addFile("DOMDocument.xml", big));
        CHECK(writer.addFile("LIBRARY/nested/small.txt", small));
        CHECK(writer.addFile("LIBRARY/blob.bin", binary));
        CHECK(writer.close());
    }

    CHECK(fs::exists(archive));

    ZipReader reader;
    CHECK(reader.open(archive));
    CHECK(reader.containsFile("DOMDocument.xml"));
    CHECK(reader.containsFile("LIBRARY/nested/small.txt"));
    CHECK(reader.containsFile("LIBRARY/blob.bin"));
    CHECK(!reader.containsFile("missing.txt"));

    CHECK(reader.readTextFile("DOMDocument.xml") == big);
    CHECK(reader.readTextFile("LIBRARY/nested/small.txt") == small);
    CHECK(reader.readFile("LIBRARY/blob.bin") == binary);
}

TEST(zip_writer_handles_an_empty_entry)
{
    ScratchDirectory scratch("zip_empty");
    const std::string archive = scratch.file("empty.zip");

    {
        ZipWriter writer;
        CHECK(writer.open(archive));
        // Nothing to compress, so this has to fall back to storing.
        CHECK(writer.addFile("empty.txt", std::string()));
        CHECK(writer.close());
    }

    ZipReader reader;
    CHECK(reader.open(archive));
    CHECK(reader.containsFile("empty.txt"));
    CHECK(reader.readTextFile("empty.txt").empty());
}

TEST(zip_writer_rejects_use_before_open)
{
    ZipWriter writer;
    CHECK(!writer.isOpen());
    CHECK(!writer.addFile("x.txt", std::string("data")));
    CHECK(!writer.errorString().empty());
}

TEST(fla_writer_produces_a_readable_file)
{
    // Opt-in: needs a real document to pack.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !fs::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }

    std::string source;
    for (const auto& entry : fs::directory_iterator(corpus))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".fla")
        {
            source = entry.path().string();
            break;
        }
    }

    if (source.empty())
    {
        std::printf("    skipped: no .fla files in the corpus\n");
        return;
    }

    FLAParser parser;
    std::unique_ptr<fla::FLADocument> original(parser.parse(source));
    CHECK(original != nullptr);
    if (!original || !original->document)
        return;

    ScratchDirectory scratch("fla");
    const std::string written = scratch.file("written.fla");

    fla::FLAWriter writer;
    if (!writer.write(*original->document, written))
    {
        std::printf("    write failed: %s\n", writer.errorString().c_str());
        CHECK(false);
        return;
    }

    CHECK(fs::exists(written));
    CHECK(!readAll(written).empty());

    // The real check: the file we produced reads back as a document.
    FLAParser reparser;
    std::unique_ptr<fla::FLADocument> reloaded(reparser.parse(written));
    CHECK(reloaded != nullptr);
    if (!reloaded || !reloaded->document)
    {
        std::printf("    re-read failed: %s\n", reparser.errorString().c_str());
        return;
    }

    CHECK(reloaded->document->width == original->document->width);
    CHECK(reloaded->document->height == original->document->height);
    CHECK(reloaded->document->frameRate == original->document->frameRate);
    CHECK(reloaded->document->timelines.size() == original->document->timelines.size());

    const size_t symbolsBefore = original->document->symbolList
        ? original->document->symbolList->symbols.size() : 0;
    const size_t symbolsAfter = reloaded->document->symbolList
        ? reloaded->document->symbolList->symbols.size() : 0;
    CHECK(symbolsBefore == symbolsAfter);

    std::printf("    packed %s (%zu symbols) and read it back\n",
        fs::path(source).filename().string().c_str(), symbolsAfter);
}

TEST(fla_writer_packs_a_new_document_as_animate_does)
{
    // A document made here has no package files of its own: it still gets
    // the marker Animate looks for, the media type and the metadata file.
    std::unique_ptr<fla::FLADocument> created(fla::createEmptyDocument());
    CHECK(created && created->document);
    if (!created || !created->document)
        return;
    ScratchDirectory scratch("fla_new");
    const std::string written = scratch.file("Untitled-1.fla");
    fla::FLAWriter writer;
    CHECK(writer.write(*created->document, written));
    ZipReader reader;
    CHECK(reader.open(written));
    CHECK(reader.readTextFile("Untitled-1.xfl") == "PROXY-CS5");
    CHECK(reader.readTextFile("mimetype") == "application/vnd.adobe.xfl");
    CHECK(reader.containsFile("META-INF/metadata.xml"));
    CHECK(reader.containsFile("DOMDocument.xml"));
}

TEST(imported_bitmaps_and_sounds_are_saved_and_read_back)
{
    std::unique_ptr<fla::FLADocument> created(fla::createEmptyDocument());
    CHECK(created && created->document);
    if (!created || !created->document)
        return;
    fla::Document* document = created->document;
    const std::vector<uint32_t> pixels(4, 0xff336699u);
    fla::Bitmap* bitmap = fla::media::newBitmap(*document, "photo.png", "C:/art/photo.png", 2, 2,
        fla::encodeLosslessBitmap(2, 2, pixels), false);
    fla::CommandStack stack;
    stack.push(fla::CommandPtr(new fla::AddBitmapCommand(document, bitmap, "Import")));
    fla::media::Sound sound;
    sound.name = "line.wav";
    sound.channels = 1;
    sound.samples = {0, 1000, -1000, 32767};
    auto* addSound = new fla::AddSoundCommand(document, sound, "Import");
    stack.push(fla::CommandPtr(addSound));
    // A second import of the same name gets a name of its own.
    CHECK(fla::media::freeName(*document, "photo.png") == "photo 2.png");

    ScratchDirectory scratch("fla_import");
    const std::string written = scratch.file("imported.fla");
    fla::FLAWriter writer;
    CHECK(writer.write(*document, written));
    FLAParser parser;
    std::unique_ptr<fla::FLADocument> read(parser.parse(written));
    CHECK(read && read->document);
    if (!read || !read->document)
        return;
    CHECK(read->document->resources.size() == 1);
    if (read->document->resources.size() == 1)
    {
        const fla::Bitmap* back = static_cast<const fla::Bitmap*>(read->document->resources.front());
        fla::DecodedBitmap decoded;
        CHECK(back->name == "photo.png" && fla::decodeLosslessBitmap(back->datData, decoded));
        CHECK(decoded.pixels == pixels);
    }
    CHECK(read->document->sounds.size() == 1);
    if (read->document->sounds.size() == 1)
        CHECK(read->document->sounds.front().format == "44kHz 16bit Mono");

    // Undone, both leave the library.
    stack.undo();
    stack.undo();
    CHECK(document->resources.empty() && document->sounds.empty() && document->extraMedia.empty());
}
