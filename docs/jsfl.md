# JSFL scripting

Phoenix runs JSFL, the JavaScript API that FLA pipelines are written in: commands from the
Commands menu, scripts named on the command line, tools in the toolbox, XUL dialogs, HTML
extension panels and native extension libraries. This note is for a developer adding an API member
or fixing a script that behaves differently in Phoenix than its author expected.

What scripts can reach is listed for users in [website/scripting.html](../website/scripting.html).
Python scripts and the MCP server bind the same object model; see
[python-and-mcp.md](python-and-mcp.md). ActionScript in Test Movie is a different engine; see
[actionscript.md](actionscript.md). The document model the scripts edit, and its commands and undo
stack, are in [architecture.md](architecture.md).

## Map of the code

| Piece | Files | Qt? |
|---|---|---|
| Engine: QuickJS runtimes, binding, preludes, old-dialect rewrite, events | [src/jsfl/engine.h](../src/jsfl/engine.h), [src/jsfl/engine.cpp](../src/jsfl/engine.cpp) | no |
| Value, Object, ClassInfo, ClassDef | [src/jsfl/object.h](../src/jsfl/object.h), [src/jsfl/object.cpp](../src/jsfl/object.cpp) | no |
| The JSFL classes (Flash, Document, Timeline, Layer, Frame, Element, Library, Item...) | [src/jsfl/model.h](../src/jsfl/model.h), `model_*.cpp`, [src/jsfl/extensions.h](../src/jsfl/extensions.h) | no |
| One run's undo steps and cached state | [src/jsfl/session.h](../src/jsfl/session.h), [src/jsfl/session.cpp](../src/jsfl/session.cpp) | no |
| What the model needs from the application | [src/jsfl/host.h](../src/jsfl/host.h) | no |
| JSFL tools and HTML panel contexts | [src/jsfl/tool_script.h](../src/jsfl/tool_script.h), [src/jsfl/panel_script.h](../src/jsfl/panel_script.h) (implemented in engine.cpp) | no |
| XUL dialog model | [src/jsfl/xul.h](../src/jsfl/xul.h), [src/jsfl/xul.cpp](../src/jsfl/xul.cpp), [src/jsfl/model_xul.cpp](../src/jsfl/model_xul.cpp) | no |
| E4X rewrite | [src/jsfl/e4x.h](../src/jsfl/e4x.h), [src/jsfl/e4x.cpp](../src/jsfl/e4x.cpp) | no |
| FLfile and file:/// URIs | [src/jsfl/flfile.h](../src/jsfl/flfile.h), [src/jsfl/flfile.cpp](../src/jsfl/flfile.cpp) | no |
| Native extension libraries | [src/jsfl/native_extensions.h](../src/jsfl/native_extensions.h), [src/jsfl/native_extensions.cpp](../src/jsfl/native_extensions.cpp), [src/jsfl/mm_jsapi.h](../src/jsfl/mm_jsapi.h) | no |
| The window's Host, Commands menu, Output panel, headless runs | [src/gui/script_runner.h](../src/gui/script_runner.h), [src/gui/script_runner.cpp](../src/gui/script_runner.cpp) | yes |
| A JSFL tool as a stage `Tool` | [src/gui/script_tool.h](../src/gui/script_tool.h), [src/gui/script_tool.cpp](../src/gui/script_tool.cpp) | yes |
| XUL dialogs as Qt dialogs | [src/gui/xul_dialog.h](../src/gui/xul_dialog.h), [src/gui/xul_dialog.cpp](../src/gui/xul_dialog.cpp) | yes |
| HTML (CEP) panels | [src/gui/cep_panel.h](../src/gui/cep_panel.h), [src/gui/cep_panel.cpp](../src/gui/cep_panel.cpp) | yes |
| Extension packages (ZXP, .mxi, CSXS manifests) | [src/parser/extension_package.h](../src/parser/extension_package.h), [src/parser/extension_package.cpp](../src/parser/extension_package.cpp) | no |
| Tests | [tests/jsfl_test_host.h](../tests/jsfl_test_host.h), `tests/test_jsfl*.cpp` | no |

Everything under `src/jsfl` is free of Qt. The tests build against it with an in-memory host, and
the window supplies a `Host` that reaches its tabs, stage and timeline.

## The engine

### QuickJS

