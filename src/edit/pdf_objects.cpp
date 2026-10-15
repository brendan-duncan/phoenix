#include "pdf_objects.h"

#include "zlib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fla {

namespace pdf {

const Object* Object::get(const std::string& key) const
{
    for (const auto& [name, value] : entries)
        if (name == key)
            return &value;
    return nullptr;
}

void Object::set(const std::string& key, Object value)
{
    for (auto& [name, existing] : entries)
        if (name == key)
        {
            existing = std::move(value);
            return;
        }
    entries.emplace_back(key, std::move(value));
}

void Object::remove(const std::string& key)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                      [&key](const std::pair<std::string, Object>& e) { return e.first == key; }),
        entries.end());
}

namespace {

bool isWhite(uint8_t c)
{
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

bool isDelimiter(uint8_t c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
        c == '%';
}

int hexValue(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// Reads PDF tokens and objects from bytes.
class Lexer
{
public:
    Lexer(const uint8_t* data, size_t size, size_t at = 0)
        : _data(data)
        , _size(size)
        , at(at)
    {}

    size_t at;

    void skipWhite()
    {
        while (at < _size)
        {
            if (isWhite(_data[at]))
                ++at;
            else if (_data[at] == '%')
            {
                while (at < _size && _data[at] != '\n' && _data[at] != '\r')
                    ++at;
            }
            else
                break;
        }
    }

    /// A bare word (keyword or number), or empty at a delimiter or the end.
    std::string word()
    {
        skipWhite();
        const size_t start = at;
        while (at < _size && !isWhite(_data[at]) && !isDelimiter(_data[at]))
            ++at;
        return std::string(reinterpret_cast<const char*>(_data + start), at - start);
    }

    bool peekWord(const char* expected)
    {
        const size_t saved = at;
        const bool match = word() == expected;
        at = saved;
        return match;
    }

    static bool numeric(const std::string& w)
    {
        if (w.empty())
            return false;
        for (const char c : w)
            if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '+')
                return false;
        return true;
    }

    /// One object; false at the end, a stray delimiter or nesting too deep.
    bool parse(Object& out, int depth = 0)
    {
        if (depth > 64)
            return false;
        skipWhite();
        if (at >= _size)
            return false;
        const uint8_t c = _data[at];
        if (c == '/')
        {
            ++at;
            std::string name;
            while (at < _size && !isWhite(_data[at]) && !isDelimiter(_data[at]))
            {
                if (_data[at] == '#' && at + 2 < _size && hexValue(_data[at + 1]) >= 0 && hexValue(_data[at + 2]) >= 0)
                {
                    name += static_cast<char>(hexValue(_data[at + 1]) * 16 + hexValue(_data[at + 2]));
                    at += 3;
                    continue;
                }
                name += static_cast<char>(_data[at++]);
            }
            out = Object();
            out.type = Object::Type::Name;
            out.text = name;
            return true;
        }
        if (c == '(')
        {
            out = Object();
            out.type = Object::Type::String;
            literal(out.text);
            return true;
        }
        if (c == '<' && at + 1 < _size && _data[at + 1] == '<')
        {
            at += 2;
            out = Object();
            out.type = Object::Type::Dictionary;
            for (;;)
            {
                skipWhite();
                if (at + 1 < _size && _data[at] == '>' && _data[at + 1] == '>')
                {
                    at += 2;
                    break;
                }
                Object key;
                if (!parse(key, depth + 1))
                    return true;
                if (key.type != Object::Type::Name)
                    continue;
                Object value;
                if (!parse(value, depth + 1))
                    return true;
                out.entries.emplace_back(key.text, std::move(value));
            }
            return true;
        }
        if (c == '<')
        {
            ++at;
            out = Object();
            out.type = Object::Type::String;
            int high = -1;
            while (at < _size && _data[at] != '>')
            {
                const int v = hexValue(_data[at++]);
                if (v < 0)
                    continue;
                if (high < 0)
                    high = v;
                else
                {
                    out.text += static_cast<char>(high * 16 + v);
                    high = -1;
                }
            }
            if (high >= 0)
                out.text += static_cast<char>(high * 16);
            ++at;
            return true;
        }
        if (c == '[')
        {
            ++at;
            out = Object();
            out.type = Object::Type::Array;
            for (;;)
            {
                skipWhite();
                if (at >= _size)
                    return true;
                if (_data[at] == ']')
                {
                    ++at;
                    return true;
                }
                Object item;
                if (!parse(item, depth + 1))
                {
                    ++at;
                    continue;
                }
                out.items.push_back(std::move(item));
            }
        }
        if (isDelimiter(c))
            return false;
        const std::string w = word();
        out = Object();
        if (w == "true" || w == "false")
        {
            out.type = Object::Type::Boolean;
            out.boolean = w == "true";
            return true;
        }
        if (w == "null")
            return true;
        if (numeric(w))
        {
            out.type = Object::Type::Number;
            out.number = std::atof(w.c_str());
            // "N G R": a reference.
            if (w.find('.') == std::string::npos && w[0] != '-')
            {
                const size_t saved = at;
                const std::string g = word();
                if (!g.empty() && numeric(g) && g.find('.') == std::string::npos && word() == "R")
                {
                    out.type = Object::Type::Reference;
                    out.reference = std::atoi(w.c_str());
                    out.generation = std::atoi(g.c_str());
                    return true;
                }
                at = saved;
            }
            return true;
        }
        // A keyword where a value belongs (a damaged file): as a name.
        out.type = Object::Type::Name;
        out.text = w;
        return !w.empty();
    }

private:
    void literal(std::string& text)
    {
        ++at;
        int depth = 1;
        while (at < _size)
        {
            const uint8_t c = _data[at++];
            if (c == '\\' && at < _size)
            {
                const uint8_t e = _data[at++];
                switch (e)
                {
                case 'n': text += '\n'; break;
                case 'r': text += '\r'; break;
                case 't': text += '\t'; break;
                case 'b': text += '\b'; break;
                case 'f': text += '\f'; break;
                case '\r':
                    if (at < _size && _data[at] == '\n')
                        ++at;
                    break;
                case '\n': break;
                default:
                    if (e >= '0' && e <= '7')
                    {
                        int value = e - '0';
                        for (int i = 0; i < 2 && at < _size && _data[at] >= '0' && _data[at] <= '7'; ++i)
                            value = value * 8 + (_data[at++] - '0');
                        text += static_cast<char>(value & 0xFF);
                    }
                    else
                        text += static_cast<char>(e);
                }
                continue;
            }
            if (c == '(')
                ++depth;
            else if (c == ')' && --depth == 0)
                return;
            text += static_cast<char>(c);
        }
    }

    const uint8_t* _data;
    size_t _size;
};

std::vector<uint8_t> inflateData(const std::vector<uint8_t>& data, bool raw)
{
    std::vector<uint8_t> out;
    if (data.empty())
        return out;
    z_stream stream{};
    if (inflateInit2(&stream, raw ? -15 : 15) != Z_OK)
        return out;
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    uint8_t buffer[65536];
    for (;;)
    {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        const int status = ::inflate(&stream, Z_NO_FLUSH);
        out.insert(out.end(), buffer, buffer + (sizeof(buffer) - stream.avail_out));
        if (status == Z_STREAM_END || (status != Z_OK && status != Z_BUF_ERROR))
            break;
        if (status == Z_BUF_ERROR && stream.avail_in == 0)
            break;
        if (stream.avail_in == 0 && stream.avail_out != 0)
            break;
    }
    inflateEnd(&stream);
    return out;
}

std::vector<uint8_t> flate(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out = inflateData(data, false);
    // No zlib header, or a damaged one: the deflate data on its own.
    if (out.empty() && data.size() > 2)
        out = inflateData(data, true);
    return out;
}

std::vector<uint8_t> unpredict(const std::vector<uint8_t>& data, const Object* parms)
{
    if (!parms || !parms->isDictionary())
        return data;
    const auto value = [parms](const char* key, int fallback) {
        const Object* v = parms->get(key);
        return v && v->type == Object::Type::Number ? static_cast<int>(v->number) : fallback;
    };
    const int predictor = value("Predictor", 1);
    if (predictor < 2)
        return data;
    const int colors = std::max(1, value("Colors", 1));
    const int bits = std::max(1, value("BitsPerComponent", 8));
    const int columns = std::max(1, value("Columns", 1));
    const size_t bpp = std::max<size_t>(1, static_cast<size_t>(colors * bits + 7) / 8);
    const size_t row = (static_cast<size_t>(colors) * bits * columns + 7) / 8;
    std::vector<uint8_t> out;
    if (predictor == 2)
    {
        // TIFF: each byte after the first pixel adds the one a pixel back
        // (8-bit samples).
        out = data;
        if (bits == 8)
            for (size_t start = 0; start + row <= out.size(); start += row)
                for (size_t i = bpp; i < row; ++i)
                    out[start + i] = static_cast<uint8_t>(out[start + i] + out[start + i - bpp]);
        return out;
    }
    // PNG: a filter byte a row.
    std::vector<uint8_t> previous(row, 0);
    for (size_t at = 0; at + 1 + row <= data.size(); at += row + 1)
    {
        const uint8_t type = data[at];
        std::vector<uint8_t> line(data.begin() + static_cast<std::ptrdiff_t>(at + 1),
            data.begin() + static_cast<std::ptrdiff_t>(at + 1 + row));
        for (size_t i = 0; i < row; ++i)
        {
            const int left = i >= bpp ? line[i - bpp] : 0;
            const int up = previous[i];
            const int corner = i >= bpp ? previous[i - bpp] : 0;
            int add = 0;
            switch (type)
            {
            case 1: add = left; break;
            case 2: add = up; break;
            case 3: add = (left + up) / 2; break;
            case 4:
            {
                const int p = left + up - corner;
                const int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - corner);
                add = pa <= pb && pa <= pc ? left : pb <= pc ? up : corner;
                break;
            }
            default: break;
            }
            line[i] = static_cast<uint8_t>(line[i] + add);
        }
        out.insert(out.end(), line.begin(), line.end());
        previous = line;
    }
    return out;
}

std::vector<uint8_t> asciiHex(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    int high = -1;
    for (const uint8_t c : data)
    {
        if (c == '>')
            break;
        const int v = hexValue(c);
        if (v < 0)
            continue;
        if (high < 0)
            high = v;
        else
        {
            out.push_back(static_cast<uint8_t>(high * 16 + v));
            high = -1;
        }
    }
    if (high >= 0)
        out.push_back(static_cast<uint8_t>(high * 16));
    return out;
}

std::vector<uint8_t> ascii85(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    uint32_t group = 0;
    int count = 0;
    size_t i = 0;
    if (data.size() >= 2 && data[0] == '<' && data[1] == '~')
        i = 2;
    for (; i < data.size(); ++i)
    {
        const uint8_t c = data[i];
        if (c == '~')
            break;
        if (isWhite(c))
            continue;
        if (c == 'z' && count == 0)
        {
            out.insert(out.end(), 4, 0);
            continue;
        }
        if (c < '!' || c > 'u')
            continue;
        group = group * 85 + (c - '!');
        if (++count == 5)
        {
            for (int s = 3; s >= 0; --s)
                out.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
            group = 0;
            count = 0;
        }
    }
    if (count > 1)
    {
        for (int k = count; k < 5; ++k)
            group = group * 85 + 84;
        for (int s = 3; s >= 5 - count; --s)
            out.push_back(static_cast<uint8_t>((group >> (s * 8)) & 0xFF));
    }
    return out;
}

std::vector<uint8_t> lzw(const std::vector<uint8_t>& data, bool early)
{
    std::vector<uint8_t> out;
    std::vector<std::vector<uint8_t>> table;
    const auto reset = [&table]() {
        table.clear();
        for (int i = 0; i < 256; ++i)
            table.push_back({static_cast<uint8_t>(i)});
        table.emplace_back();
        table.emplace_back();
    };
    reset();
    int width = 9;
    uint32_t buffer = 0;
    int bits = 0;
    int previous = -1;
    for (const uint8_t byte : data)
    {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= width)
        {
            const int code = static_cast<int>((buffer >> (bits - width)) & ((1u << width) - 1));
            bits -= width;
            if (code == 256)
            {
                reset();
                width = 9;
                previous = -1;
                continue;
            }
            if (code == 257)
                return out;
            std::vector<uint8_t> entry;
            if (code < static_cast<int>(table.size()))
                entry = table[static_cast<size_t>(code)];
            else if (previous >= 0)
            {
                entry = table[static_cast<size_t>(previous)];
                entry.push_back(entry.front());
            }
            else
                return out;
            out.insert(out.end(), entry.begin(), entry.end());
            if (previous >= 0)
            {
                std::vector<uint8_t> added = table[static_cast<size_t>(previous)];
                added.push_back(entry.front());
                table.push_back(added);
            }
            previous = code;
            const int limit = static_cast<int>(table.size()) + (early ? 1 : 0);
            if (limit >= (1 << width) && width < 12)
                ++width;
        }
    }
    return out;
}

