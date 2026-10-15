#include "test_util.h"

#include "../src/edit/psd_import.h"

#include "zlib.h"

#include <cstdint>

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
    /// Each channel's data, compression first; ids -1, 0, 1, 2 (and 3).
    std::vector<std::vector<uint8_t>> channels;
    std::string blend = "norm";
};

/// How the document is written.
struct Kind
{
    int depth = 8;
    int mode = 3;
    /// 2 for a large document (PSB).
    int version = 1;
};

std::vector<uint8_t> raw(size_t count, uint8_t value)
{
    std::vector<uint8_t> data = {0, 0};
    data.insert(data.end(), count, value);
    return data;
}

std::vector<uint8_t> document(const std::vector<Spec>& specs, const Kind& kind = Kind())
{
    const int lengthBytes = kind.version == 2 ? 8 : 4;
    const auto putLength = [&](Writer& out, size_t value) {
        if (lengthBytes == 8)
            out.put(0, 4);
        out.put(static_cast<uint32_t>(value), 4);
    };
    Writer records;
    records.put(static_cast<uint32_t>(specs.size()), 2);
    for (const Spec& spec : specs)
    {
        records.put(static_cast<uint32_t>(spec.top), 4);
        records.put(static_cast<uint32_t>(spec.left), 4);
        records.put(static_cast<uint32_t>(spec.bottom), 4);
        records.put(static_cast<uint32_t>(spec.right), 4);
        records.put(static_cast<uint32_t>(spec.channels.size()), 2);
        const int16_t ids[] = {-1, 0, 1, 2, 3};
        for (size_t c = 0; c < spec.channels.size(); ++c)
        {
            records.put(static_cast<uint16_t>(ids[c]), 2);
            putLength(records, spec.channels[c].size());
        }
        records.text("8BIM" + spec.blend);
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
    file.put(static_cast<uint32_t>(kind.version), 2);
    file.put(0, 6);
    file.put(3, 2);
    file.put(4, 4); // height
    file.put(6, 4); // width
    file.put(static_cast<uint32_t>(kind.depth), 2);
    file.put(static_cast<uint32_t>(kind.mode), 2);
    file.put(0, 4);
    file.put(0, 4);
    putLength(file, records.bytes.size() + static_cast<size_t>(lengthBytes) + 4);
    putLength(file, records.bytes.size());
    file.append(records.bytes);
    file.put(0, 4); // the global mask
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
    bytes[25] = 9; // Lab
    const fla::psd::Image lab = fla::psd::read(bytes);
    CHECK(!lab.ok && lab.notes.size() == 1);
    CHECK((fla::psd::unpackBits(std::vector<uint8_t>{0xfe, 7, 1, 8, 9}.data(), 5, 5) == std::vector<uint8_t>{7, 7, 7, 8, 9}));
}

TEST(psd_damaged_sizes_are_not_given_memory)
{
    // A document size past what Photoshop allows (here negative as an int).
    std::vector<uint8_t> bytes = document({});
    for (int i = 14; i < 22; ++i)
        bytes[static_cast<size_t>(i)] = 0xff;
    const fla::psd::Image huge = fla::psd::read(bytes);
    CHECK(!huge.ok && huge.layers.empty() && !huge.notes.empty());

    // A layer whose right - left overflows, and one far bigger than its few
    // bytes of channels could hold: both left out, the good one kept.
    Spec wrapped{"Wrapped", 0, -2147483647 - 1, 2, 2147483647, 0, false, 255, {raw(4, 255), raw(4, 1), raw(4, 2), raw(4, 3)}};
    Spec claimed{"Claimed", 0, 0, 16000, 16000, 0, false, 255, {raw(4, 255), raw(4, 1), raw(4, 2), raw(4, 3)}};
    Spec good{"Good", 0, 0, 2, 2, 0, false, 255, {raw(4, 255), raw(4, 1), raw(4, 2), raw(4, 3)}};
    const fla::psd::Image image = fla::psd::read(document({wrapped, claimed, good}));
    CHECK(image.layers.size() == 1 && image.layers[0].name == "Good" && image.layers[0].rgba.size() == 16);
}

namespace {

/// A channel of 16-bit samples, raw.
std::vector<uint8_t> raw16(size_t count, uint16_t value)
{
    std::vector<uint8_t> data = {0, 0};
    for (size_t i = 0; i < count; ++i)
    {
        data.push_back(static_cast<uint8_t>(value >> 8));
        data.push_back(static_cast<uint8_t>(value));
    }
    return data;
}

/// A channel ZIP-compressed with prediction: each row as differences.
std::vector<uint8_t> zipPredicted(const std::vector<uint8_t>& samples, int width)
{
    std::vector<uint8_t> deltas = samples;
    for (size_t row = 0; row * width < deltas.size(); ++row)
        for (int x = width - 1; x > 0; --x)
            deltas[row * width + x] = static_cast<uint8_t>(samples[row * width + x] - samples[row * width + x - 1]);
    uLongf size = compressBound(static_cast<uLong>(deltas.size()));
    std::vector<uint8_t> packed(size);
    compress(packed.data(), &size, deltas.data(), static_cast<uLong>(deltas.size()));
    packed.resize(size);
    std::vector<uint8_t> data = {0, 3};
    data.insert(data.end(), packed.begin(), packed.end());
    return data;
}

} // namespace

TEST(psd_deep_cmyk_large_and_zip_documents_are_read)
{
    // 16-bit RGB: the high byte of each sample.
    Spec deep{"Deep", 0, 0, 2, 3, 0, false, 255, {raw16(6, 0xffff), raw16(6, 0x8000), raw16(6, 0x4000), raw16(6, 0x1234)}};
    deep.blend = "mul ";
    Kind sixteen;
    sixteen.depth = 16;
    fla::psd::Image image = fla::psd::read(document({deep}, sixteen));
    CHECK(image.ok && image.layers.size() == 1);
    CHECK(image.layers[0].rgba[0] == 0x80 && image.layers[0].rgba[1] == 0x40 && image.layers[0].rgba[2] == 0x12);
    CHECK(image.layers[0].blend == "multiply");

    // CMYK, ink inverted as Photoshop stores it: no cyan, full magenta, no
    // yellow, no black is a pure magenta.
    Spec ink{"Ink", 0, 0, 1, 2, 0, false, 255, {raw(2, 255), raw(2, 255), raw(2, 0), raw(2, 255), raw(2, 255)}};
    Kind cmyk;
    cmyk.mode = 4;
    image = fla::psd::read(document({ink}, cmyk));
    CHECK(image.ok);
    CHECK(image.layers[0].rgba[0] == 255 && image.layers[0].rgba[1] == 0 && image.layers[0].rgba[2] == 255);

    // A large document, with eight-byte lengths, and ZIP with prediction.
    const std::vector<uint8_t> ramp = {10, 20, 30, 40, 50, 60};
    Spec zipped{"Zipped", 0, 0, 2, 3, 0, false, 255, {raw(6, 255), zipPredicted(ramp, 3), raw(6, 0), raw(6, 0)}};
    zipped.blend = "lddg";
    Kind large;
    large.version = 2;
    image = fla::psd::read(document({zipped}, large));
    CHECK(image.ok && image.layers.size() == 1);
    for (size_t p = 0; p < ramp.size(); ++p)
        CHECK(image.layers[0].rgba[p * 4] == ramp[p]);
    CHECK(image.layers[0].blend == "add");

    // A blend the FLA has no like of is drawn as Normal, and said so.
    Spec odd{"Odd", 0, 0, 1, 1, 0, false, 255, {raw(1, 255), raw(1, 1), raw(1, 2), raw(1, 3)}};
    odd.blend = "hue ";
    image = fla::psd::read(document({odd}));
    CHECK(image.ok && image.layers[0].blend.empty() && image.notes.size() == 1);
}
