// E4X (ECMAScript for XML), which QuickJS does not have: a source rewrite of
// its syntax into plain JavaScript, and a small XML and XMLList in
// JavaScript for the rewritten code to run on.
//
// Property access on XML values goes through a Proxy, so x.item, x.item[0]
// and x.item.length() need no rewrite. The rewrite turns what JavaScript
// cannot parse into property names the Proxy knows: x.@id is x["@?id"],
// x..item is x["..item"], x.* is x["*"].
//
// A filter x.(pred) runs pred once per element, inside with (element), so
// bare names find the element's children as they did. Rewriting bare names
// instead would need to know which names the script declares; with does it
// at run time, as E4X itself did.
//
// Reading an attribute gives its value as a string, so x.@a == y.@b and
// x.@a.split(",") behave; followed by a member access or index it stays an
// XMLList (x.@a.length(), x.@a[0]).

#include "e4x.h"

#include <cstring>
#include <algorithm>

namespace jsfl {
namespace e4x {

namespace {

bool identStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

bool identPart(char c)
{
    return identStart(c) || (c >= '0' && c <= '9');
}

bool digit(char c)
{
    return c >= '0' && c <= '9';
}

bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool xmlNamePart(char c)
{
    return identPart(c) || c == '-' || c == ':' || c == '.';
}

/// From \a at, past a string or template literal starting there.
size_t skipString(const std::string& s, size_t at)
{
    const char quote = s[at];
    size_t i = at + 1;
    while (i < s.size() && s[i] != quote)
    {
        if (s[i] == '\\')
            ++i;
        else if (quote != '`' && s[i] == '\n')
            break;
        ++i;
    }
    return std::min(i + 1, s.size());
}

/// A JavaScript string literal of \a text.
std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '\\' || c == '"')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out + "\"";
}

/// The pieces of an XML literal: its text as string literals, its holes as
/// calls, joined with +. A new line in the literal starts a new line in the
/// result too, so the lines after it keep their numbers.
class Literal
{
public:
    void text(const std::string& part)
    {
        for (const char c : part)
        {
            _pending += c;
            // A line ends at \n, \r\n or a lone \r.
            if (c == '\r' || (c == '\n' && !_afterReturn))
            {
                flush();
                ++_lines;
            }
            else if (c == '\n')
                flush();
            _afterReturn = c == '\r';
        }
    }
    void hole(const std::string& code, int kind)
    {
        flush();
        join();
        _expression += "__e4x.hole(" + code + ", " + std::to_string(kind) + ")";
    }
    std::string finish()
    {
        flush();
        const std::string result = _any ? _expression : "\"\"";
        return result + std::string(static_cast<size_t>(_lines), '\n');
    }

private:
    void join()
    {
        if (_any)
            _expression += _lines ? " +" : " + ";
        _expression += std::string(static_cast<size_t>(_lines), '\n');
        _lines = 0;
        _any = true;
    }
    void flush()
    {
        if (_pending.empty())
            return;
        join();
        _expression += quoted(_pending);
        _pending.clear();
    }

    std::string _expression;
    std::string _pending;
    bool _any = false;
    /// New lines since the last piece.
    int _lines = 0;
    bool _afterReturn = false;
};

class Rewriter
{
public:
    explicit Rewriter(const std::string& source)
        : s(source)
    {
        out.reserve(s.size() + s.size() / 8);
    }

    std::string run()
    {
        code(0);
        // An unmatched closer ends code() early; carry on past it.
        while (i < s.size())
        {
            out += s[i++];
            code(0);
        }
        return out;
    }

private:
    /// Copies and rewrites code until the end, or until an unmatched
    /// \a closer (')' or '}'), which it leaves for the caller.
    void code(char closer)
    {
        int depth = 0;
        while (i < s.size())
        {
            // Given up on: the rest as written.
            if (gaveUp || spent())
            {
                copyTo(s.size());
                return;
            }
            const char c = s[i];
            const char next = i + 1 < s.size() ? s[i + 1] : '\0';
            if (c == '/' && next == '/')
            {
                const size_t end = s.find_first_of("\r\n", i);
                copyTo(end == std::string::npos ? s.size() : end);
                continue;
            }
            if (c == '/' && next == '*')
            {
                const size_t end = s.find("*/", i + 2);
                copyTo(end == std::string::npos ? s.size() : end + 2);
                continue;
            }
            if (c == '"' || c == '\'' || c == '`')
            {
                copyTo(skipString(s, i));
                regexAllowed = false;
                continue;
            }
            if (c == '/' && regexAllowed)
            {
                size_t j = i + 1;
                bool inClass = false;
                while (j < s.size() && s[j] != '\n' && s[j] != '\r' && (inClass || s[j] != '/'))
                {
                    if (s[j] == '\\')
                        ++j;
                    else if (s[j] == '[')
                        inClass = true;
                    else if (s[j] == ']')
                        inClass = false;
                    ++j;
                }
                j = std::min(j + 1, s.size());
                while (j < s.size() && identPart(s[j]))
                    ++j;
                copyTo(j);
                regexAllowed = false;
                continue;
            }
            if (digit(c) || (c == '.' && digit(next)))
            {
                number();
                continue;
            }
            if (identStart(c))
            {
                word();
                continue;
            }
            if (c == '.')
            {
                dot();
                continue;
            }
            if (c == '@')
            {
                ++i;
                out += "__e4xIt";
                attribute("");
                continue;
            }
            if (c == '<' && regexAllowed && (identStart(next) || next == '>' || next == '{') && literal())
                continue;
            if (c == '(' || c == '[' || c == '{')
                ++depth;
            else if (c == ')' || c == ']' || c == '}')
            {
                if (depth == 0 && closer && c == closer)
                    return;
                if (depth > 0)
                    --depth;
            }
            out += c;
            ++i;
            if (!space(c))
                regexAllowed = !(c == ')' || c == ']' || c == '}');
        }
    }