std::vector<uint8_t> runLength(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < data.size();)
    {
        const int length = data[i++];
        if (length == 128)
            break;
        if (length < 128)
        {
            const size_t count = std::min<size_t>(static_cast<size_t>(length) + 1, data.size() - i);
            out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                data.begin() + static_cast<std::ptrdiff_t>(i + count));
            i += count;
        }
        else if (i < data.size())
            out.insert(out.end(), static_cast<size_t>(257 - length), data[i++]);
    }
    return out;
}

std::string canonicalFilter(const std::string& name)
{
    if (name == "Fl")
        return "FlateDecode";
    if (name == "AHx")
        return "ASCIIHexDecode";
    if (name == "A85")
        return "ASCII85Decode";
    if (name == "LZW")
        return "LZWDecode";
    if (name == "RL")
        return "RunLengthDecode";
    if (name == "DCT")
        return "DCTDecode";
    if (name == "CCF")
        return "CCITTFaxDecode";
    return name;
}

std::string numberText(double value)
{
    if (std::fabs(value - std::round(value)) < 1e-9 && std::fabs(value) < 1e15)
        return std::to_string(static_cast<long long>(std::llround(value)));
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.10g", value);
    return buffer;
}

void write(const Object& object, std::string& out, const std::string* remainingFilter, int depth)
{
    if (depth > 64)
        return;
    switch (object.type)
    {
    case Object::Type::Null: out += "null"; break;
    case Object::Type::Boolean: out += object.boolean ? "true" : "false"; break;
    case Object::Type::Number: out += numberText(object.number); break;
    case Object::Type::Reference:
        out += std::to_string(object.reference) + " " + std::to_string(object.generation) + " R";
        break;
    case Object::Type::String:
        out += '(';
        for (const char c : object.text)
        {
            if (c == '(' || c == ')' || c == '\\')
                out += '\\';
            if (c == '\r')
            {
                out += "\\r";
                continue;
            }
            if (c == '\n')
            {
                out += "\\n";
                continue;
            }
            out += c;
        }
        out += ')';
        break;
    case Object::Type::Name:
        out += '/';
        for (const char c : object.text)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 33 || u > 126 || isDelimiter(u) || c == '#')
            {
                char buffer[4];
                std::snprintf(buffer, sizeof(buffer), "#%02X", u);
                out += buffer;
            }
            else
                out += c;
        }
        break;
    case Object::Type::Array:
        out += "[ ";
        for (const Object& item : object.items)
        {
            write(item, out, nullptr, depth + 1);
            out += ' ';
        }
        out += ']';
        break;
    case Object::Type::Dictionary:
    case Object::Type::Stream:
        out += "<< ";
        for (const auto& [key, value] : object.entries)
        {
            if (object.type == Object::Type::Stream && remainingFilter && (key == "Filter" || key == "DecodeParms"))
                continue;
            Object name;
            name.type = Object::Type::Name;
            name.text = key;
            write(name, out, nullptr, depth + 1);
            out += ' ';
            write(value, out, nullptr, depth + 1);
            out += ' ';
        }
        if (object.type == Object::Type::Stream && remainingFilter && !remainingFilter->empty())
            out += "/Filter /" + *remainingFilter + " ";
        out += ">>";
        break;
    }
}

