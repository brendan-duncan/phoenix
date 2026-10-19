#include "test_util.h"

#include "../src/data/bitmap.h"
#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/static_text.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/parser/binary_fla.h"
#include "../src/parser/compound_file.h"
#include "../src/parser/fla_parser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace {

/// A minimal compound file of 512-byte sectors holding one stream of
/// \a size bytes: big enough (4096 or more) to sit in ordinary sectors,
/// smaller to sit in the mini stream.
std::vector<uint8_t> compoundFile(const std::string& name, size_t size)
{
    const size_t sector = 512;
    const bool mini = size < 4096;
    const size_t dataSectors = mini ? (size * 1 + 63) / 64 * 64 / sector + 1 : (size + sector - 1) / sector;
    // Sectors: 0 FAT, 1 directory, 2 mini FAT, then data.
    std::vector<uint8_t> file((3 + dataSectors + 1) * sector, 0);
    const auto put16 = [&](size_t at, uint16_t v) {
        file[at] = static_cast<uint8_t>(v);
        file[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    const auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            file[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    const uint8_t signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::copy(signature, signature + 8, file.begin());
    put16(24, 0x3E);
    put16(26, 3);
    put16(28, 0xFFFE);
    put16(30, 9);
    put16(32, 6);
    put32(44, 1);          // FAT sectors
    put32(48, 1);          // first directory sector
    put32(56, 4096);       // mini stream cutoff
    put32(60, 2);          // first mini FAT sector
    put32(64, 1);          // mini FAT sectors
    put32(68, 0xFFFFFFFE); // no DIFAT
    for (int i = 0; i < 109; ++i)
        put32(76 + 4 * i, i == 0 ? 0 : 0xFFFFFFFF);
    const auto at = [&](size_t s) { return (s + 1) * sector; };

    // FAT: 0 is the FAT itself, 1 and 2 single-sector chains, then the data.
    std::vector<uint32_t> fat(sector / 4, 0xFFFFFFFF);
    fat[0] = 0xFFFFFFFD;
    fat[1] = 0xFFFFFFFE;
    fat[2] = 0xFFFFFFFE;
    for (size_t i = 0; i < dataSectors; ++i)
        fat[3 + i] = i + 1 < dataSectors ? static_cast<uint32_t>(4 + i) : 0xFFFFFFFE;
    for (size_t i = 0; i < fat.size(); ++i)
        put32(at(0) + 4 * i, fat[i]);

    // Mini FAT: the stream's mini sectors in a row.
    const size_t miniSectors = (size + 63) / 64;
    for (size_t i = 0; i < sector / 4; ++i)
        put32(at(2) + 4 * i, mini && i < miniSectors ? (i + 1 < miniSectors ? static_cast<uint32_t>(i + 1) : 0xFFFFFFFE) : 0xFFFFFFFF);

    // Directory: the root, then the stream.
    const auto entry = [&](size_t index, const std::string& n, uint8_t type, uint32_t child, uint32_t start, uint32_t length) {
        const size_t base = at(1) + 128 * index;
        for (size_t i = 0; i < n.size(); ++i)
            put16(base + 2 * i, static_cast<uint8_t>(n[i]));
        put16(base + 64, static_cast<uint16_t>(2 * (n.size() + 1)));
        file[base + 66] = type;
        put32(base + 68, 0xFFFFFFFF);
        put32(base + 72, 0xFFFFFFFF);
        put32(base + 76, child);
        put32(base + 116, start);
        put32(base + 120, length);
    };
    entry(0, "Root Entry", 5, 1, mini ? 3 : 0xFFFFFFFE, mini ? static_cast<uint32_t>(miniSectors * 64) : 0);
    entry(1, name, 2, 0xFFFFFFFF, mini ? 0 : 3, static_cast<uint32_t>(size));
    for (size_t i = 2; i < 4; ++i)
    {
        put32(at(1) + 128 * i + 68, 0xFFFFFFFF);
        put32(at(1) + 128 * i + 72, 0xFFFFFFFF);
        put32(at(1) + 128 * i + 76, 0xFFFFFFFF);
    }

    // The stream's bytes count up.
    for (size_t i = 0; i < size; ++i)
        file[at(3) + i] = static_cast<uint8_t>(i * 7);
    return file;
}

} // namespace

TEST(compound_file_reads_streams_from_sectors_and_the_mini_stream)
{
    for (size_t size : {size_t(100), size_t(5000)})
    {
        CompoundFile file;
        CHECK(file.read(compoundFile("Contents", size)));
        const auto found = file.streams().find("Contents");
        CHECK(found != file.streams().end());
        if (found == file.streams().end())
            continue;
        CHECK(found->second.size() == size);
        bool same = true;
        for (size_t i = 0; i < found->second.size(); ++i)
            same = same && found->second[i] == static_cast<uint8_t>(i * 7);
        CHECK(same);
    }
    CompoundFile notOne;
    CHECK(!notOne.read(std::vector<uint8_t>(600, 0)));
}

TEST(binary_fla_reads_as_its_xfl_twin)
{
    // Opt-in, with the corpus's binary Freya_Fairy_07.fla (Flash CS3) and
    // Yard_Sale_03.fla (MX 2004), and the same documents saved by Animate.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus)
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS\n");
        return;
    }
    const std::pair<const char*, const char*> pairs[2] = {
        {"Freya_Fairy_07.fla", "Freya_Fairy_07_2.fla"}, {"Yard_Sale_03.fla", "Yard_Sale_03_2.fla"}};
    for (const auto& pair : pairs)
    {
        const std::filesystem::path binary = std::filesystem::path(corpus) / pair.first;
        const std::filesystem::path xfl = std::filesystem::path(corpus) / pair.second;
        if (!std::filesystem::exists(binary) || !std::filesystem::exists(xfl))
        {
            std::printf("    skipped: no %s\n", pair.first);
            continue;
        }
        FLAParser binaryParser;
        FLAParser xflParser;
        std::unique_ptr<fla::FLADocument> a(binaryParser.parse(binary.string()));
        std::unique_ptr<fla::FLADocument> b(xflParser.parse(xfl.string()));
        CHECK(a && a->document);
        CHECK(b && b->document);
        if (!a || !a->document || !b || !b->document)
        {
            std::printf("    %s: %s\n", pair.first, binaryParser.errorString().c_str());
            continue;
        }
        const fla::Document& da = *a->document;
        const fla::Document& db = *b->document;
        CHECK(da.width == db.width && da.height == db.height);
        CHECK(da.frameRate == db.frameRate);
        CHECK(da.timelines.size() == db.timelines.size());
        CHECK(da.resources.size() == db.resources.size());
        // It opens on the scene it was saved on (Yard_Sale_03's sixth).
        CHECK(da.currentSceneIndex() == db.currentSceneIndex());
        // Bitmaps say what the twin says: a JPEG is marked as one and has its
        // size, as the reference app needs.
        const auto isJpeg = [](const fla::Bitmap& bitmap) {
            for (const auto& [key, value] : bitmap.extraAttributes)
            {
                if (key == "isJPEG")
                    return value == "true";
            }
            return false;
        };
        for (const fla::Resource* ra : da.resources)
        {
            if (!ra || ra->resourceType() != fla::Resource::Type::Bitmap)
                continue;
            const fla::Bitmap& ba = *static_cast<const fla::Bitmap*>(ra);
            for (const fla::Resource* rb : db.resources)
            {
                if (!rb || rb->resourceType() != fla::Resource::Type::Bitmap)
                    continue;
                const fla::Bitmap& bb = *static_cast<const fla::Bitmap*>(rb);
                if (bb.name != ba.name)
                    continue;
                const bool same = ba.frameRight == bb.frameRight && ba.frameBottom == bb.frameBottom &&
                    isJpeg(ba) == isJpeg(bb);
                if (!same)
                    std::printf("    %s: bitmap %s differs\n", pair.first, ba.name.c_str());
                CHECK(same);
            }
        }
        for (size_t t = 0; t < da.timelines.size() && t < db.timelines.size(); ++t)
        {
            const fla::Timeline& ta = *da.timelines[t];
            const fla::Timeline& tb = *db.timelines[t];
            CHECK(ta.name == tb.name);
            CHECK(ta.layers.size() == tb.layers.size());
            for (size_t l = 0; l < ta.layers.size() && l < tb.layers.size(); ++l)
            {
                const fla::Layer& la = *ta.layers[l];
                const fla::Layer& lb = *tb.layers[l];
                const bool same = la.name == lb.name && la.frames.size() == lb.frames.size() &&
                    la.locked == lb.locked && la.visible == lb.visible;
                if (!same)
                    std::printf("    %s: layer %s differs\n", pair.first, la.name.c_str());
                CHECK(same);
                for (size_t f = 0; f < la.frames.size() && f < lb.frames.size(); ++f)
                {
                    CHECK(la.frames[f]->index == lb.frames[f]->index);
                    CHECK(la.frames[f]->duration == lb.frames[f]->duration);
                    CHECK(la.frames[f]->tweenType == lb.frames[f]->tweenType);
                    CHECK(la.frames[f]->motionTweenRotate == lb.frames[f]->motionTweenRotate);
                    CHECK(la.frames[f]->motionTweenRotateTimes == lb.frames[f]->motionTweenRotateTimes);
                    // Texts keep their line breaks and grow to fit as the
                    // twin's do.
                    const auto texts = [](const fla::Frame& frame) {
                        std::vector<const fla::StaticText*> found;
                        for (const fla::Element* element : frame.elements)
                        {
                            if (element && element->elementType() == fla::Element::Type::StaticText)
                                found.push_back(static_cast<const fla::StaticText*>(element));
                        }
                        return found;
                    };
                    const auto ta = texts(*la.frames[f]);
                    const auto tb = texts(*lb.frames[f]);
                    CHECK(ta.size() == tb.size());
                    for (size_t i = 0; i < ta.size() && i < tb.size(); ++i)
                    {
                        std::string sa;
                        std::string sb;
                        for (const fla::TextRun& run : ta[i]->runs)
                            sa += run.text;
                        for (const fla::TextRun& run : tb[i]->runs)
                            sb += run.text;
                        if (sa != sb)
                            std::printf("    %s: text \"%s\" differs\n", pair.first, sb.c_str());
                        CHECK(sa == sb);
                        CHECK(ta[i]->autoExpand == tb[i]->autoExpand);
                    }
                }
            }
        }
    }
}

TEST(compound_file_survives_a_difat_that_points_at_itself)
{
    // Counts in the header far past the file, and a DIFAT sector whose
    // next link is itself: read in moments, not grown to gigabytes.
    std::vector<uint8_t> file = compoundFile("Contents", 5000);
    const auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            file[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    const size_t sector = 3; // the stream's first sector
    put32(44, 0xFFFFFFFF);
    put32(68, static_cast<uint32_t>(sector));
    put32(72, 0xFFFFFFFF);
    put32((sector + 1) * 512 + 508, static_cast<uint32_t>(sector));
    CompoundFile compound;
    compound.read(file);
    CHECK(compound.streams().size() <= 1);
}

namespace {

/// A Contents stream with nothing in it, of MX 2004 unless given.
std::vector<uint8_t> emptyContents(uint16_t version = 0x129)
{
    std::vector<uint8_t> contents(400, 0);
    contents[0] = static_cast<uint8_t>(version);
    contents[1] = static_cast<uint8_t>(version >> 8);
    return contents;
}

/// A Contents stream of CS3's layout naming one scene, "Page 1".
std::vector<uint8_t> contentsWithScene(uint16_t version)
{
    std::vector<uint8_t> c = {static_cast<uint8_t>(version), static_cast<uint8_t>(version >> 8)};
    c.insert(c.end(), 25, 0);
    const std::string cls = "CDocumentPage";
    c.insert(c.end(), {0xFF, 0xFF, 0, 0, static_cast<uint8_t>(cls.size()), 0});
    c.insert(c.end(), cls.begin(), cls.end());
    const auto u32 = [&c](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            c.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto text = [&c](const std::string& s) {
        c.push_back(static_cast<uint8_t>(s.size()));
        c.insert(c.end(), s.begin(), s.end());
    };
    const auto linkage = [&]() {
        u32(3);
        c.push_back(0);
        text("");
        text("");
        text("");
        u32(0);
    };
    const auto source = [&]() {
        text("");
        text("");
        c.insert(c.end(), 12 + 8, 0);
    };
    c.push_back(17); // schema
    text("Page 1");
    text("Scene 1");
    u32(0);
    c.push_back(0);
    text("");
    u32(1);
    u32(5); // item header
    u32(1);
    c.insert(c.end(), 8, 0);
    u32(1);
    c.insert(c.end(), 8, 0);
    u32(0);
    linkage();
    source();
    u32(0); // last modified
    text("");
    text("");
    u32(0);
    c.push_back(0);
    u32(0);
    u32(0);
    linkage();
    source();
    c.insert(c.end(), 34, 0);
    c.insert(c.end(), 400, 0); // the end of the list and the rest, empty
    return c;
}

/// A page stream: the page's class declared, then \a body.
std::vector<uint8_t> pageStream(const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> page = {1, 0xFF, 0xFF, 0, 0, 8, 0};
    const std::string cls = "CPicPage";
    page.insert(page.end(), cls.begin(), cls.end());
    page.insert(page.end(), body.begin(), body.end());
    page.resize(page.size() + 64, 0);
    return page;
}

} // namespace

TEST(binary_fla_stops_a_stream_at_an_object_inside_itself)
{
    // The page's first child is a reference to the page, still being read:
    // the page ends there, and the document opens with a warning.
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = emptyContents();
    streams["Page 1"] = pageStream({0, 0, 2, 0});
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.find("still being read") != std::string::npos && files.count("DOMDocument.xml"));
}

TEST(binary_fla_stops_a_stream_at_objects_nested_past_a_limit)
{
    // Shapes inside shapes, thousands deep: a warning, not a stack overflow.
    std::vector<uint8_t> body = {0, 0};
    const std::string cls = "CPicShape";
    body.insert(body.end(), {0xFF, 0xFF, 0, 0, static_cast<uint8_t>(cls.size()), 0});
    body.insert(body.end(), cls.begin(), cls.end());
    body.insert(body.end(), {0, 0});
    for (int i = 0; i < 5000; ++i)
        body.insert(body.end(), {0x03, 0x80, 0, 0}); // the class, declared third
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = emptyContents();
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.find("too deep") != std::string::npos);
}

TEST(binary_fla_refuses_objects_repeated_past_reason)
{
    // A layer and frame holding shapes, each a group of thirty references
    // to the one before: a billion groups unless writing stops.
    std::vector<uint8_t> body = {0, 0};
    const auto declare = [&body](const std::string& name) {
        body.insert(body.end(), {0xFF, 0xFF, 0, 0, static_cast<uint8_t>(name.size()), 0});
        body.insert(body.end(), name.begin(), name.end());
    };
    const auto zeros = [&body](size_t count) { body.insert(body.end(), count, 0); };
    declare("CPicLayer"); // class 3, layer 4
    zeros(2);
    declare("CPicFrame"); // class 5, frame 6
    zeros(2);
    declare("CPicShape"); // class 7, the first shape 8
    zeros(4); // no children
    zeros(43); // its point and empty shape
    for (int level = 0; level < 6; ++level)
    {
        body.insert(body.end(), {0x07, 0x80, 0, 0});
        for (int i = 0; i < 30; ++i)
            body.insert(body.end(), {static_cast<uint8_t>(8 + level), 0});
        zeros(2);
        zeros(43);
    }
    zeros(2); // the end of the frame's children
    zeros(117); // the rest of the frame
    zeros(2); // the end of the layer's children
    zeros(34);
    zeros(2);
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = contentsWithScene(0x43);
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(!BinaryFla::convert(streams, files, error, &warning));
    CHECK(error.find("past reason") != std::string::npos);
}

TEST(binary_fla_keeps_what_comes_before_a_class_it_does_not_know)
{
    // A layer whose frame holds an object of a class Phoenix cannot read:
    // the layer is kept, up to that object.
    std::vector<uint8_t> body = {0, 0};
    const auto declare = [&body](const std::string& name) {
        body.insert(body.end(), {0xFF, 0xFF, 0, 0, static_cast<uint8_t>(name.size()), 0});
        body.insert(body.end(), name.begin(), name.end());
    };
    declare("CPicLayer");
    body.insert(body.end(), {0, 0});
    declare("CPicNotAClass");
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = emptyContents();
    streams["Page 1"] = pageStream(body);
    streams["Symbol 1"] = pageStream({0, 0, 0, 0});
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.find("CPicNotAClass") != std::string::npos && warning.find("Page 1") != std::string::npos);
    CHECK(warning.find("Symbol 1") == std::string::npos);

    // A Contents version Phoenix does not know still refuses the document.
    streams["Contents"][0] = 0x99;
    CHECK(!BinaryFla::convert(streams, files, error, &warning) && error.find("version") != std::string::npos);
}

