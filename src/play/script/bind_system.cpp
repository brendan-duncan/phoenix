// flash.ui, flash.system, flash.media and flash.external.

#include "native.h"

#include "../display/stage.h"

#include <string>

namespace fla {
namespace play {
namespace script {

namespace {

/// flash.media.Sound: the library sound it plays.
struct SoundHost : DispatcherHost
{
    std::string linkage;
};

/// flash.media.SoundChannel: the channel a sound plays on.
struct SoundChannelHost : DispatcherHost
{
    int channel = -1;
    Val transform;
};

const char* osName()
{
#if defined(_WIN32)
    return "Windows 10";
#elif defined(__APPLE__)
    return "Mac OS 10.15";
#else
    return "Linux";
#endif
}

const char* versionPrefix()
{
#if defined(_WIN32)
    return "WIN";
#elif defined(__APPLE__)
    return "MAC";
#else
    return "LNX";
#endif
}

} // namespace

void Bindings::defineSystem()
{
    // flash.ui.
    const auto constants = [this](const char* name, std::initializer_list<std::pair<const char*, const char*>> values) {
        NativeClassBuilder builder = vm.defineNativeClass(name);
        for (const auto& [constant, value] : values)
            builder.constant(constant, text(value));
        builder.final().define();
    };
    constants("flash.ui.MouseCursor",
        {{"ARROW", "arrow"}, {"AUTO", "auto"}, {"BUTTON", "button"}, {"HAND", "hand"}, {"IBEAM", "ibeam"}});
    vm.defineNativeClass("flash.ui.Mouse")
        .final()
        .staticMethod("hide", NATIVE(this) {
            mouseHidden = true;
            if (runtime.changed)
                runtime.changed();
            return Val();
        })
        .staticMethod("show", NATIVE(this) {
            mouseHidden = false;
            if (runtime.changed)
                runtime.changed();
            return Val();
        })
        .staticGetter("cursor", NATIVE(this) { return text(mouseCursor); })
        .staticSetter("cursor", NATIVE(this) {
            mouseCursor = vm.toString(args[0]);
            return Val();
        })
        .staticGetter("supportsCursor", NATIVE() { return boolean(true); })
        .staticGetter("supportsNativeCursor", NATIVE() { return boolean(false); })
        .staticMethod("registerCursor", NATIVE() { return Val(); })
        .staticMethod("unregisterCursor", NATIVE() { return Val(); })
        .define();

    NativeClassBuilder keyboard = vm.defineNativeClass("flash.ui.Keyboard");
    for (char c = 'A'; c <= 'Z'; ++c)
        keyboard.constant(std::string(1, c), number(c));
    for (int d = 0; d <= 9; ++d)
    {
        keyboard.constant("NUMBER_" + std::to_string(d), number(48 + d));
        keyboard.constant("NUMPAD_" + std::to_string(d), number(96 + d));
    }
    for (int f = 1; f <= 15; ++f)
        keyboard.constant("F" + std::to_string(f), number(111 + f));
    for (const auto& [name, code] : std::initializer_list<std::pair<const char*, int>>{{"BACKSPACE", 8}, {"TAB", 9},
             {"ENTER", 13}, {"COMMAND", 15}, {"SHIFT", 16}, {"CONTROL", 17}, {"ALTERNATE", 18}, {"CAPS_LOCK", 20},
             {"NUMPAD", 21}, {"ESCAPE", 27}, {"SPACE", 32}, {"PAGE_UP", 33}, {"PAGE_DOWN", 34}, {"END", 35},
             {"HOME", 36}, {"LEFT", 37}, {"UP", 38}, {"RIGHT", 39}, {"DOWN", 40}, {"INSERT", 45}, {"DELETE", 46},
             {"NUMPAD_MULTIPLY", 106}, {"NUMPAD_ADD", 107}, {"NUMPAD_ENTER", 108}, {"NUMPAD_SUBTRACT", 109},
             {"NUMPAD_DECIMAL", 110}, {"NUMPAD_DIVIDE", 111}, {"SEMICOLON", 186}, {"EQUAL", 187}, {"COMMA", 188},
             {"MINUS", 189}, {"PERIOD", 190}, {"SLASH", 191}, {"BACKQUOTE", 192}, {"LEFTBRACKET", 219},
             {"BACKSLASH", 220}, {"RIGHTBRACKET", 221}, {"QUOTE", 222}})
        keyboard.constant(name, number(code));
    keyboard.staticGetter("capsLock", NATIVE() { return boolean(false); })
        .staticGetter("numLock", NATIVE() { return boolean(false); })
        .staticGetter("hasVirtualKeyboard", NATIVE() { return boolean(false); })
        .staticGetter("physicalKeyboardType", NATIVE() { return text("alphanumeric"); })
        .staticMethod("isAccessible", NATIVE() { return boolean(false); })
        .final()
        .define();
    vm.defineNativeClass("flash.ui.KeyLocation")
        .constant("STANDARD", number(0))
        .constant("LEFT", number(1))
        .constant("RIGHT", number(2))
        .constant("NUM_PAD", number(3))
        .constant("D_PAD", number(4))
        .final()
        .define();
    vm.defineNativeClass("flash.ui.ContextMenu")
        .extends("flash.events.EventDispatcher")
        .field("builtInItems", Val::null())
        .field("customItems", Val::null())
        .constructor(NATIVE() {
            if (!self.host<DispatcherHost>())
            {
                auto host = std::make_shared<DispatcherHost>();
                host->dispatcher = std::make_shared<display::EventDispatcher>();
                host->dispatcher->script = self.ref;
                self->host = host;
            }
            vm.setProperty(self, "builtInItems", vm.newObject());
            vm.setProperty(self, "customItems", vm.newArray());
            return Val();
        })
        .method("hideBuiltInItems", NATIVE() { return Val(); })
        .define();
    vm.defineNativeClass("flash.ui.ContextMenuItem")
        .extends("flash.events.EventDispatcher")
        .field("caption", text(""))
        .field("separatorBefore", boolean(false))
        .field("enabled", boolean(true))
        .field("visible", boolean(true))
        .constructor(NATIVE() {
            if (!self.host<DispatcherHost>())
            {
                auto host = std::make_shared<DispatcherHost>();
                host->dispatcher = std::make_shared<display::EventDispatcher>();
                host->dispatcher->script = self.ref;
                self->host = host;
            }
            vm.setProperty(self, "caption", text(stringArg(vm, args, 0)));
            vm.setProperty(self, "separatorBefore", boolean(booleanArg(args, 1)));
            vm.setProperty(self, "enabled", boolean(booleanArg(args, 2, true)));
            vm.setProperty(self, "visible", boolean(booleanArg(args, 3, true)));
            return Val();
        })
        .define();

    // flash.system.
    NativeClassBuilder capabilities = vm.defineNativeClass("flash.system.Capabilities");
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, Val>>{
             {"playerType", text("StandAlone")}, {"version", text(std::string(versionPrefix()) + " 11,2,202,0")},
             {"os", text(osName())}, {"manufacturer", text("Phoenix")}, {"language", text("en")},
             {"isDebugger", boolean(true)}, {"hasAudio", boolean(true)}, {"hasMP3", boolean(true)},
             {"hasAudioEncoder", boolean(false)}, {"hasVideoEncoder", boolean(false)}, {"hasEmbeddedVideo", boolean(false)},
             {"hasStreamingAudio", boolean(true)}, {"hasStreamingVideo", boolean(false)}, {"hasAccessibility", boolean(false)},
             {"hasIME", boolean(false)}, {"hasPrinting", boolean(false)}, {"hasScreenBroadcast", boolean(false)},
             {"hasScreenPlayback", boolean(false)}, {"hasTLS", boolean(true)}, {"avHardwareDisable", boolean(false)},
             {"localFileReadDisable", boolean(false)}, {"isEmbeddedInAcrobat", boolean(false)},
             {"screenDPI", number(96)}, {"pixelAspectRatio", number(1)}, {"screenColor", text("color")},
             {"serverString", text("")}, {"cpuArchitecture", text("x86")}, {"supports32BitProcesses", boolean(true)},
             {"supports64BitProcesses", boolean(true)}, {"touchscreenType", text("none")}, {"maxLevelIDC", text("")}})
    {
        const Val fixed = value;
        capabilities.staticGetter(name, NATIVE(fixed) { return fixed; });
    }
    capabilities.staticGetter("screenResolutionX", NATIVE(this) { return number(runtime._stage->stageWidth()); })
        .staticGetter("screenResolutionY", NATIVE(this) { return number(runtime._stage->stageHeight()); })
        .staticMethod("hasMultiChannelAudio", NATIVE() { return boolean(false); })
        .final()
        .define();