size_t findText(const std::vector<uint8_t>& bytes, const char* text, size_t from)
{
    const size_t n = std::strlen(text);
    if (bytes.size() < n)
        return std::string::npos;
    for (size_t i = from; i + n <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, text, n) == 0)
            return i;
    return std::string::npos;
}

} // namespace

std::string File::text(const Object& object, const std::string* remainingFilter)
{
    std::string out;
    write(object, out, remainingFilter, 0);
    return out;
}

bool File::open(std::vector<uint8_t> bytes, std::string& error)
{
    _bytes = std::move(bytes);
    _entries.clear();
    _cache.clear();
    if (findText(_bytes, "%PDF", 0) == std::string::npos || findText(_bytes, "%PDF", 0) > 1024)
    {
        error = "This is not a PDF file.";
        return false;
    }
    if (!readTables(error))
        return false;
    if (_trailer.get("Encrypt"))
    {
        error = "The file is encrypted.";
        return false;
    }
    return true;
}

bool File::readTables(std::string& error)
{
    // startxref, near the end.
    const size_t tail = _bytes.size() > 2048 ? _bytes.size() - 2048 : 0;
    size_t found = std::string::npos;
    for (size_t at = tail; (at = findText(_bytes, "startxref", at)) != std::string::npos; ++at)
        found = at;
    bool ok = false;
    if (found != std::string::npos)
    {
        Lexer lex(_bytes.data(), _bytes.size(), found + 9);
        const std::string offset = lex.word();
        if (Lexer::numeric(offset))
            ok = readTable(static_cast<size_t>(std::atoll(offset.c_str())), 0);
    }
    // No table, or one that points nowhere: the objects where they lie.
    if (!ok || !_trailer.get("Root"))
        rebuild();
    if (!_trailer.get("Root"))
    {
        error = "The file's catalog could not be found.";
        return false;
    }
    return true;
}