TEST(binary_fla_reads_cs4_cubics_and_custom_eases)
{
    std::vector<uint8_t> body = {0, 0, 0xFF, 0xFF, 0, 0, 9, 0};
    const std::string layerClass = "CPicLayer";
    body.insert(body.end(), layerClass.begin(), layerClass.end());
    body.insert(body.end(), {0, 0}); // the layer's CPicObj: schema, flags
    const auto u32 = [&body](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            body.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto f64 = [&u32](double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(static_cast<uint32_t>(bits));
        u32(static_cast<uint32_t>(bits >> 32));
    };
    // A frame of the newer kind: CPicObj, then its shape (schema 6) with
    // two cubics, the first followed by one extra point (flag bit 0; bit 2
    // adds none) and the second by two.
    const auto frame = [&](bool first, uint8_t shapeSchema, uint8_t frameSchema, const std::string& label) {
        if (first)
        {
            body.insert(body.end(), {0xFF, 0xFF, 0, 0, 9, 0});
            const std::string cls = "CPicFrame";
            body.insert(body.end(), cls.begin(), cls.end());
        }
        else
        {
            body.insert(body.end(), {0x05, 0x80}); // the frame class, declared fifth
        }
        body.insert(body.end(), {0, 0, 0, 0});
        body.insert(body.end(), 8, 0);
        body.push_back(shapeSchema);
        body.insert(body.end(), 24, 0); // matrix
        body.push_back(5);              // shape data schema
        u32(0);
        body.insert(body.end(), {0, 0, 0, 0, 0}); // no fills, no strokes, no edges
        if (shapeSchema >= 3)
        {
            u32(2);
            body.insert(body.end(), 32, 7);
            body.push_back(1);
            body.insert(body.end(), 10, 7);
            body.push_back(0x05);
            body.insert(body.end(), 8, 7);
            body.insert(body.end(), 32, 7);
            body.push_back(0);
            body.push_back(0x03);
            body.insert(body.end(), 16, 7);
        }
        else
        {
            u32(0);
        }
        body.push_back(frameSchema);
        body.insert(body.end(), {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); // duration, motion, ease, sound...
        u32(0);
        u32(0);
        body.insert(body.end(), {0, 0});
        body.push_back(static_cast<uint8_t>(label.size()));
        body.insert(body.end(), label.begin(), label.end());
        u32(5); // actions: version 5, no script
        body.insert(body.end(), 16, 0);
        body.push_back(0);
        body.insert(body.end(), 12, 0);
        body.insert(body.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0}); // no morph, u32, no hints, CString
        body.insert(body.end(), 12, 0);
        if (frameSchema >= 24)
        {
            u32(1);
            u32(1); // custom eases follow: six curves of two points
            for (int target = 0; target < 6; ++target)
            {
                u32(2);
                for (double v : {0.0, 0.0, 0.0, 0.0, 0.5, 0.1, 0.9, 0.5, 1.0, 1.0, 1.0, 1.0})
                    f64(v);
            }
        }
    };
    frame(true, 6, 26, "");
    frame(false, 2, 22, "next");
    body.insert(body.end(), {0, 0}); // no more frames
    body.insert(body.end(), 8, 0);
    body.push_back(11);
    body.push_back(0);
    body.insert(body.end(), 3 + 4 + 4 + 4 + 4, 0);
    body.insert(body.end(), {0, 0, 0, 1, 1});
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = contentsWithScene(0x43);
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.empty());
    const auto xml = files["DOMDocument.xml"];
    const std::string text(xml.begin(), xml.end());
    // The second frame read in place, and the first's curve kept.
    CHECK(text.find("name=\"next\"") != std::string::npos);
    CHECK(text.find("hasCustomEase=\"true\"") != std::string::npos);
    CHECK(text.find("<Point x=\"0.5\" y=\"0.1\"/>") != std::string::npos);
}

