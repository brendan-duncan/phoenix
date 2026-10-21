#pragma once

#include "movie_runtime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace as3 {
class Val;
class VM;
namespace avm1 {
class Interpreter;
}
}

namespace fla {

namespace display {
struct BitmapFilter;
class BitmapData;
}

namespace play {

class MovieRuntime;
struct Clip;
class ActionsHost;

/// ActionScript 1 and 2 as a movie opened from a SWF has it, compiled: kept
/// on a frame (its DoActions, and the DoInitActions that come in on it) or
/// on an instance (a movie clip's onClipEvent handlers, a button's on()
/// handlers) as a CompiledActions element among its extra children.
struct CompiledActions
{
    struct Block
    {
        enum class Kind
        {
            Frame,
            Init,
            Clip,
            Button
        };
        Kind kind = Kind::Frame;
        /// A DoInitAction's symbol, by library name.
        std::string symbol;
        /// A clip action's events (as PlaceObject numbers them) or a button
        /// action's conditions, and a keyPress's key code.
        uint32_t events = 0;
        int key = 0;
        std::shared_ptr<const std::vector<uint8_t>> actions;
    };
    /// The SWF version the movie was published for.
    int version = 0;
    std::vector<Block> blocks;
};

/// The compiled actions among an element's extra children (a frame's or an
/// instance's), null when it has none.
std::shared_ptr<const CompiledActions> compiledActionsIn(const std::vector<std::string>& extraChildren);

/// The events a movie clip's handler runs on, as onClipEvent and on() name
/// them ("enterFrame", "keyPress <Left>"), from its event flags and key; and
/// those of a button's handler, from its conditions.
std::vector<std::string> clipEventNames(uint32_t events, int key);
std::vector<std::string> buttonEventNames(uint32_t conditions);

/// The pixels an ActionScript 2 BitmapData keeps (its $bitmap), null for
/// anything else.
std::shared_ptr<display::BitmapData> bitmapOfHolder(const as3::Val& holder);

/// Runs compiled actions on a MovieRuntime's clips: the AVM1 interpreter,
/// with the runtime answering for the clips, their timelines and the player.
/// Made the first time a movie has some.
class ActionsPlayer
{
public:
    ActionsPlayer(MovieRuntime& runtime, as3::VM& vm, int version);
    ~ActionsPlayer();

    ActionsPlayer(const ActionsPlayer&) = delete;
    ActionsPlayer& operator=(const ActionsPlayer&) = delete;

    as3::avm1::Interpreter& interpreter() { return *_interpreter; }

    /// Runs \a block's actions on \a clip's timeline.
    void run(const CompiledActions::Block& block, Clip& clip, const std::string& where);
    /// A function that runs \a blocks' actions, one after another, on the
    /// timeline it is called on: a handler for the runtime to call.
    as3::Val handler(const std::vector<const CompiledActions::Block*>& blocks, const std::string& where);
    /// Calls the function \a clip's object has as \a name (onEnterFrame,
    /// onLoad), as ActionScript 1 finds it, when it has one.
    void callHandler(Clip& clip, const std::string& name);
    /// What waits for the frame to start (loads finished), done.
    void frameStarted();

private:
    /// The player's side of the library's classes, as an object of functions.
    as3::Val natives();
    /// Those of flash.display.BitmapData (as2_bitmap.cpp).
    as3::Val bitmapNatives();
    /// The clip a path given BitmapData.draw names, from the root.
    Clip* drawTarget(const std::string& path);
    /// Those of flash.filters (as2_filters.cpp).
    as3::Val filterNatives();
    /// Those of SharedObject (as2_shared_object.cpp), and saving what they
    /// hold as the movie ends.
    as3::Val sharedObjectNatives();
    void saveSharedObjects();
    /// Those of XML (as2_xml.cpp), and loading for it: a file or the web,
    /// told on the frame after.
    as3::Val xmlNatives();
    void load(const MovieRuntime::Request& request, std::function<void(const MovieRuntime::Response&)> done);
    /// A flash.filters object's filter: false for anything else.
    bool filterOf(const as3::Val& value, display::BitmapFilter& out);

    MovieRuntime& _runtime;
    as3::VM& _vm;
    std::unique_ptr<ActionsHost> _host;
    std::unique_ptr<as3::avm1::Interpreter> _interpreter;
    /// The filters scripts gave display objects, as they gave them, for
    /// reading back (as2_filters.cpp).
    struct ScriptFilters;
    std::shared_ptr<ScriptFilters> _scriptFilters;
    /// The SharedObjects scripts opened, to save as the movie ends.
    struct SharedObjects;
    std::shared_ptr<SharedObjects> _sharedObjects;
    /// Loads finished, to tell of as the next frame starts; and whether
    /// the player is still about for them.
    std::vector<std::function<void()>> _posted;
    std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

} // namespace play

} // namespace fla