bool File::readTable(size_t offset, int depth)
{
    if (offset >= _bytes.size() || depth > 32)
        return false;
    Lexer lex(_bytes.data(), _bytes.size(), offset);
    if (lex.peekWord("xref"))
    {
        lex.word();
        for (;;)
        {
            const size_t saved = lex.at;
            const std::string first = lex.word();
            if (first == "trailer" || !Lexer::numeric(first))
            {
                lex.at = saved;
                break;
            }
            const int start = std::atoi(first.c_str());
            const int count = std::atoi(lex.word().c_str());
            for (int i = 0; i < count; ++i)
            {
                const std::string where = lex.word();
                lex.word(); // generation
                const std::string kind = lex.word();
                const int number = start + i;
                if (kind == "n" && !_entries.count(number))
                    _entries[number] = Entry{1, static_cast<size_t>(std::atoll(where.c_str())), 0, 0};
                else if (!_entries.count(number))
                    _entries[number] = Entry{0, 0, 0, 0};
            }
        }
        if (lex.word() != "trailer")
            return false;
        Object trailer;
        if (!lex.parse(trailer) || !trailer.isDictionary())
            return false;
        // The newest save's trailer counts; earlier ones fill in.
        if (_trailer.entries.empty())
            _trailer = trailer;
        if (const Object* hybrid = trailer.get("XRefStm"))
            readTable(static_cast<size_t>(hybrid->number), depth + 1);
        if (const Object* previous = trailer.get("Prev"))
            readTable(static_cast<size_t>(previous->number), depth + 1);
        return true;
    }
    // A cross-reference stream.
    Object stream;
    if (!parseAt(offset, stream) || stream.type != Object::Type::Stream)
        return false;
    const Object* type = stream.get("Type");
    if (!type || type->text != "XRef")
        return false;
    const std::vector<uint8_t> data = decoded(stream);
    std::vector<int> widths;
    if (const Object* w = stream.get("W"))
        for (const Object& item : w->items)
            widths.push_back(static_cast<int>(item.number));
    if (widths.size() < 3)
        return false;
    std::vector<int> index;
    if (const Object* list = stream.get("Index"))
        for (const Object& item : list->items)
            index.push_back(static_cast<int>(item.number));
    if (index.empty())
        index = {0, static_cast<int>(number(stream.get("Size"), 0))};
    const size_t row = static_cast<size_t>(widths[0] + widths[1] + widths[2]);
    size_t at = 0;
    const auto field = [&data](size_t from, int width) {
        uint64_t value = 0;
        for (int i = 0; i < width && from + static_cast<size_t>(i) < data.size(); ++i)
            value = (value << 8) | data[from + static_cast<size_t>(i)];
        return value;
    };
    for (size_t pair = 0; pair + 1 < index.size(); pair += 2)
        for (int i = 0; i < index[pair + 1] && at + row <= data.size(); ++i, at += row)
        {
            const int kind = widths[0] == 0 ? 1 : static_cast<int>(field(at, widths[0]));
            const uint64_t second = field(at + static_cast<size_t>(widths[0]), widths[1]);
            const uint64_t third = field(at + static_cast<size_t>(widths[0] + widths[1]), widths[2]);
            const int number = index[pair] + i;
            if (_entries.count(number))
                continue;
            if (kind == 1)
                _entries[number] = Entry{1, static_cast<size_t>(second), 0, 0};
            else if (kind == 2)
                _entries[number] = Entry{2, 0, static_cast<int>(second), static_cast<int>(third)};
            else
                _entries[number] = Entry{0, 0, 0, 0};
        }
    if (_trailer.entries.empty())
    {
        _trailer = stream;
        _trailer.type = Object::Type::Dictionary;
        _trailer.raw.clear();
    }
    if (const Object* previous = stream.get("Prev"))
        readTable(static_cast<size_t>(previous->number), depth + 1);
    return true;
}

