#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/bitmap.h"
#include "../src/data/edge.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/group.h"
#include "../src/data/layer.h"
#include "../src/data/shape.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_list.h"
#include "../src/data/timeline.h"
#include "../src/edit/new_document.h"
#include "../src/parser/fla_parser.h"
#include "../src/phx/container.h"
#include "../src/phx/phx_document.h"
#include "../src/phx/wire.h"
#include "../src/writer/xfl_content.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdint>
#include <cstdlib>

namespace {

std::string scratchPath(const char* name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_phx_tests";
    std::filesystem::create_directories(folder);
    return (folder / name).string();
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

/// Every edge's original text dropped, so both sides of a comparison write
/// their edges from the geometry (a .phx keeps geometry, not the text).
void dropEdgeText(fla::Element& element)
{
    if (element.elementType() == fla::Element::Type::Shape)
    {
        for (fla::Edge* edge : static_cast<fla::Shape&>(element).edges)
        {
            if (edge)
                edge->data.clear();
        }
    }
    else if (element.elementType() == fla::Element::Type::Group)
    {
        for (fla::Element* member : static_cast<fla::Group&>(element).members)
        {
            if (member)
                dropEdgeText(*member);
        }
    }
}

void dropEdgeText(fla::Timeline& timeline)
{
    for (fla::Layer* layer : timeline.layers)
    {
        for (fla::Frame* frame : layer->frames)
        {
            for (fla::Element* element : frame->elements)
            {
                if (element)
                    dropEdgeText(*element);
            }
        }
    }
}

void dropEdgeText(fla::Document& document)
{
    for (fla::Timeline* timeline : document.timelines)
        dropEdgeText(*timeline);
    if (document.symbolList)
    {
        for (fla::Symbol* symbol : document.symbolList->symbols)
        {
            for (fla::Timeline* timeline : symbol->timelines)
                dropEdgeText(*timeline);
        }
    }
}

/// The package an export to FLA would write, as one comparable text.
std::string packageOf(const fla::Document& document, std::string& firstEntry)
{
    std::vector<fla::XFLEntry> entries;
    std::vector<std::string> unsupported;
    std::string error;
    if (!fla::XFLContent::build(document, "doc", entries, unsupported, error))
        return "build failed: " + error;
    std::string all;
    for (const fla::XFLEntry& entry : entries)
    {
        all += "== " + entry.path + "\n";
        if (entry.isBinary())
            all.append(reinterpret_cast<const char*>(entry.binary->data()), entry.binary->size());
        else
            all += entry.text;
        all += "\n";
        if (firstEntry.empty())
            firstEntry = entry.path;
    }
    return all;
}

/// Where two texts first differ, with a little around it.
std::string firstDifference(const std::string& a, const std::string& b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
        ++i;
    const size_t from = i > 120 ? i - 120 : 0;
    return "at " + std::to_string(i) + "\n  fla: " + a.substr(from, 240) + "\n  phx: " + b.substr(from, 240);
}

} // namespace

TEST(phx_wire_round_trips_every_kind_of_field)
{
    phx::Writer w;
    w.uint(1, 0);
    w.uint(2, 300);
    w.uint(3, ~0ULL);
    w.sint(4, -1);
    w.sint(5, -123456789012LL);
    w.real(6, 3.25);
    w.fixed32(7, 0xA1B2C3D4);
    w.string(8, "héllo");
    phx::Packed packed;
    packed.sint(-5);
    packed.sint(7);
    packed.writeTo(w, 9);

    phx::Reader r(w.buffer());
    int seen = 0;
    while (r.next())
    {
        ++seen;
        switch (r.field())
        {
        case 1: CHECK(r.uint() == 0); break;
        case 2: CHECK(r.uint() == 300); break;
        case 3: CHECK(r.uint() == ~0ULL); break;
        case 4: CHECK(r.sint() == -1); break;
        case 5: CHECK(r.sint() == -123456789012LL); break;
        case 6: CHECK(r.real() == 3.25); break;
        case 7: CHECK(r.fixed32() == 0xA1B2C3D4); break;
        case 8: CHECK(r.string() == "héllo"); break;
        case 9:
        {
            std::vector<int64_t> values;
            r.varints([&values](uint64_t v) { values.push_back(phx::Reader::unzigzag(v)); });
            CHECK(values.size() == 2 && values[0] == -5 && values[1] == 7);
            break;
        }
        default: CHECK(false); break;
        }
    }
    CHECK(seen == 9 && !r.failed());

    // A field cut short is damage, not a crash.
    phx::Reader cut(w.buffer().data(), w.buffer().size() - 3);
    while (cut.next())
    {
    }
    CHECK(cut.failed());
}

TEST(phx_container_finds_damage_and_recovers_its_index)
{
    const std::string path = scratchPath("container.phx");
    {
        phx::ContainerWriter out;
        std::string error;
        CHECK(out.open(path, error));
        CHECK(out.add(phx::kDocument, std::string(1000, 'a'), true, true) != 0);
        CHECK(out.add(phx::fourcc("ABCD"), std::string("ancillary"), false, false) != 0);
        CHECK(out.finish(error));
        CHECK(!std::filesystem::exists(path + ".saving"));
    }
    std::vector<uint8_t> good = readFile(path);

    {
        phx::ContainerReader in;
        std::string error;
        CHECK(in.openMemory(good, error) && !in.recovered());
        CHECK(in.chunks().size() == 2);
        std::string content;
        CHECK(in.content(in.chunks()[0], content, error) && content == std::string(1000, 'a'));
    }

    // A damaged header: the trailer still finds the index.
    {
        std::vector<uint8_t> bad = good;
        bad[20] ^= 0xFF;
        phx::ContainerReader in;
        std::string error;
        CHECK(in.openMemory(bad, error) && in.recovered() && in.chunks().size() == 2);
    }
    // A damaged index and trailer: the chunks are walked.
    {
        std::vector<uint8_t> bad = good;
        bad[bad.size() - 1] ^= 0xFF;
        bad[20] ^= 0xFF;
        phx::ContainerReader in;
        std::string error;
        CHECK(in.openMemory(bad, error) && in.recovered() && in.chunks().size() == 2);
    }
    // A damaged chunk: its checksum says so.
    {
        std::vector<uint8_t> bad = good;
        phx::ContainerReader probe;
        std::string error;
        probe.openMemory(good, error);
        const phx::ChunkInfo second = probe.chunks()[1];
        bad[static_cast<size_t>(second.offset) + 32] ^= 0x01;
        phx::ContainerReader in;
        CHECK(in.openMemory(bad, error));
        std::string content;
        CHECK(!in.content(in.chunks()[1], content, error) && !error.empty());
    }
    // A newer major version is refused with a reason.
    {
        std::vector<uint8_t> newer = good;
        newer[8] = 2;
        phx::ContainerReader in;
        std::string error;
        CHECK(!in.openMemory(newer, error) && error.find("newer") != std::string::npos);
    }
}

TEST(phx_round_trips_a_new_document)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(1280, 720, 30.0));
    CHECK(document && document->document);
    if (!document || !document->document)
        return;
    const std::string path = scratchPath("new.phx");
    std::string error;
    CHECK(phx::save(*document->document, path, phx::SaveOptions(), error));

    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> back(phx::load(path, error, warning, readOnly));
    CHECK(back && back->document && warning.empty() && !readOnly);
    if (!back || !back->document)
        return;
    CHECK(back->document->width == 1280 && back->document->height == 720 && back->document->frameRate == 30.0);
    std::string a, b;
    CHECK(packageOf(*document->document, a) == packageOf(*back->document, b));
}

