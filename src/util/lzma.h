#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lzma {

/// Decodes raw LZMA data (no header) of the given properties: the five
/// bytes of lc/lp/pb and the dictionary size that precede it in a .lzma
/// file or an LZMA-compressed SWF. Stops after \a size bytes, or at the
/// end marker. False when the data is damaged.
bool decode(const uint8_t properties[5], const uint8_t* data, size_t length, size_t size, std::vector<uint8_t>& out);

} // namespace lzma
