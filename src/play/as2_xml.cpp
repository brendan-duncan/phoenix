// ActionScript 1 and 2's XML (the XMLNode tree, not ActionScript 3's E4X):
// the player's side of the library's classes (resources/as2/xml.as). It
// parses text as the player does, leniently, saying what went wrong by a
// status number rather than throwing, and loads XML from files and the
// web for XML.load.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fla {
namespace play {

namespace {

/// XML.status: what went wrong.
enum Status
{
    NoError = 0,
    CdataNotTerminated = -2,
    DeclNotTerminated = -3,
    DoctypeNotTerminated = -4,
    CommentNotTerminated = -5,
    ElementMalformed = -6,
    AttributeNotTerminated = -8,
    StartNotMatched = -9,
    EndNotMatched = -10
};

/// UTF-8 for a code point.
void appendUtf8(std::string& out, uint32_t code)
{
    if (code < 0x80)
        out += char(code);
    else if (code < 0x800)
    {
        out += char(0xc0 | (code >> 6));
        out += char(0x80 | (code & 0x3f));
    }
    else if (code < 0x10000)
    {
        out += char(0xe0 | (code >> 12));
        out += char(0x80 | ((code >> 6) & 0x3f));
        out += char(0x80 | (code & 0x3f));
    }
    else
    {
        out += char(0xf0 | (code >> 18));
        out += char(0x80 | ((code >> 12) & 0x3f));
        out += char(0x80 | ((code >> 6) & 0x3f));
        out += char(0x80 | (code & 0x3f));
    }
}

/// Entities decoded each on its own, as the player decodes them: a known
/// one or a character reference; anything else (a lone &, an unknown name)
/// kept as it is.
std::string unescaped(const std::string& text)
{
    std::string out;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t amp = text.find('&', at);
        if (amp == std::string::npos)
        {
            out.append(text, at, std::string::npos);
            break;
        }
        out.append(text, at, amp - at);
        // The entity: up to a ;, with no & before it.
        size_t end = amp + 1;
        while (end < text.size() && text[end] != ';' && text[end] != '&')
            ++end;
        if (end >= text.size() || text[end] != ';')
        {
            out += '&';
            at = amp + 1;
            continue;
        }
        const std::string name = text.substr(amp + 1, end - amp - 1);
        uint32_t code = 0;
        bool known = true;
        if (name == "lt")
            code = '<';
        else if (name == "gt")
            code = '>';
        else if (name == "amp")
            code = '&';
        else if (name == "quot")
            code = '"';
        else if (name == "apos")
            code = '\'';
        else if (name.size() > 1 && name[0] == '#')
        {
            const bool hex = name[1] == 'x';
            const std::string digits = name.substr(hex ? 2 : 1);
            known = !digits.empty() && std::all_of(digits.begin(), digits.end(), [hex](char c) {
                return hex ? std::isxdigit(static_cast<unsigned char>(c)) != 0 : std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
            if (known)
                code = static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10));
            known = known && code > 0 && code <= 0x10ffff;
        }
        else
            known = false;
        if (known)
            appendUtf8(out, code);
        else
            out.append(text, amp, end - amp + 1);
        at = end + 1;
    }
    return out;
}

class Parser
{
public:
    Parser(as3::VM& vm, const std::string& text, bool ignoreWhite)
        : _vm(vm)
        , _text(text)
        , _ignoreWhite(ignoreWhite)
    {}