TEST(phx_keeps_locked_guides)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(550, 400, 24.0));
    CHECK(document && document->document);
    if (!document || !document->document)
        return;
    CHECK(!document->document->guidesLocked);
    document->document->guidesLocked = true;
    const std::string path = scratchPath("locked_guides.phx");
    std::string error;
    CHECK(phx::save(*document->document, path, phx::SaveOptions(), error));
    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> back(phx::load(path, error, warning, readOnly));
    CHECK(back && back->document && back->document->guidesLocked);
}

TEST(phx_keeps_what_a_newer_phoenix_wrote)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(640, 480, 24.0));
    if (!document || !document->document)
        return;
    const std::string path = scratchPath("older.phx");
    std::string error;
    CHECK(phx::save(*document->document, path, phx::SaveOptions(), error));

    // As a newer Phoenix would write it: a field this one does not know on
    // the document, a new ancillary chunk, and the same again critical.
    const std::string newer = scratchPath("newer.phx");
    {
        phx::ContainerReader in;
        CHECK(in.open(path, error));
        phx::ContainerWriter out;
        CHECK(out.open(newer, error));
        for (const phx::ChunkInfo& chunk : in.chunks())
        {
            std::string content;
            CHECK(in.content(chunk, content, error));
            if (chunk.type == phx::kDocument)
            {
                phx::Writer extra;
                extra.string(999, "from the future");
                content += extra.buffer();
            }
            out.add(chunk.type, content, true, (chunk.flags & phx::kCritical) != 0, chunk.mediaId, chunk.symbolName);
        }
        out.add(phx::fourcc("NEWA"), std::string("later"), false, false);
        CHECK(out.finish(error));
    }

    std::string warning;
    bool readOnly = true;
    std::unique_ptr<fla::FLADocument> loaded(phx::load(newer, error, warning, readOnly));
    CHECK(loaded && loaded->document && !readOnly);
    if (!loaded || !loaded->document)
        return;
    CHECK(loaded->document->phxUnknown.find("from the future") != std::string::npos);
    CHECK(loaded->document->phxUnknownChunks.size() == 1);

    // Saved here and read again, both are still there.
    const std::string resaved = scratchPath("resaved.phx");
    CHECK(phx::save(*loaded->document, resaved, phx::SaveOptions(), error));
    std::unique_ptr<fla::FLADocument> again(phx::load(resaved, error, warning, readOnly));
    CHECK(again && again->document);
    if (!again || !again->document)
        return;
    CHECK(again->document->phxUnknown.find("from the future") != std::string::npos);
    CHECK(again->document->phxUnknownChunks.size() == 1 &&
        again->document->phxUnknownChunks.front().content == "later");

    // A critical part this Phoenix does not know: open, but read-only.
    {
        phx::ContainerReader in;
        CHECK(in.open(resaved, error));
        const std::string critical = scratchPath("critical.phx");
        phx::ContainerWriter out;
        CHECK(out.open(critical, error));
        for (const phx::ChunkInfo& chunk : in.chunks())
            out.addStored(chunk, in.stored(chunk));
        out.add(phx::fourcc("NEWC"), std::string("needed"), false, true);
        CHECK(out.finish(error));
        std::string criticalWarning;
        std::unique_ptr<fla::FLADocument> locked(phx::load(critical, error, criticalWarning, readOnly));
        CHECK(locked && readOnly && criticalWarning.find("NEWC") != std::string::npos);
    }
}

