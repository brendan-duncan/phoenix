#include "test_util.h"

#include "../src/parser/zip_reader.h"
#include "../src/util/zip_archive.h"
#include "../src/util/zip_writer.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

namespace {

std::string zipScratch(const char* name)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "phoenix_zip_tests";
    std::filesystem::create_directories(folder);
    return (folder / name).string();
}

std::vector<uint8_t> readAll(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void put16(std::vector<uint8_t>& data, size_t at, uint16_t v)
{
    data[at] = static_cast<uint8_t>(v);
    data[at + 1] = static_cast<uint8_t>(v >> 8);
}

void put32(std::vector<uint8_t>& data, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        data[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

size_t find(const std::vector<uint8_t>& data, const char* signature)
{
    for (size_t i = data.size() - 4 + 1; i-- > 0;)
    {
        if (std::memcmp(&data[i], signature, 4) == 0)
            return i;
    }
    return std::string::npos;
}

/// A zip of one deflated text file, as the writer makes it.
std::vector<uint8_t> goodZip(const std::string& text)
{
    const std::string path = zipScratch("good.zip");
    ZipWriter writer;
    writer.open(path);
    writer.addFile("DOMDocument.xml", text);
    writer.close();
    return readAll(path);
}

/// Opens \a data as a zip and extracts its first entry into \a out.
bool openAndExtract(const std::vector<uint8_t>& data, size_t& entries, std::vector<uint8_t>& out)
{
    const std::string path = zipScratch("damaged.zip");
    writeAll(path, data);
    ZipArchive archive;
    entries = 0;
    out.clear();
    if (!archive.open(path))
        return false;
    entries = archive.getEntries().size();
    return entries > 0 && archive.extractFile(archive.getEntries()[0], out);
}

} // namespace

TEST(zip_archive_survives_damaged_directories)
{
    const std::string text(5000, 'x');
    const std::vector<uint8_t> good = goodZip(text);
    const size_t central = find(good, "PK\x01\x02");
    const size_t end = find(good, "PK\x05\x06");
    CHECK(central != std::string::npos && end != std::string::npos);
    if (central == std::string::npos || end == std::string::npos)
        return;

    size_t entries = 0;
    std::vector<uint8_t> out;
    CHECK(openAndExtract(good, entries, out) && entries == 1 && out.size() == text.size());

    // An uncompressed size of 0xFFFFFFFF (as Zip64 writes it): not set aside
    // up front; the stream itself says how long it is.
    {
        std::vector<uint8_t> bad = good;
        put32(bad, central + 24, 0xFFFFFFFF);
        CHECK(openAndExtract(bad, entries, out) && out.size() == text.size());
    }
    // A compressed size past the end of the file: refused.
    {
        std::vector<uint8_t> bad = good;
        put32(bad, central + 20, 0xFFFFFFF0);
        CHECK(!openAndExtract(bad, entries, out) && entries == 1 && out.empty());
    }
    // The directory said to be past the end, or in the last few bytes.
    {
        std::vector<uint8_t> bad = good;
        put32(bad, end + 16, 0xFFFFFF00);
        CHECK(!openAndExtract(bad, entries, out) && entries == 0);
        put32(bad, end + 16, static_cast<uint32_t>(bad.size() - 4));
        CHECK(!openAndExtract(bad, entries, out) && entries == 0);
    }
    // Far more entries claimed than there are: the one there is.
    {
        std::vector<uint8_t> bad = good;
        put16(bad, end + 8, 0xFFFE);
        put16(bad, end + 10, 0xFFFE);
        CHECK(openAndExtract(bad, entries, out) && entries == 1);
    }
}

TEST(zip_reader_keeps_a_folder_document_inside_its_folder)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "phoenix_zip_tests" / "folder";
    std::filesystem::create_directories(root / "doc");
    writeAll((root / "doc" / "DOMDocument.xml").string(), {'<', 'a', '/', '>'});
    writeAll((root / "secret.txt").string(), {'s'});
    ZipReader reader;
    CHECK(reader.open((root / "doc").string()));
    CHECK(reader.readFile("DOMDocument.xml").size() == 4);
    CHECK(!reader.containsFile("../secret.txt") && reader.readFile("../secret.txt").empty());
    CHECK(reader.readFile((root / "secret.txt").string()).empty());
}

TEST(zip_reader_opens_a_folder_document_by_its_xfl_file)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "phoenix_zip_tests" / "marker";
    std::filesystem::create_directories(root / "doc");
    writeAll((root / "doc" / "DOMDocument.xml").string(), {'<', 'a', '/', '>'});
    writeAll((root / "doc" / "doc.xfl").string(), {'P', 'R', 'O', 'X', 'Y'});
    ZipReader reader;
    CHECK(reader.open((root / "doc" / "doc.xfl").string()));
    CHECK(reader.readFile("DOMDocument.xml").size() == 4);
}