void File::rebuild()
{
    // Every "N G obj", the last of a number winning, and the trailer or
    // cross-reference stream naming the catalog.
    std::map<int, Entry> found;
    for (size_t at = 0; (at = findText(_bytes, "obj", at)) != std::string::npos; at += 3)
    {
        if (at + 3 < _bytes.size() && !isWhite(_bytes[at + 3]) && !isDelimiter(_bytes[at + 3]))
            continue;
        // Back over " G N".
        size_t p = at;
        const auto back = [this, &p]() {
            while (p > 0 && isWhite(_bytes[p - 1]))
                --p;
            const size_t end = p;
            while (p > 0 && std::isdigit(_bytes[p - 1]))
                --p;
            return end > p ? std::atoi(std::string(_bytes.begin() + static_cast<std::ptrdiff_t>(p),
                                                 _bytes.begin() + static_cast<std::ptrdiff_t>(end))
                                           .c_str())
                           : -1;
        };
        const int generation = back();
        const int number = back();
        if (generation < 0 || number < 0)
            continue;
        found[number] = Entry{1, p, 0, 0};
    }
    for (const auto& [number, entry] : found)
        _entries[number] = entry;
    for (size_t at = 0; (at = findText(_bytes, "trailer", at)) != std::string::npos; at += 7)
    {
        Lexer lex(_bytes.data(), _bytes.size(), at + 7);
        Object trailer;
        if (lex.parse(trailer) && trailer.get("Root"))
            _trailer = trailer;
    }
    if (_trailer.get("Root"))
        return;
    for (const auto& [number, entry] : found)
    {
        const Object* candidate = object(number);
        if (!candidate || !candidate->isDictionary())
            continue;
        const Object* type = candidate->get("Type");
        if (type && type->text == "XRef" && candidate->get("Root"))
        {
            _trailer = *candidate;
            _trailer.type = Object::Type::Dictionary;
            _trailer.raw.clear();
        }
        else if (type && type->text == "Catalog")
        {
            Object root;
            root.type = Object::Type::Reference;
            root.reference = number;
            _trailer.set("Root", root);
        }
    }
}