TEST(phx_round_trips_real_documents_as_their_fla_would)
{
    // Opt-in: every FLA in PHOENIX_FLA_CORPUS, saved as .phx and read back,
    // must export to exactly the FLA package the original exports to.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }
    using Clock = std::chrono::steady_clock;
    double flaSeconds = 0.0;
    double phxSaveSeconds = 0.0;
    double phxLoadSeconds = 0.0;
    double phxReloadSeconds = 0.0;
    double phxResaveSeconds = 0.0;
    uint64_t flaBytes = 0;
    uint64_t phxBytes = 0;
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".fla")
            continue;
        const std::string path = entry.path().string();
        const auto readStart = Clock::now();
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> original(parser.parse(path));
        const double readSeconds = std::chrono::duration<double>(Clock::now() - readStart).count();
        if (!original || !original->document)
            continue;
        dropEdgeText(*original->document);

        const std::string phxPath = scratchPath("corpus.phx");
        std::string error;
        const auto saveStart = Clock::now();
        const bool saved = phx::save(*original->document, phxPath, phx::SaveOptions(), error);
        const double saveSeconds = std::chrono::duration<double>(Clock::now() - saveStart).count();
        CHECK(saved);
        if (!saved)
        {
            std::printf("    %s: save failed: %s\n", path.c_str(), error.c_str());
            continue;
        }
        std::string warning;
        bool readOnly = false;
        const auto loadStart = Clock::now();
        std::unique_ptr<fla::FLADocument> back(phx::load(phxPath, error, warning, readOnly));
        const double loadSeconds = std::chrono::duration<double>(Clock::now() - loadStart).count();
        CHECK(back && back->document && warning.empty() && !readOnly);
        if (!back || !back->document)
        {
            std::printf("    %s: load failed: %s\n", path.c_str(), error.c_str());
            continue;
        }
        std::string firstA, firstB;
        const std::string expected = packageOf(*original->document, firstA);
        const std::string actual = packageOf(*back->document, firstB);
        if (expected != actual)
        {
            std::printf("    %s differs %s\n", entry.path().filename().string().c_str(),
                firstDifference(expected, actual).c_str());
            CHECK(false);
        }
        ++checked;
        flaSeconds += readSeconds;
        phxSaveSeconds += saveSeconds;
        phxLoadSeconds += loadSeconds;
        {
            // Again, now the file is no longer new to the system: the first
            // open of a just-written file also pays for virus scanning.
            const auto again = Clock::now();
            std::unique_ptr<fla::FLADocument> reread(phx::load(phxPath, error, warning, readOnly));
            phxReloadSeconds += std::chrono::duration<double>(Clock::now() - again).count();
            // Saved again as a Phoenix document: its media copied as stored.
            if (reread && reread->document)
            {
                const auto resave = Clock::now();
                CHECK(phx::save(*reread->document, scratchPath("corpus_resaved.phx"), phx::SaveOptions(), error));
                phxResaveSeconds += std::chrono::duration<double>(Clock::now() - resave).count();
            }
        }
        flaBytes += std::filesystem::file_size(entry.path());
        phxBytes += std::filesystem::file_size(phxPath);
    }
    std::printf("    %d documents: FLA %.1f MB read in %.2f s; PHX %.1f MB, saved in %.2f s, read in %.2f s "
                "(again: %.2f s), saved again in %.2f s\n",
        checked, flaBytes / 1048576.0, flaSeconds, phxBytes / 1048576.0, phxSaveSeconds, phxLoadSeconds,
        phxReloadSeconds, phxResaveSeconds);
}

