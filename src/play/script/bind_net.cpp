// flash.net: URLRequest, URLVariables, URLLoader, navigateToURL, sendToURL
// and SharedObject.

#include "native.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fla {
namespace play {
namespace script {

namespace {

/// flash.net.URLLoader: what it loaded, and in what form.
struct URLLoaderHost : DispatcherHost
{
    std::string dataFormat = "text";
    double bytesLoaded = 0.0;
    double bytesTotal = 0.0;
    Val data;
    /// Bumped by each load and close: what an older one brings is dropped.
    int generation = 0;
};

/// flash.net.SharedObject: its data and the file it is kept in.
struct SharedObjectHost : DispatcherHost
{
    std::string path;
    Val data;
};

bool isWeb(const std::string& url)
{
    std::string lower = url.substr(0, 8);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.compare(0, 7, "http://") == 0 || lower.compare(0, 8, "https://") == 0;
}

/// escape(), as URLVariables writes its values.
std::string escaped(const std::string& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value)
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
    return out;
}

std::string unescaped(const std::string& value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2])))
        {
            out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }
        else if (value[i] == '+')
            out += ' ';
        else
            out += value[i];
    }
    return out;
}

/// URLVariables.decode: name=value pairs, a name given twice an array.
void decodeVariables(VM& vm, const Val& target, const std::string& source)
{
    std::stringstream pairs(source);
    std::string pair;
    while (std::getline(pairs, pair, '&'))
    {
        if (pair.empty())
            continue;
        const size_t equals = pair.find('=');
        if (equals == std::string::npos)
            vm.throwError("Error", "Error #2101: The String passed to URLVariables.decode() must be a URL-encoded query "
                                   "string containing name/value pairs.",
                2101);
        const std::string name = unescaped(pair.substr(0, equals));
        const Val value = text(unescaped(pair.substr(equals + 1)));
        const Val* had = target->properties.lookup(name);
        if (!had)
            target->properties[name] = value;
        else if (had->isArray())
            target->properties[name]->elements.push_back(value);
        else
            target->properties[name] = vm.newArray({*had, value});
    }
}

std::string encodeVariables(VM& vm, const Val& source)
{
    std::string out;
    for (const auto& [name, value] : source->properties)
    {
        const std::vector<Val> values = value.isArray() ? value->elements : std::vector<Val>{value};
        for (const Val& each : values)
        {
            if (!out.empty())
                out += '&';
            out += escaped(name) + "=" + escaped(vm.toString(each));
        }
    }
    return out;
}

} // namespace