    void copyTo(size_t end)
    {
        out.append(s, i, end - i);
        i = end;
    }

    void number()
    {
        size_t j = i;
        while (j < s.size() && identPart(s[j]))
            ++j;
        if (j < s.size() && s[j] == '.')
        {
            ++j;
            while (j < s.size() && identPart(s[j]))
                ++j;
        }
        copyTo(j);
        regexAllowed = false;
    }

    void word()
    {
        size_t j = i;
        while (j < s.size() && identPart(s[j]))
            ++j;
        const std::string name = s.substr(i, j - i);
        if (name == "function" && s.compare(j, 2, "::") == 0)
        {
            // function::name, E4X's way to reach a method rather than a
            // child: after a dot it is the method; in a filter, the
            // element's.
            if ((out.empty() || out.back() != '.') && filterDepth > 0)
                out += "__e4xIt.";
            i = j + 2;
            return;
        }
        copyTo(j);
        static const char* const kBeforeExpression[] = {"return", "typeof", "case", "do", "else", "in", "of", "new",
            "delete", "void", "throw", "instanceof", "yield"};
        regexAllowed = false;
        for (const char* keyword : kBeforeExpression)
            regexAllowed = regexAllowed || name == keyword;
    }

    void dot()
    {
        const char next = i + 1 < s.size() ? s[i + 1] : '\0';
        if (next == '.' && i + 2 < s.size() && s[i + 2] == '.')
        {
            copyTo(i + 3);
            regexAllowed = true;
            return;
        }
        if (next == '.')
        {
            const size_t j = i + 2;
            const char after = j < s.size() ? s[j] : '\0';
            if (after == '*')
            {
                out += "[\"..*\"]";
                i = j + 1;
                regexAllowed = false;
                return;
            }
            if (after == '@')
            {
                i = j + 1;
                attribute("..");
                return;
            }
            if (identStart(after))
            {
                size_t end = j;
                while (end < s.size() && identPart(s[end]))
                    ++end;
                out += "[\"..";
                out.append(s, j, end - j);
                out += "\"]";
                i = end;
                regexAllowed = false;
                return;
            }
        }
        if (next == '@')
        {
            i += 2;
            attribute("");
            return;
        }
        if (next == '*')
        {
            // x.* += y appends y to x's children.
            size_t k = i + 2;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t'))
                ++k;
            if (s.compare(k, 2, "+=") == 0 && (k + 2 >= s.size() || s[k + 2] != '='))
            {
                out += "[__e4x.append] =";
                i = k + 2;
                regexAllowed = true;
                return;
            }
            out += "[\"*\"]";
            i += 2;
            regexAllowed = false;
            return;
        }
        if (next == '(' && !gaveUp && nesting < kDeepest)
        {
            const Nest nest(nesting);
            out += "[__e4x.filter]((__e4xIt) => { with (__e4xIt) return (";
            i += 2;
            regexAllowed = true;
            ++filterDepth;
            code(')');
            --filterDepth;
            if (i < s.size())
                ++i;
            out += "); })";
            regexAllowed = false;
            return;
        }
        out += '.';
        ++i;
        regexAllowed = false;
    }

    /// After an @ (at i): the attribute's name, * or [expression], as a
    /// property name. Its value, unless it is used as an XMLList.
    void attribute(const char* prefix)
    {
        std::string key;
        bool computed = false;
        std::string expression;
        if (i < s.size() && s[i] == '*')
        {
            key = "*";
            ++i;
        }
        else if (i < s.size() && s[i] == '[')
        {
            ++i;
            std::string saved;
            saved.swap(out);
            regexAllowed = true;
            codeUntilBracket();
            expression.swap(out);
            out.swap(saved);
            if (i < s.size())
                ++i;
            computed = true;
        }
        else
        {
            const size_t start = i;
            while (i < s.size() && identPart(s[i]))
                ++i;
            key = s.substr(start, i - start);
        }
        size_t k = i;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t'))
            ++k;
        const bool asList = key == "*" || (k < s.size() && (s[k] == '.' || s[k] == '['));
        const std::string at = std::string(prefix) + (asList ? "@" : "@?");
        if (computed)
            out += "[\"" + at + "\" + (" + expression + ")]";
        else
            out += "[\"" + at + key + "\"]";
        regexAllowed = false;
    }

    /// Code up to an unmatched ']'.
    void codeUntilBracket()
    {
        // code() stops on ')' or '}' only; wrap the index in a paren pair
        // of its own by scanning for the matching ']' first.
        int depth = 0;
        const size_t start = i;
        size_t j = i;
        for (; j < s.size(); ++j)
        {
            const char c = s[j];
            if (c == '"' || c == '\'' || c == '`')
            {
                j = skipString(s, j) - 1;
                continue;
            }
            if (c == '(' || c == '[' || c == '{')
                ++depth;
            else if (c == ')' || c == ']' || c == '}')
            {
                if (depth == 0)
                    break;
                --depth;
            }
        }
        const std::string index = s.substr(start, j - start);
        if (gaveUp || nesting >= kDeepest)
            out += index;
        else
        {
            Rewriter inner(index);
            inner.filterDepth = filterDepth;
            inner.nesting = nesting + 1;
            out += inner.run();
            gaveUp = inner.gaveUp;
        }
        i = j;
    }