TEST(phx_media_load_when_needed_and_are_copied_as_stored)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(320, 240, 24.0));
    if (!document || !document->document)
        return;
    // A bitmap and a package file, of bytes that do not compress (stored as
    // they are) and bytes that do (deflated).
    std::vector<uint8_t> noise(5000);
    uint32_t state = 12345;
    for (uint8_t& byte : noise)
    {
        state = state * 1103515245u + 12345u;
        byte = static_cast<uint8_t>(state >> 16);
    }
    noise[0] = 0x89;
    noise[1] = 'P';
    noise[2] = 'N';
    noise[3] = 'G';
    auto* bitmap = new fla::Bitmap(document->document);
    bitmap->name = "noise.png";
    bitmap->href = "noise.png";
    bitmap->imageData = noise;
    document->document->resources.push_back(bitmap);
    document->document->extraFiles.emplace_back("bin/sound.dat", std::vector<uint8_t>(40000, 7));

    const std::string first = scratchPath("lazy_a.phx");
    std::string error;
    CHECK(phx::save(*document->document, first, phx::SaveOptions(), error));

    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> loaded(phx::load(first, error, warning, readOnly));
    CHECK(loaded && loaded->document && warning.empty());
    if (!loaded || !loaded->document)
        return;
    const auto* back = static_cast<const fla::Bitmap*>(loaded->document->resources.front());
    const fla::LazyBytes& file = loaded->document->extraFiles.front().second;
    // Known without reading: there, and how big.
    CHECK(!back->imageData.loaded() && back->imageData.size() == noise.size());
    CHECK(!file.loaded() && file.size() == 40000);

    // Saved elsewhere: copied as stored, still not read.
    const std::string second = scratchPath("lazy_b.phx");
    CHECK(phx::save(*loaded->document, second, phx::SaveOptions(), error));
    CHECK(!back->imageData.loaded() && !file.loaded());
    CHECK(back->imageData.location() && back->imageData.location()->path.find("lazy_b.phx") != std::string::npos);

    // Saved over the file it reads from: still readable, and right.
    CHECK(phx::save(*loaded->document, second, phx::SaveOptions(), error));
    CHECK(back->imageData.get() == noise);
    CHECK(file.get() == std::vector<uint8_t>(40000, 7));

    // A copy read back holds the same bytes.
    std::unique_ptr<fla::FLADocument> again(phx::load(second, error, warning, readOnly));
    CHECK(again && again->document);
    if (again && again->document)
        CHECK(static_cast<const fla::Bitmap*>(again->document->resources.front())->imageData.get() == noise);

    // The file media were to come from is gone, unread: the save refuses
    // rather than lose them.
    std::unique_ptr<fla::FLADocument> orphan(phx::load(first, error, warning, readOnly));
    CHECK(orphan && orphan->document);
    if (!orphan || !orphan->document)
        return;
    std::filesystem::remove(first);
    std::string lost;
    CHECK(!phx::save(*orphan->document, scratchPath("lazy_c.phx"), phx::SaveOptions(), lost));
    CHECK(lost.find("could not be read") != std::string::npos);
}

// A snapshot is the document as it was when taken: written after the
// document has changed (as the recovery copy is, on another thread), the
// file holds the earlier state.
TEST(phx_snapshot_writes_the_document_as_it_was)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(1280, 720, 30.0));
    CHECK(document && document->document);
    if (!document || !document->document)
        return;
    std::string error;
    const std::shared_ptr<phx::SaveSnapshot> snapshot =
        phx::snapshot(*document->document, phx::SaveOptions(), error);
    CHECK(snapshot != nullptr);
    if (!snapshot)
        return;
    document->document->width = 640;
    const std::string path = scratchPath("snapshot.phx");
    CHECK(phx::write(*snapshot, path, error));

    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> back(phx::load(path, error, warning, readOnly));
    CHECK(back && back->document && back->document->width == 1280);
}

