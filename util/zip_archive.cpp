#include "zip_archive.h"
#include <fstream>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <zlib.h>

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

class ZipArchive::Impl
{
public:
    std::string filepath;
    std::ifstream file;
    std::vector<ZipFileEntry> entries;

    bool findEndOfCentralDirectory(uint64_t& offset);

    bool parseCentralDirectory(uint64_t offset, uint32_t size);

    bool decompressData(const std::vector<uint8_t>& compressed,
                       std::vector<uint8_t>& decompressed,
                       uint32_t uncompressed_size);
};

bool ZipArchive::Impl::findEndOfCentralDirectory(uint64_t& offset)
{
    file.seekg(0, std::ios::end);
    uint64_t file_size = file.tellg();

    const int max_comment_size = 65535;
    const int search_size = sizeof(EndOfCentralDirectory) + max_comment_size;
    int search_start = (file_size > search_size) ? file_size - search_size : 0;

    file.seekg(search_start, std::ios::beg);
    std::vector<uint8_t> buffer(file_size - search_start);
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

    for (int i = buffer.size() - sizeof(EndOfCentralDirectory); i >= 0; i--)
    {
        uint32_t sig = *reinterpret_cast<uint32_t*>(&buffer[i]);
        if (sig == END_OF_CENTRAL_DIR_SIG)
        {
            offset = search_start + i;
            return true;
        }
    }

    return false;
}

bool ZipArchive::Impl::parseCentralDirectory(uint64_t offset, uint32_t size)
{
    file.seekg(offset, std::ios::beg);

    while (true)
    {
        CentralDirectoryHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (header.signature != CENTRAL_DIR_HEADER_SIG)
        {
            break;
        }

        std::string filename(header.filename_length, '\0');
        file.read(&filename[0], header.filename_length);

        file.seekg(header.extra_field_length + header.file_comment_length, std::ios::cur);

        ZipFileEntry entry;
        entry.filename = filename;
        entry.compressed_size = header.compressed_size;
        entry.uncompressed_size = header.uncompressed_size;
        entry.compression_method = header.compression_method;
        entry.crc32 = header.crc32;
        entry.local_header_offset = header.local_header_offset;

        entries.push_back(entry);
    }

    return true;
}

bool ZipArchive::Impl::decompressData(const std::vector<uint8_t>& compressed,
                                     std::vector<uint8_t>& decompressed,
                                     uint32_t uncompressed_size)
{
    decompressed.resize(uncompressed_size);

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    stream.next_in = const_cast<uint8_t*>(compressed.data());
    stream.avail_in = compressed.size();
    stream.next_out = decompressed.data();
    stream.avail_out = uncompressed_size;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return false;
    }

    int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    return ret == Z_STREAM_END;
}

ZipArchive::ZipArchive()
    : pImpl(new Impl())
{}

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

    pImpl->file.seekg(eocd_offset, std::ios::beg);
    EndOfCentralDirectory eocd;
    pImpl->file.read(reinterpret_cast<char*>(&eocd), sizeof(eocd));

    if (eocd.signature != END_OF_CENTRAL_DIR_SIG)
    {
        return false;
    }

	pImpl->filepath = filepath;

    return pImpl->parseCentralDirectory(eocd.central_dir_offset, eocd.central_dir_size);
}

void ZipArchive::close()
{
    if (pImpl->file.is_open())
    {
        pImpl->file.close();
    }
    pImpl->entries.clear();
}

const std::vector<ZipFileEntry>& ZipArchive::getEntries() const
{
    return pImpl->entries;
}

bool ZipArchive::extractFile(const ZipFileEntry& entry, std::vector<uint8_t>& output)
{
    std::ifstream file;
    file.open(pImpl->filepath, std::ios::binary);
    file.seekg(entry.local_header_offset, std::ios::beg);

    LocalFileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.signature != LOCAL_FILE_HEADER_SIG)
    {
        return false;
    }

    file.seekg(header.filename_length + header.extra_field_length, std::ios::cur);

    std::vector<uint8_t> compressed_data(entry.compressed_size);
    file.read(reinterpret_cast<char*>(compressed_data.data()), entry.compressed_size);

    if (entry.compression_method == 0)
    {
        output = compressed_data;
        return true;
    }
    else if (entry.compression_method == 8)
    {
        return pImpl->decompressData(compressed_data, output, entry.uncompressed_size);
    }

    return false;
}

bool ZipArchive::extractFile(const std::string& filename, std::vector<uint8_t>& output)
{
    for (const auto& entry : pImpl->entries)
    {
        if (entry.filename == filename)
        {
            return extractFile(entry, output);
        }
    }
    return false;
}

bool ZipArchive::extractAll(const std::string& output_directory)
{
    fs::create_directories(output_directory);

    for (const auto& entry : pImpl->entries)
    {
        if (entry.filename.back() == '/' || entry.filename.back() == '\\')
        {
            fs::create_directories(fs::path(output_directory) / entry.filename);
            continue;
        }

        std::vector<uint8_t> data;
        if (!extractFile(entry, data))
        {
            std::cerr << "Failed to extract: " << entry.filename << std::endl;
            continue;
        }

        fs::path output_path = fs::path(output_directory) / entry.filename;
        fs::create_directories(output_path.parent_path());

        std::ofstream out(output_path, std::ios::binary);
        if (!out.is_open())
        {
            std::cerr << "Failed to write: " << output_path << std::endl;
            continue;
        }

        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    return true;
}