The engine is [quickjs-ng](https://github.com/quickjs-ng/quickjs), pulled by `FetchContent` in the
top-level [CMakeLists.txt](../CMakeLists.txt) at tag `v0.17.0` with its examples and install
turned off. [tests/CMakeLists.txt](../tests/CMakeLists.txt) builds the JSFL tests only when the
`qjs` target exists, which means configuring from the top-level CMakeLists.

### One runtime per run

`jsfl::Engine` ([engine.h](../src/jsfl/engine.h)) runs a command. `Engine::run` gives every run:

- its own `JSRuntime` with `JS_SetMemoryLimit(RunOptions::memoryLimit)` (512 MB by default) and a
  2 MB stack (`JS_SetMaxStackSize`);
- a `JSContext`, and a `Binding` stored as the context's opaque pointer (see below);
- a fresh `Session` named after `RunOptions::undoName`;
- an interrupt handler, `interrupt()`, over a `Guard`. Every 50 ms it calls `RunOptions::poll`
  (the window uses this to show its Stop dialog), then stops the script if `Engine::stop()` set
  the atomic flag or the `timeLimit` deadline has passed.

`RunResult` holds `ok`, `interrupted` (stopped, timed out or out of memory), the error's message,
the file and line found in the stack by `locate()`, the stack itself, and `returned`: the value of
the last statement, or of `RunOptions::callFunction` when set. `plainValue()` turns model objects
inside it into null, since they belong to the session that has just ended.

`RunOptions::compileOnly` compiles without running; the corpus test uses it.

Only one run happens at a time (`Engine::run` refuses with "A script is already running."), and
nothing from one run carries over to the next. The exception is a script that leaves event
listeners behind (see [Events](#events)).

Tools and panels get their own long-lived runtimes: 256 MB for a tool and 512 MB for a panel.

### Preparing the source

Before evaluation, `prepare()` rewrites the script:

1. If `e4x::mayUse()` finds E4X markers, the E4X prelude (`e4x::prelude()`, which defines `XML`,
   `XMLList` and `__e4x`) is evaluated once in the context, and `e4x::rewrite()` turns the E4X
   syntax into plain JavaScript, line for line (see [E4X](#e4x)).
2. `Engine::modernize()` rewrites the old SpiderMonkey dialect, keeping line numbers:
   - `for each (x in y)` becomes `for (x of __jsflEach(y))`;
   - a setter with no parameter, `set x() {}`, gets one (`__jsflValue`);
   - `class` used as a name (`var class`, a parameter) becomes `__jsflClass`, while a real class
     declaration (a name after it) and an object key `class:` stay.
   It skips strings, template literals, comments and regular expressions, using a small
   regex-or-divide state.
3. If evaluation fails with "return not in a function", the script is run again wrapped in
   `(function () { ... })();`, because the old engine allowed a `return` at the top level.

### What every context gets

`installGlobals()` sets up each context: runs, tools and panels alike.

- `fl`: the wrapped `Flash` object (from `makeFlash`), with `fl.runScript` replaced by an own
  property that works on script values directly (see below).
- `FLfile`: the wrapped `FileSystem` object.
- `alert`, `confirm`, `prompt`, which go to the `Host`.
- `Binding::singleton` for `Flash`, `FileSystem` and `OutputPanel`. A method taken off its object
  (`var trace = fl.trace; trace("x")`) then still finds its object.
- `Session::evaluate`, which runs XUL handler code in this context and returns the error text.
- `kPrelude`, the JavaScript embedded in engine.cpp:
  - `__jsflEach`, plus `Object.prototype.toSource` and `uneval` polyfills;
  - `fl.phoenix.call(name, args)` and `fl.phoenix.operations()` over `fl.phoenixCall`, which reaches
    `Host::callOperation` (Phoenix's assistant operations; see [python-and-mcp.md](python-and-mcp.md));
  - `fl.Math`: `concatMatrix`, `invertMatrix` (identity for a singular matrix), `pointDistance`,
    `transformPoint`;
  - `fl.fileExists`, and a `fl.compilerErrors` stub whose `save` writes an empty file;
  - `fl.tools` for scripts that are not tools: `toolObjs` and `activeTool` built from the hidden
    `fl.__toolNames()` and `fl.__activeTool()`, no-op `ToolObj` setters, `snapPoint`,
    `constrainPoint`. A tool's context defines its own `fl.tools` before this runs, and
    `if (!fl.tools)` keeps it;
  - `an` as another name for `fl`.
- `kListeners`: `fl.addEventListener`, `fl.removeEventListener` and the internal `fl.__dispatch`.
- The native extension libraries (`native::install`).

### Running a script from the application

`ScriptRunner` ([script_runner.cpp](../src/gui/script_runner.cpp)) owns the `jsfl::Engine`, the
`JsflHost` and, when built with Python, a `python::Engine`. It picks the engine by file extension in
`ScriptRunner::run`.

| Entry point | Code path | Notes |
|---|---|---|
| Commands menu | `ScriptRunner::fillCommandsMenu` lists `*.jsfl` (and `*.py`) in `<settings>/Configuration/Commands`; Run Command... opens a file dialog | `runFile`: gestures settled first (`settleGestures`, `flushPendingMerge`), undo step named after the file's base name, no time limit, a Stop dialog (`LongRun`) after 1 s |
| `Phoenix script.jsfl` | [src/main.cpp](../src/main.cpp): `ScriptRunner::handToRunning` writes the path into `<settings>/script-requests/<uuid>.request` if another Phoenix holds `scripts.lock`; otherwise this process runs it once its window is up | The running Phoenix watches the folder (`takeRequests`) and runs requests one at a time, when nothing modal is open |
| `Phoenix run-script x.jsfl [docs...] [--timeout S]` | [src/gui/command_line.cpp](../src/gui/command_line.cpp) builds a `MainWindow` that is never shown and calls `ScriptRunner::runHeadless` | Trace goes to stdout, errors and stack to stderr, exit code 1 on failure; `--timeout` defaults to 600 s (0 for none); `--no-native-extensions` |
| `Phoenix batch --jsfl x.jsfl <files or folders> [--save] [--out dir]` | `ScriptRunner::runBatch` | Each document is opened alone, preceded by `== file` on stdout; `--save` writes changes back if the document is modified, `--out` writes a copy; the document is closed afterwards |
| `capture-window --script` and the `script` step | `runFile` on an unseen window | See [Testing](#testing) |
| HTML panel `evalScript` | `ScriptRunner::evaluate` on a `PanelScript` | See [Extensions](#extensions-and-panels) |

The Commands folder, the Tools folder and External Libraries sit under `ScriptRunner::configFolder()`
(`<settings>/Configuration`). The settings base is `PHOENIX_SETTINGS_DIR` when set, otherwise the
platform's AppData location. `fl.configURI` and `fl.configDirectory` point at that folder.

When nobody can answer a dialog, `JsflHost` still answers (`noOneToAsk()`: the window is hidden, or
the platform is `offscreen` or `minimal`). `alert` traces its text, `confirm` returns true, `prompt`
returns its default, an open-file browse returns the first document named on the command line
(`fileAnswer`), and `showDialog` returns false, so the XUL panel is accepted with its default values.

### fl.runScript and shared globals

In JavaScript, `fl.runScript(uri, functionName?, args...)` is defined in `installGlobals`. It reads
the file, runs `prepare()` on it, and evaluates it with `JS_EVAL_TYPE_GLOBAL` in the **same
context**. Everything the included file declares therefore becomes a global of the caller, which
is how library includes work. If a function name is given, that global function is called with the
remaining arguments as live script values, so the callee sees the caller's own objects. While it
runs, `Session::scriptPath` is switched to the included file, so `fl.scriptURI` follows nested
includes. The tests `jsfl_run_script_includes_share_the_global` and
`jsfl_run_script_survives_classes_first_made_inside_it` cover this.

`Flash::runScript` (a `std::function` in [model.h](../src/jsfl/model.h)), behind the `ClassDef`
method of the same name, is what other bindings set. The Python engine sets it to run `.jsfl`
files from Python.

## The object model

### Value, Object and class tables

[object.h](../src/jsfl/object.h) knows nothing about QuickJS:

- `Value` is the set of values a script can hold: undefined, null, boolean, number, string,
  `Array` (`items`), `Map` (a plain object, fields in order), `Object` (an `ObjectPtr` into the
  model) and `Date`. Its conversions (`toNumber`, `toInt`, `toBool`, `toString`) follow
  JavaScript's rules: numeric strings, `0x` hex, and NaN mapping to 0 for `toInt`.
- `arg(args, i)` returns undefined past the end; `given(args, i)` means present and not undefined.
- `Object` has `classInfo()`, `identity()` and `scope()`. When `identity()` is non-null, reaching
  the same thing twice gives the same script object (`fl.documents[0] == fl.getDocumentDOM()`).
  `scope()`, the owning document, keeps an address reused by a later document from matching.
- `ClassInfo` is a name, a parent `ClassInfo*`, and vectors of `Property` (getter, optional setter)
  and `Method` (name, `length`, call).
- `ClassDef<T>` is the declaration helper.

```cpp
const ClassInfo& Layer::classInfo() const
{
    static const ClassInfo info = []() {
        ClassInfo info;
        ClassDef<Layer> def(info, "Layer");
        def.get("frameCount", [](Layer& l) { return Value(fla::timeline::layerEnd(l.get())); });
        def.prop("locked", [](Layer& l) { return Value(l.get().locked); },
            [](Layer& l, const Value& v) {
                editLayer(l, "Lock Layer", [&v](fla::LayerSnapshot&, fla::LayerState& state) {
                    state.locked = v.toBool();
                });
            });
        def.method("getFrame", 1, [](Layer& l, const Args& args) { /* ... */ return Value(); });
        describeLayerExtras(def);   // members kept in another file (extensions.h)
        return info;
    }();
    return info;
}
```

`get` and `prop` also take member-function pointers, and `prop` converts the value to the setter's
parameter type through `detail::fromValue`. A C++ exception from a member reaches the script as an
`Error`: throw `jsfl::Error("...")` with a message written for the script's author.

### The binding

`Binding` in [engine.cpp](../src/jsfl/engine.cpp) maps the tables onto QuickJS:

- `prototype(info)` builds one prototype per `ClassInfo`, chained through `parent`. Each property
  becomes an enumerable accessor and each method a function, plus a `toString` that returns
  `[object Name]`. Prototypes are built on first use.
- `wrap()` makes a `PhoenixObject`-class JS object holding a `new ObjectPtr`, released by
  `finalizeObject`. Objects with an identity are cached in `_identities`, keyed by
  (class, identity, scope).
- `self()` checks that `this` is an instance of the member's class (or a subclass). If not, it falls
  back to a singleton, or throws "This is not a Layer."
- `toJS` and `fromJS` convert `Value`s. `fromJS` stops at depth 16, drops functions, converts
  dates, and passes E4X values (Proxies whose handler has `e4x: true`) to the model as their XML
  text.
- `trampoline` turns `PendingException` into `JS_EXCEPTION`, `std::bad_alloc` into out-of-memory,
  and any other `std::exception` into a plain `Error` carrying `what()`.

Functions are kept in a `std::deque` so that adding one while another runs (a prototype built on
first use) does not move the one running.

### The wrapped classes

[model.h](../src/jsfl/model.h) declares the classes. Each one holds plain pointers into the
document and checks them before use.

| C++ class | JSFL classes (`ClassDef` name) | Holds | Identity | Declared in |
|---|---|---|---|---|
| `Flash` | `Flash` (`fl`) | `Session&`, `runScript` | itself | [model_flash.cpp](../src/jsfl/model_flash.cpp), plus `describeFlashTools` ([model_view.cpp](../src/jsfl/model_view.cpp)), `describeFlashPublishing` ([model_publish.cpp](../src/jsfl/model_publish.cpp)) |
| `OutputPanel` | `outputPanel` | session | the session | model_flash.cpp |
| `FileSystem` | `FLfile` | nothing | none | [flfile.cpp](../src/jsfl/flfile.cpp) |
| `Document` | `Document` | `fla::FLADocument*` | the document | [model_document.cpp](../src/jsfl/model_document.cpp) plus style, view, import, profiles, commands, publishing, data |
| `Library` | `Library` | document | document + 1 | [model_library.cpp](../src/jsfl/model_library.cpp) |
| `Item` | `Item`, `SymbolItem`, `BitmapItem`, `SoundItem`, `VideoItem`, `FolderItem` | `Kind`, pointer, name (sounds and videos by name) | pointer | model_library.cpp |
| `Timeline` | `Timeline` | `fla::Timeline*` | the timeline | [model_timeline.cpp](../src/jsfl/model_timeline.cpp), [model_layers.cpp](../src/jsfl/model_layers.cpp) |
| `Layer` | `Layer` | `fla::Layer*` | the layer | model_timeline.cpp, model_layers.cpp |
| `Frame` | `Frame` | layer + frame **number** | none | model_timeline.cpp |
| `Element` | `Element`, `Instance`, `SymbolInstance`, `BitmapInstance`, `Text`, `Shape` | `fla::Element*`, its class fixed at construction | the element | [model_element.cpp](../src/jsfl/model_element.cpp), geometry in [model_shape.cpp](../src/jsfl/model_shape.cpp) |
| shape parts | `Edge`, `HalfEdge`, `Vertex`, `Contour` | | | model_shape.cpp |
| XUL | `fl.xmlui` | session | | [model_xul.cpp](../src/jsfl/model_xul.cpp) |

Details that matter when adding members:

- An `Element`'s class comes from `classOf()` by element type: groups, rectangle and oval objects
  all use `Shape`, and static text uses `Text`. The class is chosen when the wrapper is made, so it
  still reports its class after the element has gone.
- The element classes are built in one place, `elementClasses()`, because each points at its
  parent: `Element` <- `Instance` <- `SymbolInstance` / `BitmapInstance`, and `Element` <- `Text` /
  `Shape`. The item classes follow the same pattern in model_library.cpp.
- A `Frame` is a frame number on its layer, not a keyframe pointer. `Frame::get()` resolves
  whichever keyframe span covers that number now, so `frm.duration` still works after a script
  clears the keyframe under it. Frame objects have no identity.
- Wrapping helpers shared across files: `wrapDocument`, `wrapTimeline`, `wrapLayer`,
  `wrapElement`, `wrapSymbol`, `findItem`, `selectedElements`, `currentKeyframe`, `placeItem`,
  `setTransform`, `compose`, `colorText` and `parseColor`.
- Large classes are split with `describe*` functions declared in
  [extensions.h](../src/jsfl/extensions.h) and called from the class's table. To extend a class from
  a new file, add a `describe*` there and call it from the table.

### Liveness: reachable-object checks

Every `DocumentPart` member starts with `get()` (or `data()`), which calls
`DocumentPart::require` and then `Session::alive`:

- `Session::data()` throws "This document has been closed." when the host no longer lists the
  document.
- `Session::alive()` looks the pointer up in `fla::Document::reachable()`, a set of every
  `DOMElement` the document holds. The set is cached per document until a **structural** `push`
  or `invalidate()` clears it.
- A wrapper whose object has gone throws "This layer is no longer in the document (it was
  deleted, or an edit replaced it)." The script gets an error instead of Phoenix crashing on a
  dangling pointer. `jsfl_stale_objects_fail_clearly` tests this.

### Edits become undoable commands

A member never changes the document directly. It builds an `fla::Command` (from `src/edit`, see
[architecture.md](architecture.md)) and calls `DocumentPart::push(command, structural)`, which
calls `Session::push`:

1. `beginStep()` calls `checkWritable()` (closed documents and documents from a newer Phoenix,
   `Host::readOnly`, are refused) and opens a macro on the document's `CommandStack`, named after
   the run (`history.beginMacro(undoName)`), if one is not already open.
2. The command is pushed and executed.
3. If `structural` (the default), the reachable set is cleared. Pass `false` for property changes
   that neither add nor remove objects, such as layer flags (`editLayer` with `LayerEditCommand`),
   transforms, or `SetPropertyCommand<T>`.
4. `Host::changed()` is called, and the window marks its cached bounds stale.

```cpp
e.push(fla::CommandPtr(new fla::SetPropertyCommand<fla::Point>(
           element.transformationPoint, element.transformationPoint, local, "Transformation Point")),
    false);
```

When a host call pushes its own commands (`Host::importFile`), call `session.beginStep(document)`
first so they land in the run's step. When it changes the document by other means
(`Host::runCommand`), call `session.invalidate(document)` afterwards; `runCommand()` in
[model_commands.cpp](../src/jsfl/model_commands.cpp) does this.

`jsfl_a_run_is_one_undo_step` pins down the result. A run makes one step per document it touched,
named after the script. A script that only reads makes none. A script that fails keeps what it did
before the error, as one closed step.

### Session

`Session` ([session.h](../src/jsfl/session.h)) is one run's state.

- **Steps.** A `CommandStack` cannot change histories while a macro is open, so any call that may
  switch, open, close or save documents (`fl.openDocument`, `createDocument`, `closeDocument`,
  `closeAll`, `setActiveWindow`, the save calls, and some import and publish calls in
  model_document.cpp, model_import.cpp and model_publish.cpp) calls `beforeSwitch()` first, which
  closes the open steps. A script that switches documents and comes back therefore makes two steps on that
  document. `closeSteps()` also calls `breakMergeChain()`, so the next edit by hand does not merge
  into the script's step.
- **finish()**, at the end of a normal run: closes steps, `showFrameSelections()`,
  `forgetShapes()` (drops shape-geometry edit state, including a `beginEdit` never ended), then
  `Host::refresh()`.
- **settle()**, for contexts that stay loaded (tools, panels, listening scripts), after each call:
  the same as `finish`, and it also forgets the reachable sets and the cached frame and layer
  selections, because the person may change them between calls. The next change opens a new step.
- **Cached selections.** `frameSelection(document, timeline)` and `layerSelection(...)` start from
  the host (what the person had selected, or the current layer) for the timeline being edited, and
  are then kept and changed by the script. `librarySelection` is kept the same way. `position()`
  holds a playhead and current layer for timelines other than the one being edited.
- **showFrameSelections()** runs at finish and settle. For each open document it pushes the
  script's frame selection for the edited timeline to the timeline view
  (`Host::setSelectedFrames`), but only if the script made one for that timeline and it differs
  from what is shown. A script that only read `getSelectedFrames()` leaves the view alone.
- `detach()` marks the session finished so its destructor asks nothing of a host that is going
  away (a window closing with tools loaded).
- `scriptPath`, `evaluate` and `dialogs` (the stack of open XUL dialogs that `fl.xmlui` works on)
  are set by the engine.

### The Host interface

[host.h](../src/jsfl/host.h) lists everything the object model needs from the application, so the
model never touches Qt. It covers output and dialogs, documents (open, create, save, close, path,
name, modified, readOnly), editing (`history`, `selection`, `changed`, `refresh`), where editing
happens (`editedTimeline`, `editTimeline`, `editInPlace`, `viewMatrix`, the current frame and layer,
`selectedFrames`, `setSelectedFrames`), drawing (`drawingStyle`, `bounds`, `bitmapPixels`), the
toolbox and view (`tools`, `activeTool`, `selectTool`, `reloadTools`, `zoom`), and commands only the
window can run (`runCommand`, `importFile`, `exportPNG`, `exportSWF`, `publish`, `callOperation`).
Members that are not pure virtual have defaults that do nothing or fail with "needs the Phoenix
window".

Implementations:

| Host | Where | Used for |
|---|---|---|
| `JsflHost` | [src/gui/script_runner.cpp](../src/gui/script_runner.cpp) (a friend of `MainWindow`) | the window, run-script, batch, tools, panels |
| `jsfl_test::Host` | [tests/jsfl_test_host.h](../tests/jsfl_test_host.h) | most object-model tests (documents in memory, output in a string, a small fake toolbox, `bounds` = the transform's origin plus 10x10, recorded SWF exports and publishes) |
| `TestHost` | [tests/test_jsfl.cpp](../tests/test_jsfl.cpp) | the engine tests and corpus runs |
| `PlainHost`, `XulHost` | [tests/test_jsfl_e4x.cpp](../tests/test_jsfl_e4x.cpp), [tests/test_jsfl_xul.cpp](../tests/test_jsfl_xul.cpp) | E4X and XUL tests |

Notes on `JsflHost`:

- Documents are the window's tabs. The shown tab uses the window's own `_editContext` command
  stack and `_selection`. A background tab uses its tab's history, a selection the host keeps for
  it, and remembered edited timeline, frame and layer (`_edited`, `_frames`, `_layers`, cleared by
  `forget()` when the tab closes).
- `open()` reads FLA, XFL and .phx without File > Open's dialogs; parser warnings are traced.
- `bounds()` asks the stage (`PhoenixView::elementBounds`) and calls `invalidateBounds()` first if
  a `changed()` came in since.
- `editTimeline()` moves the view to a scene or a symbol. `editInPlace()` uses
  `PhoenixView::enterSymbol`, and `viewMatrix()` returns `PhoenixView::scopeToStage()`.
- `exportPNG`, `exportSWF` and `publish` activate the document first and work only on the shown
  one.

## Compatibility notes

These came from running real scripts. Most are in the 0.21.x changelog, and TODO.md's "4.
Scripting and extensions" and "Parity 4" sections have the history. When fixing a similar report,
first find out what the script expects, then change the model to match, with a test.

| Behavior | Where | Why |
|---|---|---|
| `fl.selectTool(name)` returns `true` or `false`. It accepts JSFL names (`"arrow"`, `"oval"`, the `kToolNames` table), toolbox names in any case, and a script tool's full name with " Tool" (`"Rig Tool"` finds the toolbox's "Rig") | `toolboxName()` in [model_view.cpp](../src/jsfl/model_view.cpp) | Scripts test the result to tell the user how to install a tool |
| `fl.tools.toolObjs` and `activeTool` report built-in tools by their JSFL names | `scriptName()` | Scripts compare against `"arrow"` and similar names |
| `timeline.setSelectedFrames(...)` on the edited timeline moves the playhead to the **last** selected frame and the current layer to the last range's layer, selects the elements on that frame in unlocked, visible, non-folder layers of the ranges, makes the ranges' layers the selected layers, and the timeline view shows the frames once the call settles | `setSelectedFrames` in [model_timeline.cpp](../src/jsfl/model_timeline.cpp), `Session::showFrameSelections` | This is what dragging across frames does; scripts then read `document.selection` |
| `setSelectedFrames(start, end)` and the array form treat `end` as exclusive; `getSelectedFrames()` returns `[layer, first, last + 1, ...]` | same | JSFL convention |
| A raw shape's `x` and `y` (`fla::Element::Type::Shape`) are the center of its bounds; setting them translates by the difference. Other elements use the matrix's `tx` and `ty` | `describeElement` in [model_element.cpp](../src/jsfl/model_element.cpp) | A shape's matrix says nothing about where its lines are |
| `exportPublishProfileString()` returns the bare `<flash_profile>` element without its `current` attribute; `exportPublishProfile(uri)` writes `<flash_profiles>` with a declaration | `exportProfile(..., bare)` in [model_profiles.cpp](../src/jsfl/model_profiles.cpp) | Scripts copy a profile and import it back |
| `document.testMovie()` (and `testScene()`) runs synchronously. It plays the document as the script has it then, and the script continues once the Test Movie window closes. Failures are traced, not thrown. `canTestMovie()` reports whether it can run | [model_commands.cpp](../src/jsfl/model_commands.cpp), `JsflHost::runCommand` | Scripts change the document for a test and put it back afterwards |
| `exitEditMode()` returns to the current scene, and the instance just left stays selected | `exitEditMode` in model_document.cpp, `JsflHost::editTimeline` | Same as leaving a symbol by hand |
| `enterEditMode("inPlace")` edits the selected instance's symbol in place; `document.viewMatrix` then maps edited-space coordinates (what a tool's mouse reads) to document space | model_document.cpp, model_view.cpp | Tools that draw inside symbols |
| `fl.tools.constrainPoint(from, to)` snaps to 45-degree steps only while Shift is down; otherwise it returns `to` unchanged | both `fl.tools` preludes in engine.cpp | Tools call it on every move |
| `fl.reloadTools()` loads `.jsfl` files added to the Tools folder since startup. Files already loaded are not reloaded | `MainWindow::reloadScriptTools`, `loadScriptTools` | Installers that drop a tool in place |
| `fl.version` is `"WIN 24,0,0,0 Phoenix <version>"` (`MAC` elsewhere) | model_flash.cpp | Scripts parse `fl.version.split(" ")[1]` |
| `fl.getDocumentDOM()` is null with no document open; `findItemIndex` returns `""` when missing | model_flash.cpp, model_library.cpp | Scripts test for exactly these values |
| `document.livePreview` is stored per document but changes nothing | model_commands.cpp | Scripts toggle it around long edits |

## JSFL tools

A `.jsfl` file in `Configuration/Tools` becomes a toolbox button. `MainWindow::loadScriptTools`
creates a `jsfl::ToolScript` per file through the `JsflHost`, wraps it in a `ScriptTool` (a stage
`Tool`), and adds an action to the toolbox. Load errors go to the Output panel.

### ToolScript (engine.cpp)

`ToolScript::load` creates a runtime and context that last until Phoenix quits, with one `Session`
named "Tool". It then runs `installGlobals`, the tool prelude `kToolPrelude`, the script itself,
and `fl.tools.__configure()`. That last call makes a `ToolObj`, sets it as `fl.tools.activeTool`,
and calls the script's `configureTool()`. The name, menu string, tooltip and icon (resolved next to
the script) are read off the `ToolObj`'s hidden fields. If the tool sets an options file
(`setOptionsFile`), its `<property name variable type defaultValue list min max>` entries become
`ToolScript::Option`s, start at their defaults, and are written onto the `ToolObj` by
`setOptions`, which then calls `notifySettingsChanged()`.

`kToolPrelude` defines:

- `fl.tools`: `penLoc`, `penDownLoc`, `mouseIsDown`, `altIsDown`, `ctlIsDown`, `shiftIsDown`,
  `getKeyDown()`, `setCursor(n)`, `snapPoint` (returns the point unchanged), `constrainPoint`, and
  the internal `__set` and `__configure`;
- `fl.drawingLayer`: `beginDraw(persistent)`, `endDraw`, `beginFrame`, `endFrame`, `setColor`,
  `moveTo`, `lineTo`, `curveTo`, `cubicCurveTo`, `frameRect`, `drawPath`, `newPath`. Paths are kept
  as JS arrays, and `__flat()` serializes them for C++ as one line per path:
  `color;M x y;L x y;...`;
- a `Path` class with `addPoint`, `addCurve`, `addCubicCurve`, `close`, `newContour`, `clear` and
  `getNumberOfPoints`. `makeShape(suppressFill, suppressStroke)` turns the path into SVG path data
  and calls `document.addNewPath`, which is how a tool adds to the document.

`ToolScript::call(function, state)` evaluates `fl.tools.__set(...)` with the pen and key state, then
`function({x, y})`. Each evaluation has a 10-second deadline. Afterwards the call settles: the undo
step is renamed to the tool's name and closed, and the view refreshes.

**The busy flag.** `Impl::busy` is set while a call runs. A call made during another call (the
script's own `fl.selectTool` triggers `deactivate`, for example) is treated as part of the outer
call and does not settle; only the outer call settles. `ToolScript::dispatch` (events) returns
immediately while busy, because the events a call causes, such as moving the playhead, are its own
doing. Before this guard, a tool that moved the playhead froze Phoenix.

`preview()` parses `__flat()` into `Preview` paths. `cursor()` reads `fl.tools.__cursor` (0 is a
plus, 1 and 2 arrows, 3 four-way, 4 and 5 two-way, 6 an X, 7 a hand).

### ScriptTool (src/gui/script_tool.cpp)

- Mouse press, move, release and double-click, and key press, call `mouseDown`, `mouseMove`,
  `mouseUp`, `mouseDoubleClick` and `keyDown` with stage coordinates, the press point, and Qt's
  modifiers. `setCursor` is called on every move, followed by `view.setCursor(cursor())`.
  `activate` and `deactivate` follow picking and putting down the tool.
- `paintOverlay` strokes the preview paths, unfilled, each in its own color.
- `usesControlKey()` returns true. In `PhoenixView`'s mouse press, Ctrl normally switches the
  gesture to the Selection tool (`ctrlIsArrow`) unless the active tool claims Ctrl. JSFL tools
  claim it, so the script sees `fl.tools.ctlIsDown`.
- `showsSelectionBounds()` returns false. `hasOptions` and `showOptions` build a small Qt form
  from the options file: a combo box for `Strings` or lists, a check box for `Boolean`, a spin box
  for `Number`, and a line edit for anything else.
- `MainWindow::scriptEvent` forwards events to each tool's `dispatch`.

`jsfl_tools_preview_while_dragging_and_draw_on_release` in
[tests/test_jsfl_extensions.cpp](../tests/test_jsfl_extensions.cpp) shows a complete tool driven
without a window.

## XUL dialogs

`fl.xmlPanel(uri)`, `document.xmlPanel(uri)` and `fl.xmlPanelFromString(xml)` call `showXmlPanel` in
[model_xul.cpp](../src/jsfl/model_xul.cpp):

1. `xul::Dialog::load` parses with a forgiving parser (`xul::parse`: either kind of quote,
   unescaped entities, script text taken as is, mismatched close tags forgiven) and collects
   `Control`s: text box, check box, radio group, menu list, list box, color chip, slider, file
   chooser, target list, property, button, label.
2. `fillTargets` fills target lists from the document's instances.
3. `dialog.run` sends handler code to `Session::evaluate`, so `oncreate`, `onchange`, `oncommand`
   and `<script>` blocks run in the script's own context and can call its functions. Their errors
   are traced as `xmlPanel: ...`.
4. The dialog is pushed on `Session::dialogs` (the target of `fl.xmlui.get`, `set`, `getEnabled`,
   `setEnabled`, `getVisible`, `setVisible`, `accept`, `cancel` and the `ControlItemElement(s)`
   members), and `create()` runs the scripts and `oncreate`s.
5. `Host::showDialog` shows it. If it returns false (nobody to ask), the dialog is accepted as it
   stands and a note is traced.
6. `dialog.result()` returns `dismiss` plus each control's value by id.

`XulDialog` ([src/gui/xul_dialog.cpp](../src/gui/xul_dialog.cpp)) builds Qt widgets from the
model. The person's edits go to `Dialog::edit`, `toggle`, `choose` and `click`; script changes
come back through the `changed` and `closedByScript` callbacks. A `<flash src>` panel is played
like Test Movie plays a movie (`TestMovieWindow::setPanel`), and its `MMExecute` calls go to
`Dialog::execute`, which runs the code in the script that opened the dialog through
`Session::evaluate`, so `fl.xmlui` reaches the dialog from the movie. Docked SWF panels are in
[src/gui/swf_panels.cpp](../src/gui/swf_panels.cpp): one per movie in `Configuration/WindowSWF`,
each with its own `PanelScript` context for `MMExecute`, as an HTML panel's `evalScript` has. While a dialog is open, `JsflHost` counts it (`dialogsOpen`, `dialogsShown`) so
that `LongRun` does not count the person's time in the dialog as the script running long.

## E4X

The engine has no native E4X. [e4x.cpp](../src/jsfl/e4x.cpp) rewrites the syntax, only when
`mayUse()` finds markers, onto `XML` and `XMLList` classes built on `Proxy` in `e4x::prelude()`. The
mapping is documented in [e4x.h](../src/jsfl/e4x.h): `x.@id` becomes `x["@?id"]` (a string) unless
used as a list, along with `..`, `.*`, `.(filter)` through `with`, literals with `{holes}`,
`.* +=` and `function::`. Values passed to the model become their XML text (`Binding::isE4X`). The
gaps recorded in TODO.md: `==` between two XML values compares identity, `typeof` returns
"object", namespaces are ignored (names match by local name), and deleting a filter result does
nothing. Tests are in [tests/test_jsfl_e4x.cpp](../tests/test_jsfl_e4x.cpp).

## FLfile

`FileSystem`'s table is in [flfile.cpp](../src/jsfl/flfile.cpp): `exists`, `read`, `write` (with
append), `copy`, `remove`, `createFolder`, `listFolder` (with masks), attributes, size, dates (as
strings and `...DateObj`), `platformPathToURI` and `uriToPlatformPath`. `runCommandLine` is added
by `describeFileSystemProcesses` in [model_publish.cpp](../src/jsfl/model_publish.cpp).
`jsfl::uri::toPath` accepts `file:///C|/...` and `file:///C:/...` and percent-decodes; anything that
is not a `file:` URI is taken as a path.

## Events

`fl.addEventListener(type, fn)` returns an id and `fl.removeEventListener(type, id?)` removes
listeners. Both live in `kListeners`. After a run, `Engine::run` checks `fl.__listeners.length`.
If listeners remain, the context is not freed: it moves to `Engine::_kept` with its session
settled. `Engine::dispatch(type)` then evaluates `fl.__dispatch(type)` in each kept context with a
5-second limit, traces errors, settles the session, and drops a context whose last listener has
been removed. Events raised while a script runs or while a dispatch is in progress are queued
(`_pending`, consecutive duplicates merged) and delivered by `flushEvents()` afterwards.
`forgetListeners()` drops them all.

The window raises events from `MainWindow::scriptEvent`: `documentNew`, `documentOpened`,
`documentChanged`, `documentClosed`, `documentSaved`, `timelineChanged`, `layerChanged`,
`frameChanged`, `selectionChanged`, `prePublish` and `postPublish`. If the command stack is in the
middle of executing, the event is posted for later. `ScriptRunner::dispatchEvent` sends it to the
JSFL and Python engines and emits `eventDispatched`, which HTML panels receive as
`com.adobe.events.flash.<type>`. Tools and panels have their own `dispatch`.
`jsfl_listeners_hear_events_after_their_script_ends` tests this.

## Extensions and panels

- **Packages.** [extension_package.cpp](../src/parser/extension_package.cpp) reads a ZXP (a zip)
  or a folder. Files listed by an `.mxi` are copied into the Configuration folder (Commands, Tools
  and so on); CSXS manifests describe HTML panels. Installed extensions are kept under the
  extensions folder next to the settings, and removal deletes only the files that are still the
  ones the package placed. Commands > Manage Extensions, opening a `.zxp`, or dropping one installs
  it (`ScriptRunner::installExtension`). Details are in [importers.md](importers.md).
- **HTML (CEP) panels.** `CepPanel` ([src/gui/cep_panel.cpp](../src/gui/cep_panel.cpp)) shows the
  page in a web view behind a CSInterface host. `evalScript` goes to a `jsfl::PanelScript`, created
  on the first call through `ScriptRunner::panelScript` and loaded with the manifest's ScriptPath
  file. Every later `evalScript` runs in that same context, so functions defined in the file stay
  available. Each call settles (one undo step named after the panel), returns
  `String(value)`, and runs through `ScriptRunner::evaluate`, which gives it the Stop dialog and
  reports errors to the Output panel. `jsfl_a_panel_context_keeps_its_functions_between_calls`
  tests this.
- **Native extension libraries.** Shared libraries in `Configuration/External Libraries`
  (`.dll`; `.dylib`, `.bundle` or `.so` on macOS; `.so` elsewhere) are loaded by `native::install`
  the first time a script runs after they appear, and stay loaded. Each library needs
  `MM_InitWrapper` (produced by the `MM_STATE` macro in [mm_jsapi.h](../src/jsfl/mm_jsapi.h)) and
  gets a global object named after its file stem, carrying the functions its `MM_Init` defined
  with `JS_DefineFunction`. A library whose name is already a script global is skipped, and a note
  is traced. mm_jsapi.h is Phoenix's own copy of the C interface, written so
  libraries built against the published header load unchanged: same types, same macros, same order
  of `MM_Environment` function pointers. `jsval` is `long`. `PHOENIX_NATIVE_EXTENSIONS=off` (or
  `--no-native-extensions`) skips loading entirely. Tests in
  [tests/test_jsfl_native.cpp](../tests/test_jsfl_native.cpp) build two libraries from
  `tests/native_extension/`. TODO.md lists the table order and `jsval` width as not yet checked
  against a real extension.

## Adding an API member

1. Find the class's table: `X::classInfo()` in the matching `model_*.cpp`, or a `describe*`
   function from [extensions.h](../src/jsfl/extensions.h). Add a `get`, `prop` or `method` there.
   For several related members, add a new `describe*` function and file.
2. Read through `get()` and `data()` so stale objects fail clearly. Use `arg()` and `given()` for
   optional parameters and convert with `Value`'s JavaScript-style conversions.
3. Change the document only through `push(command, structural)`, with `structural = true` if
   objects are added or removed. Reuse commands from `src/edit` rather than writing new ones.
4. Get anything that needs the window (pixels, bounds, the toolbox, playback) through a `Host`
   member. Give a new member a default in host.h that fails with a clear message, implement it in
   `JsflHost`, and add a fake to `jsfl_test::Host` if tests need it.
5. Return what JSFL scripts expect: a boolean where scripts test the result, `null` or `""` where
   they check for missing values. Throw `jsfl::Error` with a sentence aimed at the script's author.
6. Python picks up the new member automatically, since it binds the same tables (see
   [python-and-mcp.md](python-and-mcp.md)).
7. Add a test, then update [website/scripting.html](../website/scripting.html) and add a line to
   CHANGELOG.md under Unreleased.

## Testing

### Unit tests

The JSFL tests are part of `phoenix_tests`, listed in
[tests/CMakeLists.txt](../tests/CMakeLists.txt) under `PHOENIX_TESTS_HAVE_JSFL`:

| File | Covers |
|---|---|
| [test_jsfl.cpp](../tests/test_jsfl.cpp) | the engine: trace, return values, errors with lines, stop, memory limit, panel contexts, undo steps, stale objects, runScript, the old dialect, and the opt-in corpus tests |
| [test_jsfl_document.cpp](../tests/test_jsfl_document.cpp) | Document members |
| [test_jsfl_objects.cpp](../tests/test_jsfl_objects.cpp) | later object-model work: stroke and fill, toolbox and zoom, imports, target lists, the compatibility fixes above |
| [test_jsfl_extensions.cpp](../tests/test_jsfl_extensions.cpp) | shape geometry, persistent data, layer copy and paste, publishing, runCommandLine, tools, listeners |
| [test_jsfl_e4x.cpp](../tests/test_jsfl_e4x.cpp), [test_jsfl_xul.cpp](../tests/test_jsfl_xul.cpp), [test_jsfl_native.cpp](../tests/test_jsfl_native.cpp) | E4X, XUL, native libraries |

Tests use the `TEST` and `CHECK` macros from [tests/test_util.h](../tests/test_util.h).
`jsfl_test::Fixture` provides a host with one empty 550x400 document and runs a string with a
20-second limit and the undo name "Test Script":

```cpp
TEST(jsfl_layer_lock_is_one_step)
{
    jsfl_test::Fixture f;
    CHECK(f.run("var l = fl.getDocumentDOM().getTimeline().layers[0];\n"
                "l.locked = true; fl.trace(l.locked);").ok);
    CHECK(f.take() == "true\n");
    CHECK(f.host.history(f.document).undoName() == "Test Script");
}
```

Run one group by passing a substring of test names: `phoenix_tests.exe jsfl_`. Several tests write
under the system temp folder (`jsfl_test::scratchFolder`, the test host's default
`configDirectory`), so when several checkouts run the suite at the same time, give each run its own
`TEMP` and `TMP`. `PHOENIX_JSFL_CORPUS` (a folder of `.jsfl` files) turns on `jsfl_corpus_compiles`,
and adding `PHOENIX_JSFL_DOCUMENT` turns on `jsfl_corpus_runs`, which tallies the errors scripts
stop on. Two MIT-licensed scripts from real pipelines are vendored in `tests/data/jsfl`.

### Checking a real script headless

Run the script against the real `JsflHost` without showing a window:

```sh
QT_QPA_PLATFORM=offscreen PHOENIX_SETTINGS_DIR=/path/to/private/settings \
  Phoenix run-script check.jsfl some.fla --timeout 60
```

A private `PHOENIX_SETTINGS_DIR` keeps the run away from your own Commands, Tools, External
Libraries and settings, and from the `scripts.lock` request hand-off of a Phoenix you have open.
Everything is answered as described in [Running a script](#running-a-script-from-the-application).
Bounds and exports come from a real (unseen) `PhoenixView`. `testMovie` and XUL dialogs need
someone to answer them, so they are refused or accepted with their defaults.

### Checking in a hidden window

`Phoenix capture-window <file|-> --out shot.png --steps steps.txt` lays out and draws the main
window without putting it on screen (`WA_DontShowOnScreen`). Unless `PHOENIX_SETTINGS_DIR` is set,
it uses a temporary settings folder. It then plays the steps from
[src/gui/capture_steps.cpp](../src/gui/capture_steps.cpp) (the syntax is in
[capture_steps.h](../src/gui/capture_steps.h)). Use this to drive JSFL tools and dialogs as a
person would:

```text
# a JSFL tool (by its toolbox name), a drag with Ctrl held
tool Rig
drag 400,300 520,360 ctrl
# the step the tool's call made is named after its setToolName
expect rig
# answer the dialog the next step opens, then run the script that opens it
dialog Radius=12 press=OK
script ask.jsfl
capture out.png
```

The steps are `tool`, `click`, `drag`, `stroke`, `key`, `menu`, `goto`, `dialog`, `script`,
`capture` and `grab`, along with `selected` and `expect`, which check the selection kind and the
start of the last undo step's name (case-insensitive). A `dialog` step arms an answer for the dialog
opened by the **next** step, and the run fails if no dialog opens. `--script` runs a setup script
before the steps. `capture-window` saves nothing back to the file.