TEST(phx_container_refuses_sizes_past_the_file)
{
    const std::string path = scratchPath("hostile.phx");
    {
        phx::ContainerWriter out;
        std::string error;
        CHECK(out.open(path, error));
        out.add(phx::kDocument, std::string(1000, 'a'), true, true);
        // A chunk that says it inflates to 100 GB.
        phx::PreparedChunk big = phx::prepare(phx::fourcc("BIGC"), std::string(1000, 'b'), true, false);
        big.info.length = 100ull << 30;
        out.addStored(big.info, reinterpret_cast<const uint8_t*>(big.stored.data()));
        CHECK(out.finish(error));
    }
    const std::vector<uint8_t> good = readFile(path);
    {
        phx::ContainerReader in;
        std::string error;
        CHECK(in.openMemory(good, error) && in.chunks().size() == 2);
        std::string content;
        CHECK(in.chunks().size() == 2 && !in.content(in.chunks()[1], content, error) && content.empty());
        CHECK(error.find("damaged") != std::string::npos);
    }
    // Header and trailer damaged, so the chunks are walked, and the first
    // chunk's stored length so large that offset + length wraps round.
    {
        std::vector<uint8_t> bad = good;
        bad[20] ^= 0xFF;
        bad[bad.size() - 1] ^= 0xFF;
        for (int i = 0; i < 8; ++i)
            bad[32 + 8 + static_cast<size_t>(i)] = i == 0 ? 0xF0 : 0xFF;
        phx::ContainerReader in;
        std::string error;
        CHECK(!in.openMemory(bad, error) && in.chunks().empty());
    }
}

TEST(phx_damaged_documents_read_what_they_can)
{
    std::unique_ptr<fla::FLADocument> document(fla::createEmptyDocument(320, 240, 24.0));
    CHECK(document && document->document && !document->document->timelines.empty());
    if (!document || !document->document || document->document->timelines.empty())
        return;
    fla::Layer* layer = document->document->timelines[0]->layers[0];
    fla::Frame* frame = layer->frames[0];
    frame->duration = 2147483647;

    // Groups nested past the reader's limit.
    fla::DOMElement* parent = frame;
    fla::Group* outer = nullptr;
    for (int i = 0; i < 260; ++i)
    {
        auto* group = new fla::Group(parent);
        if (outer)
            static_cast<fla::Group*>(parent)->members.push_back(group);
        else
            outer = group;
        parent = group;
    }
    frame->elements.push_back(outer);

    // A shape whose path starts with a move that has no point.
    auto* shape = new fla::Shape(frame);
    auto* edge = new fla::Edge(shape);
    auto* path = new fla::Path(edge);
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Move, {}, path));
    path->segments.push_back(new fla::PathSegment(fla::PathSegment::Command::Line, {fla::Point(10, 10)}, path));
    edge->paths.push_back(path);
    shape->edges.push_back(edge);
    frame->elements.push_back(shape);

    const std::string file = scratchPath("damaged.phx");
    std::string error;
    CHECK(phx::save(*document->document, file, phx::SaveOptions(), error));
    std::string warning;
    bool readOnly = false;
    std::unique_ptr<fla::FLADocument> back(phx::load(file, error, warning, readOnly));
    CHECK(back && back->document);
    if (!back || !back->document)
        return;
    CHECK(warning.find("damaged") != std::string::npos);
    const fla::Frame* read = back->document->timelines[0]->layers[0]->frames[0];
    CHECK(read->duration == fla::kMaxFrames);
    CHECK(read->elements.size() == 2);
    if (read->elements.size() != 2)
        return;
    int depth = 0;
    for (const fla::Element* at = read->elements[0]; at && at->elementType() == fla::Element::Type::Group;)
    {
        ++depth;
        const auto* group = static_cast<const fla::Group*>(at);
        at = group->members.empty() ? nullptr : group->members[0];
    }
    CHECK(depth > 0 && depth <= 200);
    const auto* readShape = static_cast<const fla::Shape*>(read->elements[1]);
    CHECK(readShape->edges.size() == 1 && readShape->edges[0]->paths.size() == 1);
    if (readShape->edges.size() == 1 && readShape->edges[0]->paths.size() == 1)
    {
        const fla::Path* readPath = readShape->edges[0]->paths[0];
        CHECK(readPath->segments.size() == 1 && readPath->segments[0]->command == fla::PathSegment::Command::Line);
    }
}
