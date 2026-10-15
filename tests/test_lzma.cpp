#include "test_util.h"

#include "../src/util/lzma.h"

#include <string>
#include <vector>

namespace {

std::vector<uint8_t> fromHex(const std::string& hex)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

} // namespace

TEST(lzma_decodes_what_liblzma_wrote)
{
    // Python's lzma.compress(text, format=lzma.FORMAT_ALONE): the five
    // property bytes, then the data after the 8-byte size.
    std::string text;
    for (int i = 0; i < 300; ++i)
        text += "frame " + std::to_string(i % 37) + " of " + std::to_string(i / 37) + ", ";
    const uint8_t properties[5] = {0x5d, 0x00, 0x00, 0x80, 0x00};
    const std::vector<uint8_t> data = fromHex(
        "00331c8846809cf3600622752dc48310f24224c29528b8d180667c23457f8aef14de0a810040f12fd08d084e6b026457"
        "35bbcb577d244ea4fbdf5c9446c6818b586c5ee0bbd8c5168648526ab56f89bace13d92947b97180ae5e2e479b8e879c"
        "1fe315a341295a8a47d7af98462f8e73cb7ff52be6bdcb2759f2f5bf88fde1cf11c301d33e42e02b4c231e30ba554c66"
        "9cdfce25cb2e75c0ddaa3329ebb8c23b15fe55154888c5d7cd304c214a7e58c665bc4f769335441f6bd16393cbd6138f"
        "a9fdf37805b15511e62b415cd33e4d261ba148b3185863f7f05b5935010bd18ce9a9819cd085052804c6afd8e5caeade"
        "aa89a570a9369831b4a75e89fb133945b2182146c15e5a95cecaf5a779246c802f9a2b44651c5350d0d2b10e55ab5a76"
        "73124f26e084842399b47ea905d25ec54f0331f965946f4fcc7266f8368cffff9b2d3a00");
    std::vector<uint8_t> out;
    CHECK(lzma::decode(properties, data.data(), data.size(), text.size(), out));
    CHECK(std::string(out.begin(), out.end()) == text);
    // Cut short or damaged, it says so rather than making something up.
    CHECK(!lzma::decode(properties, data.data(), data.size() / 2, text.size(), out));
    std::vector<uint8_t> damaged = data;
    damaged[0] = 1;
    CHECK(!lzma::decode(properties, damaged.data(), damaged.size(), text.size(), out));
}
