// HTML extension panels' Node.js: resources/cep/adobe_cep.js and node.js run
// in QuickJS as a panel's page would run them, their calls answered by the
// same cep::NativeHost the panels use, so files, processes, zlib and the
// JavaScript modules are tried without a web view.
#include "test_util.h"

#include "../src/util/cep_native.h"
#include "../src/util/json.h"

#include <quickjs.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

const fs::path kCep = fs::path(__FILE__).parent_path().parent_path() / "resources" / "cep";

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// What a web view gives a page that the scripts ask for: the console
/// (into __out), timers on a clock the test moves, and the prompt the
/// bridge calls Phoenix through.
const char* const kHost = R"JS(
var window = globalThis;
var __out = [];
var console = {log: function () { __out.push(Array.prototype.join.call(arguments, " ")); }};
console.info = console.warn = console.error = console.log;
var document = {addEventListener: function () {}};
var __timers = [], __timerId = 0, __clock = 0;
function setTimeout(fn, ms) {
    var args = Array.prototype.slice.call(arguments, 2);
    var id = ++__timerId;
    __timers.push({id: id, at: __clock + (ms || 0), fn: fn, args: args});
    return id;
}
function clearTimeout(id) {
    __timers = __timers.filter(function (t) { return t.id !== id; });
}
function setInterval(fn, ms) {
    var id = ++__timerId;
    function again() { __timers.push({id: id, at: __clock + (ms || 1), fn: run, args: []}); }
    function run() { again(); fn(); }
    again();
    return id;
}
var clearInterval = clearTimeout;
function __runTimers(step) {
    __clock += step;
    for (var n = 0; n < 10000; n++) {
        __timers.sort(function (a, b) { return a.at - b.at || a.id - b.id; });
        if (!__timers.length || __timers[0].at > __clock)
            break;
        var t = __timers.shift();
        t.fn.apply(null, t.args);
    }
}
var __MARK = "\u0001phoenix-call\u0001";
function prompt(message) {
    message = String(message);
    return message.indexOf(__MARK) === 0 ? __native(message.slice(__MARK.length)) : null;
}
if (typeof atob !== "function") {
    var __B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var btoa = function (s) {
        var out = "";
        for (var i = 0; i < s.length; i += 3) {
            var n = (s.charCodeAt(i) << 16) | ((s.charCodeAt(i + 1) || 0) << 8) | (s.charCodeAt(i + 2) || 0);
            out += __B64[(n >> 18) & 63] + __B64[(n >> 12) & 63] + (i + 1 < s.length ? __B64[(n >> 6) & 63] : "=") +
                (i + 2 < s.length ? __B64[n & 63] : "=");
        }
        return out;
    };
    var atob = function (s) {
        var out = "", buffer = 0, bits = 0;
        for (var i = 0; i < s.length; i++) {
            var v = __B64.indexOf(s.charAt(i));
            if (v < 0) continue;
            buffer = ((buffer << 6) | v) & 0xFFFFFF;
            bits += 6;
            if (bits >= 8) { bits -= 8; out += String.fromCharCode((buffer >> bits) & 255); }
        }
        return out;
    };
}
)JS";

/// A panel's page: the shims loaded into a QuickJS context, its calls to
/// Phoenix answered by a NativeHost (and web requests by canned replies).
class Page
{
public:
    Page(bool node, const fs::path& folder)
        : _host([&]() {
            cep::NativeHost::Options options;
            options.node = node;
            options.root = utf8(folder);
            options.page = utf8(folder / "index.html");
            options.execPath = "phoenix";
            return options;
        }())
    {
        _rt = JS_NewRuntime();
        JS_SetMaxStackSize(_rt, 4 * 1024 * 1024);
        _ctx = JS_NewContext(_rt);
        JS_SetContextOpaque(_ctx, this);
        JSValue global = JS_GetGlobalObject(_ctx);
        JS_SetPropertyStr(_ctx, global, "__native", JS_NewCFunction(_ctx, &Page::nativeCall, "__native", 1));
        JS_FreeValue(_ctx, global);
        ok = eval(kHost, "<host>") && eval(readText(kCep / "adobe_cep.js"), "adobe_cep.js");
        if (node)
            ok = ok && eval(readText(kCep / "node.js"), "node.js");
        ok = ok && eval("var __dir = " + json::quote(utf8(folder)) + ";", "<dir>");
    }
    ~Page()
    {
        JS_FreeContext(_ctx);
        JS_FreeRuntime(_rt);
    }