void Bindings::defineNet()
{
    // AMF: the formats, and the aliases classes are written with.
    vm.defineNativeClass("flash.net.ObjectEncoding")
        .constant("AMF0", number(0))
        .constant("AMF3", number(3))
        .constant("DEFAULT", number(3))
        .define();
    vm.defineFunction("flash.net.registerClassAlias", NATIVE(this) {
        if (args[0].isNullish())
            vm.throwError("TypeError", "Error #2007: Parameter aliasName must be non-null.", 2007);
        if (!args[1].isClass())
            vm.throwError("TypeError", "Error #2007: Parameter classObject must be non-null.", 2007);
        classAliases[vm.toString(args[0])] = args[1]->cls;
        return Val();
    }, 2);
    vm.defineFunction("flash.net.getClassByAlias", NATIVE(this) {
        const std::string alias = vm.toString(args[0]);
        if (std::shared_ptr<as3::ClassInfo> cls = classOfAlias(alias))
            return vm.classValue(cls.get());
        vm.throwError("ReferenceError", "Error #1014: Class " + alias + " could not be found.", 1014);
    }, 1);
    const auto constants = [this](const char* name, std::initializer_list<std::pair<const char*, const char*>> values) {
        NativeClassBuilder builder = vm.defineNativeClass(name);
        for (const auto& [constant, value] : values)
            builder.constant(constant, text(value));
        builder.final().define();
    };
    constants("flash.net.URLRequestMethod",
        {{"GET", "GET"}, {"POST", "POST"}, {"PUT", "PUT"}, {"DELETE", "DELETE"}, {"HEAD", "HEAD"}, {"OPTIONS", "OPTIONS"}});
    constants("flash.net.URLLoaderDataFormat", {{"BINARY", "binary"}, {"TEXT", "text"}, {"VARIABLES", "variables"}});
    constants("flash.net.SharedObjectFlushStatus", {{"FLUSHED", "flushed"}, {"PENDING", "pending"}});

    vm.defineNativeClass("flash.net.URLRequest")
        .final()
        .field("url", Val::null())
        .field("method", text("GET"))
        .field("data", Val::null())
        .field("contentType", text("application/x-www-form-urlencoded"))
        .field("digest", Val::null())
        .field("requestHeaders", Val::null())
        .field("followRedirects", boolean(true))
        .field("cacheResponse", boolean(true))
        .field("useCache", boolean(true))
        .field("authenticate", boolean(true))
        .field("manageCookies", boolean(true))
        .field("idleTimeout", number(0))
        .field("userAgent", text("Mozilla/5.0 (Phoenix)"))
        .constructor(NATIVE() {
            if (given(args, 0))
                vm.setProperty(self, "url", args[0].isNull() ? Val::null() : text(vm.toString(args[0])));
            vm.setProperty(self, "requestHeaders", vm.newArray());
            return Val();
        })
        .define();
    vm.defineNativeClass("flash.net.URLRequestHeader")
        .final()
        .field("name", text(""))
        .field("value", text(""))
        .constructor(NATIVE() {
            vm.setProperty(self, "name", text(stringArg(vm, args, 0)));
            vm.setProperty(self, "value", text(stringArg(vm, args, 1)));
            return Val();
        })
        .define();
    vm.defineNativeClass("flash.net.URLVariables")
        .dynamic()
        .constructor(NATIVE() {
            if (given(args, 0) && !args[0].isNull())
                decodeVariables(vm, self, vm.toString(args[0]));
            return Val();
        })
        .method("decode", NATIVE() {
            decodeVariables(vm, self, stringArg(vm, args, 0));
            return Val();
        }, 1)
        .prototypeMethod("toString", NATIVE() { return text(encodeVariables(vm, self)); })
        .define();

    // What a URLRequest asks for.
    const auto requestOf = [this](VM& vm, const Val& value) {
        MovieRuntime::Request request;
        if (value.isString())
        {
            request.url = value.str();
            return request;
        }
        if (!value.isObject())
            vm.throwError("TypeError", "Error #2007: Parameter request must be non-null.", 2007);
        const Val url = vm.getProperty(value, "url");
        request.url = url.isNullish() ? std::string() : vm.toString(url);
        request.method = vm.toString(vm.getProperty(value, "method"));
        std::transform(request.method.begin(), request.method.end(), request.method.begin(), ::toupper);
        if (request.method.empty() || request.method == "UNDEFINED")
            request.method = "GET";
        const Val contentType = vm.getProperty(value, "contentType");
        request.contentType = contentType.isNullish() ? std::string() : vm.toString(contentType);
        const Val headers = vm.getProperty(value, "requestHeaders");
        if (headers.isObject())
            for (const Val& header : headers->elements)
                if (header.isObject())
                    request.headers.emplace_back(
                        vm.toString(vm.getProperty(header, "name")), vm.toString(vm.getProperty(header, "value")));
        const Val data = vm.getProperty(value, "data");
        if (!data.isNullish())
        {
            if (const ByteArrayHost* bytes = byteArrayOf(data))
                request.body = bytes->bytes;
            else
            {
                const std::string body = vm.toString(data);
                if (request.method == "GET")
                    request.url += (request.url.find('?') == std::string::npos ? "?" : "&") + body;
                else
                    request.body.assign(body.begin(), body.end());
            }
        }
        (void)this;
        return request;
    };
    // A request to the web with something in it: told, never sent unseen.
    const auto tellSent = [this](const char* by, const MovieRuntime::Request& request) {
        if (!isWeb(request.url) || request.body.empty() || !runtime.notice)
            return;
        std::string body(request.body.begin(), request.body.begin() + std::min<size_t>(request.body.size(), 2000));
        if (request.body.size() > 2000)
            body += "...";
        runtime.notice(std::string(by) + " sent " + request.method + " " + request.url + " with: " + body);
    };

    vm.defineFunction("flash.net.navigateToURL", NATIVE(this, requestOf) {
        MovieRuntime::Request request = requestOf(vm, args[0]);
        const std::string window = stringArg(vm, args, 1, "_blank");
        if (!request.body.empty() && runtime.notice)
            runtime.notice("navigateToURL opened " + request.url + "; the " + request.method +
                " data it would send was not sent: " + std::string(request.body.begin(), request.body.end()));
        if (runtime.getURL)
            runtime.getURL(request.url, window);
        return Val();
    }, 1);
    vm.defineFunction("flash.net.sendToURL", NATIVE(this, requestOf, tellSent) {
        MovieRuntime::Request request = requestOf(vm, args[0]);
        tellSent("sendToURL", request);
        if (isWeb(request.url) && runtime.fetch)
            runtime.fetch(request, [](const MovieRuntime::Response&) {});
        return Val();
    }, 1);

    // URLLoader.
    const auto loaderOf = [](VM& vm, const Val& self) -> URLLoaderHost& {
        return hostOf<URLLoaderHost>(vm, self, "URLLoader");
    };
    const auto startLoad = [this, requestOf, tellSent, loaderOf](VM& vm, const Val& self, const Val& requestValue) {
        URLLoaderHost& host = loaderOf(vm, self);
        const MovieRuntime::Request request = requestOf(vm, requestValue);
        tellSent("URLLoader", request);
        const int generation = ++host.generation;
        const Val target = self;
        post([this, target, generation]() {
            const URLLoaderHost* h = target.host<URLLoaderHost>();
            if (h && h->generation == generation)
                send(target, "flash.events.Event", "open");
        });
        load(request, [this, target, generation, url = request.url](const MovieRuntime::Response& response) {
            URLLoaderHost* h = target.host<URLLoaderHost>();
            if (!h || h->generation != generation)
                return;
            if (response.status != 0)
            {
                Val status = makeEvent("flash.events.HTTPStatusEvent", "httpStatus");
                status.host<EventHost>()->status = response.status;
                dispatchScriptEvent(*h->dispatcher, status);
            }
            if (!response.ok)
            {
                const std::string message = "Error #2032: Stream Error. URL: " + url;
                if (!h->dispatcher->hasEventListener("ioError"))
                {
                    runtime.tell("Error #2044: Unhandled ioError:. text=" + message, nullptr, 0);
                    return;
                }
                Val error = makeEvent("flash.events.IOErrorEvent", "ioError");
                error.host<EventHost>()->text = message;
                error.host<EventHost>()->errorID = 2032;
                dispatchScriptEvent(*h->dispatcher, error);
                return;
            }
            h->bytesLoaded = h->bytesTotal = static_cast<double>(response.bytes.size());
            Val progress = makeEvent("flash.events.ProgressEvent", "progress");
            progress.host<EventHost>()->bytesLoaded = h->bytesLoaded;
            progress.host<EventHost>()->bytesTotal = h->bytesTotal;
            dispatchScriptEvent(*h->dispatcher, progress);
            try
            {
                if (h->dataFormat == "binary")
                    h->data = newByteArray(response.bytes);
                else
                {
                    std::string body(response.bytes.begin(), response.bytes.end());
                    if (body.size() >= 3 && body.compare(0, 3, "\xEF\xBB\xBF") == 0)
                        body.erase(0, 3);
                    if (h->dataFormat == "variables")
                        h->data = this->vm.construct(this->vm.findDefinition("flash.net.URLVariables"), {text(body)});
                    else
                        h->data = text(body);
                }
            }
            catch (as3::ScriptException& exception)
            {
                report(exception, "URLLoader " + url);
                return;
            }
            send(target, "flash.events.Event", "complete");
        });
    };
    vm.defineNativeClass("flash.net.URLLoader")
        .extends("flash.events.EventDispatcher")
        .constructor(NATIVE(startLoad) {
            if (!self.host<URLLoaderHost>())
            {
                auto host = std::make_shared<URLLoaderHost>();
                host->dispatcher = std::make_shared<display::EventDispatcher>();
                host->dispatcher->script = self.ref;
                self->host = host;
            }
            if (given(args, 0) && !args[0].isNull())
                startLoad(vm, self, args[0]);
            return Val();
        })
        .property("data", NATIVE(loaderOf) {
            const Val& data = loaderOf(vm, self).data;
            return data.isUndefined() ? Val() : data;
        }, NATIVE(loaderOf) {
            loaderOf(vm, self).data = args[0];
            return Val();
        })
        .property("dataFormat", NATIVE(loaderOf) { return text(loaderOf(vm, self).dataFormat); }, NATIVE(loaderOf) {
            loaderOf(vm, self).dataFormat = vm.toString(args[0]);
            return Val();
        })
        .property("bytesLoaded", NATIVE(loaderOf) { return number(loaderOf(vm, self).bytesLoaded); }, NATIVE(loaderOf) {
            loaderOf(vm, self).bytesLoaded = vm.toNumber(args[0]);
            return Val();
        })
        .property("bytesTotal", NATIVE(loaderOf) { return number(loaderOf(vm, self).bytesTotal); }, NATIVE(loaderOf) {
            loaderOf(vm, self).bytesTotal = vm.toNumber(args[0]);
            return Val();
        })
        .method("load", NATIVE(startLoad) {
            startLoad(vm, self, args[0]);
            return Val();
        }, 1)
        .method("close", NATIVE(loaderOf) {
            ++loaderOf(vm, self).generation;
            return Val();
        })
        .define();

    // SharedObject: kept as JSON in a file of Phoenix's, by the movie and
    // the name.
    std::shared_ptr<std::map<std::string, Val>> opened = std::make_shared<std::map<std::string, Val>>();
    const auto pathOf = [this](const std::string& name, const std::string& localPath) {
        if (runtime.sharedObjectFolder.empty())
            return std::string();
        std::string movie = runtime.url;
        const size_t slash = movie.find_last_of("/\\");
        if (slash != std::string::npos)
            movie = movie.substr(slash + 1);
        if (movie.empty())
            movie = "movie";
        std::string where = localPath;
        for (char& c : where)
            if (c == ':' || c == '\\')
                c = '_';
        while (!where.empty() && where[0] == '/')
            where.erase(0, 1);
        namespace fs = std::filesystem;
        fs::path path = fs::u8path(runtime.sharedObjectFolder) / fs::u8path(movie);
        if (!where.empty())
            path /= fs::u8path(where);
        return (path / fs::u8path(name + ".json")).u8string();
    };
    const auto flush = [](VM& vm, SharedObjectHost& host) {
        if (host.path.empty())
            return true;
        namespace fs = std::filesystem;
        const Val json = vm.callMethod(vm.findDefinition("JSON"), "stringify", {host.data});
        std::error_code error;
        fs::create_directories(fs::u8path(host.path).parent_path(), error);
        std::ofstream out(fs::u8path(host.path), std::ios::binary);
        out << vm.toString(json);
        return static_cast<bool>(out);
    };
    _flushShared = [this, opened, flush]() {
        for (auto& [key, object] : *opened)
            if (SharedObjectHost* host = object.host<SharedObjectHost>())
            {
                try
                {
                    flush(vm, *host);
                }
                catch (...)
                {
                }
            }
        opened->clear();
    };
    const auto shared = [](VM& vm, const Val& self) -> SharedObjectHost& {
        return hostOf<SharedObjectHost>(vm, self, "SharedObject");
    };
    vm.defineNativeClass("flash.net.SharedObject")
        .extends("flash.events.EventDispatcher")
        .staticVar("defaultObjectEncoding", number(3))
        .constructor(NATIVE() {
            if (!self.host<SharedObjectHost>())
                vm.throwError("ArgumentError", "Error #2012: SharedObject class cannot be instantiated.", 2012);
            return Val();
        })
        .staticMethod("getLocal", NATIVE(this, opened, pathOf) {
            const std::string name = stringArg(vm, args, 0);
            const std::string localPath = stringArg(vm, args, 1);
            const std::string key = localPath + "/" + name;
            const auto found = opened->find(key);
            if (found != opened->end())
                return found->second;
            auto host = std::make_shared<SharedObjectHost>();
            host->dispatcher = std::make_shared<display::EventDispatcher>();
            host->path = pathOf(name, localPath);
            host->data = vm.newObject();
            if (!host->path.empty())
            {
                std::ifstream in(std::filesystem::u8path(host->path), std::ios::binary);
                if (in)
                {
                    std::stringstream saved;
                    saved << in.rdbuf();
                    try
                    {
                        const Val data = vm.callMethod(vm.findDefinition("JSON"), "parse", {text(saved.str())});
                        if (data.isObject())
                            host->data = data;
                    }
                    catch (as3::ScriptException&)
                    {
                    }
                }
            }
            Val made = instance("flash.net.SharedObject", host);
            host->dispatcher->script = made.ref;
            (*opened)[key] = made;
            return made;
        }, 1)
        .staticMethod("getRemote", NATIVE(this) {
            auto host = std::make_shared<SharedObjectHost>();
            host->dispatcher = std::make_shared<display::EventDispatcher>();
            host->data = vm.newObject();
            Val made = instance("flash.net.SharedObject", host);
            host->dispatcher->script = made.ref;
            return made;
        }, 1)
        .getter("data", NATIVE(shared) { return shared(vm, self).data; })
        .getter("size", NATIVE(shared) {
            const Val json = vm.callMethod(vm.findDefinition("JSON"), "stringify", {shared(vm, self).data});
            const std::string text = vm.toString(json);
            return number(text == "{}" ? 0 : static_cast<double>(text.size()));
        })
        .property("objectEncoding", NATIVE() { return number(3); }, NATIVE() { return Val(); })
        .property("client", NATIVE() { return self; }, NATIVE() { return Val(); })
        .property("fps", NATIVE() { return number(0); }, NATIVE() { return Val(); })
        .method("flush", NATIVE(shared, flush) { return text(flush(vm, shared(vm, self)) ? "flushed" : "pending"); })
        .method("clear", NATIVE(shared) {
            SharedObjectHost& host = shared(vm, self);
            host.data = vm.newObject();
            if (!host.path.empty())
            {
                std::error_code error;
                std::filesystem::remove(std::filesystem::u8path(host.path), error);
            }
            return Val();
        })
        .method("close", NATIVE() { return Val(); })
        .method("connect", NATIVE() { return Val(); })
        .method("send", NATIVE() { return Val(); })
        .method("setDirty", NATIVE() { return Val(); })
        .method("setProperty", NATIVE(shared) {
            vm.setProperty(shared(vm, self).data, stringArg(vm, args, 0), args[1]);
            return Val();
        }, 1)
        .define();
}

} // namespace script
} // namespace play
} // namespace fla
