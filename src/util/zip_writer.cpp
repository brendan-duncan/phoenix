#include "zip_writer.h"

#include "zlib.h"

#include <cstring>
#include <limits>

namespace {

constexpr uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054b50;

/// Version 2.0, the minimum that understands deflate.
constexpr uint16_t kVersion = 20;

constexpr uint16_t kMethodStore = 0;
constexpr uint16_t kMethodDeflate = 8;

/// Deflates into a raw stream with no zlib wrapper, which is what ZIP stores.
/// Returns false when the result would not be smaller than the input, leaving
/// the caller to store the entry instead.
bool deflateBuffer(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    if (size == 0)
        return false;

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));

    // A negative window size selects raw deflate.
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }

    out.resize(size);

    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());

    const int result = deflate(&stream, Z_FINISH);
    const size_t produced = out.size() - stream.avail_out;
    deflateEnd(&stream);

    // Z_STREAM_END means it all fitted. Anything else means the compressed form
    // needed at least as much room as the original, so storing is better.
    if (result != Z_STREAM_END)
        return false;

    out.resize(produced);
    return produced < size;
}

} // namespace

ZipWriter::~ZipWriter()
{
    if (_stream.is_open())
        _stream.close();
}

bool ZipWriter::fail(const std::string& message)
{
    _errorString = message;
    return false;
}

void ZipWriter::writeUint16(uint16_t value)
{
    // ZIP is little-endian regardless of the host.
    const uint8_t bytes[2] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF)
    };
    _stream.write(reinterpret_cast<const char*>(bytes), 2);
}

void ZipWriter::writeUint32(uint32_t value)
{
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    _stream.write(reinterpret_cast<const char*>(bytes), 4);
}

void ZipWriter::writeBytes(const void* data, size_t size)
{
    if (size > 0)
        _stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

bool ZipWriter::open(const std::string& path)
{
    _entries.clear();
    _errorString.clear();

    _stream.open(path, std::ios::binary | std::ios::trunc);
    if (!_stream)
        return fail("Failed to open for writing: " + path);

    return true;
}

bool ZipWriter::addFile(const std::string& name, const uint8_t* data, size_t size)
{
    if (!_stream.is_open())
        return fail("Archive is not open");

    if (name.empty())
        return fail("Archive entry needs a name");

    if (size > std::numeric_limits<uint32_t>::max())
        return fail("Entry too large for a non-ZIP64 archive: " + name);

    Entry entry;
    entry.name = name;
    // Paths inside a ZIP are always forward-slashed, whatever the host uses.
    for (char& c : entry.name)
    {
        if (c == '\\')
            c = '/';
    }

    entry.uncompressedSize = static_cast<uint32_t>(size);
    entry.crc32 = static_cast<uint32_t>(
        crc32(0, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(size)));
    entry.localHeaderOffset = static_cast<uint32_t>(_stream.tellp());

    std::vector<uint8_t> compressed;
    const bool useDeflate = deflateBuffer(data, size, compressed);

    entry.method = useDeflate ? kMethodDeflate : kMethodStore;
    entry.compressedSize = useDeflate
        ? static_cast<uint32_t>(compressed.size())
        : static_cast<uint32_t>(size);

    writeUint32(kLocalHeaderSignature);
    writeUint16(kVersion);
    writeUint16(0);             // flags
    writeUint16(entry.method);
    writeUint16(0);             // modification time
    writeUint16(0);             // modification date
    writeUint32(entry.crc32);
    writeUint32(entry.compressedSize);
    writeUint32(entry.uncompressedSize);
    writeUint16(static_cast<uint16_t>(entry.name.size()));
    writeUint16(0);             // extra field length
    writeBytes(entry.name.data(), entry.name.size());

    if (useDeflate)
        writeBytes(compressed.data(), compressed.size());
    else
        writeBytes(data, size);

    if (!_stream)
        return fail("Failed to write entry: " + name);

    _entries.push_back(entry);
    return true;
}

bool ZipWriter::addFile(const std::string& name, const std::string& text)
{
    return addFile(name, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool ZipWriter::addFile(const std::string& name, const std::vector<uint8_t>& data)
{
    return addFile(name, data.data(), data.size());
}

bool ZipWriter::close()
{
    if (!_stream.is_open())
        return true;

    const uint32_t centralDirectoryOffset = static_cast<uint32_t>(_stream.tellp());

    for (const Entry& entry : _entries)
    {
        writeUint32(kCentralHeaderSignature);
        writeUint16(kVersion);  // version made by
        writeUint16(kVersion);  // version needed
        writeUint16(0);         // flags
        writeUint16(entry.method);
        writeUint16(0);         // modification time
        writeUint16(0);         // modification date
        writeUint32(entry.crc32);
        writeUint32(entry.compressedSize);
        writeUint32(entry.uncompressedSize);
        writeUint16(static_cast<uint16_t>(entry.name.size()));
        writeUint16(0);         // extra field length
        writeUint16(0);         // comment length
        writeUint16(0);         // disk number where the file starts
        writeUint16(0);         // internal attributes
        writeUint32(0);         // external attributes
        writeUint32(entry.localHeaderOffset);
        writeBytes(entry.name.data(), entry.name.size());
    }

    const uint32_t centralDirectorySize =
        static_cast<uint32_t>(_stream.tellp()) - centralDirectoryOffset;
    const uint16_t entryCount = static_cast<uint16_t>(_entries.size());

    writeUint32(kEndOfCentralDirectorySignature);
    writeUint16(0);             // this disk number
    writeUint16(0);             // disk holding the central directory
    writeUint16(entryCount);    // entries on this disk
    writeUint16(entryCount);    // entries in total
    writeUint32(centralDirectorySize);
    writeUint32(centralDirectoryOffset);
    writeUint16(0);             // comment length

    const bool ok = static_cast<bool>(_stream);
    _stream.close();

    if (!ok)
        return fail("Failed to write the central directory");

    return true;
}
