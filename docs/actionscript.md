# ActionScript: Test Movie, the player and HTML5 pages

How Phoenix runs a movie's scripts. There are three consumers of the same scripts:

| Consumer | Where | What runs the scripts |
|---|---|---|
| **Test Movie** (Control > Test Movie) and `Phoenix play` | [src/play](../src/play), drawn by [src/gui](../src/gui) | `fla::play::MovieRuntime` over libas3's VM (ActionScript), or QuickJS (HTML5 Canvas JavaScript) |
| **SWF publishing** | [src/edit/swf_actionscript3.cpp](../src/edit/swf_actionscript3.cpp), [src/edit/swf_movie.cpp](../src/edit/swf_movie.cpp) | libas3 compiles to ABC / AVM1 actions; see [swf.md](swf.md) |
| **HTML5 publishing** | [src/edit/html5_export.cpp](../src/edit/html5_export.cpp), [src/gui/html5_publish.cpp](../src/gui/html5_publish.cpp), [resources/html5](../resources/html5) | libas3 translates to JavaScript; the browser runs it on Phoenix's JavaScript runtime |

This note covers the first and third in depth, and the parts of the second that Test Movie
shares (class lookup, compiled scripts kept from imported SWFs). The DoAction/DoABC writing
itself is in [swf.md](swf.md); how SWF import stores what it reads is in [importers.md](importers.md)
and [fla-format.md](fla-format.md). The document model (`Timeline`, `Layer`, `Frame`,
`SymbolInstance`) is in [architecture.md](architecture.md).

Everything under `src/play` is free of Qt. The GUI supplies drawing, sound, images, networking and
the window through callbacks and the `display::Backend` interface.

---

## 1. libas3

libas3 is an ActionScript parser, compiler and bytecode VM by the same author, developed in its own
repository. Phoenix does not vendor it.

### How it is built

[CMakeLists.txt](../CMakeLists.txt), at the top, before `project()`:

- `PHOENIX_LIBAS3_SOURCE` (cache path): a libas3 checkout to build as it stands.
- If unset and `../libas3/CMakeLists.txt` exists beside the Phoenix checkout, that is used.
- Otherwise `FetchContent` fetches `https://github.com/brendan-duncan/libas3` at the pinned
  `GIT_TAG` (a commit hash). Move the pin forward on purpose when libas3 changes; CI and releases
  build the pin.
- libas3's library target is `as3_parser`; Phoenix wraps it in an `INTERFACE` target `libas3`.
  There is an MSVC/Ninja fix-up so `as3_parser.pdb` lands where libas3's post-build copy expects.

[cmake/PhoenixCore.cmake](../cmake/PhoenixCore.cmake) lists `PHOENIX_CORE_PLAY_SOURCES` (everything in
`src/play`, plus `edit/swf_actionscript3.cpp`, `edit/swf_movie.cpp`, `edit/html5_export.cpp`,
`edit/html5_actionscript2.cpp`). `phoenix_add_core(... PLAY)` links those against `as3_parser` and
`qjs` (QuickJS) and defines `PHOENIX_PLAY`. The tests only build the play tests when both
`phoenix_core` and `as3_parser` targets exist ([tests/CMakeLists.txt](../tests/CMakeLists.txt)).

### The pieces Phoenix uses

libas3's source tree is `src/{lexer,parser,ast,runtime,bytecode,abc,avm1,js}`. Phoenix includes
them by those paths (`"bytecode/vm.h"`, `"abc/abc_domain.h"`, ...). Its own docs (`docs/EMBEDDING.md`,
`ABC.md`, `AVM1.md`, `JAVASCRIPT.md` in the libas3 repo) describe the internals; this table is only
what Phoenix calls.

| libas3 piece | Phoenix uses it for | Called from |
|---|---|---|
| `as3::Parser`, `as3::Compiler` → `as3::BytecodeFunction` | Compiling FLA frame scripts, instance scripts and (AS1/2 path) class files from source | `MovieRuntime::compiled`, `MovieRuntime::loadClasses`, `play::checkSyntax` |
| `as3::VM` | One VM per movie start. Natives (`registerBuiltin`, `defineFunction`, `setGlobal`), native classes (`defineNativeClass(...)` builder), `setClassResolver`, `setIncludeResolver`, `setClock`, `runTimers`, `setLooseNames`, `newInstance` + `runConstructor`, `callFunction`, error state (`hasError`, `getError`, `getErrorLine`, `getErrorStack`) | `MovieRuntime`, `script::Bindings` |
| `as3::HostObject`, `as3::Val` / `as3::Value` | C++ state attached to script objects (clips, display objects, events, geometry) | `ClipObject`, `GlobalObject`, `TextFieldObject` in [movie_runtime.cpp](../src/play/movie_runtime.cpp); the `*Host` structs in [bindings.h](../src/play/script/bindings.h) |
| `as3::abc::Domain` | Running the ABC an imported SWF carried | `MovieRuntime::start` |
| `as3::avm1::Interpreter`, `as3::avm1::Host` | Running the AVM1 actions an imported SWF carried | `play::ActionsPlayer`, `play::ActionsHost` in [avm1_player.cpp](../src/play/avm1_player.cpp) |
| `as3::abc::compile` | AS3 to ABC for SWF publishing | [swf_actionscript3.cpp](../src/edit/swf_actionscript3.cpp) (see [swf.md](swf.md)) |
| `as3::js::translate`, `as3::js::runtime()` (`as3.js`) | AS3 to JavaScript for HTML5 | `swfmovie::translateActionScript3`, `Html5Publish::runtime` |
| `as3::js::as2::translate`, `as3::js::as2::runtime()` (`avm1.js`) | AS1/2 to JavaScript for HTML5 | [html5_actionscript2.cpp](../src/edit/html5_actionscript2.cpp) |

The same libas3 parser handles AS3 and AS1/2 source; AS1/2-only syntax (`on (release) {}`,
`onClipEvent`, `tellTarget`) parses too. The difference at run time is mostly `VM::setLooseNames`
(an unset variable reads as `undefined` in AS1/2) and which globals Phoenix installs.

---

## 2. The runtime: `fla::play::MovieRuntime`

[src/play/movie_runtime.h](../src/play/movie_runtime.h), [src/play/movie_runtime.cpp](../src/play/movie_runtime.cpp).

`MovieRuntime` plays a document's scenes (`std::vector<const Timeline*>`) at a frame rate. It never
changes the document: everything a script does lives in `Clip` state and the display list.

### 2.1 Lifecycle and host wiring

```
MovieRuntime movie(scenes, frameRate);   // constructor calls start() once
movie.setHiddenLayers(...);              // ┐
movie.setLibrary(symbols, soundIds, soundClasses);
movie.setLibraryBitmaps(bitmaps);        // │ "set before start()"
movie.setActionScriptVersion(v);         // │
movie.setJavaScript(isCanvas);           // │
movie.setClasses(sources, documentClass);// │
movie.setCompiledScripts(abcFiles);      // ┘
movie.findClass = ...; movie.trace = ...; movie.scriptError = ...; ...
movie.start();                           // again, now with the settings
loop: movie.step();                      // one frame
input: pointerOver/pointerDown/pointerUp/pointerAt, keyDown/keyUp, then afterEvents()
```