    vm.defineNativeClass("flash.system.System")
        .final()
        .staticGetter("totalMemory", NATIVE() { return number(64.0 * 1024 * 1024); })
        .staticGetter("totalMemoryNumber", NATIVE() { return number(64.0 * 1024 * 1024); })
        .staticGetter("privateMemory", NATIVE() { return number(96.0 * 1024 * 1024); })
        .staticGetter("freeMemory", NATIVE() { return number(16.0 * 1024 * 1024); })
        .staticGetter("processCPUUsage", NATIVE() { return number(0); })
        .staticVar("useCodePage", boolean(false))
        .staticGetter("ime", NATIVE() { return Val::null(); })
        .staticGetter("vmVersion", NATIVE() { return text("1.4.0"); })
        .staticMethod("setClipboard", NATIVE() { return Val(); }, 1)
        .staticMethod("gc", NATIVE() { return Val(); })
        .staticMethod("pause", NATIVE() { return Val(); })
        .staticMethod("resume", NATIVE() { return Val(); })
        .staticMethod("exit", NATIVE() { return Val(); })
        .staticMethod("disposeXML", NATIVE() { return Val(); })
        .staticMethod("pauseForGCIfCollectionImminent", NATIVE() { return Val(); })
        .define();

    vm.defineNativeClass("flash.system.Security")
        .final()
        .constant("REMOTE", text("remote"))
        .constant("LOCAL_WITH_FILE", text("localWithFile"))
        .constant("LOCAL_WITH_NETWORK", text("localWithNetwork"))
        .constant("LOCAL_TRUSTED", text("localTrusted"))
        .constant("APPLICATION", text("application"))
        .staticGetter("sandboxType", NATIVE() { return text("localTrusted"); })
        .staticVar("exactSettings", boolean(true))
        .staticVar("disableAVM1Loading", boolean(false))
        .staticMethod("allowDomain", NATIVE() { return Val(); })
        .staticMethod("allowInsecureDomain", NATIVE() { return Val(); })
        .staticMethod("loadPolicyFile", NATIVE() { return Val(); }, 1)
        .staticMethod("showSettings", NATIVE() { return Val(); })
        .define();
    vm.defineNativeClass("flash.system.SecurityPanel")
        .constant("DEFAULT", text("default"))
        .constant("LOCAL_STORAGE", text("localStorage"))
        .constant("PRIVACY", text("privacy"))
        .final()
        .define();

