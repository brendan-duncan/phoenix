#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/// Writes a plain ZIP archive, which is all a .fla file is.
///
/// Entries are deflated, falling back to stored when compression does not pay.
/// The output uses no data descriptors and no ZIP64 fields, which keeps it to
/// the oldest and most widely readable shape of the format.
///
/// Free of Qt.
class ZipWriter
{
public:
    ZipWriter() = default;

    ~ZipWriter();

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    /// Creates \a path, replacing anything already there.
    bool open(const std::string& path);

    bool isOpen() const { return _stream.is_open(); }

    /// Adds an entry. \a name is the path inside the archive and always uses
    /// forward slashes.
    bool addFile(const std::string& name, const uint8_t* data, size_t size);

    bool addFile(const std::string& name, const std::string& text);

    bool addFile(const std::string& name, const std::vector<uint8_t>& data);

    /// Writes the central directory and closes the file. Must be called for the
    /// archive to be readable; the destructor only cleans up.
    bool close();

    const std::string& errorString() const { return _errorString; }

private:
    struct Entry
    {
        std::string name;
        uint32_t crc32 = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint16_t method = 0;
        uint32_t localHeaderOffset = 0;
    };

    bool fail(const std::string& message);

    void writeUint16(uint16_t value);

    void writeUint32(uint32_t value);

    void writeBytes(const void* data, size_t size);

    std::ofstream _stream;
    std::vector<Entry> _entries;
    std::string _errorString;
};