The constructor already calls `start()`; hosts configure and then call `start()` again (Test
Movie does so through `TestMovieWindow::restart`). `start()` resets everything: a fresh `as3::VM`,
the `Stage`'s children and listeners cleared, a new root `Clip` named `_level0`.

Host callbacks (all `std::function` members; none are required):

| Member | Meaning |
|---|---|
| `trace`, `error`, `scriptError` | Output; `ScriptError` carries the `DOMElement*` (a `Frame` or `SymbolInstance`) and line, so the GUI can open the script |
| `changed` | Something shown changed |
| `findClass` | AS3 class source by qualified name (a `ClassPath`, see §4.4) |
| `fetch`, `decodeImage` | Network and non-PNG image decoding for `URLLoader`/`Loader` |
| `playSound`, `stopSound`, `stopAllSounds`, `frameSound` | Library sounds from scripts, keyframe sounds |
| `getURL`, `notice` | Links; things the player did not do (loads, form posts) |
| `frameRateChanged` | `stage.frameRate` set by a script |
| `heldFrame` | Time Remap behavior: the frame a clip is held on, `-1` for none |
| `hitShape` | Shape-accurate hit test of a clip's art as last drawn |
| `folder`, `url`, `sharedObjectFolder` | Where loads resolve, `loaderInfo.url`, where `SharedObject` saves |

The GUI sets these in `connectMovie` ([src/gui/movie_hooks.cpp](../src/gui/movie_hooks.cpp)) and
`TestMovieWindow`'s constructor ([src/gui/test_movie_window.cpp](../src/gui/test_movie_window.cpp)).

### 2.2 Which language plays

`start()` picks one of three modes:

| Mode | Chosen when | Scripts run by |
|---|---|---|
| JavaScript | `setJavaScript(true)` (an HTML5 Canvas document, `canvas::isCanvasDocument`) | `canvas::Scripts` (QuickJS + CreateJS layer), §6 |
| ActionScript 3 (`_as3`) | version 3, unless the scripts look like AS1/2 and not AS3; or version 0 (unknown) with class sources, a document class, or AS3-looking scripts | `script::Bindings` (flash.* classes over the display list), §4 |
| ActionScript 1/2 | otherwise | Globals installed by `setupGlobals`, plus `ActionsPlayer` for compiled SWF actions, §3 |

`scriptsLookLikeActionScript2` / `scriptsLookLikeActionScript3` scan frame scripts (and, for AS2,
instance scripts) of the scenes and library symbols for marker strings (`onClipEvent`, `_root.`,
`attachMovie(`... vs. `import flash.`, `addEventListener`, `:void`...). This handles files whose
publish settings were changed without rewriting the scripts. `html5as2::usesActionScript2` uses
the same marks so HTML5 publishing agrees with Test Movie.

### 2.3 Clips

`struct Clip` is the root timeline or one movie clip or button instance as it plays. Graphic
symbols are not clips; they are drawn as timeline art.

| Field | Purpose |
|---|---|
| `symbol`, `timeline`, `parent`, `layer`, `placed` | What it is and where it was placed |
| `name`, `key` | Instance name; `key` is the name, or `#layer:index:symbol` for unnamed instances (`keyOf`). Keys decide whether an instance on the next keyframe is the same clip (it plays on) or a new one |
| `frame` (from 0), `playing`, `length`, `button` | Playhead |
| `display` | Its `display::MovieClip` or `display::SimpleButton` |
| `timelineObjects`, `timelineFrames` | What its current keyframes placed on the display list |
| `children`, `parked` | Child clips; `parked` holds named instances kept off stage for CreateJS semantics |
| `object` | What scripts see (a `ClipObject` host in AS1/2, a class instance with a `DisplayHost` in AS3) |
| `clipEvents`, `listeners`, `frameScripts` | `onClipEvent`/`on` handlers, AS3 listeners, `addFrameScript` functions |
| `texts` | Text a script put in a field, by instance name |
| `made` | Instance a script made (`attachMovie`, `duplicateMovieClip`, `new` + `addChild`) |
| `life` | `shared_ptr<Clip*>` nulled in `~Clip`; script objects hold it, so a script keeping an object past its clip finds nothing instead of freed memory |
| `gone` | Taken off for good; freed later by `freeGone` once no script runs |

Clips taken off by a frame or a scene change go to `_gone` and are freed in `freeGone()` at the end
of `settle()`. `alive(const Clip*)` lets the GUI check pointers it kept from the last draw.

`syncChildren(clip, entered)` brings `clip.children` up to the current frame: for each playing
layer's keyframe, each non-graphic `SymbolInstance` is matched by key to an existing child or made
new (and appended to `entered`). New clips get their children synced immediately, so a script
anywhere sees the whole stage. It then calls `syncDisplay`, which rebuilds the display list order
(bottom layer first) from:

- child clips' display objects, with the placed matrix, color transform, filters and blend mode
  (`setPlacedMatrix`, `setPlacedColorTransform`, ...);
- dynamic and input `StaticText` as `display::TextField` (keyed by instance name);
- everything else (shapes, groups, bitmaps, static text, graphic instances) as
  `display::TimelineArt` with `element` set; art on a mask layer gets `maskArt`.

A button's display object is a `SimpleButton` whose four states are `TimelineArt` of the button
symbol's frames 0 to 3 (`attachDisplay`).

Component instances that the FLA keeps as raw XML (`DOMComponentInstance`) are read into a
`SymbolInstance` by `instanceOf`; their `parametersAsXML` is kept for `componentParameters` and
applied by `Bindings::applyParameters` (§4.5).

### 2.4 Layers that play: `plays()` and `playing()`

Two helpers decide which layers count, for frame scripts, frame sounds and placement alike:

- `plays(const Layer&)` (file-local in movie_runtime.cpp): not a guide layer and not a folder.
- `MovieRuntime::playing(timeline, layer)`: `plays(layer)`, and additionally, when
  `setHiddenLayers(false)`, a hidden layer (`!layer.visible`) of a **scene** (a timeline whose parent
  is the `Document`) is left out. Layers inside symbols always play.

Test Movie sets this from the publish settings' Include hidden layers option
(`publish::read(...).swfHiddenLayers`). The default is `true` (hidden layers play).

### 2.5 One frame: `step()`

```
step()
 ├─ AS3: Bindings::frameStarted()   timers due, posted load results; placeMadeClips()
 │  AS1/2 with compiled actions: vm.runTimers(ms since start) (setInterval/setTimeout); settle()
 ├─ advance(root, entered, scripts)  every playing playhead +1 (wraps); Time Remap via heldFrame;
 │                                   root past its last frame → next scene (applyScene)
 ├─ stage.enterFrame()               display list's enterFrame (AS3 listeners)
 ├─ AS3: runClass(entered...)        objects + constructors for newly placed clips
 ├─ eachClip: onClipEvent(enterFrame), onEnterFrame (AS1/2), Clip::listeners "enterFrame"
 ├─ stage.frameConstructed()
 ├─ runFrameScripts(clips whose frame moved)
 ├─ enter(entered)                   first frames of new clips (see below), then settle()
 ├─ JavaScript: canvas.tick(); settle()
 ├─ stage.exitFrame()
 └─ stage.render()                   only when stage.invalidate() was called
```

`enter(entered)` differs per mode:

- **AS3**: for every entered clip, `syncChildren` then `runClass` (construct its object) for all of
  them first; then every non-button's frame scripts. So constructors of all newly placed clips run
  before any of their frame scripts.
- **AS1/2**: per clip: run its instance script (gathering `on`/`onClipEvent` handlers, below), wire
  compiled `ClipAction` blocks as handlers, `registeredClass` (an `Object.registerClass` class from
  compiled actions), clip events `initialize`, `construct`, `load`, and `onLoad`; then children,
  `runClass`, its frame scripts.

`settle()` resolves what scripts asked for while running: clips sent to frames (`goTo` pushes onto
`_sent`), a scene move (`goToScene` defers to `_sceneMove`, applied by `applyScene` only when no
script is running), and clips made by scripts (`_madeToEnter`). It loops up to **8 rounds**, so a
chain of gotos in frame scripts is cut short. Then `freeGone()` and `changed()`.

`seek(frame)` (the time slider) is `goTo(root, frame, playing)` + `settle()`.

### 2.6 Frame scripts

`runFrameScripts(clip)`: for every playing layer whose keyframe starts on `clip.frame`, top layer
first:

1. fire `frameSound(frame)` if the keyframe has a `soundName`;
2. if not in JavaScript mode and the keyframe carries `CompiledActions` in its extra children (an
   imported SWF), run them with `runCompiled`;
3. else run `frame->actionScript->code`: `canvas::Scripts::runFrame` for JavaScript, otherwise
   `run(frame, code, clip, where)`.

In AS3, a frame that `addFrameScript` gave a function runs that function instead (the compiled-ABC
case, where frame scripts are methods of a timeline class).

`run()` compiles once per `DOMElement*` key (`compiled()` caches in `_compiled`, including failures
as null) and executes with `this` = `objectFor(scope)`. `_current` is the clip a bare `stop()` or
`gotoAndPlay()` acts on; `tellTarget` changes it. `where` strings are `"<symbol or scene>, frame N"`
and appear in error messages.

AS3 source is passed through `separateImports` first: libas3 keeps only the first of a run of
`import` statements that open a script, so each import outside a package gets an extra empty
statement (line numbers stay the same).

### 2.7 Scenes

The root's `timeline` is the current scene. Playing past the last frame of a scene moves to the next
(and from the last back to the first) in `advance`. `gotoAndPlay("Scene", frame)`, a label in
another scene, `nextScene()` and `prevScene()` go through `goToScene`. Compiled AVM1 gotos count
frames through all scenes (`ActionsHost::gotoFrame`). `scene()`, `sceneCount()` expose it to the GUI.

### 2.8 Buttons, pointer and keys

There are two input paths:

- **Through the stage** (`setStagePointer(true)`, what `PhoenixView::setMovie` sets): the GUI sends
  pointer events to `display::Stage::pointerMove/Down/Up/wheel`, which finds the target
  (`hitTarget`) and dispatches the display list's mouse events. `pointerOver/Down/Up` then only drive
  AS2 button handlers and button state frames.
- **Without it** (tests, simple hosts): `pointerOver(Clip*)`, `pointerDown()`, `pointerUp()` also send
  display-list mouse events (`sendPointer`) to the clip named.

Button handling: `buttonFrame(button)` gives up/over/down; `runButtonEvent` runs the `on()` handlers
gathered by `buttonHandlers` (rollOver, rollOut, press, release, releaseOutside, dragOver,
dragOut); `buttonSound` plays the sound on the state frame.

`keyDown/keyUp(code, character, shift, ctrl, alt)`: `code` is a Flash key code (Left 37, "A" 65);
`TestMovieWindow`'s `flashKeyCode` maps Qt keys, including the number pad. AS2 `keyPress` handlers,
`onClipEvent(keyDown/keyUp)`, `Key` listeners and the stage's focus all hear it. Typing into a
focused input field is `MovieRuntime::type` (append, Backspace, Enter in multiline fields).

Dragging (`startDrag`/`stopDrag`, lock center, bounds), `_droptarget` (`clipUnderPointer`) and
`hitTest` live on the runtime; `hits()` uses the `hitShape` callback when the shape flag is set,
the clip's last-drawn box otherwise (`Clip::placement`).

### 2.9 Sounds

- Keyframe sounds: `frameSound(frame)` is called as a keyframe with `soundName` is entered;
  `TestMovieWindow` reads its sync settings with `frame_sound::read` (event, start, stop, stream) and
  plays through `AudioEngine`.
- Script sounds: `setLibrary(..., soundIdentifiers, soundClasses)` registers AS2 linkage identifiers
  (`Sound.attachSound`) and AS3 sound classes; `soundObject` makes the script object and calls
  `playSound(linkage, offset, loops, volume)`.

---

## 3. ActionScript 1 and 2

### 3.1 Source scripts (from the FLA)

| Where | Stored as | Run by |
|---|---|---|
| Keyframe | `Frame::actionScript->code` | `MovieRuntime::run` (libas3 compiler, loose names) |
| Button / movie clip instance | `<Actionscript><script>` among the instance's extra children, read by `fla::scripts::instanceScript` ([src/edit/script_edit.h](../src/edit/script_edit.h); `play::instanceScript` wraps it) | Compiled and run once on the clip as it is entered |

