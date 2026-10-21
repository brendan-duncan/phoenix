// ActionScript 1 and 2's XML (the XMLNode tree, not ActionScript 3's E4X):
// the player's side of the library's classes (resources/as2/xml.as). It
// parses text as the player does, leniently, saying what went wrong by a
// status number rather than throwing, and loads XML from files and the
// web for XML.load.

#include "avm1_player.h"
#include "legacy_xml.h"
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
        return parseLegacyXml(vm, text, in.toBoolean(args[1]), false);
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
