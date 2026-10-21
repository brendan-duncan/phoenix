// ActionScript 1 and 2's loading: a movie or an image into a clip or a level
// (loadMovie, loadMovieNum, MovieClipLoader), and variables onto a clip
// (loadVariables). What comes is put in place as the next frame starts, as
// the player does it (resources/as2/movie_clip_loader.as).

#include "avm1_player.h"
#include "movie_runtime.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "display/backend.h"
#include "display/bitmap_data.h"
#include "display/display_object.h"
#include "script/bindings.h"

#include <cctype>
#include <cmath>

namespace fla {
namespace play {

namespace {

/// Text as a form sends it: "+" a space, "%41" the byte it gives.
std::string formDecoded(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '+')
            out += ' ';
        else if (c == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(text[i + 2])))
        {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else
            out += c;
    }
    return out;
}

/// An image file's pixels, premultiplied; null when it is not one.
std::shared_ptr<display::BitmapData> imageOf(MovieRuntime& runtime, const std::vector<uint8_t>& bytes)
{
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
    bool premultiplied = false;
    if (script::decodePng(bytes, width, height, pixels))
        premultiplied = false;
    else if (runtime.decodeImage && runtime.decodeImage(bytes, width, height, pixels))
        premultiplied = true;
    else
        return nullptr;
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height)
        return nullptr;
    auto data = std::make_shared<display::BitmapData>(width, height, true, 0);
    if (data->disposed())
        return nullptr;
    uint32_t* bits = data->bits();
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        bits[i] = premultiplied ? pixels[i] : display::premultiplied(pixels[i]);
    data->changed();
    return data;
}

} // namespace

void ActionsPlayer::loadMovie(const std::string& url, Clip& clip, const as3::Val& loader)
{
    MovieRuntime::Request request;
    request.url = url;
    const std::shared_ptr<Clip*> life = clip.life;
    load(request, [this, life, loader, url](const MovieRuntime::Response& response) {
        Clip* target = *life;
        if (!target || target->gone)
            return;
        // A MovieClipLoader's listeners hear how it goes.
        const auto tell = [this, loader](const std::string& message, std::vector<as3::Val> args) {
            if (!loader.isObject())
                return;
            args.insert(args.begin(), as3::Val::string(message));
            ++_runtime._scriptRuns;
            _interpreter->callMethod(loader, "broadcastMessage", args, "MovieClipLoader, " + message);
        };
        std::shared_ptr<LoadedMovie> movie;
        std::shared_ptr<display::BitmapData> image;
        if (response.ok && !response.bytes.empty())
        {
            if (_runtime.readMovie)
                movie = _runtime.readMovie(response.bytes, url);
            if (!movie)
                image = imageOf(_runtime, response.bytes);
        }
        if (!movie && !image)
        {
            tell("onLoadError", {as3::Val(_runtime.objectFor(*target)), as3::Val::string("URLNotFound")});
            return;
        }
        if (!movie)
        {
            movie = std::make_shared<LoadedMovie>();
            movie->url = url;
        }
        movie->bytes = response.bytes.size();
        _runtime.loadInto(*target, movie);
        // An image shows in the clip, at its corner.
        if (image && target->display && target->display->isContainer())
            static_cast<display::DisplayObjectContainer*>(target->display.get())
                ->addChild(std::make_shared<display::Bitmap>(image));
        const as3::Val object = _runtime.objectFor(*target);
        const as3::Val bytes = as3::Val::number(static_cast<double>(response.bytes.size()));
        tell("onLoadStart", {object});
        tell("onLoadProgress", {object, bytes, bytes});
        tell("onLoadComplete", {object, as3::Val::number(response.status)});
        if (loader.isObject())
            _runtime.afterFirstFrame(*target, [this, life, tell]() {
                if (Clip* loaded = *life)
                    tell("onLoadInit", {as3::Val(_runtime.objectFor(*loaded))});
            });
    });
}

