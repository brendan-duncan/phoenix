// flash.utils: ByteArray, Endian, CompressionAlgorithm and Timer.

#include "native.h"
#include "amf.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace fla {
namespace play {
namespace script {

namespace {

/// flash.utils.Timer: its delay and count, and when it next fires.
struct TimerHost : DispatcherHost
{
    double delay = 0.0;
    int repeatCount = 0;
    int currentCount = 0;
    bool running = false;
    double due = 0.0;
};

ByteArrayHost& bytes(VM& vm, const Val& self)
{
    return hostOf<ByteArrayHost>(vm, self, "ByteArray");
}

/// \a count bytes from the position, or an EOFError.
const uint8_t* take(VM& vm, ByteArrayHost& b, size_t count)
{
    if (b.position > b.bytes.size() || b.bytes.size() - b.position < count)
        vm.throwError("flash.errors.EOFError", "Error #2030: End of file was encountered.", 2030);
    const uint8_t* at = b.bytes.data() + b.position;
    b.position += count;
    return at;
}

/// Room for \a count bytes at the position.
uint8_t* put(ByteArrayHost& b, size_t count)
{
    if (b.position + count > b.bytes.size())
        b.bytes.resize(b.position + count);
    uint8_t* at = b.bytes.data() + b.position;
    b.position += count;
    return at;
}

/// \a size bytes as a number in the array's byte order.
uint64_t readOrdered(VM& vm, ByteArrayHost& b, size_t size)
{
    const uint8_t* at = take(vm, b, size);
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i)
        value |= static_cast<uint64_t>(at[b.littleEndian ? i : size - 1 - i]) << (8 * i);
    return value;
}

void writeOrdered(ByteArrayHost& b, uint64_t value, size_t size)
{
    uint8_t* at = put(b, size);
    for (size_t i = 0; i < size; ++i)
        at[b.littleEndian ? i : size - 1 - i] = static_cast<uint8_t>(value >> (8 * i));
}

/// The bytes of a string as UTF-8, a BOM at the start skipped.
std::string utf8(const uint8_t* data, size_t size)
{
    std::string text(reinterpret_cast<const char*>(data), size);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    // Flash Player stops at a zero byte.
    const size_t zero = text.find('\0');
    if (zero != std::string::npos)
        text.resize(zero);
    return text;
}

/// zlib (or raw deflate) in and out.
bool deflateBytes(std::vector<uint8_t>& data, bool raw)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, raw ? -15 : 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    std::vector<uint8_t> out(deflateBound(&stream, static_cast<uLong>(data.size())) + 16);
    stream.next_in = data.data();
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    const size_t made = stream.total_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END)
        return false;
    out.resize(made);
    data.swap(out);
    return true;
}

bool inflateBytes(std::vector<uint8_t>& data, bool raw)
{
    z_stream stream{};
    if (inflateInit2(&stream, raw ? -15 : 15) != Z_OK)
        return false;
    std::vector<uint8_t> out;
    std::vector<uint8_t> chunk(65536);
    stream.next_in = data.data();
    stream.avail_in = static_cast<uInt>(data.size());
    int result = Z_OK;
    while (result == Z_OK)
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END)
            break;
        out.insert(out.end(), chunk.data(), chunk.data() + (chunk.size() - stream.avail_out));
        if (stream.avail_in == 0 && result == Z_OK && stream.avail_out != 0)
            break;
    }
    inflateEnd(&stream);
    if (result != Z_STREAM_END)
        return false;
    data.swap(out);
    return true;
}

} // namespace

bool ByteArrayHost::getIndex(const Val& key, Val& out)
{
    if (!key.isNumber())
        return false;
    const double i = key.num;
    out = i >= 0 && i < static_cast<double>(bytes.size()) && i == std::floor(i) ? Val::number(bytes[static_cast<size_t>(i)])
                                                                                  : Val();
    return true;
}

bool ByteArrayHost::setIndex(const Val& key, const Val& value)
{
    if (!key.isNumber() || key.num < 0 || key.num != std::floor(key.num) || key.num > 1e9)
        return false;
    const size_t i = static_cast<size_t>(key.num);
    if (i >= bytes.size())
        bytes.resize(i + 1);
    bytes[i] = static_cast<uint8_t>(value.toInt32());
    return true;
}

