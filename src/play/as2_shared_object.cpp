// ActionScript 2's SharedObject: the player's side of the library's class
// (resources/as2/shared_object.as). Its data is kept in .sol files in AMF0,
// as Flash Player keeps those of ActionScript 1 and 2, beside ActionScript
// 3's; and saved again as the movie ends, as the player does.

#include "avm1_player.h"
#include "movie_runtime.h"
#include "avm1/avm1_interpreter.h"
#include "bytecode/vm.h"
#include "runtime/value.h"
#include "script/amf.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>

namespace fla {
namespace play {

struct ActionsPlayer::SharedObjects
{
    struct Opened
    {
        std::string name;
        std::string file;
        as3::Val data;
    };
    /// By file.
    std::map<std::string, Opened> opened;
};

namespace {

/// What AMF asks of ActionScript 1 and 2's values.
class Avm1Amf : public script::AmfContext
{
public:
    Avm1Amf(as3::VM& vm, as3::avm1::Interpreter& interpreter, std::function<bool(const as3::Val&)> isClip)
        : AmfContext(vm)
        , _interpreter(interpreter)
        , _isClip(std::move(isClip))
    {}
    bool actionScript2() const override { return true; }
    std::vector<std::string> namesOf(const as3::Val& object) override { return _interpreter.forInNames(object); }
    bool isAccessor(const as3::Val& object, const std::string& name) override
    {
        return _interpreter.isAccessor(object, name);
    }
    bool isMovieClip(const as3::Val& value) override { return _isClip && _isClip(value); }

private:
    as3::avm1::Interpreter& _interpreter;
    std::function<bool(const as3::Val&)> _isClip;
};

bool hasProperties(const as3::Val& data)
{
    return data.isObject() && !data->properties.empty();
}

bool save(Avm1Amf& context, const std::string& name, const std::string& file, const as3::Val& data)
{
    namespace fs = std::filesystem;
    if (file.empty())
        return true;
    std::error_code error;
    // Nothing to keep: no file.
    if (!hasProperties(data))
    {
        fs::remove(fs::u8path(file), error);
        return true;
    }
    const std::vector<uint8_t> bytes = script::writeSharedObject(context, name, data, false);
    fs::create_directories(fs::u8path(file).parent_path(), error);
    std::ofstream out(fs::u8path(file), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

} // namespace

as3::Val ActionsPlayer::sharedObjectNatives()
{
    if (!_sharedObjects)
        _sharedObjects = std::make_shared<SharedObjects>();
    std::shared_ptr<SharedObjects> kept = _sharedObjects;
    as3::VM& vm = _vm;
    MovieRuntime& runtime = _runtime;
    as3::avm1::Interpreter& in = *_interpreter;
    const std::function<bool(const as3::Val&)> isClip = [](const as3::Val& value) {
        return MovieRuntime::clipOfObject(value) != nullptr;
    };
    const as3::Val natives = vm.newObject();
    const auto define = [&](const char* name, as3::NativeMethod method) {
        natives->properties[name] = vm.makeFunction(name, std::move(method));
    };
    // load(name, localPath): the data kept for it (a new object for none).
    define("load", [kept, &vm, &runtime, &in, isClip](as3::VM&, const as3::Val&, as3::Args args) -> as3::Val {
        const std::string name = in.toString(args[0]);
        const std::string localPath = args[1].isUndefined() ? std::string() : in.toString(args[1]);
        const std::string file = runtime.sharedObjectFile(name, localPath);
        as3::Val data = vm.newObject();
        if (!file.empty())
        {
            std::ifstream source(std::filesystem::u8path(file), std::ios::binary);
            if (source)
            {
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
                Avm1Amf context(vm, in, isClip);
                as3::Val read;
                if (script::readSharedObject(context, bytes, read) && read.isObject())
                    data = read;
            }
        }
        kept->opened[file.empty() ? localPath + "/" + name : file] = {name, file, data};
        return data;
    });
    // save(name, localPath, data): true when kept.
    define("save", [kept, &vm, &runtime, &in, isClip](as3::VM&, const as3::Val&, as3::Args args) {
        const std::string name = in.toString(args[0]);
        const std::string localPath = args[1].isUndefined() ? std::string() : in.toString(args[1]);
        Avm1Amf context(vm, in, isClip);
        return as3::Val::boolean(save(context, name, runtime.sharedObjectFile(name, localPath), args[2]));
    });
    // size(name, data): its file's size; 0 with nothing in it.
    define("size", [&vm, &in, isClip](as3::VM&, const as3::Val&, as3::Args args) {
        if (!hasProperties(args[1]))
            return as3::Val::number(0);
        Avm1Amf context(vm, in, isClip);
        return as3::Val::number(
            static_cast<double>(script::writeSharedObject(context, in.toString(args[0]), args[1], false).size()));
    });
    return natives;
}

void ActionsPlayer::saveSharedObjects()
{
    if (!_sharedObjects)
        return;
    for (auto& [key, opened] : _sharedObjects->opened)
    {
        try
        {
            Avm1Amf context(_vm, *_interpreter, [](const as3::Val& value) { return MovieRuntime::clipOfObject(value) != nullptr; });
            save(context, opened.name, opened.file, opened.data);
        }
        catch (...)
        {
        }
    }
    _sharedObjects->opened.clear();
}

} // namespace play
} // namespace fla
