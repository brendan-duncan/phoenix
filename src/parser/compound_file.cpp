#include "compound_file.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>

namespace {

constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr uint32_t kFree = 0xFFFFFFFF;
constexpr uint32_t kNoStream = 0xFFFFFFFF;

uint16_t u16(const std::vector<uint8_t>& data, size_t at)
{
    return at + 2 <= data.size() ? static_cast<uint16_t>(data[at] | (data[at + 1] << 8)) : 0;
}

uint32_t u32(const std::vector<uint8_t>& data, size_t at)
{
    if (at + 4 > data.size())
        return 0;
    return static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
        (static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
}

struct Entry
{
    std::string name;
    uint8_t type = 0;
    uint32_t left = kNoStream;
    uint32_t right = kNoStream;
    uint32_t child = kNoStream;
    uint32_t start = 0;
    uint64_t size = 0;
};

/// A UTF-16 name as UTF-8.
std::string utf8(const std::vector<uint8_t>& data, size_t at, size_t bytes)
{
    std::string out;
    for (size_t i = 0; i + 1 < bytes; i += 2)
    {
        uint32_t c = u16(data, at + i);
        if (c == 0)
            break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes)
        {
            const uint32_t low = u16(data, at + i + 2);
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        }
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

} // namespace

bool CompoundFile::isCompoundFile(const std::vector<uint8_t>& data)
{
    static const uint8_t signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    return data.size() >= 512 && std::memcmp(data.data(), signature, 8) == 0;
}

bool CompoundFile::read(const std::vector<uint8_t>& data)
{
    _streams.clear();
    _error.clear();
    if (!isCompoundFile(data))
    {
        _error = "Not a compound file.";
        return false;
    }

    const uint16_t sectorShift = u16(data, 30);
    const uint16_t miniShift = u16(data, 32);
    if (sectorShift < 7 || sectorShift > 16 || miniShift > sectorShift)
    {
        _error = "Compound file header is damaged.";
        return false;
    }
    const size_t sectorSize = size_t(1) << sectorShift;
    const size_t miniSize = size_t(1) << miniShift;
    const uint32_t fatSectors = u32(data, 44);
    const uint32_t firstDirectory = u32(data, 48);
    const uint32_t miniCutoff = u32(data, 56);
    const uint32_t firstMiniFat = u32(data, 60);
    const uint32_t miniFatSectors = u32(data, 64);
    uint32_t difatSector = u32(data, 68);
    const uint32_t difatSectors = u32(data, 72);

    const auto sectorAt = [&](uint32_t sector) -> size_t { return (static_cast<size_t>(sector) + 1) * sectorSize; };
    const uint32_t sectorCount = static_cast<uint32_t>(data.size() / sectorSize);

    // The FAT's sectors: 109 listed in the header, the rest in DIFAT sectors.
    // There cannot be more of them than the file has sectors, and the DIFAT
    // chain is guarded against loops.
    const uint32_t fatCount = std::min(fatSectors, sectorCount);
    std::vector<uint32_t> fatList;
    for (int i = 0; i < 109 && fatList.size() < fatCount; ++i)
        fatList.push_back(u32(data, 76 + 4 * i));
    const size_t perDifat = sectorSize / 4 - 1;
    std::set<uint32_t> difatSeen;
    for (uint32_t n = 0; n < difatSectors && difatSector < sectorCount && fatList.size() < fatCount; ++n)
    {
        if (!difatSeen.insert(difatSector).second)
            break;
        const size_t at = sectorAt(difatSector);
        for (size_t i = 0; i < perDifat && fatList.size() < fatSectors; ++i)
            fatList.push_back(u32(data, at + 4 * i));
        difatSector = u32(data, at + 4 * perDifat);
    }
    std::vector<uint32_t> fat;
    for (uint32_t sector : fatList)
    {
        if (sector >= sectorCount)
            continue;
        const size_t at = sectorAt(sector);
        for (size_t i = 0; i < sectorSize / 4; ++i)
            fat.push_back(u32(data, at + 4 * i));
    }

    // A chain of sectors through a table, guarded against loops.
    const auto chain = [&](uint32_t start, const std::vector<uint32_t>& table, uint32_t limit) {
        std::vector<uint32_t> sectors;
        std::set<uint32_t> seen;
        for (uint32_t s = start; s != kEndOfChain && s != kFree && s < table.size() && s < limit; s = table[s])
        {
            if (!seen.insert(s).second)
                break;
            sectors.push_back(s);
        }
        return sectors;
    };
    const auto readChain = [&](uint32_t start, uint64_t size) {
        std::vector<uint8_t> out;
        for (uint32_t sector : chain(start, fat, sectorCount))
        {
            const size_t at = sectorAt(sector);
            const size_t take = std::min(sectorSize, data.size() - std::min(data.size(), at));
            out.insert(out.end(), data.begin() + at, data.begin() + at + take);
            if (out.size() >= size)
                break;
        }
        if (out.size() > size)
            out.resize(static_cast<size_t>(size));
        return out;
    };

    // The directory.
    const std::vector<uint8_t> directory = readChain(firstDirectory, UINT64_MAX);
    std::vector<Entry> entries;
    for (size_t at = 0; at + 128 <= directory.size(); at += 128)
    {
        Entry entry;
        const uint16_t nameBytes = std::min<uint16_t>(u16(directory, at + 64), 64);
        entry.name = utf8(directory, at, nameBytes);
        entry.type = directory[at + 66];
        entry.left = u32(directory, at + 68);
        entry.right = u32(directory, at + 72);
        entry.child = u32(directory, at + 76);
        entry.start = u32(directory, at + 116);
        entry.size = u32(directory, at + 120);
        if (sectorShift > 9)
            entry.size |= static_cast<uint64_t>(u32(directory, at + 124)) << 32;
        entries.push_back(entry);
    }
    if (entries.empty() || entries[0].type != 5)
    {
        _error = "Compound file directory is damaged.";
        return false;
    }

    // Small streams live in the mini stream, in mini sectors.
    const std::vector<uint8_t> miniStream = readChain(entries[0].start, entries[0].size);
    std::vector<uint32_t> miniFat;
    {
        const std::vector<uint8_t> table = readChain(firstMiniFat, static_cast<uint64_t>(miniFatSectors) * sectorSize);
        for (size_t at = 0; at + 4 <= table.size(); at += 4)
            miniFat.push_back(u32(table, at));
    }
    const auto readMini = [&](uint32_t start, uint64_t size) {
        std::vector<uint8_t> out;
        for (uint32_t sector : chain(start, miniFat, static_cast<uint32_t>(miniStream.size() / miniSize)))
        {
            const size_t at = static_cast<size_t>(sector) * miniSize;
            const size_t take = std::min(miniSize, miniStream.size() - std::min(miniStream.size(), at));
            out.insert(out.end(), miniStream.begin() + at, miniStream.begin() + at + take);
            if (out.size() >= size)
                break;
        }
        if (out.size() > size)
            out.resize(static_cast<size_t>(size));
        return out;
    };

    // Each storage's children are a tree of siblings, walked with a list
    // rather than recursion: a damaged tree can be one long branch. Streams
    // that share sectors would each copy them, so all the streams together
    // may not come to much more than the file.
    const uint64_t budget = static_cast<uint64_t>(data.size()) * 2 + (1u << 20);
    uint64_t total = 0;
    std::set<uint32_t> visited;
    std::vector<std::pair<uint32_t, std::string>> pending = {{entries[0].child, std::string()}};
    while (!pending.empty())
    {
        const uint32_t index = pending.back().first;
        const std::string prefix = std::move(pending.back().second);
        pending.pop_back();
        if (index == kNoStream || index >= entries.size() || !visited.insert(index).second)
            continue;
        const Entry& entry = entries[index];
        pending.emplace_back(entry.left, prefix);
        pending.emplace_back(entry.right, prefix);
        if (entry.type == 2)
        {
            std::vector<uint8_t> stream = entry.size < miniCutoff ? readMini(entry.start, entry.size)
                                                                  : readChain(entry.start, entry.size);
            total += stream.size();
            if (total > budget)
            {
                _streams.clear();
                _error = "Compound file directory is damaged.";
                return false;
            }
            _streams[prefix + entry.name] = std::move(stream);
        }
        else if (entry.type == 1)
        {
            pending.emplace_back(entry.child, prefix + entry.name + "/");
        }
    }
    return true;
}
