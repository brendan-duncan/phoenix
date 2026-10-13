#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ZipFileEntry
{
    std::string filename;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
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

    /// The entry of that name, or null; found by index, not by a scan.
    const ZipFileEntry* find(const std::string& filename) const;

private:
    class Impl;
    Impl* pImpl;
};
