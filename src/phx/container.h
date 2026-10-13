#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// The .phx container (docs/PHOENIX_FORMAT.md): a header, chunks each with a
/// checksum, an index of the chunks and a trailer. Knows nothing of what the
/// chunks hold. Free of Qt.
namespace phx {

constexpr uint16_t kMajorVersion = 1;
constexpr uint16_t kMinorVersion = 0;

/// The four letters of a chunk type as a number, first letter lowest.
constexpr uint32_t fourcc(const char (&s)[5])
{
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
std::string typeName(uint32_t type);

constexpr uint32_t kIndex = fourcc("INDX");
constexpr uint32_t kDocument = fourcc("DOCU");
constexpr uint32_t kSymbol = fourcc("SYMB");
constexpr uint32_t kMedia = fourcc("MDIA");
constexpr uint32_t kXflPackage = fourcc("XFLP");
constexpr uint32_t kThumbnail = fourcc("THMB");
constexpr uint32_t kMetadata = fourcc("META");

enum ChunkFlags : uint32_t
{
    kCompressionMask = 0x0F,
    kDeflate = 0x01,
    kCritical = 0x10,
};

/// One chunk as the index lists it.
struct ChunkInfo
{
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t flags = 0;
    uint64_t offset = 0;        // of the chunk header
    uint64_t storedLength = 0;
    uint64_t length = 0;
    uint32_t crc = 0;
    uint32_t mediaId = 0;
    std::string symbolName;
};

/// A chunk ready to write: its content compressed (or not) and checksummed.
/// Made by prepare(), which is safe to call on several threads at once.
struct PreparedChunk
{
    ChunkInfo info;
    std::string stored;
};

/// Compresses (when \a compress and it helps) and checksums \a content.
PreparedChunk prepare(uint32_t type, std::string content, bool compress, bool critical, uint32_t mediaId = 0,
    const std::string& symbolName = std::string());

/// Writes a .phx file: chunks as they are added, then the index. Everything
/// goes to a temporary file beside the target, which replaces the target
/// only once it is complete and on disk.
class ContainerWriter
{
public:
    ContainerWriter() = default;
    ~ContainerWriter();
    ContainerWriter(const ContainerWriter&) = delete;
    ContainerWriter& operator=(const ContainerWriter&) = delete;

    bool open(const std::string& path, std::string& error);

    /// Adds a chunk holding \a content, deflated with \a compress. Returns its
    /// id, or 0 on a write error.
    uint32_t add(uint32_t type, const std::string& content, bool compress, bool critical, uint32_t mediaId = 0,
        const std::string& symbolName = std::string());
    uint32_t add(uint32_t type, const void* content, size_t size, bool compress, bool critical, uint32_t mediaId = 0,
        const std::string& symbolName = std::string());

    /// Adds a chunk made by prepare().
    uint32_t add(const PreparedChunk& chunk);

    /// Adds a chunk copied as it was stored in another file: \a stored is its
    /// stored (possibly compressed) bytes, and \a info its index entry there.
    uint32_t addStored(const ChunkInfo& info, const uint8_t* stored);

    /// Writes the index and the trailer, puts the file on disk and moves it
    /// over the target.
    bool finish(std::string& error);

    const std::vector<ChunkInfo>& chunks() const { return _chunks; }

private:
    uint32_t writeChunk(ChunkInfo info, const void* stored);
    bool write(const void* data, size_t size);
    void abandon();

    void* _file = nullptr;
    std::string _path;
    std::string _tempPath;
    uint64_t _offset = 0;
    uint32_t _nextId = 1;
    bool _failed = false;
    std::vector<ChunkInfo> _chunks;
};

/// A .phx file, mapped into memory (or held in it).
class ContainerReader
{
public:
    ContainerReader() = default;
    ~ContainerReader();
    ContainerReader(const ContainerReader&) = delete;
    ContainerReader& operator=(const ContainerReader&) = delete;

    /// Reads the file and its index; false with \a error when it is not a
    /// .phx file or is damaged past reading. A damaged index is rebuilt by
    /// walking the chunks.
    bool open(const std::string& path, std::string& error);
    bool openMemory(std::vector<uint8_t> data, std::string& error);

    uint16_t majorVersion() const { return _major; }
    uint16_t minorVersion() const { return _minor; }
    const std::vector<ChunkInfo>& chunks() const { return _chunks; }

    /// A chunk's content, decompressed; false with \a error when its checksum
    /// does not match or it cannot be inflated.
    bool content(const ChunkInfo& chunk, std::string& out, std::string& error) const;
    bool content(const ChunkInfo& chunk, std::vector<uint8_t>& out, std::string& error) const;

    /// A chunk's bytes as stored, for copying it into a new file unchanged.
    const uint8_t* stored(const ChunkInfo& chunk) const;

    /// Whether the index had to be rebuilt.
    bool recovered() const { return _recovered; }

private:
    /// Whether a chunk header at \a offset and \a storedLength bytes after it
    /// lie within the file.
    bool fits(uint64_t offset, uint64_t storedLength) const;
    bool readIndex(uint64_t offset);
    void walkChunks();
    bool openBytes(std::string& error);
    void unmap();

    /// The file's bytes: mapped, or held in _owned.
    const uint8_t* _bytes = nullptr;
    size_t _size = 0;
    std::vector<uint8_t> _owned;
    void* _mapping = nullptr;
    uint16_t _major = 0;
    uint16_t _minor = 0;
    std::vector<ChunkInfo> _chunks;
    bool _recovered = false;
};

/// CRC-32 (zlib's), and FNV-1a 64 for media content hashes.
uint32_t crc32(const void* data, size_t size);
uint64_t fnv1a64(const void* data, size_t size);

/// Whether a chunk of \a storedLength deflated bytes could hold \a length
/// bytes: a damaged or hostile size is not given the memory it asks for.
bool plausibleLength(uint64_t storedLength, uint64_t length);

/// Inflates \a in into exactly \a outSize bytes; false when the stream is
/// damaged or its size differs. Sizes past 4 GB are fine.
bool inflateAll(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize);

/// Whether a file starts with the .phx magic.
bool isPhx(const std::string& path);

} // namespace phx