TEST(binary_fla_reads_flash_8_contents_and_32_bit_tags)
{
    // Contents 0x138 (Flash 8), and a page whose second layer names its
    // class with the 32-bit tag a stream of more than 0x7FFE objects uses.
    std::vector<uint8_t> layer = {0, 0, 0, 0};       // CPicObj: schema, flags, no children
    layer.insert(layer.end(), 8, 0);                 // transformation point
    layer.push_back(11);                             // layer schema
    layer.push_back(0);                              // name
    layer.insert(layer.end(), 3 + 4 + 4 + 4 + 4, 0); // current, hidden, locked; u32; color; u32 x2
    layer.insert(layer.end(), {0, 0, 0, 1, 1});      // type, no parent, u8, autoNamed
    std::vector<uint8_t> body = {0, 0, 0xFF, 0xFF, 0, 0, 9, 0};
    const std::string cls = "CPicLayer";
    body.insert(body.end(), cls.begin(), cls.end());
    body.insert(body.end(), layer.begin(), layer.end());
    body.insert(body.end(), {0xFF, 0x7F, 0x03, 0x00, 0x00, 0x80}); // 0x7FFF, class 3
    body.insert(body.end(), layer.begin(), layer.end());
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = emptyContents(0x138);
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    // Both layers read (a misread tag would stop the page with a warning).
    CHECK(BinaryFla::convert(streams, files, error, &warning) && warning.empty() && files.count("DOMDocument.xml"));
}

