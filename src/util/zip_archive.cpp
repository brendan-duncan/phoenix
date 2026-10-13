#include "zip_archive.h"
#include "../third_party/zlib/zlib.h"
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <iostream>

//#include <QImage>

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct LocalFileHeader
{
    uint32_t signature;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
};

struct CentralDirectoryHeader
{
    uint32_t signature;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t file_comment_length;
    uint16_t disk_number_start;
    uint16_t internal_file_attributes;
    uint32_t external_file_attributes;
    uint32_t local_header_offset;
};

struct EndOfCentralDirectory
{
    uint32_t signature;
    uint16_t disk_number;
    uint16_t central_dir_disk;
    uint16_t num_entries_this_disk;
    uint16_t num_entries;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_length;
};
#pragma pack(pop)

constexpr uint32_t LOCAL_FILE_HEADER_SIG = 0x04034b50;
constexpr uint32_t CENTRAL_DIR_HEADER_SIG = 0x02014b50;
constexpr uint32_t END_OF_CENTRAL_DIR_SIG = 0x06054b50;
constexpr uint32_t ZIP64_LOCATOR_SIG = 0x07064b50;
constexpr uint32_t ZIP64_END_SIG = 0x06064b50;

class ZipArchive::Impl
{
public:
    std::string filepath;
    std::ifstream file;
    uint64_t file_size = 0;
    std::vector<ZipFileEntry> entries;
    /// Entries by name: a production FLA has thousands, and each item read
    /// would otherwise scan them all.
    std::unordered_map<std::string, size_t> byName;

    bool findEndOfCentralDirectory(uint64_t& offset);

    bool parseCentralDirectory(uint64_t offset, uint64_t count);

    bool decompressData(const std::vector<uint8_t>& compressed,
                       std::vector<uint8_t>& decompressed);
};

bool ZipArchive::Impl::findEndOfCentralDirectory(uint64_t& offset)
{
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end < static_cast<std::streamoff>(sizeof(EndOfCentralDirectory)))
        return false;
    file_size = static_cast<uint64_t>(end);

    const uint64_t max_comment_size = 65535;
    const uint64_t search_size = sizeof(EndOfCentralDirectory) + max_comment_size;
    const uint64_t search_start = (file_size > search_size) ? file_size - search_size : 0;

    file.seekg(static_cast<std::streamoff>(search_start), std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(file_size - search_start));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())))
        return false;

    for (size_t i = buffer.size() - sizeof(EndOfCentralDirectory) + 1; i-- > 0;)
    {
        uint32_t sig = 0;
        memcpy(&sig, &buffer[i], sizeof(sig));
        if (sig == END_OF_CENTRAL_DIR_SIG)
        {
            offset = search_start + i;
            return true;
        }
    }

    return false;
}

namespace {

uint64_t load64(const uint8_t* p)
{
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | p[i];
    return value;
}

/// The Zip64 extended information in an entry's extra field: the sizes and
/// offset its central directory header marks with 0xFFFFFFFF.
void readZip64Extra(const std::vector<uint8_t>& extra, ZipFileEntry& entry, const CentralDirectoryHeader& header)
{
    size_t at = 0;
    while (at + 4 <= extra.size())
    {
        const uint16_t id = static_cast<uint16_t>(extra[at] | (extra[at + 1] << 8));
        const uint16_t size = static_cast<uint16_t>(extra[at + 2] | (extra[at + 3] << 8));
        at += 4;
        if (size > extra.size() - at)
            return;
        if (id == 0x0001)
        {
            // Only the fields that overflowed are present, in this order.
            size_t field = at;
            const size_t end = at + size;
            if (header.uncompressed_size == 0xFFFFFFFF && field + 8 <= end)
            {
                entry.uncompressed_size = load64(&extra[field]);
                field += 8;
            }
            if (header.compressed_size == 0xFFFFFFFF && field + 8 <= end)
            {
                entry.compressed_size = load64(&extra[field]);
                field += 8;
            }
            if (header.local_header_offset == 0xFFFFFFFF && field + 8 <= end)
                entry.local_header_offset = load64(&extra[field]);
            return;
        }
        at += size;
    }
}

} // namespace

bool ZipArchive::Impl::parseCentralDirectory(uint64_t offset, uint64_t count)
{
    // The entry count, not the directory size, says where to stop: archives
    // that overstate the size are common. Every header is checked as read,
    // and none can be past the end of the file.
    if (offset >= file_size)
        return false;
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

    const uint64_t most = (file_size - offset) / sizeof(CentralDirectoryHeader);
    while (entries.size() < count && entries.size() < most)
    {
        CentralDirectoryHeader header{};
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
            header.signature != CENTRAL_DIR_HEADER_SIG)
        {
            break;
        }

        std::string filename(header.filename_length, '\0');
        std::vector<uint8_t> extra(header.extra_field_length);
        if (!file.read(&filename[0], header.filename_length) ||
            !file.read(reinterpret_cast<char*>(extra.data()), header.extra_field_length))
        {
            break;
        }

        file.seekg(header.file_comment_length, std::ios::cur);

        ZipFileEntry entry;
        entry.filename = filename;
        entry.compressed_size = header.compressed_size;
        entry.uncompressed_size = header.uncompressed_size;
        entry.compression_method = header.compression_method;
        entry.crc32 = header.crc32;
        entry.local_header_offset = header.local_header_offset;
        readZip64Extra(extra, entry, header);

        entries.push_back(entry);
    }

    return true;
}