Instance scripts: before running one, `enter()` registers natives `__onClipEvent` and `__on`. The
compiled `on (...) {}` / `onClipEvent (...) {}` blocks call them with an array of event names and the
handler function, and the native stores the function in `Clip::clipEvents[event]`. The gathering
closure holds the clip's `life`, so it is harmless if the clip dies mid-script. The handlers run with
the clip as scope (`_x`, `_name`, `_parent` are the clip's).

`setupGlobals()` installs the AS1/2 surface as VM builtins and globals. Notable entries:

- Timeline control on `_current`: `stop`, `play`, `gotoAndPlay`/`gotoAndStop` (scene-aware),
  `nextFrame`, `prevFrame`, `nextScene`, `prevScene`, `__tellTarget`, `call`, `ifFrameLoaded` (always
  true).
- `getProperty`, `setProperty`, `set`, `eval`, `getTimer`, `random`, `getURL`, `fscommand`,
  `startDrag`, `stopDrag`, `duplicateMovieClip`, `removeMovieClip`, `updateAfterEvent`.
- `_root`, `_level0`, `_global` (a `GlobalObject` host that reads and writes VM globals), `Key`,
  `Mouse`, `Selection`, `Sound`, `stopAllSounds`, `SoundMixer`.
- `loadMovie`, `loadMovieNum`, `loadVariables`, `loadVariablesNum` (and unload) do nothing and send a
  `notice`.
- Library symbols with an AS3-style class file (`setClasses`) get a constructor builtin under the
  class's short name (see `loadClasses`).

Clip properties (`_x`, `_alpha`, `_xscale`, `_currentframe`, `_xmouse`, methods like `attachMovie`,
`hitTest`, `createEmptyMovieClip`, ...) are resolved in `MovieRuntime::property` and
`MovieRuntime::setProperty`, reached through `ClipObject::get/set`. Writes go to the clip's display
object, so they override what the timeline places (§5.2). Paths are resolved by `resolve()`
(`"hat"`, `"/hat"`, `"_root.hat"`, `"../hat"`, `"_parent.hat"`, `"_level0.hat"`, `"this"`);
`slashPath` writes Flash 4 style `/hat/bow`.

Text fields: `textOf` gives what a field shows (text a script set via `Clip::texts`, or the value of
the variable a field names); `setFieldText` writes either. `scriptRuns()` counts scripts run so a
renderer can tell when variable-bound fields may have changed.

**Classes.** `setClasses(sources, documentClass)` + `loadClasses()` compile class sources with the
same compiler; `runClass` copies a class's members onto a clip's object and calls its constructor.
`classFiles(document, folder, documentClass)` gathers sources for the document class and symbols'
`linkageClassName` from the folder and `AS3PackagePaths`. The GUI passes no sources
(`setClasses({}, documentClassOf(data))` in `connectMovie`), so in practice this path matters only
for version 1/2 movies given sources directly (tests). AS2 class files are compiled for SWF and
HTML5 publishing through `swfmovie::actionScript2Classes` with the class path (see [swf.md](swf.md)).

### 3.2 Compiled actions (from an imported SWF)

[src/play/avm1_player.h](../src/play/avm1_player.h), [src/play/avm1_player.cpp](../src/play/avm1_player.cpp).

SWF import cannot turn AVM1 bytecode back into source, so it keeps it on the frame or instance as an
extra child element:

```xml
<CompiledActions swfVersion="8">
  <DoAction>hex...</DoAction>
  <DoInitAction symbol="LibraryName">hex...</DoInitAction>
  <ClipAction events="..." key="...">hex...</ClipAction>
  <ButtonAction conditions="...">hex...</ButtonAction>
</CompiledActions>
```

`compiledActionsIn(extraChildren)` parses this into `CompiledActions { version, blocks }`, each
`Block` having a `Kind` (`Frame`, `Init`, `Clip`, `Button`), the init symbol, event flags or button
conditions, key code, and the action bytes. `clipEventNames` / `buttonEventNames` map the SWF
event bits to the names `onClipEvent`/`on` use (`"enterFrame"`, `"keyPress <Left>"`, ...).
`MovieRuntime::compiledActionsOf` caches the parse per element.

`ActionsPlayer` (made on first use by `MovieRuntime::actionsPlayer(version)`) owns an
`as3::avm1::Interpreter` on the movie's `as3::VM` and an `ActionsHost`, Phoenix's implementation of
`as3::avm1::Host`. The host answers the interpreter's questions from the runtime: `root`, `level`,
`parent`, `child` (case-insensitive before SWF 7), `targetPath`, `gotoFrame`/`gotoLabel` (frames
counted through scenes on the root), `setPlaying`, `callFrame` (`runFrameActions`),
`cloneSprite`/`removeSprite`, `startDrag`/`endDrag`, `getURL` (`FSCommand:` traced, loads refused
with a notice), `trace`, `error`.

Where they run:

- `DoAction`: `runFrameScripts` → `runCompiled(clip, frame, false, ...)`.
- `DoInitAction`: `syncChildren` → `runInitActions` before the frame's objects are made, once per
  frame element (`_initsRun`).
- `ClipAction`: wrapped into handler functions by `ActionsPlayer::handler` and stored in
  `Clip::clipEvents` in `enter()`.
- `onEnterFrame`, `onLoad` set by compiled code: `ActionsPlayer::callHandler`.
- `Object.registerClass`: `MovieRuntime::registeredClass` asks the interpreter for the
  constructor registered under the symbol's linkage and applies it (`Interpreter::applyClass`).

---

## 4. ActionScript 3

### 4.1 Setup

In `start()` with `_as3`:

1. `script::Bindings` is made over the runtime and VM; `setupActionScript3()` defines `trace`.
2. `Bindings::install()` sets the VM clock (`Bindings::now`), the class resolver
   (`Bindings::findSource`), the include resolver (files relative to `MovieRuntime::folder`) and
   defines every flash.* class (`defineGeom`, `defineEvents`, `defineDisplay`, `defineBitmaps`,
   `defineFilters`, `defineText`, `defineUtils`, `defineNet`, `defineSystem`).
3. If the movie has compiled ABC (`setCompiledScripts`), Phoenix first forces its own
   `fl.text.RuntimeManager` ([resources/as3/fl/text/RuntimeManager.as](../resources/as3/fl/text/RuntimeManager.as))
   to be defined, so it wins over the movie's copy (which needs the text layout framework from a
   shared library), then loads each abcFile into an `as3::abc::Domain`. Classes from the ABC are
   then found before class files.