    /// An XML literal at '<'; false, with nothing consumed, when it is not
    /// one.
    bool literal()
    {
        if (!deepEnough())
        {
            gaveUp = true;
            return false;
        }
        const Nest nest(nesting);
        const size_t start = i;
        const size_t outStart = out.size();
        Literal parts;
        bool ok = false;
        bool list = false;
        if (s.compare(i, 2, "<>") == 0)
        {
            list = true;
            i += 2;
            ok = content(parts, std::string(), true);
        }
        else
            ok = element(parts);
        if (!ok)
        {
            i = start;
            out.resize(outStart);
            return false;
        }
        out += list ? "XMLList(" : "XML(";
        out += parts.finish();
        out += ")";
        regexAllowed = false;
        return true;
    }

    std::string holeCode()
    {
        // At '{': the expression up to its '}'.
        ++i;
        std::string saved;
        saved.swap(out);
        const bool savedRegex = regexAllowed;
        regexAllowed = true;
        code('}');
        regexAllowed = savedRegex;
        std::string expression;
        expression.swap(out);
        out.swap(saved);
        if (i < s.size())
            ++i;
        return expression;
    }

    bool element(Literal& parts)
    {
        if (!deepEnough())
        {
            gaveUp = true;
            return false;
        }
        const Nest nest(nesting);
        // At '<'.
        ++i;
        parts.text("<");
        std::string name;
        if (i < s.size() && s[i] == '{')
            parts.hole(holeCode(), 2);
        else
        {
            const size_t begin = i;
            while (i < s.size() && xmlNamePart(s[i]))
                ++i;
            if (i == begin)
                return false;
            name = s.substr(begin, i - begin);
            parts.text(name);
        }
        while (true)
        {
            const size_t begin = i;
            while (i < s.size() && space(s[i]))
                ++i;
            parts.text(s.substr(begin, i - begin));
            if (i >= s.size())
                return false;
            if (s.compare(i, 2, "/>") == 0)
            {
                parts.text("/>");
                i += 2;
                return true;
            }
            if (s[i] == '>')
            {
                parts.text(">");
                ++i;
                break;
            }
            if (i == begin && begin > 0 && !space(s[begin - 1]))
                return false;
            if (s[i] == '{')
                parts.hole(holeCode(), 2);
            else
            {
                const size_t nameStart = i;
                while (i < s.size() && xmlNamePart(s[i]))
                    ++i;
                if (i == nameStart)
                    return false;
                parts.text(s.substr(nameStart, i - nameStart));
            }
            while (i < s.size() && space(s[i]))
                ++i;
            if (i >= s.size() || s[i] != '=')
                return false;
            ++i;
            parts.text("=");
            while (i < s.size() && space(s[i]))
                ++i;
            if (i >= s.size())
                return false;
            if (s[i] == '"' || s[i] == '\'')
            {
                const size_t close = s.find(s[i], i + 1);
                if (close == std::string::npos)
                    return false;
                parts.text(s.substr(i, close + 1 - i));
                i = close + 1;
            }
            else if (s[i] == '{')
                parts.hole(holeCode(), 1);
            else
                return false;
        }
        return content(parts, name, false);
    }

    /// An element's content and its closing tag; or a list literal's, up
    /// to </>.
    bool content(Literal& parts, const std::string& name, bool list)
    {
        while (i < s.size())
        {
            if (gaveUp || spent())
                return false;
            if (s.compare(i, 3, "</{") == 0 && !list)
            {
                i += 2;
                parts.text("</");
                parts.hole(holeCode(), 2);
                while (i < s.size() && space(s[i]))
                    ++i;
                if (i >= s.size() || s[i] != '>')
                    return false;
                parts.text(">");
                ++i;
                return true;
            }
            if (s.compare(i, 2, "</") == 0)
            {
                const size_t close = s.find('>', i);
                if (close == std::string::npos)
                    return false;
                std::string closing = s.substr(i + 2, close - i - 2);
                while (!closing.empty() && space(closing.back()))
                    closing.pop_back();
                if (list ? !closing.empty() : (!name.empty() && closing != name && closing.find('{') == std::string::npos))
                    return false;
                if (!list)
                    parts.text(s.substr(i, close + 1 - i));
                i = close + 1;
                return true;
            }
            const char* const kVerbatim[][2] = {{"<!--", "-->"}, {"<![CDATA[", "]]>"}, {"<?", "?>"}};
            bool copied = false;
            for (const auto& verbatim : kVerbatim)
            {
                if (s.compare(i, std::strlen(verbatim[0]), verbatim[0]) == 0)
                {
                    const size_t end = s.find(verbatim[1], i);
                    if (end == std::string::npos)
                        return false;
                    const size_t stop = end + std::strlen(verbatim[1]);
                    parts.text(s.substr(i, stop - i));
                    i = stop;
                    copied = true;
                    break;
                }
            }
            if (copied)
                continue;
            if (s[i] == '<')
            {
                if (!element(parts))
                    return false;
                continue;
            }
            if (s[i] == '{')
            {
                parts.hole(holeCode(), 0);
                continue;
            }
            parts.text(std::string(1, s[i]));
            ++i;
        }
        return false;
    }

    /// How deep literals, their holes and filters nest. Past kDeepest, or
    /// once literals that do not close have been tried over and over (each
    /// try may look through the rest of the script), the rewriting gives up
    /// and leaves the rest as written, to fail as JavaScript, rather than
    /// run out of stack or take for ever.
    bool deepEnough() const { return !gaveUp && nesting < kDeepest; }
    bool spent()
    {
        if (++work > 64 * s.size() + 100000)
            gaveUp = true;
        return gaveUp;
    }
    struct Nest
    {
        explicit Nest(int& depth) : depth(depth) { ++depth; }
        ~Nest() { --depth; }
        Nest(const Nest&) = delete;
        Nest& operator=(const Nest&) = delete;
        int& depth;
    };
    static constexpr int kDeepest = 64;