    // One application domain: every definition the VM has.
    const Val domain = vm.defineNativeClass("flash.system.ApplicationDomain")
        .final()
        .field("parentDomain", Val::null())
        .staticVar("currentDomain", Val::null())
        .staticGetter("MIN_DOMAIN_MEMORY_LENGTH", NATIVE() { return number(1024); })
        .method("getDefinition", NATIVE() {
            const std::string name = stringArg(vm, args, 0);
            const Val found = vm.findDefinition(name);
            if (found.isUndefined())
                vm.throwError("ReferenceError", "Error #1065: Variable " + name + " is not defined.", 1065);
            return found;
        }, 1)
        .method("hasDefinition", NATIVE() { return boolean(!vm.findDefinition(stringArg(vm, args, 0)).isUndefined()); }, 1)
        .method("getQualifiedDefinitionNames", NATIVE() { return vm.newArray(); })
        .property("domainMemory", NATIVE() { return Val::null(); }, NATIVE() { return Val(); })
        .define();
    vm.setProperty(domain, "currentDomain", vm.construct(domain));
    vm.defineNativeClass("flash.system.SecurityDomain")
        .final()
        .staticGetter("currentDomain", NATIVE() { return vm.construct(vm.findDefinition("flash.system.SecurityDomain")); })
        .define();
    vm.defineNativeClass("flash.system.LoaderContext")
        .field("checkPolicyFile", boolean(false))
        .field("applicationDomain", Val::null())
        .field("securityDomain", Val::null())
        .field("allowCodeImport", boolean(true))
        .field("allowLoadBytesCodeExecution", boolean(true))
        .field("parameters", Val::null())
        .field("requestedContentParent", Val::null())
        .field("imageDecodingPolicy", text("onDemand"))
        .constructor(NATIVE() {
            vm.setProperty(self, "checkPolicyFile", boolean(booleanArg(args, 0)));
            vm.setProperty(self, "applicationDomain", given(args, 1) ? args[1] : Val::null());
            vm.setProperty(self, "securityDomain", given(args, 2) ? args[2] : Val::null());
            return Val();
        })
        .define();
    vm.defineFunction("flash.system.fscommand", NATIVE(this) {
        if (runtime.trace)
            runtime.trace("fscommand(" + stringArg(vm, args, 0) + ", " + stringArg(vm, args, 1) + ")");
        return Val();
    }, 2);

