#include "container.h"

#include "wire.h"

#include <zlib.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace phx {

namespace {

const uint8_t kMagic[8] = {0x89, 'P', 'H', 'X', '\r', '\n', 0x1A, '\n'};
const uint8_t kTrailerMark[4] = {'P', 'H', 'X', '!'};
constexpr size_t kHeaderSize = 32;
constexpr size_t kChunkHeaderSize = 32;
constexpr size_t kTrailerSize = 16;
// zlib's one-call compress counts in 32 bits; bigger chunks are stored.
constexpr size_t kMaxDeflate = 0x7FFFFFFF;

void put16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put64(uint8_t* p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t get16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

std::string encodeIndex(const std::vector<ChunkInfo>& chunks)
{
    Writer index;
    for (const ChunkInfo& chunk : chunks)
    {
        Writer entry;
        entry.uint(1, chunk.id);
        entry.fixed32(2, chunk.type);
        entry.uint(3, chunk.offset);
        entry.uint(4, chunk.storedLength);
        entry.uint(5, chunk.length);
        if (chunk.flags)
            entry.uint(6, chunk.flags);
        entry.fixed32(7, chunk.crc);
        if (chunk.mediaId)
            entry.uint(8, chunk.mediaId);
        if (!chunk.symbolName.empty())
            entry.string(9, chunk.symbolName);
        index.message(1, entry);
    }
    return index.buffer();
}

bool decodeIndex(const uint8_t* data, size_t size, std::vector<ChunkInfo>& chunks)
{
    Reader index(data, size);
    while (index.next())
    {
        if (index.field() != 1 || index.type() != WireType::Bytes)
            continue;
        ChunkInfo chunk;
        Reader entry = index.message();
        while (entry.next())
        {
            switch (entry.field())
            {
            case 1: chunk.id = static_cast<uint32_t>(entry.uint()); break;
            case 2: chunk.type = entry.fixed32(); break;
            case 3: chunk.offset = entry.uint(); break;
            case 4: chunk.storedLength = entry.uint(); break;
            case 5: chunk.length = entry.uint(); break;
            case 6: chunk.flags = static_cast<uint32_t>(entry.uint()); break;
            case 7: chunk.crc = entry.fixed32(); break;
            case 8: chunk.mediaId = static_cast<uint32_t>(entry.uint()); break;
            case 9: chunk.symbolName = entry.string(); break;
            default: break;
            }
        }
        if (entry.failed())
            return false;
        chunks.push_back(chunk);
    }
    return !index.failed();
}

} // namespace

std::string typeName(uint32_t type)
{
    std::string name(4, ' ');
    for (int i = 0; i < 4; ++i)
    {
        const char c = static_cast<char>(type >> (8 * i));
        name[static_cast<size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
    }
    return name;
}

uint32_t crc32(const void* data, size_t size)
{
    uLong crc = ::crc32(0L, Z_NULL, 0);
    const Bytef* p = static_cast<const Bytef*>(data);
    while (size > 0)
    {
        const uInt piece = static_cast<uInt>(std::min<size_t>(size, 1u << 30));
        crc = ::crc32(crc, p, piece);
        p += piece;
        size -= piece;
    }
    return static_cast<uint32_t>(crc);
}

bool plausibleLength(uint64_t storedLength, uint64_t length)
{
    // Deflate shrinks by at most about 1032 to 1; storedLength is already
    // known to fit in the file.
    return length <= storedLength * 1032 + 1024 && length <= SIZE_MAX;
}

bool inflateAll(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize)
{
    // In pieces, as zlib counts in 32 bits.
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return false;
    stream.next_in = const_cast<Bytef*>(in);
    stream.next_out = out;
    int status = Z_OK;
    while (status == Z_OK)
    {
        if (stream.avail_in == 0 && inSize > 0)
        {
            stream.avail_in = static_cast<uInt>(std::min<size_t>(inSize, 1u << 30));
            inSize -= stream.avail_in;
        }
        if (stream.avail_out == 0 && outSize > 0)
        {
            stream.avail_out = static_cast<uInt>(std::min<size_t>(outSize, 1u << 30));
            outSize -= stream.avail_out;
        }
        status = inflate(&stream, Z_NO_FLUSH);
    }
    const bool whole = status == Z_STREAM_END && stream.avail_out == 0 && outSize == 0;
    inflateEnd(&stream);
    return whole;
}

uint64_t fnv1a64(const void* data, size_t size)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

bool isPhx(const std::string& path)
{
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    uint8_t magic[8] = {};
    return file.read(reinterpret_cast<char*>(magic), 8) && std::equal(magic, magic + 8, kMagic);
}

// ---------------------------------------------------------------------------
// Writing

ContainerWriter::~ContainerWriter()
{
    abandon();
}

void ContainerWriter::abandon()
{
    if (_file)
    {
        std::fclose(static_cast<FILE*>(_file));
        _file = nullptr;
        std::error_code ignored;
        std::filesystem::remove(std::filesystem::u8path(_tempPath), ignored);
    }
}

bool ContainerWriter::open(const std::string& path, std::string& error)
{
    _path = path;
    _tempPath = path + ".saving";
#ifdef _WIN32
    FILE* opened = nullptr;
    if (_wfopen_s(&opened, std::filesystem::u8path(_tempPath).c_str(), L"wb") == 0)
        _file = opened;
#else
    _file = std::fopen(_tempPath.c_str(), "wb");
#endif
    if (!_file)
    {
        error = "Could not create " + _tempPath + ".";
        return false;
    }
    // The header is written again at the end, with the index's offset.
    uint8_t header[kHeaderSize] = {};
    return write(header, sizeof(header));
}

bool ContainerWriter::write(const void* data, size_t size)
{
    if (_failed || !_file)
        return false;
    if (size > 0 && std::fwrite(data, 1, size, static_cast<FILE*>(_file)) != size)
    {
        _failed = true;
        return false;
    }
    _offset += size;
    return true;
}

PreparedChunk prepare(uint32_t type, std::string content, bool compress, bool critical, uint32_t mediaId,
    const std::string& symbolName)
{
    PreparedChunk chunk;
    ChunkInfo& info = chunk.info;
    info.type = type;
    info.flags = critical ? kCritical : 0;
    info.length = content.size();
    info.mediaId = mediaId;
    info.symbolName = symbolName;
    if (compress && content.size() > 64 && content.size() <= kMaxDeflate)
    {
        // A light level: most of the gain at a fraction of the time.
        uLongf bound = compressBound(static_cast<uLong>(content.size()));
        std::string deflated(bound, '\0');
        if (compress2(reinterpret_cast<Bytef*>(deflated.data()), &bound, reinterpret_cast<const Bytef*>(content.data()),
                static_cast<uLong>(content.size()), 3) == Z_OK &&
            bound < content.size())
        {
            deflated.resize(bound);
            chunk.stored = std::move(deflated);
            info.flags |= kDeflate;
        }
    }
    if (!(info.flags & kDeflate))
        chunk.stored = std::move(content);
    info.storedLength = chunk.stored.size();
    info.crc = crc32(chunk.stored.data(), chunk.stored.size());
    return chunk;
}

uint32_t ContainerWriter::add(const PreparedChunk& chunk)
{
    return writeChunk(chunk.info, chunk.stored.data());
}

uint32_t ContainerWriter::add(uint32_t type, const std::string& content, bool compress, bool critical,
    uint32_t mediaId, const std::string& symbolName)
{
    return add(prepare(type, content, compress, critical, mediaId, symbolName));
}

uint32_t ContainerWriter::add(uint32_t type, const void* content, size_t size, bool compress, bool critical,
    uint32_t mediaId, const std::string& symbolName)
{
    ChunkInfo info;
    info.type = type;
    info.flags = critical ? kCritical : 0;
    info.length = size;
    info.mediaId = mediaId;
    info.symbolName = symbolName;
    std::vector<uint8_t> deflated;
    const void* stored = content;
    info.storedLength = size;
    if (compress && size > 64 && size <= kMaxDeflate)
    {
        // A light level: most of the gain at a fraction of the time.
        uLongf bound = compressBound(static_cast<uLong>(size));
        deflated.resize(bound);
        if (compress2(deflated.data(), &bound, static_cast<const Bytef*>(content), static_cast<uLong>(size), 3) == Z_OK &&
            bound < size)
        {
            deflated.resize(bound);
            stored = deflated.data();
            info.storedLength = bound;
            info.flags |= kDeflate;
        }
    }
    info.crc = crc32(stored, static_cast<size_t>(info.storedLength));
    return writeChunk(info, stored);
}

uint32_t ContainerWriter::addStored(const ChunkInfo& source, const uint8_t* stored)
{
    ChunkInfo info = source;
    return writeChunk(info, stored);
}

uint32_t ContainerWriter::writeChunk(ChunkInfo info, const void* stored)
{
    // Chunks start on 8-byte boundaries.
    static const uint8_t zeros[8] = {};
    if (_offset % 8)
        write(zeros, 8 - _offset % 8);
    info.id = _nextId++;
    info.offset = _offset;
    uint8_t header[kChunkHeaderSize] = {};
    put32(header, info.type);
    put32(header + 4, info.flags);
    put64(header + 8, info.storedLength);
    put64(header + 16, info.length);
    put32(header + 24, info.crc);
    put32(header + 28, info.id);
    if (!write(header, sizeof(header)) || !write(stored, static_cast<size_t>(info.storedLength)))
        return 0;
    _chunks.push_back(info);
    return info.id;
}

bool ContainerWriter::finish(std::string& error)
{
    if (!_file)
    {
        error = "The file was not opened.";
        return false;
    }
    const std::string index = encodeIndex(_chunks);
    ChunkInfo info;
    info.type = kIndex;
    info.flags = kCritical;
    info.length = index.size();
    info.storedLength = index.size();
    info.crc = crc32(index.data(), index.size());
    const std::vector<ChunkInfo> listed = _chunks;
    if (!writeChunk(info, index.data()))
    {
        error = "Could not write " + _tempPath + ".";
        abandon();
        return false;
    }
    const uint64_t indexOffset = _chunks.back().offset;
    _chunks = listed;

    uint8_t trailer[kTrailerSize] = {};
    put64(trailer, indexOffset);
    std::copy(kTrailerMark, kTrailerMark + 4, trailer + 8);
    put32(trailer + 12, crc32(trailer, 12));
    write(trailer, sizeof(trailer));

    uint8_t header[kHeaderSize] = {};
    std::copy(kMagic, kMagic + 8, header);
    put16(header + 8, kMajorVersion);
    put16(header + 10, kMinorVersion);
    put64(header + 16, indexOffset);
    put32(header + 24, crc32(header, 24));
    FILE* file = static_cast<FILE*>(_file);
    if (_failed || std::fseek(file, 0, SEEK_SET) != 0 || std::fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
        std::fflush(file) != 0)
    {
        error = "Could not write " + _tempPath + ". The disk may be full.";
        abandon();
        return false;
    }
    // On disk before it replaces the old file, so a crash leaves one or the
    // other whole.
#ifdef _WIN32
    _commit(_fileno(file));
#else
    fsync(fileno(file));
#endif
    std::fclose(file);
    _file = nullptr;

    std::error_code code;
    std::filesystem::rename(std::filesystem::u8path(_tempPath), std::filesystem::u8path(_path), code);
    if (code)
    {
        error = "Could not replace " + _path + ": " + code.message();
        std::filesystem::remove(std::filesystem::u8path(_tempPath), code);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reading

ContainerReader::~ContainerReader()
{
    unmap();
}

void ContainerReader::unmap()
{
    if (!_mapping)
        return;
#ifdef _WIN32
    UnmapViewOfFile(_mapping);
#else
    munmap(_mapping, _size);
#endif
    _mapping = nullptr;
    _bytes = nullptr;
    _size = 0;
}

bool ContainerReader::open(const std::string& path, std::string& error)
{
    // Mapped rather than read: the bytes are used where they lie, and only
    // the parts read are paged in.
    unmap();
    _owned.clear();
#ifdef _WIN32
    HANDLE file = CreateFileW(std::filesystem::u8path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "Could not open " + path + ".";
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    if (size.QuadPart > 0)
    {
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping)
        {
            _mapping = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            CloseHandle(mapping);
        }
    }
    CloseHandle(file);
    _size = static_cast<size_t>(size.QuadPart);
#else
    const int file = ::open(path.c_str(), O_RDONLY);
    if (file < 0)
    {
        error = "Could not open " + path + ".";
        return false;
    }
    struct stat info{};
    fstat(file, &info);
    _size = static_cast<size_t>(info.st_size);
    if (_size > 0)
    {
        void* mapped = mmap(nullptr, _size, PROT_READ, MAP_PRIVATE, file, 0);
        _mapping = mapped == MAP_FAILED ? nullptr : mapped;
    }
    ::close(file);
#endif
    if (!_mapping)
    {
        _size = 0;
        error = "Could not read " + path + ".";
        return false;
    }
    _bytes = static_cast<const uint8_t*>(_mapping);
    return openBytes(error);
}

bool ContainerReader::openMemory(std::vector<uint8_t> data, std::string& error)
{
    unmap();
    _owned = std::move(data);
    _bytes = _owned.data();
    _size = _owned.size();
    return openBytes(error);
}

bool ContainerReader::openBytes(std::string& error)
{
    _chunks.clear();
    _recovered = false;
    if (_size < kHeaderSize || !std::equal(kMagic, kMagic + 8, _bytes))
    {
        error = "This is not a Phoenix document.";
        return false;
    }
    _major = get16(_bytes + 8);
    _minor = get16(_bytes + 10);
    if (_major > kMajorVersion)
    {
        error = "This document was saved by a newer Phoenix (format " + std::to_string(_major) + "." +
            std::to_string(_minor) + "). Update Phoenix to open it.";
        return false;
    }

    // The index, through the header or else the trailer; failing both, the
    // chunks walked one by one.
    const bool headerGood = crc32(_bytes, 24) == Reader::load32(_bytes + 24);
    if (headerGood && readIndex(Reader::load64(_bytes + 16)))
        return true;
    if (_size >= kHeaderSize + kTrailerSize)
    {
        const uint8_t* trailer = _bytes + _size - kTrailerSize;
        if (std::equal(kTrailerMark, kTrailerMark + 4, trailer + 8) && crc32(trailer, 12) == Reader::load32(trailer + 12) &&
            readIndex(Reader::load64(trailer)))
        {
            _recovered = !headerGood;
            return true;
        }
    }
    walkChunks();
    _recovered = true;
    if (_chunks.empty())
    {
        error = "This Phoenix document is damaged: no part of it could be read.";
        return false;
    }
    return true;
}

bool ContainerReader::fits(uint64_t offset, uint64_t storedLength) const
{
    // Written so that no sum can wrap.
    return offset <= _size && _size - offset >= kChunkHeaderSize && storedLength <= _size - offset - kChunkHeaderSize;
}

bool ContainerReader::readIndex(uint64_t offset)
{
    if (!fits(offset, 0))
        return false;
    const uint8_t* header = _bytes + offset;
    if (Reader::load32(header) != kIndex)
        return false;
    const uint64_t stored = Reader::load64(header + 8);
    if (!fits(offset, stored))
        return false;
    const uint8_t* body = header + kChunkHeaderSize;
    if (crc32(body, static_cast<size_t>(stored)) != Reader::load32(header + 24))
        return false;
    std::vector<ChunkInfo> chunks;
    if (!decodeIndex(body, static_cast<size_t>(stored), chunks))
        return false;
    for (const ChunkInfo& chunk : chunks)
    {
        if (!fits(chunk.offset, chunk.storedLength))
            return false;
    }
    _chunks = std::move(chunks);
    return true;
}

void ContainerReader::walkChunks()
{
    uint64_t offset = kHeaderSize;
    while (fits(offset, 0))
    {
        const uint8_t* header = _bytes + offset;
        ChunkInfo chunk;
        chunk.type = Reader::load32(header);
        chunk.flags = Reader::load32(header + 4);
        chunk.storedLength = Reader::load64(header + 8);
        chunk.length = Reader::load64(header + 16);
        chunk.crc = Reader::load32(header + 24);
        chunk.id = Reader::load32(header + 28);
        chunk.offset = offset;
        if (!fits(offset, chunk.storedLength))
            break;
        if (chunk.type != kIndex)
        {
            // Media and symbol chunks name themselves in their content, which
            // the document reader looks at; the index only saves it a look.
            _chunks.push_back(chunk);
        }
        offset += kChunkHeaderSize + chunk.storedLength;
        offset = (offset + 7) / 8 * 8;
    }
}

const uint8_t* ContainerReader::stored(const ChunkInfo& chunk) const
{
    return _bytes + chunk.offset + kChunkHeaderSize;
}

bool ContainerReader::content(const ChunkInfo& chunk, std::vector<uint8_t>& out, std::string& error) const
{
    const uint8_t* bytes = stored(chunk);
    const size_t size = static_cast<size_t>(chunk.storedLength);
    if (crc32(bytes, size) != chunk.crc)
    {
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (checksum).";
        return false;
    }
    if ((chunk.flags & kCompressionMask) == 0)
    {
        out.assign(bytes, bytes + size);
        return true;
    }
    if ((chunk.flags & kCompressionMask) != kDeflate)
    {
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) +
            " uses a compression this Phoenix does not know.";
        return false;
    }
    if (!plausibleLength(chunk.storedLength, chunk.length))
    {
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (size).";
        return false;
    }
    out.resize(static_cast<size_t>(chunk.length));
    if (!inflateAll(bytes, size, out.data(), out.size()))
    {
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (inflate).";
        return false;
    }
    return true;
}

bool ContainerReader::content(const ChunkInfo& chunk, std::string& out, std::string& error) const
{
    const uint8_t* bytes = stored(chunk);
    const size_t size = static_cast<size_t>(chunk.storedLength);
    if (crc32(bytes, size) != chunk.crc)
    {
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (checksum).";
        return false;
    }
    switch (chunk.flags & kCompressionMask)
    {
    case 0:
        out.assign(reinterpret_cast<const char*>(bytes), size);
        return true;
    case kDeflate:
    {
        if (!plausibleLength(chunk.storedLength, chunk.length))
        {
            error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (size).";
            return false;
        }
        out.resize(static_cast<size_t>(chunk.length));
        if (!inflateAll(bytes, size, reinterpret_cast<uint8_t*>(out.data()), out.size()))
        {
            error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) + " is damaged (inflate).";
            return false;
        }
        return true;
    }
    default:
        error = "The " + typeName(chunk.type) + " part " + std::to_string(chunk.id) +
            " uses a compression this Phoenix does not know.";
        return false;
    }
}

} // namespace phx