4. `syncChildren(root)`, `runClass(root)` (the root's object is the document class), `enter`.

### 4.2 Script objects for clips

`Bindings::clipObject(clip)` makes the object scripts see:

- class: the document class for the root, else the symbol's `linkageClassName`; falls back to
  `MovieClip` (or `SimpleButton`) when not found or not a `Sprite`/`SimpleButton` subclass. A
  missing class is reported (`"The document class X was not found."`);
- `vm.newInstance(cls)`, a `DisplayHost` attached (the engine object plus the clip's `life`);
- `keepFirstFrame` for symbol classes that are not MovieClips (held on frame 1);
- `declareInstances`: for each named timeline child, if the class declares a slot of that name and
  it is still null, set it (stage instances as class variables). Run before and after the
  constructor;
- `vm.runConstructor(made)`, errors reported with the constructor's name;
- `applyParameters` for component instances.

`Bindings::wrap(DisplayObject*)` gives any other engine object a script object of the class its
`ObjectKind` maps to (`TimelineArt` shows as `flash.display.Shape`). The wrapper is cached in
`DisplayObject::script` (a weak pointer) and kept alive by `Bindings::keep` while the object lives;
`sweep()` drops objects only held by each other.

`DisplayHost::get/set` also answer AS2-style names (`_x`, `_alpha`) and children by name on
containers.

AS3 frame scripts run as compiled scripts with `this` the timeline's object. A test
(`as3_frame_scripts_share_the_timeline` in [tests/test_as3_bindings.cpp](../tests/test_as3_bindings.cpp))
checks that a `var` and a `function` declared in frame 1 are visible in later frames, that named
instances resolve, and that `this == root` on the main timeline.

`Bindings::goToNow` implements AS3 `gotoAndStop` semantics: the target frame's objects are placed
immediately, its scripts run afterward.

### 4.3 The flash.* bindings

[src/play/script/](../src/play/script). Each `bind_*.cpp` defines classes with libas3's native class
builder:

```cpp
vm.defineNativeClass("flash.geom.Point")
    .constructor(NATIVE() { ...self->host = std::make_shared<PointHost>(); ... })
    .property("x", getter, setter)
    .getter("length", NATIVE() { ... })
    .method("add", NATIVE(this) { ... return newPoint(...); }, 1)
    ...
```

`NATIVE(captures...)` ([native.h](../src/play/script/native.h)) is a lambda taking
`(as3::VM& vm, const as3::Val& self, as3::Args args)`. The helpers there (`numberArg`, `stringArg`,
`given`, `number`, `text`, ...) keep the definitions short. `hostOf<T>(vm, self, "What")` gets the
C++ state or throws `TypeError #1034`, as the player would. Display list errors map to the player's
numbers (`throwDisplayError`: #2006, #2007, #2024, #2025, #2150).

| File | Packages / classes |
|---|---|
| [bind_geom.cpp](../src/play/script/bind_geom.cpp) | `flash.geom`: `Point`, `Rectangle`, `Matrix`, `ColorTransform`, `Transform` |
| [bind_events.cpp](../src/play/script/bind_events.cpp) | `flash.events`: `EventDispatcher`, `IEventDispatcher`, `Event`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`, `TextEvent`, `TimerEvent`, `ProgressEvent`, `ErrorEvent`, `IOErrorEvent`, `SecurityErrorEvent`, `AsyncErrorEvent`, `HTTPStatusEvent`, `DataEvent`, `EventPhase` |
| [bind_display.cpp](../src/play/script/bind_display.cpp) | `flash.display`: `DisplayObject`, `InteractiveObject`, `DisplayObjectContainer`, `Sprite`, `MovieClip`, `Shape`, `SimpleButton`, `Bitmap`, `Graphics`, `Stage`, `Loader`, `LoaderInfo`, `Scene`, `FrameLabel`, the constant classes (`BlendMode`, `StageScaleMode`, `GradientType`, ...) |
| [bind_bitmap.cpp](../src/play/script/bind_bitmap.cpp) | `BitmapData`, `BitmapDataChannel` |
| [bind_filters.cpp](../src/play/script/bind_filters.cpp) | `flash.filters`: Blur, DropShadow, Glow, Bevel, GradientGlow, GradientBevel, ColorMatrix, Convolution; `fl.motion.AdjustColor`, `ColorMatrix`, `DynamicMatrix` |
| [bind_text.cpp](../src/play/script/bind_text.cpp) | `flash.text`: `TextField`, `TextFormat`, `TextLineMetrics`, `Font`, the constant classes |
| [bind_utils.cpp](../src/play/script/bind_utils.cpp) | `ByteArray`, `Timer`, `Endian`, `CompressionAlgorithm`, `IDataInput`/`IDataOutput`, `escapeMultiByte`; `flash.errors` `IOError`, `EOFError`, `IllegalOperationError` |
| [bind_net.cpp](../src/play/script/bind_net.cpp) | `URLLoader`, `URLRequest`, `URLVariables`, `URLRequestHeader`, `SharedObject`, `navigateToURL`, `sendToURL` |
| [bind_system.cpp](../src/play/script/bind_system.cpp) | `flash.media` (`Sound`, `SoundChannel`, `SoundTransform`, `SoundMixer`), `flash.system` (`Capabilities`, `System`, `Security`, `ApplicationDomain`, `LoaderContext`, `fscommand`), `flash.ui` (`Keyboard`, `Mouse`, `MouseCursor`, `ContextMenu`), `ExternalInterface` |
| [bindings.cpp](../src/play/script/bindings.cpp) | Shared machinery: wrapping, keeping, listener calls, event dispatch, loading, timers, library classes, `clipObject` |
| [png_reader.cpp](../src/play/script/png_reader.cpp) | `decodePng`, so PNG loads without a host decoder |

The language's own library (`Array`, `Vector`, `XML`, `Dictionary`, `getDefinitionByName`,
`getQualifiedClassName`, `describeType`, `getTimer`, `setTimeout`, ...) is libas3's.

**Time.** `Bindings::now()` is movie time: `frameStarted()` advances it by one frame period
(`1000 / frameRate` ms) each frame, plus real time spent within the frame. Timers
(`vm.runTimers(timerTime())`) and `getTimer` follow it, so a movie played headless at full speed
times as if at its frame rate.

**Loading.** `Bindings::load(request, done)` reads files relative to `MovieRuntime::folder`, `file:`
URLs, or http(s) through `MovieRuntime::fetch`. Results are delivered at the start of a later frame
(`post`). `Loader` decodes the bytes as an image (`Bindings::decodeImage`: PNG built in, others
through `MovieRuntime::decodeImage`); anything else, SWF included, gets `IOErrorEvent` #2124.

### 4.4 Finding classes: `ClassPath` and `Bindings::findSource`

The VM calls `Bindings::findSource(qualifiedName)` the first time a class is needed. Order:

1. Sources given by `setClasses` (by qualified or short name; a short name only matches a source
   containing `package <pkg>`).
2. `MovieRuntime::findClass` (the GUI's `ClassPath`).
3. A generated class for a library item with that linkage and no file, as the authoring tool does:
   `public dynamic class X extends <linkageBaseClass or MovieClip/SimpleButton> {}`; for sounds
   `extends flash.media.Sound`; for bitmaps `extends flash.display.BitmapData` with a
   `(width, height)` constructor. Component symbols (`DOMComponentItem`) get none: their class
   comes with the component.

Classes from loaded ABC (`abc::Domain`) are found by the VM before any of these.

`play::ClassPath` ([class_path.h](../src/play/script/class_path.h), [class_path.cpp](../src/play/script/class_path.cpp))
maps `com.studio.Hat` to `com/studio/Hat.as` and searches roots in order:

1. the movie's folder;
2. each `AS3PackagePaths` entry from `PublishSettings.xml` (entries with `$(...)` macros skipped);
3. for each `swc-path` library: a `src` folder beside a `.swc`'s `lib`/`bin` folder, or inside it
   (the `.swc` itself is not read);
4. any folder named `src` in the movie's folder or up to two levels below (gives up past 200 folders
   at a level);
5. extra paths: the user's Edit > Preferences > ActionScript class paths
   (`actionScriptClassPaths()` in [movie_hooks.cpp](../src/gui/movie_hooks.cpp));
6. `ClassPath::builtIn`: Phoenix's own classes, which the GUI points at the `:/as3/` Qt resource.

`movieClassPath(document, file)` builds it in the GUI. The same object feeds SWF publishing and the
HTML5 translator, so all three find the same classes. (SWF AS2 class lookup uses a copy with
`builtIn` cleared.) `actionScriptVersion(document)` and `documentClassOf(document)` read
`ActionScriptVersion` and `DocumentClass` from the publish settings.

### 4.5 Phoenix's own classes: resources/as3

[resources/as3](../resources/as3) holds plain ActionScript 3 classes compiled into the app as the
`as3` Qt resource (`:/as3/fl/controls/Button.as`). Read [resources/as3/README.md](../resources/as3/README.md)
for the component architecture; in summary:

- `fl.core`, `fl.controls` (Button, CheckBox, RadioButton, ComboBox, List, TextInput, TextArea,
  Label, ScrollBar/UIScrollBar, Slider, NumericStepper, ColorPicker), `fl.controls.listClasses`,
  `fl.containers.BaseScrollPane`, `fl.data`, `fl.events`, `fl.managers`, `fl.motion`, and
  `fl.skins.DefaultSkin` (Phoenix's fallback look when the movie has no skin symbols).
- Not there: DataGrid, TileList, ScrollPane, UILoader, ProgressBar, `fl.accessibility`, live
  preview.
- They are written only against the public flash.* API, so the same files run in Test Movie, compile
  into SWFs and translate to JavaScript.
- A movie's own copy of a class wins (it is found earlier on the class path, or in its ABC).

Component instances placed on a timeline: the instance is made with the symbol's first frame (the
avatar) as children, its name set, then its constructor runs; `Bindings::applyParameters` then sets
each `parametersAsXML` property by `Inspectable` `type` (Boolean, Number, Color as `#RRGGBB` → uint,
Array split at commas, Collection skipped, the rest as strings), each in its own try, between
`componentInspectorSetting = true` and `false`.

`tests/as3_components` is a separate CMake project that compiles every file in resources/as3 against
libas3 and runs `cases/*.as` against ActionScript stand-ins for flash.* (`stubs/`):

```bash
cmake -S tests/as3_components -B build-as3c -DPHOENIX_LIBAS3_SOURCE=D:/src/libas3
cmake --build build-as3c --config Release
build-as3c/Release/as3_components resources/as3 tests/as3_components [name filter]
```

### 4.6 Compiled ABC from an imported SWF

When a SWF is imported, its DoABC tags are kept as document extra files `bin/abc/1.abc`,
`bin/abc/2.abc`, ... `connectMovie` reads them back in numeric order and calls
`setCompiledScripts`. They are loaded into an `as3::abc::Domain`. The timeline class
(`<doc>_fla.MainTimeline`) is then the document class, frame scripts come from its
`addFrameScript` calls (`Clip::frameScripts`), and symbols linked to classes get them. There is no
source, so errors name the class and method (`Bindings::report` includes the `$stack`).
`tests/test_as3_bindings.cpp`'s `as3_compiled_scripts_run_from_a_published_movie` builds such a
movie with `as3::abc::compile` and plays it.

### 4.7 Errors

All error paths go through `MovieRuntime::tell(message, source, line)`, which calls `error` and
`scriptError`:

- parse errors: `"<where>: Parse error at line N, column C: ..."`; the line is parsed out;
- compile errors (exceptions from `as3::Compiler`), with the line when the message has one;
- runtime errors after `run`/`runFunction`: `report(where)` adds `, line N` and, in AS3,
  `VM::getErrorStack()`;
- exceptions from listeners and natives: `Bindings::callSafely` / `callListener` → `Bindings::report`,
  with the error's `$stack` when it is an `Error`.

An error stops only the script it is in; the movie goes on. `play::checkSyntax(source, line,
message)` is the Actions panel's Check for ActionScript (`canvas::checkSyntax` for JavaScript).
In the GUI, `TestMovieWindow` counts errors and `MainWindow::testMovie` sends them to the Output
panel with a callback that opens the script at its line (`showScript(source, line)`).

### 4.8 Known gaps (AS3, as the code stands)

- 3D: `z`, `rotationX/Y/Z` are stubs that read 0 and ignore writes, `scaleZ` reads 1,
  `Transform.matrix3D` is null.
- `Loader` loads images only (§4.3); no SWF loading.
- `ByteArray.readObject`/`writeObject` throw `IllegalOperationError` (no AMF); LZMA compression
  throws.
- `Capabilities` reports no video or audio encoders, no embedded or streaming video.
- AS1/2 `loadMovie` and `loadVariables` are refused with a notice.
- Component gaps listed in §4.5. The user guide's "Not yet" list
  ([website/actionscript.html](../website/actionscript.html)) also names video, microphone, camera
  and sockets.

---

## 5. The display list

[src/play/display/](../src/play/display): a C++ model of `flash.display` that both AS3 bindings and
the AS1/2 runtime write into. The bindings are thin; behavior (event phases, bounds, hit tests,
drag, focus, text layout state) is here.

### 5.1 Classes

| File | Contents |
|---|---|
| [display_object.h](../src/play/display/display_object.h) | `DisplayObject` (an `EventDispatcher`), `InteractiveObject`, `DisplayObjectContainer`, `Shape`, `TimelineArt`, `Sprite`, `MovieClip`, `SimpleButton`, `Bitmap`; `ObjectKind`, `DisplayError`, `TimelinePlacement` |
| [stage.h](../src/play/display/stage.h) | `Stage`: root of the list (main timeline is child 0), pointer and key entry points, focus, drag, frame events |
| [events.h](../src/play/display/events.h) | `Event`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`, `TextEvent`, `EventDispatcher` (priorities; capture, target, bubble phases through `eventParent`); broadcast events (enterFrame, exitFrame, frameConstructed, render, activate, deactivate) go to every listener on or off the list |
| [graphics.h](../src/play/display/graphics.h) | `Graphics` (the drawing API as recorded paths), `FillStyle`, `Gradient` |
| [bitmap_data.h](../src/play/display/bitmap_data.h) | `BitmapData` (premultiplied ARGB) |
| [bitmap_filter.h](../src/play/display/bitmap_filter.h) | `BitmapFilter` (one struct for all filter types, with AS defaults); `applyColorMatrix`, `applyConvolution`, `applyBoxBlur` done in the core; `filteredRect` |
| [text_field.h](../src/play/display/text_field.h) | `TextField`, `TextFormat`, `TextLine`; `setFromElement(StaticText)` for timeline fields |
| [blend.h](../src/play/display/blend.h), [geom.h](../src/play/display/geom.h) | Blend modes, `Matrix`, `Rect`, `Point`, `ColorTransform` |
| [backend.h](../src/play/display/backend.h) | `Backend`, `backend()`/`setBackend()`, `displayGeneration()`/`markDisplayChanged()` |

`DisplayObject::clip` points back to the `play::Clip` for timeline-backed MovieClips and
SimpleButtons (null for script-made ones). `DisplayObject::timeline` (`TimelinePlacement`) records
which element, layer, depth and key placed it.

### 5.2 Timeline values vs. script values

A display object keeps what the timeline places (`setPlacedMatrix`, `setPlacedColorTransform`,
`setPlacedFilters`, `setPlacedBlendMode`) separately from what scripts set. `matrixOver(placed)`
combines a placed matrix (for example a tween's in-between) with what scripts set (x/y, scale,
rotation, or the whole matrix). `transformScripted()`, `alphaScripted()`, `colorScripted()`,
`filtersScripted()`, `blendModeScripted()` and `effectsScripted()` tell the renderer whether a
script has taken over. `Clip::transform`, `Clip::opacity`, `Clip::shown`, `Clip::scriptedX/Y` read
through these.

### 5.3 The Backend

`display::Backend` is what the engine needs from a renderer:

| Method | Used for |
|---|---|
| `draw(target, source, matrix, colorTransform, blendMode, clipRect, smoothing)` | `BitmapData.draw` of a display object |
| `applyFilter(...)` | Filters the core does not do (shadows, glows, bevels); returns false when it cannot |
| `artBounds`, `artHits` | Size and hit test of `TimelineArt` |
| `layoutText`, `textIndexAt`, `textCharBox` | Text layout in real fonts, caret placement |

With no backend set, `draw` of a display object does nothing, timeline art has no size and text is
laid out with a fixed-width approximation. There is one global backend (`setBackend`).
`displayGeneration()` is bumped by `markDisplayChanged()` on any visible change; renderers compare it
with the value they last drew.

---

## 6. HTML5 Canvas documents in Test Movie

[src/edit/canvas_document.h](../src/edit/canvas_document.h): a document is a Canvas document when its
publish settings' current profile publishes the `JavaScript/HTML` format
(`canvas::kPublishFormat`, `publishesCanvas`, `isCanvasDocument`). `makeCanvasDocument` and
`newPublishSettings` create one; `documentType` gives "HTML5 Canvas" or "ActionScript 3.0" and so on
for Document properties.

[src/play/canvas/scripts.h](../src/play/canvas/scripts.h): `canvas::Scripts`, one per movie start,
owns a QuickJS runtime and context. The CreateJS classes are JavaScript (`kPrelude` in
[createjs.cpp](../src/play/canvas/createjs.cpp), split into pieces because MSVC rejects string
literals over 16K) over native functions exposed as `__phoenix` (`Native` in
[scripts.cpp](../src/play/canvas/scripts.cpp)). Unsupported members throw `"... is not available in
Test Movie"`.

- `runFrame(clip, frame, where)`: each frame script compiled once as a function, called with the
  clip's CreateJS `MovieClip` as `this`.
- `tick()`: after the frame's scripts, timers, tweens and `tick` listeners run (`step()` then
  calls `settle()` again).
- `afterEvents()`: after pointer events, a release's `pressup` follows its click.
- `sync()`: what scripts drew, wrote and set is pushed into the display list; masks follow.
- A **5 second** limit per script entry (`kTimeLimit`, enforced by `JS_SetInterruptHandler`).
- CreateJS keeps every named instance of a timeline from the start: the runtime "parks" named clips
  a frame removes (`Clip::parked`) and revives them from their first frame when a frame places them
  again (`revive`); `canvasInstance` makes instances whose keyframe is still to come.

The CreateJS feature list is in [website/actionscript.html](../website/actionscript.html).

---

## 7. Drawing a playing movie in the GUI

```
TestMovieWindow (QDialog)
 ├─ MovieRuntime  ── step() on a QTimer (twice per frame period, clock-driven)
 └─ PhoenixView   ── setMovie(&runtime)
      ├─ draws timelines with the normal renderer, reading Clip state
      │    (frame, scripted transform/alpha/visibility, texts)
      └─ DisplayPainter (display::Backend, QPainter)
           ├─ draws script-made objects and art scripts changed
           └─ drawTimeline callback → PhoenixView::drawDisplayTimeline
```

**`TestMovieWindow`** ([test_movie_window.cpp](../src/gui/test_movie_window.cpp)) builds the runtime
from all scenes, sets hidden layers, output, sounds, library symbols/sounds/bitmaps, calls
`connectMovie`, and hands the runtime to a `PhoenixView` with no pasteboard and clipping at the
stage edge. `advance()` computes how many frames are due from an elapsed timer and steps up to 8 at
once (further behind, the clock is reset rather than chased), so sound keeps time even when drawing
is slow. Restart, Pause/Play, Step and a time slider (`seek`) are in its bar. Keys go to the movie
(`flashKeyCode`), except Esc, which closes the window. `MainWindow::testMovie`
([main_window.cpp](../src/gui/main_window.cpp)) opens it modally, wiring trace and errors to the
Output panel.

**`PhoenixView::setMovie`** ([phoenix_view.cpp](../src/gui/phoenix_view.cpp)) calls
`setStagePointer(true)`, installs its `DisplayPainter` as the global `display::Backend`, sizes the
stage and its color from the document, and sets `hitShape` and `heldFrame`. Mouse events go to
`PhoenixView::movieMouse`, which forwards to the runtime and the stage.

The view draws a movie as a mix of two paths:

- Timeline-placed clips that scripts have not changed in ways only the display painter shows are
  drawn by the normal timeline renderer (`drawElement`/`drawTimeline`), so the editor's renderer
  handles all of Phoenix's art features. `displayBefore` interleaves display-list children that are
  not in place on the timeline, and draws scripted art (moved, faded or filtered) through the
  painter instead.
- Everything else, through `drawDisplayChild` → `DisplayPainter::draw`.

**`DisplayPainter`** ([display_painter.h](../src/gui/display_painter.h),
[display_painter.cpp](../src/gui/display_painter.cpp)) draws display objects with QPainter: transform,
alpha, color transform, filters (`applyFilters`: color matrix and convolution itself, the rest through
the app's filter code), blend modes, masks (drawn in stage space via `stageToDevice`), scroll rects,
`Graphics`, bitmaps and text (laid out by `TextLayout`). For timeline content it calls back:

- `drawTimeline` → `PhoenixView::drawDisplayTimeline`: `TimelineArt` with an `element` is drawn with
  `drawElement` inside its parent clip's scope; a whole frame (`frameOf`, a button state) with
  `drawTimeline`; a timeline-backed MovieClip draws its symbol's timelines at `clip->frame`.
- `timelineArtBounds`, `timelineArtHits` → the view's element bounds and hit tests.

`DisplayPainter` also implements `display::Backend`: `draw` (BitmapData.draw via `render`),
`applyFilter`, `artBounds`/`artHits` and the text methods.

---

## 8. HTML5 output

### 8.1 What is published

File > Publish with the HTML5 format runs `Html5Publish::run`
([src/gui/html5_publish.cpp](../src/gui/html5_publish.cpp)). For `movie.html` it writes:

| File | Made by |
|---|---|
| `movie.html` | Page with `<div id="phoenix-stage">` and `phoenix.start({...})` |
| `phoenix.js` | `Html5Publish::runtime()`: libas3's `as3.js` (`as3::js::runtime()`) then resources/html5's files in order (`kRuntimeFiles`) |
| `phoenix-createjs.js` | Canvas documents only: `createjs.js` |
| `phoenix-as2.js` | AS1/2 movies only: libas3's `avm1.js`, then `as2.js`, `as2components.js` |
| `movie.content.js` | `phoenix.content(<JSON>)` from `html5::content` |
| `movie.as.js` (+ `.map`) | The movie's translated ActionScript |
| `movie_assets/` | Images, frame sheets, sounds |

The runtime JS is the `html5` Qt resource (`:/html5/*.js`, [CMakeLists.txt](../CMakeLists.txt)).
[resources/html5/README.md](../resources/html5/README.md) is the authoritative description of the
page API (`phoenix.start` settings, `#frame=`, `#trace`, `#scale=none`), the runtime's files, the
frame order, the translated-module protocol and the content JSON. Not repeated here.

### 8.2 Content: `html5::content`

[src/edit/html5_export.h](../src/edit/html5_export.h), [html5_export.cpp](../src/edit/html5_export.cpp).
`content(document, ArtSource&, Options, notes)` walks the document into JSON: symbols the scenes
show plus those exported for ActionScript, each timeline as `states` (lists of entries in depth
order) and `map` (frame → state), tweens resolved per frame, text fields, labels, scenes, sounds,
linkage names. The GUI's `ArtSource` (`ViewArt` in html5_publish.cpp) draws each element to shape
JSON with the app's renderer and writes images and sounds.

`bakedRuns(timeline)` lists layer runs the JavaScript runtime cannot reproduce (node graphs,
adjustment and effect layers, layer modes, track mattes, feathered/inverted masks, motion blur,
shape operators, 3D instances, ...); those publish as pictures per frame through
`ArtSource::picture`, placed in sheets.

`Options`: `javaScript` (Canvas: frame scripts go into the content as `js`), `loop`, `allSymbols`,
`actionScriptTranslated` (AS3 translated beside the content), `actionScript2` (collects AS1/2 frame
and instance scripts into an `html5::ActionScript2` for translation; instance entries carry `h`
ids).

### 8.3 How scripts are translated

`Html5Publish::run` picks one path:

| Movie | Path |
|---|---|
| Canvas document | Frame scripts are already JavaScript; they go in the content (`js`) and run on `phoenix-createjs.js` |
| AS1/2 (`html5as2::usesActionScript2`) | `html5as2::translate` → `as3::js::as2::translate`: frame scripts, instance handlers, `#initclip` code, AS2 classes found on the class path (`swfmovie::actionScript2Classes`), and `Object.registerClass` pairs from symbols with both `linkageIdentifier` and `linkageClassName`. `swfVersion(document)` (player version from publish settings, default 8) sets case sensitivity. Output is one `avm1.movie(function ($, movie) {...})` module. Untranslatable code becomes a note `"ActionScript left out: ..."` |
| AS3 (version 3 or 0, not AS1/2, not Canvas) | `Html5Publish::actionScript` → `swfmovie::translateActionScript3` with the same `ClassPath` as Test Movie (fl.* components included). It gathers the same sources and timeline classes as SWF compilation (document class, linked classes and what they reach, a class per timeline with frame scripts, component parameters) and calls `as3::js::translate`. Phoenix appends a `phoenix.actionscript(function (movie) {...})` block calling `movie.documentClass(...)` and `movie.linkClass(symbolName, ...)`, and the source map comment |

In the browser, `as2.js` gives runtime display objects their AS2 face (proxies via
`phoenix.as2Face`) and hooks `DisplayObjectContainer.prototype._insert`/`_removeAt` to attach
handlers. AS3 classes extend the runtime's classes and follow libas3's `$construct` protocol. Both
are described in the runtime README.

The runtime reports what it lacks once per kind in the console (`phoenix.unsupported`), and a missing
`flash.*`/`fl.*` class throws ReferenceError #1065 when used. Its README lists the gaps: SWF
`Loader`, 3D, `Video`, `DisplacementMapFilter` and shaders, pointer text selection.

---

## 9. Testing and debugging

### 9.1 Unit tests

All in `phoenix_tests` ([tests/CMakeLists.txt](../tests/CMakeLists.txt)); pass a substring to run a
subset, for example `build/Release/phoenix_tests movie_runtime`. See [BUILDING.md](BUILDING.md).

| File | Covers |
|---|---|
| [test_movie_runtime.cpp](../tests/test_movie_runtime.cpp) | `MovieRuntime` on in-memory documents (a `Movie` helper builds timelines, symbols, instances and instance scripts): frame scripts, AS1, `_global`, paths, hidden scene layers, scenes, AS3 listeners, keys, errors with lines, text fields, library clips, drag/drop target, sounds, typing, document and symbol classes, `classFiles`, freed-clip safety, Time Remap, display list contents |
| [test_as3_bindings.cpp](../tests/test_as3_bindings.cpp) | flash.* classes: geometry, event phases, Sprite subclasses drawing into BitmapData (with a test `Backend`), ByteArray, timers, local loads, library symbols as classes, class path, frame scripts sharing the timeline, filters, stage pointer and keys, compiled ABC |
| [test_display.cpp](../tests/test_display.cpp) | The display engine without scripts |
| [test_canvas_scripts.cpp](../tests/test_canvas_scripts.cpp), [test_canvas_document.cpp](../tests/test_canvas_document.cpp) | Canvas JavaScript in Test Movie; Canvas document detection |
| [test_html5_runtime.cpp](../tests/test_html5_runtime.cpp) | resources/html5 loaded into QuickJS (no browser): `as3.js` then the runtime files in publish order, with a minimal host (console into `__out`, timers that never fire); events, geometry, timelines, frame order, text, CreateJS layer |
| [test_html5_as2.cpp](../tests/test_html5_as2.cpp), [test_html5_export.cpp](../tests/test_html5_export.cpp) | AS1/2 translation; content JSON |
| [test_swf_movie.cpp](../tests/test_swf_movie.cpp), test_swf_actions.cpp, [test_state_graph_play.cpp](../tests/test_state_graph_play.cpp) | SWF output and compiled actions (see [swf.md](swf.md)); States panel scripts played |

The fl.* components have their own driver (§4.5).

### 9.2 Command line

[src/gui/command_line.cpp](../src/gui/command_line.cpp):

```
Phoenix play <file> [--frames N] [--tree] [--display-list] [--click-all] [--image out.png]
                    [--click F:X,Y] [--drag F:X,Y:X,Y] [--move F:X,Y] [--key F:CODE]
```

`play` loads the document, builds a `MovieRuntime` over all scenes, calls `connectMovie` (class path,
ABC, images, SharedObjects, network) and attaches a windowless `PhoenixView` so timeline art is
measured and drawn as in Test Movie. It plays N frames (default 120), printing traces, `ERROR ...`
lines, `getURL` and `sound` lines, then a summary (`after N frames: scene s of n, frame f,
playing|stopped`).

- `--tree`: every clip with its key, name, button flag, frame, play state and script counts.
- `--display-list`: the stage's display list with each object's kind, name, stage bounds and
  visibility (not in the `help` text).
- `--click-all`: clicks every button found, playing a few frames after each.
- `--click/--drag/--move/--key F:...`: input sent before frame F.
- `--image`: the current frame drawn as Test Movie draws it.
- Exit code 1 when a script stopped on an error.

```
Phoenix display-check <folder> [--fla <file> [--frames N]]
```

`display-check` ([src/gui/display_check.cpp](../src/gui/display_check.cpp)) draws display lists built
in code (shapes, gradients, bitmaps, blend modes, filters, masks, text, a paint program's canvas)
through `DisplayPainter` to PNGs; with `--fla`, a movie after N frames with display objects mixed in.
Use it to check the painter against expectations without scripts.

### 9.3 Debugging a movie

1. `Phoenix play movie.fla --frames 60 --tree` to see which clips exist, which frame each is on and
   whether scripts ran. Add `--display-list` when objects are missing or misplaced (bounds come
   from the last draw).
2. Errors print with their `where` (`Symbol, frame N` or the instance name) and line; AS3 adds the
   stack. Compiled ABC errors name class and method only.
3. For interaction bugs, script the input (`--click 10:200,150`) and compare traces.
4. `--image` and the headless renders in [BUILDING.md](BUILDING.md) give a picture without a window.
5. For HTML5, open the published page with `#trace` (output in the page) and `#frame=N` (hold a
   frame) to compare with Phoenix's own render of that frame.

---

## 10. Where to change things

| Task | Start in |
|---|---|
| Add or fix a flash.* member | the matching `bind_*.cpp`; behavior in `src/play/display` |
| Frame order, gotos, clip lifetime | `MovieRuntime::step`, `enter`, `settle`, `syncChildren` |
| AS1/2 global or clip property | `MovieRuntime::setupGlobals`, `property`, `setProperty` |
| Compiled AVM1 behavior | `ActionsHost` in avm1_player.cpp (or libas3's interpreter) |
| Class lookup | `ClassPath`, `Bindings::findSource`, `movieClassPath` |
| A component | resources/as3, tested with tests/as3_components |
| Test Movie drawing | `PhoenixView::drawDisplayChild`/`displayBefore`/`drawDisplayTimeline`, `DisplayPainter` |
| HTML5 runtime | resources/html5 (+ its README), tests/test_html5_runtime.cpp |
| HTML5 content or translation | html5_export.cpp, html5_actionscript2.cpp, html5_publish.cpp |
| Language features | libas3 (pin bump in CMakeLists.txt) |