    // flash.media: library sounds, played through the movie's player.
    vm.defineNativeClass("flash.media.SoundTransform")
        .field("volume", number(1), as3::Coerce::Number)
        .field("pan", number(0), as3::Coerce::Number)
        .field("leftToLeft", number(1), as3::Coerce::Number)
        .field("leftToRight", number(0), as3::Coerce::Number)
        .field("rightToLeft", number(0), as3::Coerce::Number)
        .field("rightToRight", number(1), as3::Coerce::Number)
        .constructor(NATIVE() {
            vm.setProperty(self, "volume", number(numberArg(vm, args, 0, 1)));
            vm.setProperty(self, "pan", number(numberArg(vm, args, 1, 0)));
            return Val();
        })
        .define();
    const auto channelOf = [](VM& vm, const Val& self) -> SoundChannelHost& {
        return hostOf<SoundChannelHost>(vm, self, "SoundChannel");
    };
    vm.defineNativeClass("flash.media.SoundChannel")
        .extends("flash.events.EventDispatcher")
        .final()
        .constructor(NATIVE() {
            if (!self.host<SoundChannelHost>())
            {
                auto host = std::make_shared<SoundChannelHost>();
                host->dispatcher = std::make_shared<display::EventDispatcher>();
                host->dispatcher->script = self.ref;
                self->host = host;
            }
            return Val();
        })
        .method("stop", NATIVE(this, channelOf) {
            SoundChannelHost& host = channelOf(vm, self);
            if (host.channel >= 0 && runtime.stopSound)
                runtime.stopSound(host.channel);
            host.channel = -1;
            return Val();
        })
        .getter("position", NATIVE() { return number(0); })
        .getter("leftPeak", NATIVE() { return number(0); })
        .getter("rightPeak", NATIVE() { return number(0); })
        .property("soundTransform", NATIVE(channelOf) {
            SoundChannelHost& host = channelOf(vm, self);
            if (host.transform.isUndefined())
                host.transform = vm.construct(vm.findDefinition("flash.media.SoundTransform"));
            return host.transform;
        }, NATIVE(channelOf) {
            channelOf(vm, self).transform = args[0];
            return Val();
        })
        .define();
    const auto soundHost = [](VM& vm, const Val& self) -> SoundHost& { return hostOf<SoundHost>(vm, self, "Sound"); };
    vm.defineNativeClass("flash.media.Sound")
        .extends("flash.events.EventDispatcher")
        .constructor(NATIVE(this) {
            if (self.host<SoundHost>())
                return Val();
            auto host = std::make_shared<SoundHost>();
            host->dispatcher = std::make_shared<display::EventDispatcher>();
            host->dispatcher->script = self.ref;
            host->linkage = soundOf(self->cls.get());
            self->host = host;
            if (given(args, 0) && !args[0].isNull() && runtime.notice)
                runtime.notice("Sound.load: Phoenix plays the library's sounds; " +
                    vm.toString(vm.getProperty(args[0], "url")) + " was not loaded.");
            return Val();
        })
        .method("play", NATIVE(this, soundHost) {
            SoundHost& host = soundHost(vm, self);
            double volume = 1.0;
            if (args[2].isObject())
                volume = vm.toNumber(vm.getProperty(args[2], "volume"));
            const int loops = std::max(1, intArg(args, 1));
            int channel = -1;
            if (!host.linkage.empty() && runtime.playSound)
                channel = runtime.playSound(host.linkage, numberArg(vm, args, 0) / 1000.0, loops, volume);
            Val made = vm.construct(vm.findDefinition("flash.media.SoundChannel"));
            made.host<SoundChannelHost>()->channel = channel;
            if (args[2].isObject())
                made.host<SoundChannelHost>()->transform = args[2];
            return made;
        })
        .method("load", NATIVE() { return Val(); }, 1)
        .method("close", NATIVE() { return Val(); })
        .method("extract", NATIVE() { return number(0); }, 2)
        .getter("length", NATIVE() { return number(0); })
        .getter("bytesLoaded", NATIVE() { return number(0); })
        .getter("bytesTotal", NATIVE() { return number(0); })
        .getter("isBuffering", NATIVE() { return boolean(false); })
        .getter("url", NATIVE() { return Val::null(); })
        .getter("id3", NATIVE() { return vm.newObject(); })
        .define();
    vm.defineNativeClass("flash.media.SoundMixer")
        .final()
        .staticVar("bufferTime", number(0))
        .staticVar("soundTransform", Val::null())
        .staticMethod("stopAll", NATIVE(this) {
            if (runtime.stopAllSounds)
                runtime.stopAllSounds();
            return Val();
        })
        .staticMethod("computeSpectrum", NATIVE() { return Val(); })
        .staticMethod("areSoundsInaccessible", NATIVE() { return boolean(false); })
        .define();
    vm.defineNativeClass("flash.media.SoundLoaderContext")
        .field("bufferTime", number(1000))
        .field("checkPolicyFile", boolean(false))
        .constructor(NATIVE() {
            vm.setProperty(self, "bufferTime", number(numberArg(vm, args, 0, 1000)));
            vm.setProperty(self, "checkPolicyFile", boolean(booleanArg(args, 1)));
            return Val();
        })
        .define();

    // flash.external: no page holds the movie.
    vm.defineNativeClass("flash.external.ExternalInterface")
        .final()
        .staticGetter("available", NATIVE() { return boolean(false); })
        .staticGetter("objectID", NATIVE() { return Val::null(); })
        .staticVar("marshallExceptions", boolean(false))
        .staticMethod("call", NATIVE() { return Val::null(); }, 1)
        .staticMethod("addCallback", NATIVE() { return Val(); }, 2)
        .define();
}

} // namespace script
} // namespace play
} // namespace fla