TEST(binary_fla_reads_every_contents_version)
{
    // Flash 5 to CS4, with the high byte of the version 0 or 1.
    for (const uint16_t version : {0x1B, 0x11B, 0x20, 0x120, 0x29, 0x129, 0x38, 0x138, 0x3F, 0x13F, 0x43, 0x143, 0x147})
    {
        std::map<std::string, std::vector<uint8_t>> streams;
        streams["Contents"] = emptyContents(version);
        std::map<std::string, std::vector<uint8_t>> files;
        std::string error;
        std::string warning;
        CHECK(BinaryFla::convert(streams, files, error, &warning) && warning.empty());
    }
}

TEST(binary_fla_reads_flash_5_layers_frames_and_actions)
{
    // Contents 0x1B naming a scene, and its page: a layer linked to the
    // layer above it, which is serialized inline in the link and named again
    // in the page's list by reference; a frame holding an action record.
    std::vector<uint8_t> c = {0x1B, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> body;
    const auto u32 = [](std::vector<uint8_t>& b, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto text = [](std::vector<uint8_t>& b, const std::string& s) {
        b.push_back(static_cast<uint8_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    };
    const auto declare = [](std::vector<uint8_t>& b, const std::string& cls) {
        b.insert(b.end(), {0xFF, 0xFF, 1, 0, static_cast<uint8_t>(cls.size()), 0});
        b.insert(b.end(), cls.begin(), cls.end());
    };
    declare(c, "CDocumentPage");
    c.push_back(8); // schema
    text(c, "Page 1");
    text(c, "Scene 1");
    u32(c, 0);
    c.push_back(0);
    text(c, "");
    u32(c, 1);
    u32(c, 4); // item header
    u32(c, 1);
    c.insert(c.end(), 8, 0);
    u32(c, 1);
    c.insert(c.end(), 8, 0);
    u32(c, 0);
    u32(c, 0); // last modified
    c.resize(c.size() + 400, 0);

    const auto frame = [&](bool first, uint32_t actions) {
        if (first)
            declare(body, "CPicFrame");
        else
            body.insert(body.end(), {0x05, 0x80});
        body.insert(body.end(), {1, 0, 0, 0}); // CPicObj: schema 1, flags, no children
        body.insert(body.end(), 8, 0);
        body.push_back(2);              // shape schema
        body.insert(body.end(), 24, 0); // matrix
        body.push_back(4);              // shape data schema: no cubics
        u32(body, 0);
        body.insert(body.end(), {0, 0, 0, 0, 0});
        body.push_back(18); // frame schema
        body.insert(body.end(), {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        u32(body, 0);
        u32(body, 0);
        body.insert(body.end(), {0, 0});
        text(body, first ? "go" : "");
        u32(body, actions);
        for (uint32_t i = 0; i < actions; ++i)
        {
            body.push_back(9);
            u32(body, 0x10); // fscommand
            body.insert(body.end(), {1, 0, 0x80, 0xC1, 0});
            text(body, "allowscale");
            text(body, "false");
            text(body, "");
            body.insert(body.end(), 32, 0);
            u32(body, 0xFFFFFFFF);
            body.insert(body.end(), {0, 0});
            text(body, "");
            text(body, "MouseDown");
            body.insert(body.end(), 67, 0);
        }
        body.insert(body.end(), 12, 0);
        body.insert(body.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0}); // no morph, u32, no hints, CString
    };
    const auto layer = [&](const std::string& name) {
        body.insert(body.end(), {0, 0}); // no more frames
        body.insert(body.end(), 8, 0);
        body.push_back(8); // layer schema
        text(body, name);
        body.insert(body.end(), 3 + 4 + 4 + 4 + 4, 0);
        body.push_back(0); // type
        body.insert(body.end(), {0, 0});
    };
    body.insert(body.end(), {1, 0}); // the page's CPicObj
    declare(body, "CPicLayer");
    body.insert(body.end(), {1, 0});
    frame(true, 1);
    layer("Lower");
    body.insert(body.end(), {0x03, 0x80, 1, 0}); // linked: the upper layer, inline
    frame(false, 0);
    layer("Upper");
    body.insert(body.end(), {0, 0, 0}); // no link, a byte
    body.push_back(0);                  // the lower layer's last byte
    body.insert(body.end(), {7, 0});    // the upper layer again, by reference
    body.insert(body.end(), {0, 0});    // no more layers
    body.insert(body.end(), 8, 0);
    body.insert(body.end(), {2, 0, 0}); // page schema 2: no guide list
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = c;
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.empty());
    const auto xml = files["DOMDocument.xml"];
    const std::string document(xml.begin(), xml.end());
    CHECK(document.find("name=\"Lower\"") != std::string::npos);
    CHECK(document.find("name=\"Upper\"") != std::string::npos);
    CHECK(document.find("name=\"go\"") != std::string::npos);
    // The action record as the script it stands for.
    CHECK(document.find("fscommand(") != std::string::npos);
    CHECK(document.find("allowscale") != std::string::npos);
}

TEST(binary_fla_opens_on_the_saved_scene_and_reads_tween_rotation)
{
    // Contents 0x1B naming two scenes, then the next scene number, a u16
    // and the scene open at the save, by reference: the second. Its page
    // holds a motion tween turning counterclockwise three times.
    std::vector<uint8_t> c = {0x1B, 0, 0, 0, 0, 0, 0};
    const auto u32 = [](std::vector<uint8_t>& b, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto text = [](std::vector<uint8_t>& b, const std::string& s) {
        b.push_back(static_cast<uint8_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    };
    const auto declare = [](std::vector<uint8_t>& b, const std::string& cls) {
        b.insert(b.end(), {0xFF, 0xFF, 1, 0, static_cast<uint8_t>(cls.size()), 0});
        b.insert(b.end(), cls.begin(), cls.end());
    };
    const auto scene = [&](const std::string& stream, const std::string& name) {
        c.push_back(8); // schema
        text(c, stream);
        text(c, name);
        u32(c, 0);
        c.push_back(0);
        text(c, "");
        u32(c, 1);
        u32(c, 4); // item header
        u32(c, 1);
        c.insert(c.end(), 8, 0);
        u32(c, 1);
        c.insert(c.end(), 8, 0);
        u32(c, 0);
        u32(c, 0); // last modified
    };
    declare(c, "CDocumentPage"); // class 1, the first scene 2
    scene("Page 1", "Scene 1");
    c.insert(c.end(), {0x01, 0x80}); // the second scene, 3
    scene("Page 2", "Scene 2");
    c.insert(c.end(), {0, 0});              // the end of the list
    c.insert(c.end(), {3, 0, 1, 0, 3, 0}); // next scene 3, 1, the second scene
    c.resize(c.size() + 400, 0);

    std::vector<uint8_t> body = {1, 0}; // the page's CPicObj
    declare(body, "CPicLayer");
    body.insert(body.end(), {1, 0});
    declare(body, "CPicFrame");
    body.insert(body.end(), {1, 0, 0, 0}); // CPicObj: schema 1, flags, no children
    body.insert(body.end(), 8, 0);
    body.push_back(2);              // shape schema
    body.insert(body.end(), 24, 0); // matrix
    body.push_back(4);              // shape data schema: no cubics
    u32(body, 0);
    body.insert(body.end(), {0, 0, 0, 0, 0});
    body.push_back(18);                      // frame schema
    body.insert(body.end(), {10, 0, 1, 0}); // ten frames, a motion tween
    body.insert(body.end(), 9, 0);
    u32(body, 0);
    u32(body, 0);
    body.insert(body.end(), {0, 0});
    text(body, "");
    u32(body, 0); // no actions
    u32(body, 2); // counterclockwise
    u32(body, 3); // three times
    u32(body, 0);
    body.insert(body.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0}); // no morph, u32, no hints, CString
    body.insert(body.end(), {0, 0});                      // no more frames
    body.insert(body.end(), 8, 0);
    body.push_back(8); // layer schema
    text(body, "Turning");
    body.insert(body.end(), 3 + 4 + 4 + 4 + 4, 0);
    body.push_back(0);                  // type
    body.insert(body.end(), {0, 0});
    body.insert(body.end(), {0, 0, 0}); // no link, a byte
    body.insert(body.end(), {0, 0});    // no more layers
    body.insert(body.end(), 8, 0);
    body.insert(body.end(), {2, 0, 0}); // page schema 2: no guide list
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = c;
    streams["Page 2"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning));
    CHECK(warning.empty());
    const auto xml = files["DOMDocument.xml"];
    const std::string document(xml.begin(), xml.end());
    CHECK(document.find("currentTimeline=\"2\"") != std::string::npos);
    CHECK(document.find("name=\"Turning\"") != std::string::npos);
    CHECK(document.find("motionTweenRotate=\"counter-clockwise\"") != std::string::npos);
    CHECK(document.find("motionTweenRotateTimes=\"3\"") != std::string::npos);
}

TEST(binary_fla_reads_mx_normal_mode_actions)
{
    // An MX 2004 frame whose script was written in normal mode: an action
    // list (set a property, if frame is loaded holding a go to, its end)
    // rather than text. A misread list would stop the page with a warning.
    std::vector<uint8_t> body = {0, 0};
    const auto u32 = [&body](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            body.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto text = [&body](const std::string& s) {
        body.push_back(static_cast<uint8_t>(s.size()));
        body.insert(body.end(), s.begin(), s.end());
    };
    const auto declare = [&body](const std::string& cls) {
        body.insert(body.end(), {0xFF, 0xFF, 1, 0, static_cast<uint8_t>(cls.size()), 0});
        body.insert(body.end(), cls.begin(), cls.end());
    };
    declare("CPicLayer");
    body.insert(body.end(), {0, 0});
    declare("CPicFrame");
    body.insert(body.end(), {0, 0, 0, 0});
    body.insert(body.end(), 8, 0);
    body.push_back(2);              // shape schema
    body.insert(body.end(), 24, 0); // matrix
    body.push_back(5);              // shape data schema
    u32(0);
    body.insert(body.end(), {0, 0, 0, 0, 0});
    u32(0); // no cubics
    body.push_back(22); // frame schema
    body.insert(body.end(), {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    u32(0);
    u32(0);
    body.insert(body.end(), {0, 0});
    text(""); // label
    u32(1);   // actions: version 1, kind 0 (a list)
    u32(0);
    u32(0);
    u32(0);
    u32(0);
    u32(3);
    u32(0x17); // set a property
    u32(0);
    u32(7);
    text("1");
    u32(0);
    text("lips01");
    u32(0);
    u32(0x0A); // if frame is loaded
    u32(0);
    text("");
    text("30");
    u32(0);
    u32(0x13); // its end
    body.insert(body.end(), 12, 0);
    body.insert(body.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0}); // no morph, u32, no hints, CString
    body.insert(body.end(), 12, 0);
    body.insert(body.end(), {0, 0}); // no more frames
    body.insert(body.end(), 8, 0);
    body.push_back(11);
    body.push_back(0);
    body.insert(body.end(), 3 + 4 + 4 + 4 + 4, 0);
    body.insert(body.end(), {0, 0, 0, 1, 1});
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = contentsWithScene(0x43);
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string warning;
    CHECK(BinaryFla::convert(streams, files, error, &warning) && warning.empty());
    // As the script it stands for.
    const auto xml = files["DOMDocument.xml"];
    const std::string document(xml.begin(), xml.end());
    CHECK(document.find("lips01") != std::string::npos);
    CHECK(document.find(", _visible, ") != std::string::npos);
    CHECK(document.find("ifFrameLoaded (30) {\n}") != std::string::npos);
}

namespace {

/// A page whose one frame holds \a actions (an MX 2004 normal mode list of
/// \a count actions), read as XFL: the document's text.
std::string normalModeFrame(uint32_t count, const std::vector<uint8_t>& actions, std::string* warning = nullptr)
{
    std::vector<uint8_t> body = {0, 0};
    const auto u32 = [&body](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            body.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto declare = [&body](const std::string& cls) {
        body.insert(body.end(), {0xFF, 0xFF, 1, 0, static_cast<uint8_t>(cls.size()), 0});
        body.insert(body.end(), cls.begin(), cls.end());
    };
    declare("CPicLayer");
    body.insert(body.end(), {0, 0});
    declare("CPicFrame");
    body.insert(body.end(), {0, 0, 0, 0});
    body.insert(body.end(), 8, 0);
    body.push_back(2);
    body.insert(body.end(), 24, 0);
    body.push_back(5);
    u32(0);
    body.insert(body.end(), {0, 0, 0, 0, 0});
    u32(0);
    body.push_back(22);
    body.insert(body.end(), {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    u32(0);
    u32(0);
    body.insert(body.end(), {0, 0});
    body.push_back(0); // no label
    u32(1);           // version 1, kind 0: a list
    u32(0);
    u32(0);
    u32(0);
    u32(0);
    u32(count);
    body.insert(body.end(), actions.begin(), actions.end());
    body.insert(body.end(), 12, 0);
    body.insert(body.end(), {0, 0, 0, 0, 0, 0, 0, 0, 0});
    body.insert(body.end(), 12, 0);
    body.insert(body.end(), {0, 0});
    body.insert(body.end(), 8, 0);
    body.push_back(11);
    body.push_back(0);
    body.insert(body.end(), 3 + 4 + 4 + 4 + 4, 0);
    body.insert(body.end(), {0, 0, 0, 1, 1});
    std::map<std::string, std::vector<uint8_t>> streams;
    streams["Contents"] = contentsWithScene(0x43);
    streams["Page 1"] = pageStream(body);
    std::map<std::string, std::vector<uint8_t>> files;
    std::string error;
    std::string read;
    if (!BinaryFla::convert(streams, files, error, &read))
        return "error: " + error;
    if (warning)
        *warning = read;
    const auto xml = files["DOMDocument.xml"];
    return std::string(xml.begin(), xml.end());
}

/// Builds an action list: each action's code and fields in order.
struct ActionList
{
    std::vector<uint8_t> bytes;
    uint32_t count = 0;

    ActionList& action(uint32_t code)
    {
        ++count;
        return number(code);
    }
    ActionList& number(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
        return *this;
    }
    ActionList& text(const std::string& s)
    {
        bytes.push_back(static_cast<uint8_t>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
        return *this;
    }
};

/// The text of the first script in \a document, its entities undone.
std::string firstScript(const std::string& document)
{
    const size_t start = document.find("<script>");
    const size_t end = document.find("</script>", start);
    if (start == std::string::npos || end == std::string::npos)
        return std::string();
    std::string script = document.substr(start + 8, end - start - 8);
    if (script.rfind("<![CDATA[", 0) == 0)
        script = script.substr(9, script.size() - 12);
    const std::pair<const char*, const char*> entities[] = {{"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&apos;", "'"}, {"&amp;", "&"}};
    for (const auto& [entity, plain] : entities)
        for (size_t at = script.find(entity); at != std::string::npos; at = script.find(entity, at + 1))
            script.replace(at, std::strlen(entity), plain);
    return script;
}

} // namespace

TEST(binary_fla_decompiles_normal_mode_actions)
{
    // A drag and drop button's list, as MX 2004 kept it: on (press) with
    // start drag (target an expression) and set variable; on (release) with
    // stop drag, an if with an else holding set property (text and
    // expression values), get URL and go to (by label, and play), a tell
    // target, a for loop with a break, a comment, a with, and slash syntax.
    ActionList list;
    list.action(0x0B).number(0).number(1).number(0);
    list.action(0x1A).number(0).text("cap_blue").number(1).text("").text("").text("").text("").number(0).number(0).number(0);
    list.action(0x18).number(0).text("this.save_x").number(0).text("getProperty(cap_blue, _x)").number(1);
    list.action(0x0C);
    list.action(0x0B).number(0).number(6).number(0);
    list.action(0x1D);
    list.action(0x0F).number(0).text("dx>225 && /:x<480");
    list.action(0x17).number(0).number(7).text("1").number(1).text("bunny_blue").number(1);
    list.action(0x10);
    list.action(0x17).number(0).number(7).text("0").number(0).text("bunny_blue").number(0);
    list.action(0x05).number(0).text("http://example.com").text("_blank").number(0).number(0).number(0);
    list.action(0x12);
    list.action(0x00).number(0).number(1).text("").text("again").number(1);
    list.action(0x08).number(0).text("_root.status").number(0);
    list.action(0x02);
    list.action(0x09);
    list.action(0x2C).number(1).text("k=0").text("k<3").text("k++");
    list.action(0x2E);
    list.action(0x2D);
    list.action(0x1C).number(0).text("done");
    list.action(0x32).number(0).text("_root.card").number(1);
    list.action(0x29).number(1).text("gotoAndStop(2)");
    list.action(0x33);
    list.action(0x18).number(0).text("/:count").number(0).text("0").number(1);
    list.action(0x0C);
    std::string warning;
    const std::string script = firstScript(normalModeFrame(list.count, list.bytes, &warning));
    CHECK(warning.empty());
    const std::string wanted = "on (press) {\n"
                               "\tstartDrag(cap_blue, false);\n"
                               "\tthis.save_x = getProperty(cap_blue, _x);\n"
                               "}\n"
                               "on (release, releaseOutside) {\n"
                               "\tstopDrag();\n"
                               "\tif (dx>225 && _root.x<480) {\n"
                               "\t\tsetProperty(bunny_blue, _visible, 1);\n"
                               "\t}\n"
                               "\telse {\n"
                               "\t\tsetProperty(\"bunny_blue\", _visible, \"0\");\n"
                               "\t\tgetURL(\"http://example.com\", \"_blank\");\n"
                               "\t}\n"
                               "\tgotoAndPlay(\"again\");\n"
                               "\ttellTarget (\"_root.status\") {\n"
                               "\t\tstop();\n"
                               "\t}\n"
                               "\tfor (k=0; k<3; k++) {\n"
                               "\t\tbreak;\n"
                               "\t}\n"
                               "\t// done\n"
                               "\twith (_root.card) {\n"
                               "\t\tgotoAndStop(2);\n"
                               "\t}\n"
                               "\t_root.count = 0;\n"
                               "}\n";
    if (script != wanted)
        std::printf("%s\n", script.c_str());
    CHECK(script == wanted);
}

TEST(binary_fla_reads_past_a_normal_mode_action_it_does_not_know)
{
    // An action of a code Phoenix has no fields for (0x30 here, a u32 and a
    // CString): its fields are guessed by what lets the rest of the list
    // read, and a note stands in its place.
    ActionList list;
    list.action(0x02);
    list.action(0x30).number(0).text("something");
    list.action(0x01);
    std::string warning;
    const std::string script = firstScript(normalModeFrame(list.count, list.bytes, &warning));
    CHECK(warning.empty());
    CHECK(script == "stop();\n// An action Phoenix does not know (0x30) was here.\nplay();\n");
}

TEST(binary_fla_sweep)
{
    // Opt-in: PHOENIX_BINARY_FLA_LIST names a file listing binary FLAs, one
    // a line, and PHOENIX_BINARY_FLA_REPORT where to write what each read
    // gave: a line per document, "ok", the warning, or the error, and after
    // a tab how many scripts it carries. For
    // comparing the reader's results across many files before and after a
    // change.
    const char* list = std::getenv("PHOENIX_BINARY_FLA_LIST");
    const char* report = std::getenv("PHOENIX_BINARY_FLA_REPORT");
    if (!list || !report)
    {
        std::printf("    skipped: set PHOENIX_BINARY_FLA_LIST and PHOENIX_BINARY_FLA_REPORT\n");
        return;
    }
    std::ifstream in(list);
    std::ofstream out(report);
    std::string path;
    while (std::getline(in, path))
    {
        if (path.empty())
            continue;
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CompoundFile compound;
        std::string result;
        // And how many scripts it carries: frames', buttons' and clips'.
        int scripts = 0;
        if (!compound.read(data))
            result = "not a compound file";
        else
        {
            std::map<std::string, std::vector<uint8_t>> files;
            std::string error;
            std::string warning;
            if (!BinaryFla::convert(compound.streams(), files, error, &warning))
                result = "error: " + error;
            else
                result = warning.empty() ? "ok" : "warning: " + warning;
            for (const auto& [name, bytes] : files)
            {
                const std::string text(bytes.begin(), bytes.end());
                for (size_t at = text.find("<script>"); at != std::string::npos; at = text.find("<script>", at + 1))
                    ++scripts;
            }
        }
        for (char& c : result)
            if (c == '\n' || c == '\r')
                c = ' ';
        out << path << '\t' << result << '\t' << scripts << '\n';
    }
}