bool File::parseAt(size_t offset, Object& out)
{
    Lexer lex(_bytes.data(), _bytes.size(), offset);
    const std::string number = lex.word();
    const std::string generation = lex.word();
    if (!Lexer::numeric(number) || !Lexer::numeric(generation) || lex.word() != "obj")
        return false;
    if (!lex.parse(out))
        return false;
    lex.skipWhite();
    if (!out.isDictionary() || !lex.peekWord("stream"))
        return true;
    // The data: after the keyword's line end, as long as /Length says,
    // or to "endstream" when that is wrong.
    lex.word();
    size_t start = lex.at;
    if (start < _bytes.size() && _bytes[start] == '\r')
        ++start;
    if (start < _bytes.size() && _bytes[start] == '\n')
        ++start;
    size_t length = 0;
    bool known = false;
    if (const Object* given = out.get("Length"))
    {
        if (given->type == Object::Type::Number)
        {
            length = static_cast<size_t>(std::max(0.0, given->number));
            known = true;
        }
        else if (given->type == Object::Type::Reference && !_loading[given->reference])
        {
            const Object* resolvedLength = object(given->reference);
            if (resolvedLength && resolvedLength->type == Object::Type::Number)
            {
                length = static_cast<size_t>(std::max(0.0, resolvedLength->number));
                known = true;
            }
        }
    }
    if (known && start + length <= _bytes.size())
    {
        Lexer check(_bytes.data(), _bytes.size(), start + length);
        if (!check.peekWord("endstream"))
            known = false;
    }
    if (!known)
    {
        const size_t end = findText(_bytes, "endstream", start);
        length = (end == std::string::npos ? _bytes.size() : end) - start;
        // The line end before the keyword is not data.
        if (length > 0 && _bytes[start + length - 1] == '\n')
            --length;
        if (length > 0 && _bytes[start + length - 1] == '\r')
            --length;
    }
    out.type = Object::Type::Stream;
    out.raw.assign(_bytes.begin() + static_cast<std::ptrdiff_t>(start),
        _bytes.begin() + static_cast<std::ptrdiff_t>(std::min(_bytes.size(), start + length)));
    return true;
}

