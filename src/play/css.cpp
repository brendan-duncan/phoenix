// The CSS of StyleSheet.parseCSS, read as Flash Player reads it: comments
// anywhere, selectors separated by commas (no spaces in one), and a value
// running to its semicolon, except that a colon in it ends it at the line's
// end; anything else not understood throws the whole sheet away.

#include "css.h"

#include <cctype>
#include <cstring>

namespace fla {
namespace play {

namespace {

class Reader
{
public:
    explicit Reader(const std::string& text)
        : _text(text)
    {}

    bool parse(std::vector<CssRule>& rules)
    {
        for (;;)
        {
            skipSpaceAndComments();
            if (_at >= _text.size())
                return true;
            std::vector<std::string> selectors;
            if (!readSelectors(selectors))
                return false;
            std::vector<std::pair<std::string, std::string>> properties;
            if (!readProperties(properties))
                return false;
            for (const std::string& selector : selectors)
            {
                // A selector again replaces what it had.
                bool replaced = false;
                for (CssRule& rule : rules)
                    if (rule.selector == selector)
                    {
                        rule.properties = properties;
                        replaced = true;
                    }
                if (!replaced)
                    rules.push_back({selector, properties});
            }
        }
    }

private:
    static bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

    char peek(size_t ahead = 0) const { return _at + ahead < _text.size() ? _text[_at + ahead] : '\0'; }
    bool atEnd() const { return _at >= _text.size(); }

    bool skipComment()
    {
        if (peek() != '/' || peek(1) != '*')
            return false;
        _at += 2;
        while (!(peek() == '*' && peek(1) == '/'))
        {
            ++_at;
            if (atEnd())
                return false;
        }
        _at += 2;
        return true;
    }
    bool skipSpaceAndComments()
    {
        bool found = false;
        for (;;)
        {
            if (skipComment())
                found = true;
            else if (!atEnd() && isSpace(_text[_at]))
            {
                ++_at;
                found = true;
            }
            else
                return found;
        }
    }
    std::string readUntil(const char* stops)
    {
        const size_t start = _at;
        while (!atEnd() && !std::strchr(stops, _text[_at]))
            ++_at;
        return _text.substr(start, _at - start);
    }

    bool readSelectors(std::vector<std::string>& out)
    {
        for (;;)
        {
            skipSpaceAndComments();
            const std::string name = readUntil("{, \n\r\t");
            if (!name.empty())
                out.push_back(name);
            skipSpaceAndComments();
            if (atEnd())
                return false;
            if (peek() == '{')
            {
                ++_at;
                if (out.empty())
                    out.push_back(std::string());
                return true;
            }
            if (peek() == ',')
            {
                ++_at;
                continue;
            }
            // A space inside a selector's name.
            return false;
        }
    }

    bool readProperties(std::vector<std::pair<std::string, std::string>>& out)
    {
        const auto keep = [&out](const std::string& name, const std::string& value) {
            for (auto& property : out)
                if (property.first == name)
                {
                    property.second = value;
                    return;
                }
            out.emplace_back(name, value);
        };
        for (;;)
        {
            skipSpaceAndComments();
            if (atEnd())
                return true;
            if (peek() == '}')
            {
                ++_at;
                return true;
            }
            const size_t nameStart = _at;
            std::string name = readUntil(": \n\r\t");
            if (skipSpaceAndComments())
            {
                if (peek() == ':')
                    name = _text.substr(nameStart, _at - nameStart);
                else if (!atEnd())
                    return false;
            }
            if (atEnd())
                return false;
            ++_at; // :
            skipSpaceAndComments();
            const size_t valueStart = _at;
            std::string value = readUntil(";:}");
            for (;;)
            {
                if (atEnd())
                    return false;
                const char c = peek();
                if (c == ':')
                {
                    // A colon in the value: it runs to the line's end, if
                    // there is one; else to its ; or }.
                    _at = valueStart;
                    const std::string line = readUntil("\n\r");
                    if (!atEnd())
                    {
                        ++_at;
                        keep(name, line);
                        break;
                    }
                    _at = valueStart;
                    value = readUntil(";}");
                    continue;
                }
                if (c == ';')
                {
                    ++_at;
                    keep(name, value);
                    break;
                }
                // }: the value up to a line's end, if it has one.
                ++_at;
                const size_t lineEnd = value.find_first_of("\n\r");
                keep(name, lineEnd == std::string::npos ? value : value.substr(0, lineEnd));
                return true;
            }
        }
    }

    const std::string& _text;
    size_t _at = 0;
};

} // namespace

bool parseCss(const std::string& text, std::vector<CssRule>& rules)
{
    std::vector<CssRule> read;
    if (!Reader(text).parse(read))
        return false;
    rules = std::move(read);
    return true;
}

std::string camelCase(const std::string& name)
{
    std::string out;
    bool upper = false;
    for (const char c : name)
    {
        if (c == '-' && !upper)
        {
            upper = true;
            continue;
        }
        out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return out;
}

std::string fontList(const std::string& families)
{
    std::string out;
    size_t at = 0;
    while (at < families.size())
    {
        while (at < families.size() && families[at] == ' ')
            ++at;
        const size_t comma = families.find(',', at);
        std::string value = families.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        at = comma == std::string::npos ? families.size() : comma + 1;
        if (value == "mono")
            value = "_typewriter";
        else if (value == "sans-serif")
            value = "_sans";
        else if (value == "serif")
            value = "_serif";
        if (!value.empty())
        {
            if (!out.empty())
                out += ',';
            out += value;
        }
    }
    return out;
}

} // namespace play
} // namespace fla