    const std::string& s;
    std::string out;
    size_t i = 0;
    bool regexAllowed = true;
    int filterDepth = 0;
    int nesting = 0;
    /// Characters looked at, counting each look again after a literal
    /// that did not close.
    size_t work = 0;
    bool gaveUp = false;
};

const char* const kPrelude = R"JS(
(function (global) {
    var settings = {ignoreComments: true, ignoreProcessingInstructions: true, ignoreWhitespace: true,
        prettyPrinting: true, prettyIndent: 2};
    // What each XML and XMLList (and its Proxy target) holds: for an XML,
    // its node; for an XMLList, {list: true, items: [nodes], owner, name}.
    var DATA = new WeakMap();
    var FILTER = Symbol("e4x.filter"), APPEND = Symbol("e4x.append");

    function hidden(object, name, value) {
        Object.defineProperty(object, name, {value: value, writable: true, configurable: true, enumerable: false});
    }
    function node(kind, name, value) {
        return {kind: kind, name: name || "", value: value === undefined ? "" : value, attrs: [], kids: [], parent: null, proxy: null};
    }

    // Parsing.
    var entities = {lt: "<", gt: ">", amp: "&", quot: "\"", apos: "'"};
    function decode(text) {
        return text.indexOf("&") < 0 ? text : text.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);/g, function (all, code) {
            if (code.charAt(0) === "#")
                return String.fromCodePoint(code.charAt(1) === "x" ? parseInt(code.slice(2), 16) : parseInt(code.slice(1), 10));
            return entities.hasOwnProperty(code) ? entities[code] : all;
        });
    }
    function fail(message, at) {
        throw new SyntaxError("XML: " + message + (at === undefined ? "" : " at character " + at));
    }
    // The nodes of an XML fragment, kept in a parent of their own.
    function parse(text) {
        var root = node("element", "#root"), current = root, i = 0, n = text.length;
        function add(child) {
            child.parent = current;
            current.kids.push(child);
        }
        function addText(value) {
            if (settings.ignoreWhitespace && !/\S/.test(value))
                return;
            var last = current.kids[current.kids.length - 1];
            if (last && last.kind === "text")
                last.value += value;
            else
                add(node("text", "", value));
        }
        var nameRe = /[^\s\/>=]+/y, attrRe = /\s*([^\s\/>=]+)\s*=\s*("([^"]*)"|'([^']*)')/y;
        while (i < n) {
            var lt = text.indexOf("<", i);
            if (lt < 0) lt = n;
            if (lt > i)
                addText(decode(text.slice(i, lt)));
            i = lt;
            if (i >= n)
                break;
            if (text.startsWith("<!--", i)) {
                var end = text.indexOf("-->", i + 4);
                if (end < 0) fail("unclosed comment", i);
                if (!settings.ignoreComments)
                    add(node("comment", "", text.slice(i + 4, end)));
                i = end + 3;
            } else if (text.startsWith("<![CDATA[", i)) {
                var stop = text.indexOf("]]>", i + 9);
                if (stop < 0) fail("unclosed CDATA section", i);
                var data = text.slice(i + 9, stop), prev = current.kids[current.kids.length - 1];
                if (prev && prev.kind === "text")
                    prev.value += data;
                else
                    add(node("text", "", data));
                i = stop + 3;
            } else if (text.startsWith("<?", i)) {
                var close = text.indexOf("?>", i + 2);
                if (close < 0) fail("unclosed processing instruction", i);
                var body = text.slice(i + 2, close), space = body.search(/\s/);
                var target = space < 0 ? body : body.slice(0, space);
                if (target.toLowerCase() !== "xml" && !settings.ignoreProcessingInstructions)
                    add(node("processing-instruction", target, space < 0 ? "" : body.slice(space + 1).trim()));
                i = close + 2;
            } else if (text.startsWith("<!", i)) {
                // A DOCTYPE, with its internal subset if it has one.
                var depth = 0, j = i + 2;
                for (; j < n; ++j) {
                    var ch = text.charAt(j);
                    if (ch === "[") ++depth;
                    else if (ch === "]") --depth;
                    else if (ch === ">" && depth <= 0) break;
                }
                i = j + 1;
            } else if (text.charAt(i + 1) === "/") {
                var gt = text.indexOf(">", i);
                if (gt < 0) fail("unclosed tag", i);
                var closing = text.slice(i + 2, gt).trim();
                if (current === root || closing !== current.name)
                    fail("</" + closing + "> does not close <" + current.name + ">", i);
                current = current.parent;
                i = gt + 1;
            } else {
                nameRe.lastIndex = i + 1;
                var m = nameRe.exec(text);
                if (!m) fail("bad tag", i);
                var element = node("element", m[0]);
                i = nameRe.lastIndex;
                for (;;) {
                    attrRe.lastIndex = i;
                    var a = attrRe.exec(text);
                    if (!a)
                        break;
                    var attr = node("attribute", a[1], decode(a[3] !== undefined ? a[3] : a[4]));
                    attr.parent = element;
                    element.attrs.push(attr);
                    i = attrRe.lastIndex;
                }
                while (i < n && /\s/.test(text.charAt(i))) ++i;
                add(element);
                if (text.startsWith("/>", i))
                    i += 2;
                else if (text.charAt(i) === ">") {
                    current = element;
                    ++i;
                } else
                    fail("bad attribute in <" + element.name + ">", i);
            }
        }
        if (current !== root)
            fail("<" + current.name + "> is not closed");
        root.kids.forEach(function (kid) { kid.parent = null; });
        return root.kids;
    }

    // Writing.
    function escapeText(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }
    function escapeAttribute(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/"/g, "&quot;")
            .replace(/\n/g, "&#xA;").replace(/\r/g, "&#xD;").replace(/\t/g, "&#x9;");
    }
    function write(n, indent) {
        var pretty = settings.prettyPrinting, pad = pretty ? new Array(indent + 1).join(" ") : "";
        switch (n.kind) {
        case "text": return pad + escapeText(pretty ? n.value.trim() : n.value);
        case "attribute": return pad + escapeAttribute(n.value);
        case "comment": return pad + "<!--" + n.value + "-->";
        case "processing-instruction": return pad + "<?" + n.name + (n.value ? " " + n.value : "") + "?>";
        }
        var out = pad + "<" + n.name;
        n.attrs.forEach(function (a) { out += " " + a.name + "=\"" + escapeAttribute(a.value) + "\""; });
        var kids = pretty ? n.kids.filter(function (k) { return k.kind !== "text" || /\S/.test(k.value); }) : n.kids;
        if (!kids.length)
            return out + "/>";
        out += ">";
        if (!pretty || (kids.length === 1 && kids[0].kind === "text"))
            return out + kids.map(function (k) { return write(k, 0); }).join("") + "</" + n.name + ">";
        var step = settings.prettyIndent;
        kids.forEach(function (k) { out += "\n" + write(k, indent + step); });
        return out + "\n" + pad + "</" + n.name + ">";
    }
    function simple(n) {
        return n.kind !== "element" || !n.kids.some(function (k) { return k.kind === "element"; });
    }
    function stringOf(n) {
        if (n.kind === "attribute" || n.kind === "text")
            return n.value;
        if (n.kind !== "element")
            return write(n, 0);
        if (simple(n))
            return n.kids.filter(function (k) { return k.kind === "text"; }).map(function (k) { return k.value; }).join("");
        return write(n, 0);
    }
    function listString(nodes) {
        if (nodes.every(simple))
            return nodes.filter(function (k) { return k.kind !== "comment" && k.kind !== "processing-instruction"; })
                .map(stringOf).join("");
        return nodes.map(function (k) { return write(k, 0); }).join("\n");
    }

    // Lookup.
    function localName(name) {
        var colon = name.lastIndexOf(":");
        return colon < 0 ? name : name.slice(colon + 1);
    }
    function matches(n, name) {
        if (name === "*")
            return true;
        return n.kind === "element" && (n.name === name || (name.indexOf(":") < 0 && localName(n.name) === name));
    }
    function attributes(nodes, name) {
        var found = [];
        nodes.forEach(function (n) {
            n.attrs.forEach(function (a) {
                if (name === "*" || a.name === name || (name.indexOf(":") < 0 && localName(a.name) === name))
                    found.push(a);
            });
        });
        return found;
    }
    function children(nodes, name) {
        var found = [];
        nodes.forEach(function (n) {
            n.kids.forEach(function (k) { if (matches(k, name)) found.push(k); });
        });
        return found;
    }
    function descendants(nodes, name) {
        var found = [];
        function visit(n) {
            if (name.charAt(0) === "@")
                attributes([n], name.slice(1)).forEach(function (a) { found.push(a); });
            n.kids.forEach(function (k) {
                if (name.charAt(0) !== "@" && matches(k, name)) found.push(k);
                visit(k);
            });
        }
        nodes.forEach(visit);
        return found;
    }
    function attributeValue(nodes, name) {
        var found = attributes(nodes, name);
        if (!found.length)
            return undefined;
        return found.map(function (a) { return a.value; }).join("");
    }

    // Changes.
    function copy(n, parent) {
        var c = node(n.kind, n.name, n.value);
        c.parent = parent || null;
        c.attrs = n.attrs.map(function (a) { return copy(a, c); });
        c.kids = n.kids.map(function (k) { return copy(k, c); });
        return c;
    }
    function isXML(value) {
        return (typeof value === "object" || typeof value === "function") && value !== null && DATA.has(value);
    }
    function nodesOf(value) {
        var data = DATA.get(value);
        return data.list ? data.items : [data];
    }
    // The nodes \a value adds under \a parent: XML is moved in (copied when
    // it is already somewhere else), anything else is text.
    function adopt(value, parent) {
        if (isXML(value))
            return nodesOf(value).filter(function (n) { return n.kind !== "attribute"; }).map(function (n) {
                if (n.parent || n === parent)
                    n = copy(n);
                n.parent = parent;
                return n;
            }).concat(nodesOf(value).filter(function (n) { return n.kind === "attribute"; }).map(function (n) {
                var t = node("text", "", n.value);
                t.parent = parent;
                return t;
            }));
        var text = node("text", "", value === undefined ? "undefined" : String(value));
        text.parent = parent;
        return [text];
    }
    function detach(n) {
        if (!n.parent)
            return;
        var list = n.kind === "attribute" ? n.parent.attrs : n.parent.kids, at = list.indexOf(n);
        if (at >= 0)
            list.splice(at, 1);
        n.parent = null;
    }
    function setAttribute(n, name, value) {
        if (n.kind !== "element")
            return;
        var text = isXML(value) ? nodesOf(value).map(stringOf).join(" ") : String(value);
        for (var i = 0; i < n.attrs.length; ++i) {
            if (n.attrs[i].name === name) {
                n.attrs[i].value = text;
                return;
            }
        }
        var a = node("attribute", name, text);
        a.parent = n;
        n.attrs.push(a);
    }
    function setContent(n, value) {
        n.kids.forEach(function (k) { k.parent = null; });
        n.kids = adopt(value, n);
    }
    // x.name = value: the first child of that name gets the value (its
    // content, or replaced by it when it is XML); the others go.
    function put(n, name, value) {
        if (n.kind !== "element")
            return;
        var found = n.kids.filter(function (k) { return matches(k, name); });
        if (name === "*") {
            setContent(n, value);
            return;
        }
        if (!found.length) {
            var child = node("element", name);
            child.parent = n;
            n.kids.push(child);
            found = [child];
        }
        for (var i = 1; i < found.length; ++i)
            detach(found[i]);
        var first = found[0];
        if (isXML(value) && nodesOf(value).some(function (v) { return v.kind === "element"; })) {
            var at = n.kids.indexOf(first), added = adopt(value, n);
            Array.prototype.splice.apply(n.kids, [at, 1].concat(added));
            first.parent = null;
        } else
            setContent(first, value);
    }
    function insert(n, at, value) {
        var added = adopt(value, n);
        Array.prototype.splice.apply(n.kids, [at, 0].concat(added));
    }

    // The objects.
    function single(self, method) {
        var nodes = nodesOf(self);
        if (nodes.length !== 1)
            throw new TypeError("XMLList." + method + "() needs a list of one item, not " + nodes.length + ".");
        return nodes[0];
    }
    var methods = {
        length: function () { return nodesOf(this).length; },
        name: function () { var n = single(this, "name"); return n.kind === "element" || n.kind === "attribute" || n.kind === "processing-instruction" ? n.name : null; },
        localName: function () { var n = single(this, "localName"); return n.name ? localName(n.name) : null; },
        nodeKind: function () { return single(this, "nodeKind").kind; },
        parent: function () {
            var parents = nodesOf(this).map(function (n) { return n.parent; });
            if (!parents.length || !parents[0] || parents.some(function (p) { return p !== parents[0]; }))
                return undefined;
            return xmlOf(parents[0]);
        },
        childIndex: function () {
            var n = single(this, "childIndex");
            return n.parent && n.kind !== "attribute" ? n.parent.kids.indexOf(n) : -1;
        },
        children: function () { return listOf(children(nodesOf(this), "*"), null, null); },
        elements: function (name) {
            return listOf(children(nodesOf(this), name === undefined ? "*" : String(name)).filter(function (n) { return n.kind === "element"; }), null, null);
        },
        child: function (name) {
            if (typeof name === "number" || /^\d+$/.test(String(name))) {
                var found = [];
                nodesOf(this).forEach(function (n) { if (n.kids[Number(name)]) found.push(n.kids[Number(name)]); });
                return listOf(found, null, null);
            }
            return this[String(name)];
        },
        descendants: function (name) { return listOf(descendants(nodesOf(this), name === undefined ? "*" : String(name)), null, null); },
        attributes: function () { return listOf(attributes(nodesOf(this), "*"), null, null); },
        attribute: function (name) { return listOf(attributes(nodesOf(this), String(name).replace(/^@/, "")), null, null); },
        text: function () {
            var found = [];
            nodesOf(this).forEach(function (n) { n.kids.forEach(function (k) { if (k.kind === "text") found.push(k); }); });
            return listOf(found, null, null);
        },
        comments: function () {
            var found = [];
            nodesOf(this).forEach(function (n) { n.kids.forEach(function (k) { if (k.kind === "comment") found.push(k); }); });
            return listOf(found, null, null);
        },
        processingInstructions: function () {
            var found = [];
            nodesOf(this).forEach(function (n) { n.kids.forEach(function (k) { if (k.kind === "processing-instruction") found.push(k); }); });
            return listOf(found, null, null);
        },
        hasSimpleContent: function () { return nodesOf(this).every(simple); },
        hasComplexContent: function () { return !nodesOf(this).every(simple); },
        hasOwnProperty: function (name) {
            name = String(name);
            if (/^\d+$/.test(name))
                return Number(name) < nodesOf(this).length;
            if (name.charAt(0) === "@")
                return attributes(nodesOf(this), name.slice(1)).length > 0;
            return children(nodesOf(this), name).length > 0;
        },
        contains: function (value) {
            var text = isXML(value) ? listString(nodesOf(value)) : null;
            return nodesOf(this).some(function (n) { return isXML(value) ? write(n, 0) === listString(nodesOf(value)) : stringOf(n) == value; });
        },
        copy: function () {
            var copies = nodesOf(this).map(function (n) { return copy(n); });
            return DATA.get(this).list ? listOf(copies, null, null) : xmlOf(copies[0]);
        },
        toString: function () { return listString(nodesOf(this)); },
        toXMLString: function () { return nodesOf(this).map(function (n) { return write(n, 0); }).join("\n"); },
        valueOf: function () { return this; },
        toJSON: function () { return listString(nodesOf(this)); },
        appendChild: function (value) { var n = single(this, "appendChild"); insert(n, n.kids.length, value); return this; },
        prependChild: function (value) { insert(single(this, "prependChild"), 0, value); return this; },
        insertChildAfter: function (ref, value) {
            var n = single(this, "insertChildAfter");
            if (ref === null || ref === undefined)
                insert(n, 0, value);
            else {
                var at = n.kids.indexOf(nodesOf(ref)[0]);
                if (at < 0) return undefined;
                insert(n, at + 1, value);
            }
            return this;
        },
        insertChildBefore: function (ref, value) {
            var n = single(this, "insertChildBefore");
            if (ref === null || ref === undefined)
                insert(n, n.kids.length, value);
            else {
                var at = n.kids.indexOf(nodesOf(ref)[0]);
                if (at < 0) return undefined;
                insert(n, at, value);
            }
            return this;
        },
        setChildren: function (value) { setContent(single(this, "setChildren"), value); return this; },
        replace: function (name, value) {
            var n = single(this, "replace");
            if (typeof name === "number" || /^\d+$/.test(String(name))) {
                var old = n.kids[Number(name)];
                if (old) {
                    var added = adopt(value, n);
                    Array.prototype.splice.apply(n.kids, [Number(name), 1].concat(added));
                    old.parent = null;
                } else
                    insert(n, n.kids.length, value);
            } else
                put(n, String(name), value);
            return this;
        },
        setName: function (name) { single(this, "setName").name = String(name); },
        setLocalName: function (name) {
            var n = single(this, "setLocalName"), colon = n.name.lastIndexOf(":");
            n.name = (colon < 0 ? "" : n.name.slice(0, colon + 1)) + String(name);
        },
        normalize: function () {
            function tidy(n) {
                var kids = [];
                n.kids.forEach(function (k) {
                    var last = kids[kids.length - 1];
                    if (k.kind === "text" && last && last.kind === "text")
                        last.value += k.value;
                    else if (k.kind !== "text" || k.value !== "")
                        kids.push(k);
                    if (k.kind === "element")
                        tidy(k);
                });
                n.kids = kids;
            }
            nodesOf(this).forEach(tidy);
            return this;
        },
        namespace: function () { return undefined; },
        inScopeNamespaces: function () { return []; },
        namespaceDeclarations: function () { return []; },
        addNamespace: function () { return this; },
        removeNamespace: function () { return this; }
    };

    function XML(value) {
        var made;
        if (isXML(value)) {
            var nodes = nodesOf(value);
            if (nodes.length !== 1)
                throw new TypeError("XML() needs a list of one item, not " + nodes.length + ".");
            made = new.target ? xmlOf(copy(nodes[0])) : xmlOf(nodes[0]);
        } else {
            if (value === null || value === undefined)
                value = "";
            var parsed = parse(String(value));
            if (parsed.length > 1)
                throw new SyntaxError("XML: the text has more than one root element; use XMLList for a list.");
            made = xmlOf(parsed.length ? parsed[0] : node("text", "", ""));
        }
        return made;
    }
    function XMLList(value) {
        if (isXML(value))
            return listOf(nodesOf(value).slice(), null, null);
        if (value === null || value === undefined)
            return listOf([], null, null);
        return listOf(parse(String(value)), null, null);
    }
    Object.keys(methods).forEach(function (key) {
        hidden(XML.prototype, key, methods[key]);
        hidden(XMLList.prototype, key, methods[key]);
    });
    ["ignoreComments", "ignoreProcessingInstructions", "ignoreWhitespace", "prettyPrinting", "prettyIndent"].forEach(function (key) {
        Object.defineProperty(XML, key, {get: function () { return settings[key]; },
            set: function (value) { settings[key] = key === "prettyIndent" ? Number(value) : !!value; }, enumerable: true});
    });
    XML.settings = function () { return Object.assign({}, settings); };
    XML.setSettings = function (values) {
        if (!values) values = {ignoreComments: true, ignoreProcessingInstructions: true, ignoreWhitespace: true, prettyPrinting: true, prettyIndent: 2};
        Object.keys(settings).forEach(function (key) { if (key in values) XML[key] = values[key]; });
    };
    XML.defaultSettings = function () {
        return {ignoreComments: true, ignoreProcessingInstructions: true, ignoreWhitespace: true, prettyPrinting: true, prettyIndent: 2};
    };

    function isIndex(key) {
        return typeof key === "string" && /^(0|[1-9][0-9]*)$/.test(key);
    }
    // A method of the XML when called, the children of the same name
    // otherwise: x.name() and x.name.@id both work.
    function either(method, self, list) {
        var call = function () {};
        var proxy = new Proxy(call, {
            e4x: true,
            apply: function (target, thisArg, args) { return method.apply(self, args); },
            get: function (target, key) { return list[key]; },
            set: function (target, key, value) { list[key] = value; return true; },
            has: function (target, key) { return key in list; }
        });
        DATA.set(proxy, DATA.get(list));
        return proxy;
    }
    var handler = {
        e4x: true,
        get: function (target, key, receiver) {
            var data = DATA.get(target), nodes = data.list ? data.items : [data];
            if (typeof key === "symbol") {
                if (key === FILTER)
                    return function (predicate) {
                        return listOf(nodes.filter(function (n) { return n.kind === "element" && predicate(xmlOf(n)); }), null, null);
                    };
                if (key === Symbol.iterator)
                    return function () { return nodes.map(xmlOf)[Symbol.iterator](); };
                if (key === Symbol.toPrimitive)
                    return function (hint) { var text = listString(nodes); return hint === "number" ? Number(text) : text; };
                return target[key];
            }
            if (isIndex(key)) {
                var item = nodes[Number(key)];
                return item ? xmlOf(item) : undefined;
            }
            if (key.charAt(0) === "@") {
                if (key.charAt(1) === "?")
                    return attributeValue(nodes, key.slice(2));
                return listOf(attributes(nodes, key.slice(1)), data.list ? null : data, key);
            }
            if (key.charAt(0) === "." && key.charAt(1) === ".") {
                var name = key.slice(2);
                if (name.charAt(0) === "@" && name.charAt(1) === "?") {
                    var values = descendants(nodes, "@" + name.slice(2));
                    return values.length ? values.map(function (a) { return a.value; }).join("") : undefined;
                }
                return listOf(descendants(nodes, name), null, null);
            }
            var proto = Object.getPrototypeOf(target);
            if (key in proto) {
                var member = proto[key];
                if (typeof member !== "function" || key === "constructor")
                    return member;
                var same = children(nodes, key);
                return same.length ? either(member, receiver, listOf(same, data.list ? null : data, key)) : member;
            }
            var found = children(nodes, key);
            if (!found.length && typeof String.prototype[key] === "function")
                return function () { var text = listString(nodes); return text[key].apply(text, arguments); };
            return listOf(found, data.list ? null : data, key);
        },
        set: function (target, key, value) {
            var data = DATA.get(target), nodes = data.list ? data.items : [data];
            if (key === APPEND) {
                if (data.list && !nodes.length && data.owner)
                    insert(data.owner, data.owner.kids.length, value);
                else if (nodes.length)
                    insert(nodes[0], nodes[0].kids.length, value);
                return true;
            }
            if (typeof key === "symbol")
                return true;
            if (isIndex(key)) {
                if (!data.list)
                    return true;
                var old = nodes[Number(key)];
                if (old && old.parent) {
                    var parent = old.parent, at = parent.kids.indexOf(old), added = adopt(value, parent);
                    Array.prototype.splice.apply(parent.kids, [at, 1].concat(added));
                    old.parent = null;
                    nodes[Number(key)] = added[0];
                } else if (!old && data.owner && data.name && data.name.charAt(0) !== "@") {
                    var made = node("element", data.name);
                    made.parent = data.owner;
                    data.owner.kids.push(made);
                    setContent(made, value);
                    nodes.push(made);
                }
                return true;
            }
            if (key.charAt(0) === "@") {
                var name = key.charAt(1) === "?" ? key.slice(2) : key.slice(1);
                if (!nodes.length && data.owner && data.name && data.name.charAt(0) !== "@") {
                    var holder = node("element", data.name);
                    holder.parent = data.owner;
                    data.owner.kids.push(holder);
                    nodes.push(holder);
                }
                nodes.forEach(function (n) { if (n.kind === "element") setAttribute(n, name, value); else if (n.kind === "attribute") n.value = String(value); });
                return true;
            }
            if (nodes.length === 1)
                put(nodes[0], key, value);
            else if (!nodes.length && data.owner && data.name && data.name.charAt(0) !== "@") {
                var element = node("element", data.name);
                element.parent = data.owner;
                data.owner.kids.push(element);
                put(element, key, value);
            }
            return true;
        },
        deleteProperty: function (target, key) {
            var data = DATA.get(target), nodes = data.list ? data.items : [data];
            if (typeof key === "symbol")
                return true;
            if (isIndex(key)) {
                var item = nodes[Number(key)];
                if (item && data.list) {
                    detach(item);
                    nodes.splice(Number(key), 1);
                }
                return true;
            }
            if (key.charAt(0) === "@") {
                var name = key.charAt(1) === "?" ? key.slice(2) : key.slice(1);
                attributes(nodes, name).forEach(detach);
                return true;
            }
            children(nodes, key).forEach(detach);
            return true;
        },
        has: function (target, key) {
            if (typeof key === "symbol")
                return false;
            var data = DATA.get(target), nodes = data.list ? data.items : [data];
            if (isIndex(key))
                return Number(key) < nodes.length;
            if (key.charAt(0) === "@")
                return attributes(nodes, key.slice(1)).length > 0;
            return children(nodes, key).length > 0;
        },
        ownKeys: function (target) {
            var data = DATA.get(target), count = data.list ? data.items.length : 1, keys = [];
            for (var i = 0; i < count; ++i)
                keys.push(String(i));
            return keys;
        },
        getOwnPropertyDescriptor: function (target, key) {
            var data = DATA.get(target), nodes = data.list ? data.items : [data];
            if (isIndex(key) && Number(key) < nodes.length)
                return {value: xmlOf(nodes[Number(key)]), writable: true, enumerable: true, configurable: true};
            return undefined;
        },
        defineProperty: function () { return true; }
    };
    function xmlOf(n) {
        if (!n.proxy) {
            var target = Object.create(XML.prototype);
            DATA.set(target, n);
            n.proxy = new Proxy(target, handler);
            DATA.set(n.proxy, n);
        }
        return n.proxy;
    }
    function listOf(items, owner, name) {
        var data = {list: true, items: items, owner: owner, name: name};
        var target = Object.create(XMLList.prototype);
        DATA.set(target, data);
        var proxy = new Proxy(target, handler);
        DATA.set(proxy, data);
        return proxy;
    }

    hidden(global, "XML", XML);
    hidden(global, "XMLList", XMLList);
    hidden(global, "isXMLName", function (name) { return /^[A-Za-z_][\w.\-]*$/.test(String(name)); });
    hidden(global, "__e4x", {
        filter: FILTER,
        append: APPEND,
        isXML: isXML,
        hole: function (value, kind) {
            if (kind === 1)
                return "\"" + escapeAttribute(isXML(value) ? listString(nodesOf(value)) : value) + "\"";
            if (kind === 2)
                return String(value);
            return isXML(value) ? nodesOf(value).map(function (n) { return write(n, 0); }).join("") : escapeText(value);
        }
    });
    var each = global.__jsflEach;
    hidden(global, "__jsflEach", function (object) {
        return isXML(object) ? nodesOf(object).map(xmlOf) : each(object);
    });
})(this);
)JS";

} // namespace

bool mayUse(const std::string& source)
{
    static const char* const kMarkers[] = {".@", "XML", "..", ".*", ".(", "::"};
    for (const char* marker : kMarkers)
    {
        if (source.find(marker) != std::string::npos)
            return true;
    }
    // A literal: '<' and a name where a value goes.
    for (size_t at = source.find('<'); at != std::string::npos; at = source.find('<', at + 1))
    {
        if (at + 1 >= source.size() || !(identStart(source[at + 1]) || source[at + 1] == '>'))
            continue;
        size_t k = at;
        while (k > 0 && space(source[k - 1]))
            --k;
        if (k == 0 || std::strchr("=(,:[?{;!&|", source[k - 1]) || (k >= 6 && source.compare(k - 6, 6, "return") == 0))
            return true;
    }
    return false;
}

std::string rewrite(const std::string& source)
{
    return Rewriter(source).run();
}

const char* prelude()
{
    return kPrelude;
}

} // namespace e4x
} // namespace jsfl