bool File::fromObjectStream(int stream, int index, int number, Object& out)
{
    if (!_streamTables.count(stream))
    {
        const Object* holder = object(stream);
        if (!holder || holder->type != Object::Type::Stream)
            return false;
        std::vector<uint8_t> data = decoded(*holder);
        const int count = static_cast<int>(this->number(holder->get("N"), 0));
        const size_t first = static_cast<size_t>(this->number(holder->get("First"), 0));
        Lexer lex(data.data(), std::min(first, data.size()));
        std::vector<std::pair<int, size_t>> table;
        for (int i = 0; i < count; ++i)
        {
            const std::string n = lex.word();
            const std::string o = lex.word();
            if (!Lexer::numeric(n) || !Lexer::numeric(o))
                break;
            table.emplace_back(std::atoi(n.c_str()), first + static_cast<size_t>(std::atoll(o.c_str())));
        }
        _streamTables[stream] = table;
        _streamData[stream] = std::move(data);
    }
    const auto& table = _streamTables[stream];
    const std::vector<uint8_t>& data = _streamData[stream];
    size_t offset = std::string::npos;
    if (index >= 0 && index < static_cast<int>(table.size()) && table[static_cast<size_t>(index)].first == number)
        offset = table[static_cast<size_t>(index)].second;
    else
        for (const auto& [n, o] : table)
            if (n == number)
                offset = o;
    if (offset == std::string::npos || offset >= data.size())
        return false;
    Lexer lex(data.data(), data.size(), offset);
    return lex.parse(out);
}

const Object* File::object(int number)
{
    const auto cached = _cache.find(number);
    if (cached != _cache.end())
        return cached->second.get();
    const auto entry = _entries.find(number);
    if (entry == _entries.end() || entry->second.type == 0 || _loading[number])
        return nullptr;
    _loading[number] = true;
    auto made = std::make_unique<Object>();
    bool ok = entry->second.type == 1 ? parseAt(entry->second.offset, *made)
                                      : fromObjectStream(entry->second.stream, entry->second.index, number, *made);
    _loading[number] = false;
    if (!ok)
        return nullptr;
    const Object* result = made.get();
    _cache[number] = std::move(made);
    return result;
}