bool ZipArchive::Impl::decompressData(const std::vector<uint8_t>& compressed,
                                     std::vector<uint8_t>& decompressed)
{
    // Grown as it inflates, never past what deflate can expand to (about
    // 1032 to 1), rather than sized up front from a header that may lie.
    const uint64_t limit = static_cast<uint64_t>(compressed.size()) * 1032 + 1024;
    decompressed.clear();

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return false;
    }

    const uint8_t* in = compressed.data();
    size_t in_left = compressed.size();
    size_t produced = 0;
    int ret = Z_OK;
    while (ret == Z_OK)
    {
        if (stream.avail_in == 0 && in_left > 0)
        {
            stream.next_in = const_cast<uint8_t*>(in);
            stream.avail_in = static_cast<uInt>(std::min<size_t>(in_left, 1u << 30));
            in += stream.avail_in;
            in_left -= stream.avail_in;
        }
        if (decompressed.size() - produced < 4096)
        {
            if (decompressed.size() >= limit)
                break;
            const size_t grown = static_cast<size_t>(
                std::min<uint64_t>(limit, std::max<uint64_t>(decompressed.size() * 2, 65536)));
            decompressed.resize(grown);
        }
        stream.next_out = decompressed.data() + produced;
        stream.avail_out = static_cast<uInt>(std::min<size_t>(decompressed.size() - produced, 1u << 30));
        const uInt before = stream.avail_out;
        ret = inflate(&stream, Z_NO_FLUSH);
        produced += before - stream.avail_out;
    }
    inflateEnd(&stream);

    decompressed.resize(produced);
    return ret == Z_STREAM_END;
}

struct ImageDataHeader
{
    uint32_t identifier;
    uint16_t width;
    uint16_t height;
    uint16_t unknown;
    uint32_t compression_size;
    uint32_t unknown2;
    uint32_t totalChunks;
    uint32_t formatFlags;
};

ZipArchive::ZipArchive()
    : pImpl(new Impl())
{
    /*std::string filePath = "D:\\fla\\Charlotte_06_2021\\bin\\M 24 1313864024.dat";
    std::ifstream testFile(filePath, std::ios::binary);
    if (testFile.is_open())
    {
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(testFile)), std::istreambuf_iterator<char>());
        std::cout << "Test file read successfully, size: " << data.size() << " bytes" << std::endl;
        std::cout << "Header Size: " << sizeof(ImageDataHeader) << " bytes" << std::endl;

        uint8_t* ptr = data.data();
        ImageDataHeader* header = reinterpret_cast<ImageDataHeader*>(ptr);
        std::cout << "Identifier: " << std::hex << header->identifier << std::dec << std::endl;
        std::cout << "Width: " << header->width << std::endl;
        std::cout << "Height: " << header->height << std::endl;
        std::cout << "Unknown: " << header->unknown << std::endl;
        std::cout << "Compression Size: " << header->compression_size << std::endl;
        std::cout << "Unknown2: " << header->unknown2 << std::endl;
        std::cout << "Total Chunks: " << header->totalChunks << std::endl;
        std::cout << "Format Flags: " << std::hex << header->formatFlags << std::dec << std::endl;
        for (size_t i = 28; i < 28 + 8; ++i)
        {
            std::cout << "Byte " << i << ": " << (int)ptr[i] << " : " << std::hex << (int)ptr[i] << std::dec << std::endl;
        }
        uint8_t* compressedData = ptr + sizeof(ImageDataHeader);
        std::vector<uint8_t> compressed(compressedData, compressedData + header->compression_size);
        size_t uncompressed_size = header->width * header->height * 4;
        std::vector<uint8_t> decompressed;

        decompressed.resize(uncompressed_size);

        z_stream stream;
        memset(&stream, 0, sizeof(stream));

        stream.next_in = const_cast<uint8_t*>(compressed.data());
        stream.avail_in = compressed.size();
        stream.next_out = decompressed.data();
        stream.avail_out = uncompressed_size;

        if (inflateInit2(&stream, MAX_WBITS) != Z_OK)
        {
            std::cout << "Failed to init ZLib" << std::endl;
        }
        else
        {
            int ret = inflate(&stream, Z_FULL_FLUSH);
            inflateEnd(&stream);
            std::cout << "INFLATE: " << ret << std::endl;
            if (ret == Z_OK)
            {
                QImage image(header->width, header->height, QImage::Format_ARGB32);
				for (int y = 0, idx = 0; y < header->height; ++y)
				{
					for (int x = 0; x < header->width; ++x, idx += 4)
					{
						uint8_t a = decompressed[idx];
						uint8_t r = decompressed[idx + 1];
						uint8_t g = decompressed[idx + 2];
						uint8_t b = decompressed[idx + 3];
						image.setPixel(x, y, qRgba(r, g, b, a));
					}
				}
                bool res = image.save("d:/test.png");
                if (!res)
                    std::cout << "Failed to save image" << std::endl;
            }
        }
    }*/
}