bool ByteArrayHost::hasIndex(const Val& key)
{
    return key.isNumber() && key.num >= 0 && key.num < static_cast<double>(bytes.size());
}

ByteArrayHost* byteArrayOf(const Val& value)
{
    return value.host<ByteArrayHost>();
}

void Bindings::runTimers()
{
    const std::vector<Val> running = timers;
    for (const Val& timer : running)
    {
        TimerHost* host = timer.host<TimerHost>();
        // A timer catches up a few firings a frame at most.
        for (int fired = 0; host && host->running && timerTime() >= host->due && fired < 3; ++fired)
        {
            ++host->currentCount;
            host->due += std::max(1.0, host->delay);
            send(timer, "flash.events.TimerEvent", "timer");
            if (host->repeatCount > 0 && host->currentCount >= host->repeatCount)
            {
                host->running = false;
                send(timer, "flash.events.TimerEvent", "timerComplete");
            }
        }
        if (host && host->running && timerTime() >= host->due)
            host->due = now() + std::max(1.0, host->delay);
    }
    timers.erase(std::remove_if(timers.begin(), timers.end(),
                     [](const Val& timer) {
                         const TimerHost* host = timer.host<TimerHost>();
                         return !host || !host->running;
                     }),
        timers.end());
}

void Bindings::defineUtils()
{
    vm.defineNativeClass("flash.utils.Endian")
        .constant("BIG_ENDIAN", text("bigEndian"))
        .constant("LITTLE_ENDIAN", text("littleEndian"))
        .final()
        .define();
    vm.defineNativeClass("flash.utils.CompressionAlgorithm")
        .constant("DEFLATE", text("deflate"))
        .constant("LZMA", text("lzma"))
        .constant("ZLIB", text("zlib"))
        .final()
        .define();
    vm.defineNativeClass("flash.utils.IDataInput").interface().define();
    vm.defineNativeClass("flash.utils.IDataOutput").interface().define();
    vm.defineNativeClass("flash.utils.IExternalizable").interface().define();

    // ByteArray.
    vm.defineNativeClass("flash.utils.ByteArray")
        .implements("flash.utils.IDataInput")
        .implements("flash.utils.IDataOutput")
        .constructor(NATIVE(this) {
            if (!self.host<ByteArrayHost>())
            {
                auto host = std::make_shared<ByteArrayHost>();
                // ByteArray.defaultObjectEncoding, as it is now.
                host->objectEncoding =
                    vm.getProperty(vm.classValue(classNamed("flash.utils.ByteArray").get()), "defaultObjectEncoding")
                        .toUint32();
                self->host = host;
            }
            return Val();
        })
        .property("length", NATIVE() { return number(static_cast<double>(bytes(vm, self).bytes.size())); }, NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            b.bytes.resize(args[0].toUint32());
            b.position = std::min(b.position, b.bytes.size());
            return Val();
        })
        .property("position", NATIVE() { return number(static_cast<double>(bytes(vm, self).position)); }, NATIVE() {
            bytes(vm, self).position = args[0].toUint32();
            return Val();
        })
        .getter("bytesAvailable", NATIVE() {
            const ByteArrayHost& b = bytes(vm, self);
            return number(b.position < b.bytes.size() ? static_cast<double>(b.bytes.size() - b.position) : 0.0);
        })
        .property("endian", NATIVE() { return text(bytes(vm, self).littleEndian ? "littleEndian" : "bigEndian"); },
            NATIVE() {
                const std::string endian = vm.toString(args[0]);
                if (endian != "littleEndian" && endian != "bigEndian")
                    vm.throwError("ArgumentError", "Error #2008: Parameter type must be one of the accepted values.", 2008);
                bytes(vm, self).littleEndian = endian == "littleEndian";
                return Val();
            })
        .property("objectEncoding", NATIVE() { return number(bytes(vm, self).objectEncoding); }, NATIVE() {
            bytes(vm, self).objectEncoding = args[0].toUint32();
            return Val();
        })
        .property("shareable", NATIVE() { return boolean(false); }, NATIVE() { return Val(); })
        .staticVar("defaultObjectEncoding", number(3))
        .method("clear", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            b.bytes.clear();
            b.position = 0;
            return Val();
        })
        .method("readBoolean", NATIVE() { return boolean(*take(vm, bytes(vm, self), 1) != 0); })
        .method("readByte", NATIVE() { return number(static_cast<int8_t>(*take(vm, bytes(vm, self), 1))); })
        .method("readUnsignedByte", NATIVE() { return number(*take(vm, bytes(vm, self), 1)); })
        .method("readShort", NATIVE() {
            return number(static_cast<int16_t>(readOrdered(vm, bytes(vm, self), 2)));
        })
        .method("readUnsignedShort", NATIVE() { return number(static_cast<uint16_t>(readOrdered(vm, bytes(vm, self), 2))); })
        .method("readInt", NATIVE() { return number(static_cast<int32_t>(readOrdered(vm, bytes(vm, self), 4))); })
        .method("readUnsignedInt", NATIVE() { return number(static_cast<uint32_t>(readOrdered(vm, bytes(vm, self), 4))); })
        .method("readFloat", NATIVE() {
            const uint32_t raw = static_cast<uint32_t>(readOrdered(vm, bytes(vm, self), 4));
            float value;
            std::memcpy(&value, &raw, 4);
            return number(value);
        })
        .method("readDouble", NATIVE() {
            const uint64_t raw = readOrdered(vm, bytes(vm, self), 8);
            double value;
            std::memcpy(&value, &raw, 8);
            return number(value);
        })
        .method("readUTF", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const size_t length = static_cast<uint16_t>(readOrdered(vm, b, 2));
            const uint8_t* at = take(vm, b, length);
            return text(utf8(at, length));
        })
        .method("readUTFBytes", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const size_t length = uintArg(args, 0);
            const uint8_t* at = take(vm, b, length);
            return text(utf8(at, length));
        }, 1)
        .method("readMultiByte", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const size_t length = uintArg(args, 0);
            const uint8_t* at = take(vm, b, length);
            std::string charset = stringArg(vm, args, 1, "utf-8");
            std::transform(charset.begin(), charset.end(), charset.begin(), ::tolower);
            if (charset == "iso-8859-1" || charset == "latin1" || charset == "us-ascii" || charset == "windows-1252")
            {
                std::u32string chars;
                for (size_t i = 0; i < length && at[i]; ++i)
                    chars += static_cast<char32_t>(at[i]);
                return text(display::toUtf8(chars));
            }
            return text(utf8(at, length));
        }, 2)
        .method("readBytes", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            ByteArrayHost* into = byteArrayOf(args[0]);
            if (!into)
                vm.throwError("TypeError", "Error #2007: Parameter bytes must be non-null.", 2007);
            const size_t offset = uintArg(args, 1);
            size_t length = uintArg(args, 2);
            if (length == 0)
                length = b.position < b.bytes.size() ? b.bytes.size() - b.position : 0;
            const uint8_t* at = take(vm, b, length);
            const std::vector<uint8_t> copy(at, at + length);
            if (into->bytes.size() < offset + length)
                into->bytes.resize(offset + length);
            std::copy(copy.begin(), copy.end(), into->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
            return Val();
        }, 1)
        .method("readObject", NATIVE(this) {
            ByteArrayHost& b = bytes(vm, self);
            const std::vector<uint8_t> copy = b.bytes;
            size_t position = b.position;
            BindingsAmf context(*this);
            const Val read = readAmf(context, copy, position, b.objectEncoding != 0);
            b.position = position;
            return read;
        })
        .method("writeBoolean", NATIVE() {
            *put(bytes(vm, self), 1) = args[0].toBoolean() ? 1 : 0;
            return Val();
        }, 1)
        .method("writeByte", NATIVE() {
            *put(bytes(vm, self), 1) = static_cast<uint8_t>(args[0].toInt32());
            return Val();
        }, 1)
        .method("writeShort", NATIVE() {
            writeOrdered(bytes(vm, self), static_cast<uint16_t>(args[0].toInt32()), 2);
            return Val();
        }, 1)
        .method("writeInt", NATIVE() {
            writeOrdered(bytes(vm, self), static_cast<uint32_t>(args[0].toInt32()), 4);
            return Val();
        }, 1)
        .method("writeUnsignedInt", NATIVE() {
            writeOrdered(bytes(vm, self), args[0].toUint32(), 4);
            return Val();
        }, 1)
        .method("writeFloat", NATIVE() {
            const float value = static_cast<float>(vm.toNumber(args[0]));
            uint32_t raw;
            std::memcpy(&raw, &value, 4);
            writeOrdered(bytes(vm, self), raw, 4);
            return Val();
        }, 1)
        .method("writeDouble", NATIVE() {
            const double value = vm.toNumber(args[0]);
            uint64_t raw;
            std::memcpy(&raw, &value, 8);
            writeOrdered(bytes(vm, self), raw, 8);
            return Val();
        }, 1)
        .method("writeUTF", NATIVE() {
            const std::string value = stringArg(vm, args, 0);
            if (value.size() > 65535)
                vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
            ByteArrayHost& b = bytes(vm, self);
            writeOrdered(b, value.size(), 2);
            uint8_t* at = put(b, value.size());
            std::memcpy(at, value.data(), value.size());
            return Val();
        }, 1)
        .method("writeUTFBytes", NATIVE() {
            const std::string value = stringArg(vm, args, 0);
            uint8_t* at = put(bytes(vm, self), value.size());
            std::memcpy(at, value.data(), value.size());
            return Val();
        }, 1)
        .method("writeMultiByte", NATIVE() {
            const std::string value = stringArg(vm, args, 0);
            std::string charset = stringArg(vm, args, 1, "utf-8");
            std::transform(charset.begin(), charset.end(), charset.begin(), ::tolower);
            ByteArrayHost& b = bytes(vm, self);
            if (charset == "iso-8859-1" || charset == "latin1" || charset == "us-ascii" || charset == "windows-1252")
            {
                for (const char32_t c : display::toCodePoints(value))
                    *put(b, 1) = c < 256 ? static_cast<uint8_t>(c) : '?';
                return Val();
            }
            uint8_t* at = put(b, value.size());
            std::memcpy(at, value.data(), value.size());
            return Val();
        }, 2)
        .method("writeBytes", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const ByteArrayHost* from = byteArrayOf(args[0]);
            if (!from)
                vm.throwError("TypeError", "Error #2007: Parameter bytes must be non-null.", 2007);
            const size_t offset = std::min<size_t>(uintArg(args, 1), from->bytes.size());
            size_t length = uintArg(args, 2);
            if (length == 0)
                length = from->bytes.size() - offset;
            if (offset + length > from->bytes.size())
                vm.throwError("RangeError", "Error #2006: The supplied index is out of bounds.", 2006);
            const std::vector<uint8_t> copy(from->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                from->bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
            uint8_t* at = put(b, copy.size());
            if (!copy.empty())
                std::memcpy(at, copy.data(), copy.size());
            return Val();
        }, 1)
        .method("writeObject", NATIVE(this) {
            std::vector<uint8_t> written;
            BindingsAmf context(*this);
            writeAmf(context, written, args[0], bytes(vm, self).objectEncoding != 0);
            ByteArrayHost& b = bytes(vm, self);
            uint8_t* at = put(b, written.size());
            if (!written.empty())
                std::memcpy(at, written.data(), written.size());
            return Val();
        }, 1)
        .method("compress", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const std::string algorithm = stringArg(vm, args, 0, "zlib");
            if (algorithm == "lzma")
                vm.throwError("flash.errors.IllegalOperationError", "Phoenix does not compress with LZMA.", 0);
            if (b.bytes.empty())
                return Val();
            deflateBytes(b.bytes, algorithm == "deflate");
            b.position = b.bytes.size();
            return Val();
        })
        .method("uncompress", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            const std::string algorithm = stringArg(vm, args, 0, "zlib");
            if (b.bytes.empty())
                return Val();
            if (algorithm == "lzma" || !inflateBytes(b.bytes, algorithm == "deflate"))
                vm.throwError("flash.errors.IOError", "Error #2058: There was an error decompressing the data.", 2058);
            b.position = 0;
            return Val();
        })
        .method("deflate", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            if (!b.bytes.empty())
                deflateBytes(b.bytes, true);
            b.position = b.bytes.size();
            return Val();
        })
        .method("inflate", NATIVE() {
            ByteArrayHost& b = bytes(vm, self);
            if (!b.bytes.empty() && !inflateBytes(b.bytes, true))
                vm.throwError("flash.errors.IOError", "Error #2058: There was an error decompressing the data.", 2058);
            b.position = 0;
            return Val();
        })
        .method("toString", NATIVE() {
            const ByteArrayHost& b = bytes(vm, self);
            return text(utf8(b.bytes.data(), b.bytes.size()));
        })
        .define();

    // Timer: fires as the movie's clock passes its delay.
    const auto timer = [](VM& vm, const Val& self) -> TimerHost& { return hostOf<TimerHost>(vm, self, "Timer"); };
    vm.defineNativeClass("flash.utils.Timer")
        .extends("flash.events.EventDispatcher")
        .constructor(NATIVE() {
            if (self.host<TimerHost>())
                return Val();
            const double delay = numberArg(vm, args, 0);
            if (!(delay >= 0.0) || std::isinf(delay))
                vm.throwError("RangeError", "Error #2066: The Timer delay specified is out of range.", 2066);
            auto host = std::make_shared<TimerHost>();
            host->dispatcher = std::make_shared<display::EventDispatcher>();
            host->dispatcher->script = self.ref;
            host->delay = delay;
            host->repeatCount = intArg(args, 1);
            self->host = host;
            return Val();
        })
        .property("delay", NATIVE(timer) { return number(timer(vm, self).delay); }, NATIVE(this, timer) {
            TimerHost& t = timer(vm, self);
            const double delay = vm.toNumber(args[0]);
            if (!(delay >= 0.0) || std::isinf(delay))
                vm.throwError("RangeError", "Error #2066: The Timer delay specified is out of range.", 2066);
            t.delay = delay;
            if (t.running)
                t.due = now() + std::max(1.0, delay);
            return Val();
        })
        .property("repeatCount", NATIVE(timer) { return number(timer(vm, self).repeatCount); }, NATIVE(timer) {
            TimerHost& t = timer(vm, self);
            t.repeatCount = args[0].toInt32();
            if (t.repeatCount > 0 && t.currentCount >= t.repeatCount)
                t.running = false;
            return Val();
        })
        .getter("currentCount", NATIVE(timer) { return number(timer(vm, self).currentCount); })
        .getter("running", NATIVE(timer) { return boolean(timer(vm, self).running); })
        .method("start", NATIVE(this, timer) {
            TimerHost& t = timer(vm, self);
            if (t.running)
                return Val();
            t.running = true;
            t.due = now() + std::max(1.0, t.delay);
            if (std::none_of(timers.begin(), timers.end(), [&self](const Val& v) { return v.ref == self.ref; }))
                timers.push_back(self);
            return Val();
        })
        .method("stop", NATIVE(timer) {
            timer(vm, self).running = false;
            return Val();
        })
        .method("reset", NATIVE(timer) {
            TimerHost& t = timer(vm, self);
            t.running = false;
            t.currentCount = 0;
            return Val();
        })
        .define();

    vm.defineFunction("flash.utils.escapeMultiByte", NATIVE() {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (const unsigned char c : stringArg(vm, args, 0))
        {
            if (std::isalnum(c) || std::strchr("@-_.*+/", c))
                out += static_cast<char>(c);
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return text(out);
    });
    vm.defineFunction("flash.utils.unescapeMultiByte", NATIVE() {
        const std::string in = stringArg(vm, args, 0);
        std::string out;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(in[i + 2])))
            {
                out += static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else
                out += in[i];
        }
        return text(out);
    });
}

} // namespace script
} // namespace play
} // namespace fla
