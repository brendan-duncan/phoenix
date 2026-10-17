#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// The protobuf wire format, which the Phoenix format encodes its model in
/// (docs/phx-format.html): fields of a key (field number and wire type)
/// and a value. A small codec of Phoenix's own, so no protobuf library is
/// needed. Free of Qt.
namespace phx {

enum class WireType : uint32_t
{
    Varint = 0,
    Fixed64 = 1,
    Bytes = 2,
    Fixed32 = 5,
};

/// Appends fields to a buffer.
class Writer
{
public:
    std::string& buffer() { return _out; }
    const std::string& buffer() const { return _out; }
    bool empty() const { return _out.empty(); }

    void varint(uint64_t value)
    {
        while (value >= 0x80)
        {
            _out.push_back(static_cast<char>(static_cast<uint8_t>(value) | 0x80));
            value >>= 7;
        }
        _out.push_back(static_cast<char>(value));
    }

    void key(uint32_t field, WireType type) { varint((static_cast<uint64_t>(field) << 3) | static_cast<uint32_t>(type)); }

    void uint(uint32_t field, uint64_t value)
    {
        key(field, WireType::Varint);
        varint(value);
    }
    /// int32 and int64 are written as two's complement varints, as protobuf
    /// does; sint as zigzag.
    void int64(uint32_t field, int64_t value) { uint(field, static_cast<uint64_t>(value)); }
    void sint(uint32_t field, int64_t value) { uint(field, zigzag(value)); }
    void boolean(uint32_t field, bool value) { uint(field, value ? 1 : 0); }

    void fixed32(uint32_t field, uint32_t value)
    {
        key(field, WireType::Fixed32);
        raw32(value);
    }
    void fixed64(uint32_t field, uint64_t value)
    {
        key(field, WireType::Fixed64);
        raw64(value);
    }
    void real(uint32_t field, double value)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        fixed64(field, bits);
    }

    void bytes(uint32_t field, const void* data, size_t size)
    {
        key(field, WireType::Bytes);
        varint(size);
        _out.append(static_cast<const char*>(data), size);
    }
    void string(uint32_t field, const std::string& value) { bytes(field, value.data(), value.size()); }
    /// A nested message, already encoded.
    void message(uint32_t field, const Writer& nested) { bytes(field, nested._out.data(), nested._out.size()); }

    /// Fields copied as they were read: the unknown fields a reader kept.
    void rawFields(const std::string& encoded) { _out += encoded; }

    void raw32(uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            _out.push_back(static_cast<char>(value >> (8 * i)));
    }
    void raw64(uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            _out.push_back(static_cast<char>(value >> (8 * i)));
    }

    static uint64_t zigzag(int64_t value)
    {
        return (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
    }

private:
    std::string _out;
};

/// A packed list: values written one after another, then put in a field.
class Packed
{
public:
    void varint(uint64_t value) { _w.varint(value); }
    void sint(int64_t value) { _w.varint(Writer::zigzag(value)); }
    void real(double value)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        _w.raw64(bits);
    }
    void fixed32(uint32_t value) { _w.raw32(value); }
    bool empty() const { return _w.empty(); }
    /// Writes the list as field \a field of \a out, when it has anything.
    void writeTo(Writer& out, uint32_t field) const
    {
        if (!_w.empty())
            out.bytes(field, _w.buffer().data(), _w.buffer().size());
    }

private:
    Writer _w;
};

/// Reads the fields of one message, in order.
class Reader
{
public:
    Reader(const void* data, size_t size)
        : _p(static_cast<const uint8_t*>(data))
        , _end(static_cast<const uint8_t*>(data) + size)
    {}
    explicit Reader(const std::string& data)
        : Reader(data.data(), data.size())
    {}

