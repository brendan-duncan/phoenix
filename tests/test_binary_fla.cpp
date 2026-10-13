#include "test_util.h"

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/data/frame.h"
#include "../src/data/layer.h"
#include "../src/data/symbol.h"
#include "../src/data/symbol_instance.h"
#include "../src/data/timeline.h"
#include "../src/parser/binary_fla.h"
#include "../src/parser/compound_file.h"
#include "../src/parser/fla_parser.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

/// A Contents stream with nothing in it, of the oldest version read.
std::vector<uint8_t> emptyContents(uint16_t version = 0x129)
{
    std::vector<uint8_t> contents(400, 0);
    contents[0] = static_cast<uint8_t>(version);
    contents[1] = static_cast<uint8_t>(version >> 8);
    return contents;
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
