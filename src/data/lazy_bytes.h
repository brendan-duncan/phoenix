#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fla {

/// Where bytes lie in a file, for loading them only when they are first
/// needed: a Phoenix document's media stay on disk until something shows
/// them. One location is shared by every holder of the same bytes, so a save
/// that moves them to a new file points them all there at once.
struct ByteLocation
{
    /// Reads the bytes; false when the file is gone or damaged.
    std::function<bool(const ByteLocation&, std::vector<uint8_t>&)> load;
    /// The file and the part of it, for the loader and for copying the
    /// stored bytes into a new file unchanged.
    std::string path;
    uint64_t offset = 0;
    uint64_t storedLength = 0;
    uint64_t length = 0;
    uint32_t flags = 0;
    uint32_t crc = 0;
    uint32_t type = 0;
    uint32_t mediaId = 0;
    /// The bytes' own length and hash, known without reading them.
    uint64_t size = 0;
    uint64_t hash = 0;
};

/// Bytes held in memory, or read from where they lie when first asked for
/// and kept from then on. Reads like a const std::vector<uint8_t>.
class LazyBytes
{
public:
    LazyBytes() = default;
    LazyBytes(std::vector<uint8_t> bytes)
        : _state(std::make_shared<State>())
    {
        _state->bytes = std::move(bytes);
        _state->loaded = true;
    }
    explicit LazyBytes(std::shared_ptr<ByteLocation> location)
        : _state(std::make_shared<State>())
    {
        _state->location = std::move(location);
    }

    LazyBytes& operator=(std::vector<uint8_t> bytes)
    {
        *this = LazyBytes(std::move(bytes));
        return *this;
    }

    /// The bytes, read now if they have not been.
    const std::vector<uint8_t>& get() const
    {
        static const std::vector<uint8_t> none;
        if (!_state)
            return none;
        std::lock_guard<std::mutex> lock(_state->mutex);
        if (!_state->loaded)
        {
            const ByteLocation* location = _state->location.get();
            if (!location || !location->load || !location->load(*location, _state->bytes))
                _state->bytes.clear();
            _state->loaded = true;
        }
        return _state->bytes;
    }
    operator const std::vector<uint8_t>&() const { return get(); }

    /// Answered from the location without reading, when not read yet.
    bool empty() const { return size() == 0; }
    size_t size() const
    {
        if (!_state)
            return 0;
        {
            std::lock_guard<std::mutex> lock(_state->mutex);
            if (!_state->loaded && _state->location)
                return static_cast<size_t>(_state->location->size);
        }
        return get().size();
    }
    const uint8_t* data() const { return get().data(); }
    std::vector<uint8_t>::const_iterator begin() const { return get().begin(); }
    std::vector<uint8_t>::const_iterator end() const { return get().end(); }
    bool operator==(const LazyBytes& other) const { return get() == other.get(); }

    /// Where the bytes lie on disk, if they came from there unchanged;
    /// null for bytes made in memory.
    const std::shared_ptr<ByteLocation>& location() const
    {
        static const std::shared_ptr<ByteLocation> none;
        return _state ? _state->location : none;
    }
    /// Points the bytes at where a save has just put them, so later reads
    /// and saves find them there. The bytes themselves do not change.
    void relocate(std::shared_ptr<ByteLocation> location) const
    {
        if (!_state)
            return;
        std::lock_guard<std::mutex> lock(_state->mutex);
        _state->location = std::move(location);
    }

    /// Stands for these very bytes: a holder of the token can tell whether
    /// they are still the ones it saw (it is not expired and locks to the
    /// same object) without reading them, and without being fooled by
    /// another set of bytes taking their address after they are freed.
    std::weak_ptr<const void> token() const { return std::shared_ptr<const void>(_state); }

    /// Lets go of bytes read from where they lie, to be read again when next
    /// asked for, so a document put aside keeps little in memory. Bytes made
    /// in memory stay, and so do bytes another holder shares (a copy in
    /// another document, a save under way), which may be reading them. True
    /// when they were let go.
    bool unload() const
    {
        if (!_state || _state.use_count() != 1)
            return false;
        std::lock_guard<std::mutex> lock(_state->mutex);
        if (!_state->loaded || !_state->location || !_state->location->load)
            return false;
        std::vector<uint8_t>().swap(_state->bytes);
        _state->loaded = false;
        return true;
    }

    /// Whether the bytes have been read into memory.
    bool loaded() const
    {
        if (!_state)
            return true;
        std::lock_guard<std::mutex> lock(_state->mutex);
        return _state->loaded;
    }

private:
    struct State
    {
        std::mutex mutex;
        std::vector<uint8_t> bytes;
        std::shared_ptr<ByteLocation> location;
        bool loaded = false;
    };
    // Copies share the bytes, as the value they hold never changes; a new
    // value is a new state.
    std::shared_ptr<State> _state;
};

} // namespace fla