void ActionsPlayer::loadVariables(const std::string& url, Clip& clip, int method)
{
    (void)method;
    MovieRuntime::Request request;
    request.url = url;
    const std::shared_ptr<Clip*> life = clip.life;
    load(request, [this, life](const MovieRuntime::Response& response) {
        Clip* target = *life;
        if (!target || target->gone || !response.ok)
            return;
        // name=value&name=value, each a string on the clip.
        const std::string text(response.bytes.begin(), response.bytes.end());
        const as3::Val object = _runtime.objectFor(*target);
        size_t at = 0;
        while (at <= text.size())
        {
            size_t end = text.find('&', at);
            if (end == std::string::npos)
                end = text.size();
            const std::string pair = text.substr(at, end - at);
            const size_t equals = pair.find('=');
            if (!pair.empty())
            {
                const std::string name = formDecoded(pair.substr(0, equals));
                const std::string value = equals == std::string::npos ? std::string() : formDecoded(pair.substr(equals + 1));
                if (!name.empty())
                    _interpreter->setMember(object, name, as3::Val::string(value));
            }
            at = end + 1;
        }
        // onClipEvent(data), and the clip's onData.
        if (!target->clipEvents.empty())
            _runtime.runClipEvent(*target, "data");
        callHandler(*target, "onData");
    });
}

as3::Val ActionsPlayer::loadNatives()
{
    as3::VM& vm = _vm;
    as3::avm1::Interpreter& interpreter = *_interpreter;
    MovieRuntime& runtime = _runtime;
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    // A target as MovieClipLoader takes one: a clip, a path from the
    // timeline whose script this is, or a level's number.
    const auto targetOf = [&runtime, &interpreter](const as3::Val& value, bool make) -> Clip* {
        if (Clip* clip = MovieRuntime::clipOfObject(value))
            return clip;
        if (value.isNumber())
            return std::isnan(value.num) ? nullptr : runtime.level(static_cast<int>(std::floor(value.num)), make);
        if (value.isString())
            return runtime.resolve(runtime._current ? *runtime._current : *runtime._root, interpreter.toString(value));
        return nullptr;
    };
    define("loadMovie", [this, &interpreter](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = MovieRuntime::clipOfObject(self);
        if (clip && !args.empty())
        {
            const std::string url = interpreter.toString(args[0]);
            if (url.empty())
                _runtime.loadInto(*clip, nullptr);
            else
                loadMovie(url, *clip, as3::Val());
        }
        return as3::Val();
    });
    define("unloadMovie", [this](as3::VM&, const as3::Val& self, as3::Args) -> as3::Val {
        if (Clip* clip = MovieRuntime::clipOfObject(self))
            _runtime.loadInto(*clip, nullptr);
        return as3::Val();
    });
    define("loadVariables", [this, &interpreter](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        Clip* clip = MovieRuntime::clipOfObject(self);
        if (clip && !args.empty())
        {
            const std::string method = args.size() > 1 ? interpreter.toString(args[1]) : std::string();
            loadVariables(interpreter.toString(args[0]), *clip, method == "POST" ? 2 : method == "GET" ? 1 : 0);
        }
        return as3::Val();
    });
    // getBytesLoaded and getBytesTotal: the movie's, all loaded.
    const auto bytesOf = [&runtime](const as3::Val& self) -> as3::Val {
        Clip* clip = MovieRuntime::clipOfObject(self);
        if (!clip)
            return as3::Val();
        return as3::Val::number(static_cast<double>(clip->movie ? clip->movie->bytes : runtime.bytesTotal));
    };
    define("getBytesLoaded", [bytesOf](as3::VM&, const as3::Val& self, as3::Args) { return bytesOf(self); });
    define("getBytesTotal", [bytesOf](as3::VM&, const as3::Val& self, as3::Args) { return bytesOf(self); });
    // MovieClipLoader: loadClip(url, target) is true once it starts.
    define("loadClip", [this, targetOf](as3::VM&, const as3::Val& self, as3::Args args) -> as3::Val {
        if (args.size() < 2)
            return as3::Val();
        if (!args[0].isString())
            return as3::Val::boolean(false);
        Clip* target = targetOf(args[1], true);
        if (!target)
            return as3::Val::boolean(false);
        loadMovie(args[0].str(), *target, self);
        return as3::Val::boolean(true);
    });
    define("unloadClip", [this, targetOf](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        if (args.empty())
            return as3::Val();
        Clip* target = targetOf(args[0], false);
        if (!target)
            return as3::Val::boolean(false);
        _runtime.loadInto(*target, nullptr);
        return as3::Val::boolean(true);
    });
    define("getProgress", [&vm, &interpreter, &runtime, targetOf](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        if (args.empty())
            return as3::Val();
        Clip* target = targetOf(args[0], false);
        const as3::Val out = vm.newObject();
        if (target)
        {
            const double bytes = static_cast<double>(target->movie ? target->movie->bytes : runtime.bytesTotal);
            interpreter.setMember(out, "bytesLoaded", as3::Val::number(bytes));
            interpreter.setMember(out, "bytesTotal", as3::Val::number(bytes));
        }
        return out;
    });
    return natives;
}

} // namespace play
} // namespace fla