    /// [status, xmlDecl, docTypeDecl, nodes]: each node [1, name,
    /// [attribute, value...], children] or [3, text].
    as3::Val parse()
    {
        // The children being made: the document's, then each open element's.
        std::vector<std::vector<as3::Val>> children(1);
        std::vector<std::pair<std::string, std::vector<as3::Val>>> elements;
        const auto add = [&](const as3::Val& node) { children.back().push_back(node); };
        const auto text = [&](const std::string& raw) {
            if (raw.empty())
                return;
            if (_ignoreWhite && raw.find_first_not_of(" \t\r\n") == std::string::npos)
                return;
            add(_vm.newArray({as3::Val::number(3), as3::Val::string(raw)}));
        };
        // An element done: its children made into its node, added to the
        // one around it.
        const auto close = [&]() {
            auto [name, attributes] = std::move(elements.back());
            elements.pop_back();
            as3::Val node = _vm.newArray({as3::Val::number(1), as3::Val::string(name), _vm.newArray(std::move(attributes)),
                _vm.newArray(std::move(children.back()))});
            children.pop_back();
            add(node);
        };
        while (_at < _text.size() && _status == NoError)
        {
            if (_text[_at] != '<')
            {
                const size_t next = _text.find('<', _at);
                const size_t end = next == std::string::npos ? _text.size() : next;
                text(unescaped(_text.substr(_at, end - _at)));
                _at = end;
                continue;
            }
            if (starts("<!--"))
            {
                const size_t end = _text.find("-->", _at + 4);
                if (end == std::string::npos)
                    _status = CommentNotTerminated;
                else
                    _at = end + 3;
                continue;
            }
            if (starts("<![CDATA["))
            {
                const size_t end = _text.find("]]>", _at + 9);
                if (end == std::string::npos)
                {
                    _status = CdataNotTerminated;
                    continue;
                }
                text(_text.substr(_at + 9, end - _at - 9));
                _at = end + 3;
                continue;
            }
            if (startsIgnoringCase("<!DOCTYPE"))
            {
                const size_t end = _text.find('>', _at);
                if (end == std::string::npos)
                {
                    _status = DoctypeNotTerminated;
                    continue;
                }
                _docType = _text.substr(_at, end - _at + 1);
                _at = end + 1;
                continue;
            }
            if (starts("<?"))
            {
                const size_t end = _text.find("?>", _at + 2);
                if (end == std::string::npos)
                {
                    _status = DeclNotTerminated;
                    continue;
                }
                // The XML declaration kept; other instructions let go.
                const std::string body = _text.substr(_at, end - _at + 2);
                if (body.compare(0, 5, "<?xml") == 0 && (body.size() == 7 || std::isspace(static_cast<unsigned char>(body[5]))))
                    _xmlDecl = body;
                _at = end + 2;
                continue;
            }
            if (starts("</"))
            {
                _at += 2;
                const std::string name = readName();
                const size_t end = _text.find('>', _at);
                if (end == std::string::npos)
                {
                    _status = ElementMalformed;
                    continue;
                }
                _at = end + 1;
                if (elements.empty() || elements.back().first != name)
                {
                    _status = EndNotMatched;
                    continue;
                }
                close();
                continue;
            }
            // A start tag.
            ++_at;
            const std::string name = readName();
            if (name.empty())
            {
                _status = ElementMalformed;
                continue;
            }
            std::vector<as3::Val> attributes;
            std::vector<std::string> seen;
            bool empty = false;
            for (;;)
            {
                skipSpace();
                if (_at >= _text.size())
                {
                    _status = ElementMalformed;
                    break;
                }
                if (starts("/>"))
                {
                    _at += 2;
                    empty = true;
                    break;
                }
                if (_text[_at] == '>')
                {
                    ++_at;
                    break;
                }
                const std::string attribute = readName();
                skipSpace();
                if (attribute.empty() || _at >= _text.size() || _text[_at] != '=')
                {
                    _status = ElementMalformed;
                    break;
                }
                ++_at;
                skipSpace();
                if (_at >= _text.size() || (_text[_at] != '"' && _text[_at] != '\''))
                {
                    _status = AttributeNotTerminated;
                    break;
                }
                const char quote = _text[_at++];
                const size_t end = _text.find(quote, _at);
                if (end == std::string::npos)
                {
                    _status = AttributeNotTerminated;
                    break;
                }
                if (std::find(seen.begin(), seen.end(), attribute) != seen.end())
                {
                    _status = ElementMalformed;
                    break;
                }
                seen.push_back(attribute);
                attributes.push_back(as3::Val::string(attribute));
                attributes.push_back(as3::Val::string(unescaped(_text.substr(_at, end - _at))));
                _at = end + 1;
            }
            if (_status != NoError)
                continue;
            elements.emplace_back(name, std::move(attributes));
            children.emplace_back();
            if (empty)
                close();
        }
        // Elements left open: those made kept, the status saying so.
        if (_status == NoError && !elements.empty())
            _status = StartNotMatched;
        while (!elements.empty())
            close();
        return _vm.newArray({as3::Val::number(_status), _xmlDecl.empty() ? as3::Val() : as3::Val::string(_xmlDecl),
            _docType.empty() ? as3::Val() : as3::Val::string(_docType), _vm.newArray(std::move(children.front()))});
    }

private:
    bool starts(const char* what) const { return _text.compare(_at, std::strlen(what), what) == 0; }
    bool startsIgnoringCase(const char* what) const
    {
        const size_t n = std::strlen(what);
        if (_at + n > _text.size())
            return false;
        for (size_t i = 0; i < n; ++i)
            if (std::toupper(static_cast<unsigned char>(_text[_at + i])) != std::toupper(static_cast<unsigned char>(what[i])))
                return false;
        return true;
    }
    std::string readName()
    {
        const size_t start = _at;
        while (_at < _text.size())
        {
            const unsigned char c = static_cast<unsigned char>(_text[_at]);
            if (std::isspace(c) || c == '>' || c == '/' || c == '=' || c == '<' || c == '"' || c == '\'')
                break;
            ++_at;
        }
        return _text.substr(start, _at - start);
    }
    void skipSpace()
    {
        while (_at < _text.size() && std::isspace(static_cast<unsigned char>(_text[_at])))
            ++_at;
    }

