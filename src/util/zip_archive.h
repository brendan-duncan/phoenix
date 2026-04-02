#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ZipFileEntry
{
    std::string filename;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t compression_method;
    uint32_t crc32;
    uint64_t local_header_offset;
};

class ZipArchive
{
public:
    ZipArchive();

    ~ZipArchive();

    bool open(const std::string& filepath);

    void close();

    const std::vector<ZipFileEntry>& getEntries() const;

    bool extractFile(const ZipFileEntry& entry, std::vector<uint8_t>& output);

    bool extractFile(const std::string& filename, std::vector<uint8_t>& output);

    bool extractAll(const std::string& output_directory);

private:
    class Impl;
    Impl* pImpl;
};
