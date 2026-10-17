#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

namespace {

const Value kNull;
const std::string kEmptyString;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;

void appendUtf8(std::string& out, unsigned code)
{
    if (code < 0x80)
        out += static_cast<char>(code);
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser
{
public:
    explicit Parser(const std::string& text) : _text(text) {}

    bool parse(Value& out)
    {
        if (!value(out, 0))
            return false;
        space();
        return _at == _text.size();
    }

private:
    void space()
    {
        while (_at < _text.size() && (_text[_at] == ' ' || _text[_at] == '\t' || _text[_at] == '\n' || _text[_at] == '\r'))
            ++_at;
    }

    bool literal(const char* word)
    {
        size_t i = 0;
        for (; word[i]; ++i)
        {
            if (_at + i >= _text.size() || _text[_at + i] != word[i])
                return false;
        }
        _at += i;
        return true;
    }

    bool hex4(unsigned& code)
    {
        if (_at + 4 > _text.size())
            return false;
        code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = _text[_at++];
            code <<= 4;
            if (c >= '0' && c <= '9')
                code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                code |= static_cast<unsigned>(c - 'A' + 10);
            else
                return false;
        }
        return true;
    }

    bool string(std::string& out)
    {
        ++_at; // the opening quote
        while (_at < _text.size())
        {
            const char c = _text[_at++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (_at >= _text.size())
                return false;
            const char e = _text[_at++];
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned code = 0;
                if (!hex4(code))
                    return false;
                // A surrogate pair makes one character.
                if (code >= 0xD800 && code < 0xDC00 && _at + 6 <= _text.size() && _text[_at] == '\\' &&
                    _text[_at + 1] == 'u')
                {
                    const size_t back = _at;
                    _at += 2;
                    unsigned low = 0;
                    if (hex4(low) && low >= 0xDC00 && low < 0xE000)
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    else
                        _at = back;
                }
                appendUtf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool value(Value& out, int depth)
    {
        if (depth > 512)
            return false;
        space();
        if (_at >= _text.size())
            return false;
        const char c = _text[_at];
        if (c == '{')
        {
            ++_at;
            Value::Object object;
            space();
            if (_at < _text.size() && _text[_at] == '}')
            {
                ++_at;
                out = Value(std::move(object));
                return true;
            }
            while (true)
            {
                space();
                if (_at >= _text.size() || _text[_at] != '"')
                    return false;
                std::string key;
                if (!string(key))
                    return false;
                space();
                if (_at >= _text.size() || _text[_at] != ':')
                    return false;
                ++_at;
                Value member;
                if (!value(member, depth + 1))
                    return false;
                object[key] = std::move(member);
                space();
                if (_at < _text.size() && _text[_at] == ',')
                {
                    ++_at;
                    continue;
                }
                if (_at < _text.size() && _text[_at] == '}')
                {
                    ++_at;
                    out = Value(std::move(object));
                    return true;
                }
                return false;
            }
        }
        if (c == '[')
        {
            ++_at;
            Value::Array array;
            space();
            if (_at < _text.size() && _text[_at] == ']')
            {
                ++_at;
                out = Value(std::move(array));
                return true;
            }
            while (true)
            {
                Value item;
                if (!value(item, depth + 1))
                    return false;
                array.push_back(std::move(item));
                space();
                if (_at < _text.size() && _text[_at] == ',')
                {
                    ++_at;
                    continue;
                }
                if (_at < _text.size() && _text[_at] == ']')
                {
                    ++_at;
                    out = Value(std::move(array));
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            std::string s;
            if (!string(s))
                return false;
            out = Value(std::move(s));
            return true;
        }
        if (literal("true"))
        {
            out = Value(true);
            return true;
        }
        if (literal("false"))
        {
            out = Value(false);
            return true;
        }
        if (literal("null"))
        {
            out = Value();
            return true;
        }
        const char* start = _text.c_str() + _at;
        char* end = nullptr;
        const double n = std::strtod(start, &end);
        if (end == start)
            return false;
        _at += static_cast<size_t>(end - start);
        out = Value(n);
        return true;
    }

    const std::string& _text;
    size_t _at = 0;
};

} // namespace

const std::string& Value::toString() const
{
    return _type == Type::String ? _string : kEmptyString;
}

const Value::Array& Value::toArray() const
{
    return _type == Type::Array && _array ? *_array : kEmptyArray;
}

const Value::Object& Value::toObject() const
{
    return _type == Type::Object && _object ? *_object : kEmptyObject;
}

const Value& Value::operator[](const std::string& key) const
{
    if (_type != Type::Object || !_object)
        return kNull;
    const auto it = _object->find(key);
    return it == _object->end() ? kNull : it->second;
}

const Value& Value::operator[](size_t index) const
{
    if (_type != Type::Array || !_array || index >= _array->size())
        return kNull;
    return (*_array)[index];
}

Value& Value::set(const std::string& key, Value value)
{
    if (_type != Type::Object || !_object)
    {
        _type = Type::Object;
        _object = std::make_shared<Object>();
    }
    else if (_object.use_count() > 1)
        _object = std::make_shared<Object>(*_object);
    Value& slot = (*_object)[key];
    slot = std::move(value);
    return slot;
}

void Value::push(Value value)
{
    if (_type != Type::Array || !_array)
    {
        _type = Type::Array;
        _array = std::make_shared<Array>();
    }
    else if (_array.use_count() > 1)
        _array = std::make_shared<Array>(*_array);
    _array->push_back(std::move(value));
}

std::string quote(const std::string& text)
{
    std::string out = "\"";
    for (const char ch : text)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            }
            else
                out += ch;
        }
    }
    out += '"';
    return out;
}

void Value::dumpTo(std::string& out) const
{
    switch (_type)
    {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += _bool ? "true" : "false"; break;
    case Type::Number:
    {
        if (!std::isfinite(_number))
        {
            out += "null";
            break;
        }
        char buffer[32];
        if (_number == std::floor(_number) && std::fabs(_number) < 9007199254740992.0)
            std::snprintf(buffer, sizeof(buffer), "%.0f", _number);
        else
            std::snprintf(buffer, sizeof(buffer), "%.17g", _number);
        out += buffer;
        break;
    }
    case Type::String: out += quote(_string); break;
    case Type::Array:
    {
        out += '[';
        bool first = true;
        for (const Value& item : toArray())
        {
            if (!first)
                out += ',';
            first = false;
            item.dumpTo(out);
        }
        out += ']';
        break;
    }
    case Type::Object:
    {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : toObject())
        {
            if (!first)
                out += ',';
            first = false;
            out += quote(key);
            out += ':';
            item.dumpTo(out);
        }
        out += '}';
        break;
    }
    }
}

std::string Value::dump() const
{
    std::string out;
    dumpTo(out);
    return out;
}

Value Value::parse(const std::string& text, bool* ok)
{
    Value out;
    Parser parser(text);
    const bool parsed = parser.parse(out);
    if (ok)
        *ok = parsed;
    return parsed ? out : Value();
}

} // namespace json