    as3::VM& _vm;
    const std::string& _text;
    bool _ignoreWhite;
    size_t _at = 0;
    Status _status = NoError;
    std::string _xmlDecl;
    std::string _docType;
};

} // namespace

as3::Val ActionsPlayer::xmlNatives()
{
    as3::VM& vm = _vm;
    as3::avm1::Interpreter& in = *_interpreter;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    define("parse", [&vm, &in](as3::VM&, const as3::Val&, as3::Args args) {
        const std::string text = in.toString(args[0]);
        return Parser(vm, text, in.toBoolean(args[1])).parse();
    });
    // load(target, url, body): target.onData(text) when it has come, or
    // onData(undefined) when it cannot; on a later frame either way.
    std::shared_ptr<bool> alive = _alive;
    define("load", [this, alive](as3::VM&, const as3::Val&, as3::Args args) {
        const as3::Val target = args[0];
        MovieRuntime::Request request;
        request.url = _interpreter->toString(args[1]);
        if (args.size() > 2 && !args[2].isUndefined())
        {
            const std::string body = _interpreter->toString(args[2]);
            request.method = "POST";
            request.body.assign(body.begin(), body.end());
            request.contentType = "application/x-www-form-urlencoded";
        }
        load(request, [this, alive, target](const MovieRuntime::Response& response) {
            if (!*alive)
                return;
            const as3::Val bytes = as3::Val::number(static_cast<double>(response.bytes.size()));
            _interpreter->setMember(target, "_bytesLoaded", bytes);
            _interpreter->setMember(target, "_bytesTotal", bytes);
            const as3::Val text = response.ok
                ? as3::Val::string(std::string(response.bytes.begin(), response.bytes.end()))
                : as3::Val();
            _interpreter->callMethod(target, "onData", {text}, "XML.onData");
        });
        return as3::Val::boolean(true);
    });
    return natives;
}

void ActionsPlayer::load(const MovieRuntime::Request& request, std::function<void(const MovieRuntime::Response&)> done)
{
    // As ActionScript 3 loads: the web through the runtime's fetch, a file
    // by itself; told on the frame after.
    const std::shared_ptr<bool> alive = _alive;
    const auto isWeb = [](const std::string& url) {
        std::string head = url.substr(0, 8);
        std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return head.compare(0, 7, "http://") == 0 || head.compare(0, 8, "https://") == 0;
    };
    if (isWeb(request.url))
    {
        if (!_runtime.fetch)
        {
            MovieRuntime::Response failed;
            _posted.push_back([done, failed]() { done(failed); });
            return;
        }
        _runtime.fetch(request, [this, alive, done](const MovieRuntime::Response& response) {
            if (*alive)
                _posted.push_back([done, response]() { done(response); });
        });
        return;
    }
    namespace fs = std::filesystem;
    std::string path = request.url;
    if (path.compare(0, 8, "file:///") == 0)
        path = path.substr(8);
    else if (path.compare(0, 7, "file://") == 0)
        path = path.substr(7);
    const size_t query = path.find('?');
    if (query != std::string::npos)
        path = path.substr(0, query);
    std::replace(path.begin(), path.end(), '\\', '/');
    fs::path file = fs::u8path(path);
    if (!file.is_absolute())
        file = fs::u8path(_runtime.folder) / file;
    MovieRuntime::Response response;
    std::ifstream source(file, std::ios::binary);
    if (source)
    {
        response.bytes.assign(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());
        response.ok = true;
    }
    _posted.push_back([done, response]() { done(response); });
}

void ActionsPlayer::frameStarted()
{
    std::vector<std::function<void()>> tasks;
    tasks.swap(_posted);
    for (const std::function<void()>& task : tasks)
        task();
}

} // namespace play
} // namespace fla