const Object& File::resolve(const Object& value)
{
    const Object* at = &value;
    for (int hops = 0; at->type == Object::Type::Reference && hops < 16; ++hops)
    {
        const Object* next = object(at->reference);
        if (!next)
            return _null;
        at = next;
    }
    return *at;
}

double File::number(const Object* value, double fallback)
{
    if (!value)
        return fallback;
    const Object& v = resolve(*value);
    return v.type == Object::Type::Number ? v.number : fallback;
}

std::vector<uint8_t> File::decoded(const Object& stream, std::string* remaining)
{
    if (remaining)
        remaining->clear();
    std::vector<uint8_t> data = stream.raw;
    std::vector<std::string> filters;
    std::vector<const Object*> parms;
    if (const Object* filter = stream.get("Filter"))
    {
        const Object& f = resolve(*filter);
        if (f.type == Object::Type::Name)
            filters.push_back(canonicalFilter(f.text));
        else
            for (const Object& item : f.items)
                filters.push_back(canonicalFilter(resolve(item).text));
    }
    if (const Object* given = stream.get("DecodeParms"))
    {
        const Object& p = resolve(*given);
        if (p.isDictionary())
            parms.push_back(&p);
        else
            for (const Object& item : p.items)
                parms.push_back(&resolve(item));
    }
    for (size_t i = 0; i < filters.size(); ++i)
    {
        const std::string& name = filters[i];
        const Object* parm = i < parms.size() ? parms[i] : nullptr;
        if (name == "FlateDecode")
            data = unpredict(flate(data), parm);
        else if (name == "LZWDecode")
        {
            const Object* early = parm && parm->isDictionary() ? parm->get("EarlyChange") : nullptr;
            data = unpredict(lzw(data, !early || early->number != 0), parm);
        }
        else if (name == "ASCIIHexDecode")
            data = asciiHex(data);
        else if (name == "ASCII85Decode")
            data = ascii85(data);
        else if (name == "RunLengthDecode")
            data = runLength(data);
        else
        {
            // An image codec (or one Phoenix does not know): the rest is
            // for whoever reads the image.
            if (remaining)
                *remaining = name;
            break;
        }
    }
    return data;
}

std::vector<Object> File::pages()
{
    std::vector<Object> list;
    const Object* root = _trailer.get("Root");
    if (!root)
        return list;
    const Object& catalog = resolve(*root);
    const Object* tree = catalog.get("Pages");
    if (!tree)
        return list;
    // Depth first, carrying what pages inherit.
    struct Pending
    {
        const Object* node;
        Object inherited;
        int depth;
    };
    std::vector<Pending> stack{{&resolve(*tree), Object(), 0}};
    std::vector<const Object*> seen;
    while (!stack.empty() && list.size() < 10000)
    {
        Pending pending = stack.back();
        stack.pop_back();
        const Object& node = *pending.node;
        if (!node.isDictionary() || pending.depth > 64 ||
            std::find(seen.begin(), seen.end(), &node) != seen.end())
            continue;
        seen.push_back(&node);
        Object inherited = pending.inherited;
        inherited.type = Object::Type::Dictionary;
        for (const char* key : {"Resources", "MediaBox", "CropBox", "Rotate"})
            if (const Object* value = node.get(key))
                inherited.set(key, resolve(*value));
        const Object* kids = node.get("Kids");
        const Object* type = node.get("Type");
        if (kids && (!type || type->text != "Page"))
        {
            const Object& array = resolve(*kids);
            for (auto it = array.items.rbegin(); it != array.items.rend(); ++it)
                stack.push_back({&resolve(*it), inherited, pending.depth + 1});
            continue;
        }
        Object page = node;
        page.type = Object::Type::Dictionary;
        page.raw.clear();
        for (const auto& [key, value] : inherited.entries)
            if (!page.get(key))
                page.set(key, value);
        list.push_back(std::move(page));
    }
    return list;
}

} // namespace pdf

} // namespace fla