    bool ok = false;
    std::string error;
    /// The web requests the page made.
    std::vector<json::Value> requests;

    bool eval(const std::string& code, const std::string& where)
    {
        JSValue result = JS_Eval(_ctx, code.c_str(), code.size(), where.c_str(), JS_EVAL_TYPE_GLOBAL);
        const bool good = !JS_IsException(result);
        if (!good)
            error = exception();
        JS_FreeValue(_ctx, result);
        settle();
        return good;
    }

    /// Runs \a code, then turns the event loop (timers, processes, web
    /// replies) until it sets __done or \a seconds pass; what it left in
    /// __result.
    std::string run(const std::string& code, double seconds = 20)
    {
        if (!eval("var __done = false; var __result = '';\n" + code, "<test>"))
            return "threw: " + error;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
        while (global("__done") != "true")
        {
            if (std::chrono::steady_clock::now() > until)
                return "timed out: " + global("__result") + " / " + output();
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return global("__result");
    }

    std::string output() { return evaluate("__out.join('\\n')"); }

    cep::NativeHost& host() { return _host; }

private:
    void pump()
    {
        const json::Value events = _host.takeEvents();
        if (!events.toArray().empty())
            eval("window.__phoenixCep.processEvents(" + events.dump() + ");", "<events>");
        if (!_web.empty())
        {
            json::Value list(json::Value::Array{});
            for (const json::Value& event : _web)
                list.push(event);
            _web.clear();
            eval("window.__phoenixCep.processEvents(" + list.dump() + ");", "<web>");
        }
        eval("__runTimers(10);", "<timers>");
    }

    /// A web request answered with a page of its own: 200, text, its
    /// address and method echoed back.
    json::Value fakeRequest(const json::Value& args)
    {
        requests.push_back(args[0]);
        const int id = (1 << 20) + static_cast<int>(requests.size());
        json::Value response;
        response.set("id", id);
        response.set("kind", "response");
        response.set("status", 200);
        response.set("headers", json::Value::Array{json::Value::Array{"Content-Type", "text/plain"},
                                    json::Value::Array{"Set-Cookie", "a=1"}, json::Value::Array{"Set-Cookie", "b=2"}});
        _web.push_back(response);
        json::Value data;
        data.set("id", id);
        data.set("kind", "data");
        data.set("data", cep::NativeHost::toBase64(args[0]["method"].toString() + " " + args[0]["url"].toString() +
                                                   " " + cep::NativeHost::fromBase64(args[0]["body"].toString())));
        _web.push_back(data);
        json::Value end;
        end.set("id", id);
        end.set("kind", "end");
        _web.push_back(end);
        json::Value value;
        value.set("id", id);
        json::Value reply;
        reply.set("value", value);
        return reply;
    }

    static JSValue nativeCall(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        auto* page = static_cast<Page*>(JS_GetContextOpaque(ctx));
        const char* text = argc > 0 ? JS_ToCString(ctx, argv[0]) : nullptr;
        const std::string request = text ? text : "";
        JS_FreeCString(ctx, text);
        const json::Value call = json::Value::parse(request);
        const std::string name = call["name"].toString();
        std::string reply = "{}";
        if (cep::NativeHost::handles(name))
            reply = page->_host.call(name, call["args"]).dump();
        else if (name == "net.request")
            reply = page->fakeRequest(call["args"]).dump();
        return JS_NewStringLen(ctx, reply.data(), reply.size());
    }

    void settle()
    {
        JSContext* pending = nullptr;
        for (int i = 0; i < 10000 && JS_ExecutePendingJob(_rt, &pending) > 0; ++i) {}
    }
    std::string exception()
    {
        JSValue thrown = JS_GetException(_ctx);
        const char* text = JS_ToCString(_ctx, thrown);
        std::string out = text ? text : "?";
        JS_FreeCString(_ctx, text);
        JSValue stack = JS_GetPropertyStr(_ctx, thrown, "stack");
        if (!JS_IsUndefined(stack))
        {
            const char* where = JS_ToCString(_ctx, stack);
            out += std::string("\n") + (where ? where : "");
            JS_FreeCString(_ctx, where);
        }
        JS_FreeValue(_ctx, stack);
        JS_FreeValue(_ctx, thrown);
        return out;
    }
    std::string global(const char* name)
    {
        JSValue object = JS_GetGlobalObject(_ctx);
        JSValue value = JS_GetPropertyStr(_ctx, object, name);
        const char* text = JS_ToCString(_ctx, value);
        std::string out = text ? text : "";
        JS_FreeCString(_ctx, text);
        JS_FreeValue(_ctx, value);
        JS_FreeValue(_ctx, object);
        return out;
    }
    std::string evaluate(const std::string& expression)
    {
        eval("var __value = String(" + expression + ");", "<value>");
        return global("__value");
    }

    cep::NativeHost _host;
    std::vector<json::Value> _web;
    JSRuntime* _rt = nullptr;
    JSContext* _ctx = nullptr;
};

bool same(const std::string& got, const std::string& expected)
{
    if (got != expected)
        std::printf("    got:      %s\n    expected: %s\n", got.c_str(), expected.c_str());
    return got == expected;
}

/// A folder of its own for each test, removed after.
struct Folder
{
    fs::path path;
    explicit Folder(const char* name)
    {
        path = fs::temp_directory_path() / (std::string("phoenix_cep_") + name);
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
        path = fs::canonical(path);
        path.make_preferred();
    }
    ~Folder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST(cep_json_reads_and_writes_what_the_bridge_carries)
{
    bool ok = false;
    const json::Value v = json::Value::parse(R"({"a":[1,2.5,"x\u00e9\n",true,null],"b":{"c":-3}})", &ok);
    CHECK(ok);
    CHECK(v["a"][1].toNumber() == 2.5);
    CHECK(v["a"][2].toString() == "x\xc3\xa9\n");
    CHECK(v["a"][3].toBool());
    CHECK(v["a"][4].isNull());
    CHECK(v["b"]["c"].toInt() == -3);
    CHECK(same(v.dump(), R"({"a":[1,2.5,"x)" "\xc3\xa9" R"(\n",true,null],"b":{"c":-3}})"));
    json::Value::parse("{\"a\":", &ok);
    CHECK(!ok);
    CHECK(cep::NativeHost::fromBase64(cep::NativeHost::toBase64("hello, panel")) == "hello, panel");
    CHECK(cep::NativeHost::toBase64("ab") == "YWI=");
}

TEST(cep_node_is_only_there_for_panels_that_ask)
{
    Folder folder("refused");
    Page page(false, folder.path);
    CHECK(page.ok);
    // The page has no Node.js, and Phoenix refuses its calls all the same.
    CHECK(same(page.run("__result = typeof require + ' ' + typeof process + ' ' + typeof Buffer; __done = true;"),
        "undefined undefined undefined"));
    const json::Value reply = page.host().call("node.fs.read", json::Value::Array{utf8(folder.path / "x"), "utf8"});
    CHECK(reply["code"].toString() == "ERR_ACCESS_DENIED");
    // cep.process is part of CEP for every panel.
    CHECK(same(page.run("__result = typeof cep.process.createProcess; __done = true;"), "function"));
}

TEST(cep_node_path_events_util_and_buffer)
{
    Folder folder("modules");
    Page page(true, folder.path);
    CHECK(page.ok);
    if (!page.ok)
        std::printf("    %s\n", page.error.c_str());
    CHECK(same(page.run(R"JS(
        var path = require("path"), p = path.posix, w = path.win32;
        var out = [
            p.join("/a/b", "../c", "./d.txt"), p.resolve("/x/y", "z"), p.relative("/a/b/c", "/a/d"),
            p.dirname("/a/b/c.txt"), p.basename("/a/b/c.txt", ".txt"), p.extname("x.tar.gz"),
            JSON.stringify(p.parse("/home/u/f.txt")), p.normalize("a//b/../c/"), p.isAbsolute("a/b"),
            w.join("C:\\a", "..\\b", "c.txt"), w.resolve("C:\\a\\b", "..\\c"), w.relative("C:\\a\\b", "C:\\a\\c\\d"),
            w.dirname("C:\\a\\b"), w.dirname("C:\\a"), w.basename("C:\\a\\b.js", ".js"), w.isAbsolute("C:/x"),
            w.normalize("\\\\server\\share\\a\\..\\b"), JSON.stringify(w.parse("C:\\p\\f.txt")),
            p.format({dir: "/a", name: "b", ext: ".c"}), require("node:path") === path
        ];
        __result = out.join("|");
        __done = true;
    )JS"),
        "/a/c/d.txt|/x/y/z|../../d|/a/b|c|.gz|{\"root\":\"/\",\"dir\":\"/home/u\",\"base\":\"f.txt\",\"ext\":\".txt\","
        "\"name\":\"f\"}|a/c/|false|C:\\b\\c.txt|C:\\a\\c|..\\c\\d|C:\\a|C:\\|b|true|\\\\server\\share\\b|"
        "{\"root\":\"C:\\\\\",\"dir\":\"C:\\\\p\",\"base\":\"f.txt\",\"ext\":\".txt\",\"name\":\"f\"}|/a/b.c|true"));

    CHECK(same(page.run(R"JS(
        var EventEmitter = require("events");
        var util = require("util");
        function Thing() { EventEmitter.call(this); }
        util.inherits(Thing, EventEmitter);
        var t = new Thing(), seen = [];
        t.on("x", function (a, b) { seen.push("on " + a + b); });
        t.once("x", function () { seen.push("once"); });
        t.prependListener("x", function () { seen.push("first"); });
        t.emit("x", 1, 2);
        t.emit("x", 3, 4);
        class Modern extends EventEmitter {}
        var m = new Modern();
        var f = function () { seen.push("removed?"); };
        m.on("y", f); m.off("y", f); m.emit("y");
        var threw = false;
        try { m.emit("error", new Error("boom")); } catch (e) { threw = e.message === "boom"; }
        seen.push(threw, t.listenerCount("x"), util.format("%s=%d %j %%", "n", 42, {a: 1}, "extra"));
        seen.push(util.inspect({a: [1, "s"], b: null}));
        util.promisify(function (v, cb) { setTimeout(function () { cb(null, v * 2); }, 5); })(21).then(function (v) {
            seen.push("promised " + v);
            __result = seen.join("|");
            __done = true;
        });
    )JS"),
        "first|on 12|once|first|on 34|true|2|n=42 {\"a\":1} % extra|{ a: [ 1, 's' ], b: null }|promised 42"));

    CHECK(same(page.run(R"JS(
        var B = require("buffer").Buffer;
        var b = Buffer.from("h\u00e9llo \u2603", "utf8");
        var out = [B === Buffer, b.length, b.toString("hex"), b.toString("base64"), Buffer.from(b.toString("base64"), "base64").toString(),
            Buffer.from("ff00", "hex")[0], Buffer.isBuffer(b), b.slice(0, 1).toString(), Buffer.concat([Buffer.from("a"), Buffer.from("bc")]).toString(),
            Buffer.alloc(3, "ab").toString(), Buffer.byteLength("\u2603"), b.indexOf("llo"), b.equals(Buffer.from(b))];
        var n = Buffer.alloc(8);
        n.writeUInt32LE(0x01020304, 0); n.writeInt16BE(-2, 4);
        out.push(n.readUInt32LE(0).toString(16), n.readInt16BE(4), n.readUInt8(0), JSON.stringify(Buffer.from([1, 2])));
        var StringDecoder = require("string_decoder").StringDecoder;
        var d = new StringDecoder("utf8"), snow = Buffer.from("\u2603");
        out.push(d.write(snow.slice(0, 1)) + "|" + d.write(snow.slice(1)) + "|" + d.end());
        __result = out.join(",");
        __done = true;
    )JS"),
        "true,10,68c3a96c6c6f20e29883,aMOpbGxvIOKYgw==,h\xc3\xa9llo \xe2\x98\x83,255,true,h,abc,aba,3,3,true,1020304,-2,4,"
        "{\"type\":\"Buffer\",\"data\":[1,2]},|\xe2\x98\x83|"));
}

TEST(cep_node_fs_reads_and_writes_files)
{
    Folder folder("fs");
    Page page(true, folder.path);
    CHECK(page.ok);
    CHECK(same(page.run(R"JS(
        var fs = require("fs"), path = require("path");
        var dir = path.join(__dir, "a", "b");
        var out = [];
        out.push(fs.mkdirSync(dir, {recursive: true}) === path.join(__dir, "a"));
        var file = path.join(dir, "note.txt");
        fs.writeFileSync(file, "caf\u00e9\n");
        fs.appendFileSync(file, "more");
        out.push(fs.readFileSync(file, "utf8"), fs.readFileSync(file).length, fs.existsSync(file), fs.existsSync(file + "x"));
        var stat = fs.statSync(file);
        out.push(stat.isFile(), stat.isDirectory(), stat.size, stat.mtime instanceof Date, fs.statSync(dir).isDirectory());
        fs.writeFileSync(path.join(dir, "bytes.bin"), Buffer.from([0, 1, 254, 255]));
        out.push(fs.readFileSync(path.join(dir, "bytes.bin")).toString("hex"));
        out.push(fs.readdirSync(dir).join("+"));
        out.push(fs.readdirSync(path.join(__dir, "a"), {withFileTypes: true}).map(function (e) { return e.name + ":" + e.isDirectory(); }).join("+"));
        fs.copyFileSync(file, path.join(dir, "copy.txt"));
        fs.renameSync(path.join(dir, "copy.txt"), path.join(dir, "moved.txt"));
        out.push(fs.readFileSync(path.join(dir, "moved.txt"), {encoding: "utf8"}).length);
        try { fs.readFileSync(path.join(dir, "missing.txt")); } catch (e) { out.push(e.code, e.syscall); }
        try { fs.mkdirSync(dir); } catch (e) { out.push(e.code); }
        try { fs.copyFileSync(file, path.join(dir, "moved.txt"), fs.constants.COPYFILE_EXCL); } catch (e) { out.push(e.code); }
        fs.unlinkSync(path.join(dir, "moved.txt"));
        out.push(fs.existsSync(path.join(dir, "moved.txt")));
        var fd = fs.openSync(path.join(dir, "fd.txt"), "w");
        fs.writeSync(fd, "one ");
        fs.writeSync(fd, Buffer.from("two"));
        fs.closeSync(fd);
        out.push(fs.readFileSync(path.join(dir, "fd.txt"), "utf8"));
        var tmp = fs.mkdtempSync(path.join(__dir, "tmp-"));
        out.push(path.basename(tmp).length, fs.statSync(tmp).isDirectory());
        fs.readFile(file, "utf8", function (error, text) {
            out.push("cb " + (error === null) + " " + text.length);
            fs.promises.readdir(path.join(__dir, "a")).then(function (names) {
                out.push("promise " + names.join("+"));
                fs.rmSync(path.join(__dir, "a"), {recursive: true, force: true});
                out.push(fs.existsSync(path.join(__dir, "a")));
                fs.stat(path.join(__dir, "nope"), function (error) {
                    out.push("err " + error.code);
                    __result = out.join(",");
                    __done = true;
                });
            });
        });
    )JS"),
        "true,caf\xc3\xa9\nmore,10,true,false,true,false,10,true,true,0001feff,bytes.bin+note.txt,b:true,9,ENOENT,"
        "open,EEXIST,EEXIST,false,one two,10,true,cb true 9,promise b,false,err ENOENT"));

    // Streams: a file read into another through pipe.
    CHECK(same(page.run(R"JS(
        var fs = require("fs"), path = require("path");
        var from = path.join(__dir, "in.txt"), to = path.join(__dir, "out.txt");
        fs.writeFileSync(from, "streamed text");
        var chunks = [];
        var reader = fs.createReadStream(from, {encoding: "utf8"});
        reader.on("data", function (c) { chunks.push(c); });
        reader.on("end", function () {
            var writer = fs.createWriteStream(to);
            fs.createReadStream(from).pipe(writer);
            writer.on("finish", function () {
                __result = chunks.join("") + "/" + fs.readFileSync(to, "utf8");
                __done = true;
            });
        });
    )JS"),
        "streamed text/streamed text"));
}

TEST(cep_node_require_loads_modules_from_files)
{
    Folder folder("require");
    write(folder.path / "lib" / "greet.js",
        "var data = require('./data.json');\n"
        "var helper = require('helper');\n"
        "module.exports = function (name) { return data.greeting + ', ' + helper.shout(name); };\n"
        "module.exports.where = require('path').basename(__dirname);\n");
    write(folder.path / "lib" / "data.json", "{\"greeting\": \"Hello\"}");
    write(folder.path / "node_modules" / "helper" / "package.json", "{\"main\": \"src/main.js\"}");
    write(folder.path / "node_modules" / "helper" / "src" / "main.js",
        "exports.shout = function (s) { return s.toUpperCase() + '!'; };\nexports.count = (exports.count || 0) + 1;\n");
    write(folder.path / "broken.js", "throw new Error('broken on purpose');\n");
    Page page(true, folder.path);
    CHECK(page.ok);
    CHECK(same(page.run(R"JS(
        var greet = require("./lib/greet");
        var helper = require("helper");
        var out = [greet("panel"), greet.where, helper.count, require("./lib/greet.js") === greet, __dirname === __dir,
            require.resolve("helper").indexOf("main.js") > 0, typeof module];
        try { require("./missing"); } catch (e) { out.push(e.code); }
        try { require("./broken"); } catch (e) { out.push(e.message); }
        try { require("net").connect(); } catch (e) { out.push(e.code); }
        out.push(typeof cep_node.require, cep_node.Buffer === Buffer, global === window);
        // Packages that wrap Module's own functions see require use them.
        var Module = require("module").Module, lookup = Module._resolveLookupPaths.bind(Module), asked = 0;
        Module._resolveLookupPaths = function (request, parent) { asked++; return lookup(request, parent); };
        out.push(require("helper") === helper, asked, typeof require("module").createRequire(__dir + "/x.js")("./lib/greet"));
        __result = out.join(",");
        __done = true;
    )JS"),
        "Hello, PANEL!,lib,1,true,true,true,undefined,MODULE_NOT_FOUND,broken on purpose,ERR_NOT_AVAILABLE,function,true,"
        "true,true,1,function"));
}

TEST(cep_node_os_process_crypto_and_zlib)
{
    Folder folder("system");
    Page page(true, folder.path);
    CHECK(page.ok);
    CHECK(same(page.run(R"JS(
        var os = require("os"), fs = require("fs");
        var out = [os.platform() === process.platform, os.EOL === (process.platform === "win32" ? "\r\n" : "\n"),
            os.homedir().length > 0, fs.statSync(os.tmpdir()).isDirectory(), os.cpus().length > 0, os.totalmem() > 0,
            typeof process.env.PATH === "string", process.versions.node, process.cwd().length > 0,
            process.argv[1].indexOf("index.html") > 0];
        if (process.platform === "win32")
            out.push(process.env.path === process.env.PATH);
        else
            out.push(true);
        var order = [];
        process.nextTick(function () { order.push("tick"); });
        setTimeout(function () { order.push("timeout"); }, 0);
        Promise.resolve().then(function () { order.push("promise"); });
        order.push("sync");
        setTimeout(function () {
            out.push(order.join(" "));
            var crypto = require("crypto");
            out.push(crypto.createHash("md5").update("abc").digest("hex"));
            out.push(crypto.createHash("sha1").update("abc").digest("hex"));
            out.push(crypto.createHash("sha256").update("a").update("bc").digest("hex"));
            out.push(crypto.createHmac("sha256", "key").update("The quick brown fox jumps over the lazy dog").digest("hex"));
            out.push(crypto.randomBytes(16).length, /^[0-9a-f-]{36}$/.test(crypto.randomUUID()));
            var zlib = require("zlib");
            var text = "Phoenix panels ".repeat(50);
            var packed = zlib.gzipSync(text);
            out.push(packed[0] === 0x1f && packed[1] === 0x8b, packed.length < text.length, zlib.gunzipSync(packed).toString() === text,
                zlib.inflateSync(zlib.deflateSync(Buffer.from("xyz"))).toString(), zlib.inflateRawSync(zlib.deflateRawSync("raw")).toString());
            try { zlib.inflateSync(Buffer.from("not zlib")); } catch (e) { out.push(e.code); }
            var url = require("url"), qs = require("querystring");
            var u = url.parse("https://user@example.com:8080/p/a?x=1&y=two#h", true);
            out.push(u.hostname, u.port, u.pathname, u.query.y, u.hash, qs.stringify({a: [1, 2], b: "c d"}));
            out.push(url.fileURLToPath(url.pathToFileURL(__dir + "/f.txt")) === require("path").join(__dir, "f.txt"));
            __result = out.join(",");
            __done = true;
        }, 1);
    )JS"),
        "true,true,true,true,true,true,true,15.9.0,true,true,true,sync tick promise timeout,"
        "900150983cd24fb0d6963f7d28e17f72,a9993e364706816aba3e25717850c26c9cd0d89d,"
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad,"
        "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8,16,true,true,true,true,xyz,raw,Z_DATA_ERROR,"
        "example.com,8080,/p/a,two,#h,a=1&a=2&b=c%20d,true"));
}

TEST(cep_node_child_process_runs_programs)
{
    Folder folder("child");
    Page page(true, folder.path);
    CHECK(page.ok);
    // exec and execSync through the shell; spawn with output as it comes,
    // input written to it and its exit code; a program that is not there.
    CHECK(same(page.run(R"JS(
        var cp = require("child_process");
        var win = process.platform === "win32";
        var out = [];
        out.push(cp.execSync("echo synced", {encoding: "utf8"}).trim());
        try { cp.execSync("exit 3", {stdio: "pipe"}); } catch (e) { out.push("status " + e.status); }
        var r = cp.spawnSync("sort", [], {input: "banana\napple\ncherry\n", encoding: "utf8"});
        out.push(r.status + ":" + r.stdout.replace(/\r/g, "").trim().split("\n").join(" "));
        cp.exec("echo exec", function (error, stdout, stderr) {
            out.push("exec " + (error === null) + " " + stdout.trim());
            var child = win ? cp.spawn("cmd.exe", ["/d", "/s", "/c", "echo spawned & exit 5"])
                            : cp.spawn("/bin/sh", ["-c", "echo spawned; exit 5"]);
            var text = "";
            child.stdout.setEncoding("utf8");
            child.stdout.on("data", function (c) { text += c; });
            child.on("exit", function (code) { out.push("exit " + code); });
            child.on("close", function (code) {
                out.push("close " + code + " " + text.trim() + " " + (child.pid > 0));
                var sorter = cp.spawn("sort");
                var sorted = "";
                sorter.stdout.on("data", function (c) { sorted += c.toString(); });
                sorter.on("close", function (code) {
                    out.push("sorted " + code + " " + sorted.replace(/\r/g, "").trim().split("\n").join(" "));
                    var missing = cp.spawn("phoenix-no-such-program-here");
                    missing.on("error", function (e) {
                        out.push(e.code);
                        require("util").promisify(cp.exec)("echo promised").then(function (result) {
                            out.push(result.stdout.trim());
                            __result = out.join(",");
                            __done = true;
                        });
                    });
                });
                sorter.stdin.write("pear\n");
                sorter.stdin.write("apple\n");
                sorter.stdin.end();
            });
        });
    )JS"),
        "synced,status 3,0:apple banana cherry,exec true exec,exit 5,close 5 spawned true,sorted 0 apple pear,ENOENT,promised"));

    // cep.process, as CEP has it.
    CHECK(same(page.run(R"JS(
        var win = process.platform === "win32";
        var started = win ? cep.process.createProcess(process.env.ComSpec, "/d", "/s", "/c", "echo from cep")
                          : cep.process.createProcess("/bin/sh", "-c", "echo from cep");
        var text = "";
        cep.process.stdout(started.data, function (t) { text += t; });
        cep.process.onquit(started.data, function (code) {
            __result = started.err + " " + text.trim() + " " + code + " " + cep.process.isRunning(started.data).data;
            __done = true;
        });
    )JS"),
        "0 from cep 0 false"));
}

TEST(cep_node_http_goes_through_phoenix)
{
    Folder folder("http");
    Page page(true, folder.path);
    CHECK(page.ok);
    CHECK(same(page.run(R"JS(
        var https = require("https"), http = require("http");
        https.get("https://example.com/data?x=1", function (res) {
            var body = "";
            res.setEncoding("utf8");
            res.on("data", function (c) { body += c; });
            res.on("end", function () {
                var out = [res.statusCode, res.headers["content-type"], res.headers["set-cookie"].join(";"), body];
                var req = http.request({hostname: "localhost", port: 8080, path: "/post", method: "POST",
                    headers: {"Content-Type": "application/json"}}, function (res2) {
                    var text = "";
                    res2.on("data", function (c) { text += c; });
                    res2.on("end", function () {
                        out.push(text);
                        __result = out.join("|");
                        __done = true;
                    });
                });
                req.write('{"a":');
                req.end("1}");
            });
        });
    )JS"),
        "200|text/plain|a=1;b=2|GET https://example.com/data?x=1 |POST http://localhost:8080/post {\"a\":1}"));
    CHECK(page.requests.size() == 2);
    if (page.requests.size() == 2)
        CHECK(page.requests[1]["headers"]["Content-Type"].toString() == "application/json");
}