    /// Moves to the next field; false at the end of the message, or when the
    /// data is damaged (see failed()).
    bool next()
    {
        if (_failed || _p >= _end)
            return false;
        _fieldStart = _p;
        uint64_t k;
        if (!readVarint(k) || (k >> 3) == 0 || (k >> 3) > 0x1FFFFFFF)
            return fail();
        _field = static_cast<uint32_t>(k >> 3);
        _type = static_cast<WireType>(k & 7);
        switch (_type)
        {
        case WireType::Varint:
            if (!readVarint(_value))
                return fail();
            break;
        case WireType::Fixed64:
            if (_end - _p < 8)
                return fail();
            _value = load64(_p);
            _p += 8;
            break;
        case WireType::Fixed32:
            if (_end - _p < 4)
                return fail();
            _value = load32(_p);
            _p += 4;
            break;
        case WireType::Bytes:
        {
            uint64_t size;
            if (!readVarint(size) || size > static_cast<uint64_t>(_end - _p))
                return fail();
            _bytes = _p;
            _size = static_cast<size_t>(size);
            _p += size;
            break;
        }
        default:
            // Groups (3, 4) and the reserved types are not used.
            return fail();
        }
        return true;
    }

    uint32_t field() const { return _field; }
    WireType type() const { return _type; }
    bool failed() const { return _failed; }

    uint64_t uint() const { return _value; }
    int64_t int64() const { return static_cast<int64_t>(_value); }
    int32_t int32() const { return static_cast<int32_t>(_value); }
    int64_t sint() const { return unzigzag(_value); }
    bool boolean() const { return _value != 0; }
    uint32_t fixed32() const { return static_cast<uint32_t>(_value); }
    uint64_t fixed64() const { return _value; }
    double real() const
    {
        double value;
        std::memcpy(&value, &_value, sizeof(value));
        return value;
    }
    std::string string() const { return std::string(reinterpret_cast<const char*>(_bytes), _size); }
    const uint8_t* bytes() const { return _bytes; }
    size_t size() const { return _size; }
    /// The nested message the current field holds.
    Reader message() const { return Reader(_bytes, _size); }

    /// The current field as it was encoded, key and value, for keeping a
    /// field the reader does not know.
    void keep(std::string& unknown) const
    {
        unknown.append(reinterpret_cast<const char*>(_fieldStart), static_cast<size_t>(_p - _fieldStart));
    }

    /// The values of a packed list (or, for a writer that did not pack, the
    /// one value of this field), handed to \a each.
    template <typename Each>
    void varints(Each each) const
    {
        if (_type != WireType::Bytes)
        {
            each(_value);
            return;
        }
        Reader list(_bytes, _size);
        uint64_t v;
        while (list._p < list._end && list.readVarint(v))
            each(v);
    }
    template <typename Each>
    void reals(Each each) const
    {
        if (_type != WireType::Bytes)
        {
            each(real());
            return;
        }
        for (size_t i = 0; i + 8 <= _size; i += 8)
        {
            const uint64_t bits = load64(_bytes + i);
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            each(value);
        }
    }
    template <typename Each>
    void fixed32s(Each each) const
    {
        if (_type != WireType::Bytes)
        {
            each(fixed32());
            return;
        }
        for (size_t i = 0; i + 4 <= _size; i += 4)
            each(load32(_bytes + i));
    }

    static int64_t unzigzag(uint64_t value)
    {
        return static_cast<int64_t>(value >> 1) ^ -static_cast<int64_t>(value & 1);
    }
    static uint32_t load32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[3]) << 24);
    }
    static uint64_t load64(const uint8_t* p)
    {
        return static_cast<uint64_t>(load32(p)) | (static_cast<uint64_t>(load32(p + 4)) << 32);
    }

private:
    bool readVarint(uint64_t& value)
    {
        value = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            if (_p >= _end)
                return false;
            const uint8_t byte = *_p++;
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80))
                return true;
        }
        return false;
    }
    bool fail()
    {
        _failed = true;
        return false;
    }

    const uint8_t* _p;
    const uint8_t* _end;
    const uint8_t* _fieldStart = nullptr;
    uint32_t _field = 0;
    WireType _type = WireType::Varint;
    uint64_t _value = 0;
    const uint8_t* _bytes = nullptr;
    size_t _size = 0;
    bool _failed = false;
};

} // namespace phx