ZipArchive::~ZipArchive()
{
    close();
    delete pImpl;
}

bool ZipArchive::open(const std::string& filepath)
{
    pImpl->file.open(filepath, std::ios::binary);
    if (!pImpl->file.is_open())
    {
        return false;
    }

    uint64_t eocd_offset;
    if (!pImpl->findEndOfCentralDirectory(eocd_offset))
    {
        return false;
    }

    pImpl->file.clear();
    pImpl->file.seekg(static_cast<std::streamoff>(eocd_offset), std::ios::beg);
    EndOfCentralDirectory eocd{};
    if (!pImpl->file.read(reinterpret_cast<char*>(&eocd), sizeof(eocd)) || eocd.signature != END_OF_CENTRAL_DIR_SIG)
    {
        return false;
    }

    uint64_t directory_offset = eocd.central_dir_offset;
    uint64_t count = eocd.num_entries;
    if ((eocd.central_dir_offset == 0xFFFFFFFF || eocd.num_entries == 0xFFFF) && eocd_offset >= 20)
    {
        // Zip64: a locator just before the record points at the Zip64 end of
        // central directory, which holds the full count and offset.
        uint8_t locator[20] = {};
        pImpl->file.seekg(static_cast<std::streamoff>(eocd_offset - 20), std::ios::beg);
        uint32_t sig = 0;
        if (pImpl->file.read(reinterpret_cast<char*>(locator), sizeof(locator)) &&
            (memcpy(&sig, locator, 4), sig == ZIP64_LOCATOR_SIG))
        {
            const uint64_t record_offset = load64(locator + 8);
            uint8_t record[56] = {};
            pImpl->file.seekg(static_cast<std::streamoff>(record_offset), std::ios::beg);
            if (record_offset < pImpl->file_size && pImpl->file.read(reinterpret_cast<char*>(record), sizeof(record)) &&
                (memcpy(&sig, record, 4), sig == ZIP64_END_SIG))
            {
                count = load64(record + 32);
                directory_offset = load64(record + 48);
            }
        }
        pImpl->file.clear();
    }

	pImpl->filepath = filepath;

    const bool parsed = pImpl->parseCentralDirectory(directory_offset, count);
    pImpl->byName.clear();
    for (size_t i = 0; i < pImpl->entries.size(); ++i)
        pImpl->byName.emplace(pImpl->entries[i].filename, i);
    // Each extraction opens the file for itself: the handle is not held,
    // so the document can be saved over while its media are still read
    // from here as they are needed.
    pImpl->file.close();
    return parsed;
}

const ZipFileEntry* ZipArchive::find(const std::string& filename) const
{
    const auto found = pImpl->byName.find(filename);
    return found == pImpl->byName.end() ? nullptr : &pImpl->entries[found->second];
}

void ZipArchive::close()
{
    if (pImpl->file.is_open())
    {
        pImpl->file.close();
    }
    pImpl->entries.clear();
    pImpl->byName.clear();
}

const std::vector<ZipFileEntry>& ZipArchive::getEntries() const
{
    return pImpl->entries;
}

bool ZipArchive::extractFile(const ZipFileEntry& entry, std::vector<uint8_t>& output)
{
    std::ifstream file;
    file.open(pImpl->filepath, std::ios::binary);
    if (entry.local_header_offset >= pImpl->file_size)
    {
        return false;
    }
    file.seekg(static_cast<std::streamoff>(entry.local_header_offset), std::ios::beg);

    LocalFileHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.signature != LOCAL_FILE_HEADER_SIG)
    {
        return false;
    }

    file.seekg(header.filename_length + header.extra_field_length, std::ios::cur);

    // The data must lie within the file before memory is set aside for it.
    const std::streamoff start = file.tellg();
    if (start < 0 || entry.compressed_size > pImpl->file_size - static_cast<uint64_t>(start))
    {
        return false;
    }
    std::vector<uint8_t> compressed_data(static_cast<size_t>(entry.compressed_size));
    if (!file.read(reinterpret_cast<char*>(compressed_data.data()), static_cast<std::streamsize>(compressed_data.size())))
    {
        return false;
    }

    if (entry.compression_method == 0)
    {
        output = std::move(compressed_data);
        return true;
    }
    else if (entry.compression_method == 8)
    {
        return pImpl->decompressData(compressed_data, output);
    }

    return false;
}

bool ZipArchive::extractFile(const std::string& filename, std::vector<uint8_t>& output)
{
    const ZipFileEntry* entry = find(filename);
    return entry && extractFile(*entry, output);
}
