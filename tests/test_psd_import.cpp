#include "test_util.h"

#include "../src/edit/psd_import.h"

namespace {

struct Writer
{
    std::vector<uint8_t> bytes;

    void put(uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
            bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
    void text(const std::string& s) { bytes.insert(bytes.end(), s.begin(), s.end()); }
    void append(const std::vector<uint8_t>& more) { bytes.insert(bytes.end(), more.begin(), more.end()); }
};

struct Spec
{
    std::string name;
    int top, left, bottom, right;
    uint32_t section;
    bool hidden;
    uint8_t opacity;
    /// Each channel's data, compression first; ids -1, 0, 1, 2.
    std::vector<std::vector<uint8_t>> channels;
};

std::vector<uint8_t> raw(size_t count, uint8_t value)
{
    std::vector<uint8_t> data = {0, 0};
    data.insert(data.end(), count, value);
    return data;
}

std::vector<uint8_t> document(const std::vector<Spec>& specs)
{
    Writer records;
    records.put(static_cast<uint32_t>(specs.size()), 2);
    for (const Spec& spec : specs)
    {
        records.put(static_cast<uint32_t>(spec.top), 4);
        records.put(static_cast<uint32_t>(spec.left), 4);
        records.put(static_cast<uint32_t>(spec.bottom), 4);
        records.put(static_cast<uint32_t>(spec.right), 4);
        records.put(static_cast<uint32_t>(spec.channels.size()), 2);
        const int16_t ids[] = {-1, 0, 1, 2};
        for (size_t c = 0; c < spec.channels.size(); ++c)
        {
            records.put(static_cast<uint16_t>(ids[c]), 2);
            records.put(static_cast<uint32_t>(spec.channels[c].size()), 4);
        }
        records.text("8BIMnorm");
        records.put(spec.opacity, 1);
        records.put(0, 1);
        records.put(spec.hidden ? 2 : 0, 1);
        records.put(0, 1);
        Writer extra;
        extra.put(0, 4); // mask
        extra.put(0, 4); // blending ranges
        extra.put(static_cast<uint32_t>(spec.name.size()), 1);
        extra.text(spec.name);
        for (size_t pad = (4 - (spec.name.size() + 1) % 4) % 4; pad > 0; --pad)
            extra.put(0, 1);
        if (spec.section)
        {
            extra.text("8BIMlsct");
            extra.put(4, 4);
            extra.put(spec.section, 4);
        }
        records.put(static_cast<uint32_t>(extra.bytes.size()), 4);
        records.append(extra.bytes);
    }
    for (const Spec& spec : specs)
        for (const std::vector<uint8_t>& channel : spec.channels)
            records.append(channel);

    Writer file;
    file.text("8BPS");
    file.put(1, 2);
    file.put(0, 6);
    file.put(3, 2);
    file.put(4, 4); // height
    file.put(6, 4); // width
    file.put(8, 2);
    file.put(3, 2);
    file.put(0, 4);
    file.put(0, 4);
    file.put(static_cast<uint32_t>(records.bytes.size() + 4), 4);
    file.put(static_cast<uint32_t>(records.bytes.size()), 4);
    file.append(records.bytes);
    return file.bytes;
}

} // namespace

TEST(psd_layers_groups_and_rle_are_read)
{
    // Bottom first, as stored: a background, then a group holding one
    // layer (its end marker below its child).
    std::vector<uint8_t> rle = {0, 1, 0, 2, 0, 2, 0, 200, 0, 100};
    Spec background{"Background", 0, 0, 4, 6, 0, false, 255,
        {raw(24, 255), raw(24, 10), raw(24, 20), raw(24, 30)}};
    Spec end{"</Layer group>", 0, 0, 0, 0, 3, false, 255, {}};
    Spec inner{"Inner", 1, 2, 3, 3, 0, true, 128, {raw(2, 128), rle, raw(2, 0), raw(2, 0)}};
    Spec group{"Group", 0, 0, 0, 0, 1, false, 255, {}};
    const fla::psd::Image image = fla::psd::read(document({background, end, inner, group}));
    CHECK(image.ok && image.width == 6 && image.height == 4);
    CHECK(image.layers.size() == 2);
    CHECK(image.layers[0].group && image.layers[0].name == "Group" && image.layers[0].children.size() == 1);
    CHECK(image.layers[1].name == "Background" && image.layers[1].rgba.size() == 96);
    CHECK(image.layers[1].rgba[0] == 10 && image.layers[1].rgba[1] == 20 && image.layers[1].rgba[2] == 30 && image.layers[1].rgba[3] == 255);

    const fla::psd::Layer& layer = image.layers[0].children[0];
    CHECK(layer.name == "Inner" && layer.hidden && layer.opacity == 128);
    CHECK(layer.left == 2 && layer.top == 1 && layer.width == 1 && layer.height == 2);
    // Rows of the PackBits channel: 200, then 100.
    CHECK(layer.rgba[0] == 200 && layer.rgba[4] == 100 && layer.rgba[3] == 128);
}

TEST(psd_rejects_what_it_does_not_read)
{
    CHECK(!fla::psd::read({'n', 'o'}).ok);
    std::vector<uint8_t> bytes = document({});
    bytes[23] = 16; // 16-bit
    const fla::psd::Image deep = fla::psd::read(bytes);
    CHECK(!deep.ok && deep.notes.size() == 1);
    CHECK((fla::psd::unpackBits(std::vector<uint8_t>{0xfe, 7, 1, 8, 9}.data(), 5, 5) == std::vector<uint8_t>{7, 7, 7, 8, 9}));
}
