# Python scripting, operations and the MCP server

This document covers three layers that sit on top of the scripting object model:

1. **Python scripting** ([src/python](../src/python)): Python scripts run over the same `fl` / `FLfile` object model JSFL scripts use, plus the out-of-process `phoenix` Python package ([python/phoenix](../python/phoenix)).
2. **Operations** ([src/gui/script_api.h](../src/gui/script_api.h), `ScriptApi`): Phoenix's own higher-level, JSON-in/JSON-out document operations (`in_betweens`, `lip_sync`, `tidy_library`, `bounce`, ...). Scripts reach them through `fl.phoenixCall`, and AI assistants reach them through MCP.
3. **The MCP server** ([src/gui/mcp_server.h](../src/gui/mcp_server.h), `McpServer`): Model Context Protocol over stdio or a localhost port. It exposes the operations as tools.

The JSFL engine, the shared object model (`jsfl::Host`, `jsfl::Session`, `ClassInfo`, `Value`) and event types are described in [jsfl.md](jsfl.md). Window and process structure is in [architecture.md](architecture.md). The user-facing pages are [website/scripting.html](../website/scripting.html) and [website/ai-assistants.html](../website/ai-assistants.html).

```
             .py script          .jsfl script         AI assistant (MCP client)
                 |                    |                         |
        python::Engine          jsfl::Engine            McpServer (stdio / 127.0.0.1)
                 \                  /                           |
                  jsfl object model (fl, FLfile, Document, ...) |
                         |                                      |
                 jsfl::Host::callOperation  --->  ScriptApi (operations)  <---+
                         |                              |
                  JsflHost (script_runner.cpp)     MainWindow, CommandStack
```

---

## 1. Python scripting

### 1.1 Files

| File | Role |
|---|---|
| [src/python/python_api.h](../src/python/python_api.h), [python_api.cpp](../src/python/python_api.cpp) | The subset of Python's C API that Phoenix uses, declared by hand. `python::load` finds a Python install and loads its shared library at run time. |
| [src/python/python_engine.h](../src/python/python_engine.h), [python_engine.cpp](../src/python/python_engine.cpp) | `python::Engine`: turns `ClassInfo` tables into Python types, converts values, runs scripts, handles stop and time limits, and dispatches event listeners. Also holds the embedded Python support code (`kSupport`). |
| [src/python/python_keywords.h](../src/python/python_keywords.h), [python_keywords.cpp](../src/python/python_keywords.cpp) | `python::parameterNames`: the parameter-name table that keyword arguments need. |
| [src/gui/script_runner.cpp](../src/gui/script_runner.cpp) | `ScriptRunner` owns one `jsfl::Engine` and one `python::Engine` for each window and sends each `.py` / `.jsfl` path to the right engine. `JsflHost` is the window's `jsfl::Host`. |
| [python/phoenix/\_\_init\_\_.py](../python/phoenix/__init__.py), [\_server.py](../python/phoenix/_server.py) | The `phoenix` package, which lets a user's own Python drive a headless Phoenix (section 1.10). |

### 1.2 Build switch

[CMakeLists.txt](../CMakeLists.txt) defines `option(PHOENIX_PYTHON ... ON)`. When it is on:

- `phoenix_add_core(... PYTHON ...)` adds `PHOENIX_CORE_PYTHON_SOURCES` (the three `src/python/*.cpp` files) to the Qt-free `phoenix_core` library ([cmake/PhoenixCore.cmake](../cmake/PhoenixCore.cmake)). The only extra link dependency is `${CMAKE_DL_LIBS}`.
- `Phoenix` is compiled with `PHOENIX_PYTHON` defined. `ScriptRunner` checks it with `#ifdef` (`scriptPatterns`, `run`, `stop`, `running`, `isScript`). With the option off, running a `.py` file reports "This Phoenix was built without Python scripts."
- The `python/phoenix` package is installed beside the executable (`${PHOENIX_INSTALL_BINDIR}/python`, or `Phoenix.app/Contents/Resources/python` on macOS).
- `tests/CMakeLists.txt` adds `test_python.cpp` (`PHOENIX_TESTS_HAVE_PYTHON`).

No Python headers or libraries are needed at build time.

### 1.3 Finding and loading Python (`python_api.cpp`)

`python::load(hint, error)` runs once per process. The result is cached in function statics, so a failure is not retried. `Engine::available()` and `Engine::description()` call it. The hint is the `PHOENIX_PYTHON` environment variable, because `Engine` always passes an empty hint.

- `PHOENIX_PYTHON=off|0|none` turns Python off ("Python scripts are turned off").
- Otherwise `candidates()` builds an ordered list:
  1. The hint (`fromHint`): a shared library (`.dll`, `.dylib`, `*.so*`, `Python`, `Python3`), a python executable, a virtual environment, or Python's folder. On Windows a venv is recognized by `pyvenv.cfg` (beside the hint or one folder up), and its `home =` line names the base install. On POSIX, `askPython` runs `<python> -c "import sys, sysconfig ..."` through `popen` and reads `base_prefix`, `prefix`, `executable` and the library path. `prefix != base_prefix` means a venv.
  2. **Windows:** registered installs under `HKCU`/`HKLM\Software\Python\PythonCore\3.N\InstallPath`, newest minor first. Keys such as `3.12-32` (32-bit) and `3.13t` (free-threaded) are skipped because they fail the plain `3.N` parse. Then every `PATH` entry except `WindowsApps`. A folder counts as a Python install when it has `Lib/os.py` and `python3.dll` (or any `python3*.dll`).
  3. **POSIX:** `python3` on `PATH`, then fixed locations for 3.14 down to 3.8. On macOS these are `/Library/Frameworks/Python.framework/...`, `/opt/homebrew/opt/python@3.N/...` and `/usr/local/opt/python@3.N/...`. On Linux the loader searches for `libpython3.N.so.1.0`.
- Each candidate is opened (`LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`, or `dlopen(RTLD_NOW | RTLD_GLOBAL)` so extension modules can resolve symbols). `bind()` then resolves every function listed in `PHOENIX_PYTHON_FUNCTIONS` plus the data exports (`_Py_NoneStruct`, `_Py_NotImplementedStruct`, `PyExc_*`). A missing symbol or a version below 3.8 (`kMinimumMinor`) moves on to the next candidate, and the reason goes into the final error text.

Only the stable ABI is used. `PyObject` is declared as `{refcnt, type}`, which is the layout of the ordinary (not free-threaded) build. To call a new C API function, add it to the `PHOENIX_PYTHON_FUNCTIONS` X-macro in `python_api.h`. Every entry is required, so it must exist in every supported Python version. Optional symbols are bound by hand, as `Py_SetPythonHome` is.

### 1.4 Initialization (`initialize`, `setUp` in `python_engine.cpp`)

This happens once, on the first run:

- If Python is not already initialized, Phoenix sets the home. It uses `Py_SetPythonHome` when that symbol exists and falls back to `PYTHONHOME` otherwise, but only when `homeHolds()` finds `os.py` there. It then calls `Py_InitializeEx(0)`, so Python installs no signal handlers. Afterwards it calls `PyEval_SaveThread()` so the GIL is free between runs and threads that scripts start can keep going.
- `setUp` creates:
  - the module `phoenix` in `sys.modules`, with `phoenix.Error` (subclass of `Exception`), `phoenix.Object` (the root type), `trace`, `alert`, `confirm` and `prompt`;
  - the internal type `phoenix.Method` (`Bound`), which is a callable bound method;
  - `phoenix._support`, a dict namespace that runs `kSupport`. This is Python source in the C++ file that handles output redirection, error description, dates, the `fl` wrapper and listeners.
  - When the loaded Python came with a venv or executable, it calls `use_environment`, which runs `site.addsitedir(<venv>/Lib/site-packages)` (or `lib/pythonX.Y/site-packages`) and sets `sys.executable`. With `sys.executable` set, a script's subprocesses run Python and not Phoenix.

One interpreter serves every run in the process, so imported modules stay imported between runs.

### 1.5 The object-model bridge

Each `jsfl::ClassInfo` becomes a Python heap type, created lazily by `typeFor()` and cached in `g.types`. Its base is the parent class's type, or `phoenix.Object` at the root. The type gets one `PyGetSetDef` for every property and every method:

- A property getter calls `Property::get`. The setter exists only when `Property::set` does. Deleting a property raises `Phoenix's properties cannot be deleted.`
- A method getter returns a `Bound` object that holds the `ObjectPtr`, the `jsfl::Method`, and the owner `ClassInfo` (the owner is used for keyword lookup).
- Instances have no `__dict__`, so a misspelled assignment (`doc.widht = 3`) raises `AttributeError` instead of creating an attribute. `python_errors_name_the_line` tests this.

**snake_case aliases.** `snake()` converts lowerCamelCase and handles acronyms: `getDocumentDOM` → `get_document_dom`, `frameCount` → `frame_count`. An alias is added only when it differs from the original name and does not collide with another member name in the class chain (`taken`).

**Keyword arguments.** `callBound()` turns positional arguments into an `Args` vector. Each keyword is then looked up with `parameterNames(owner->name, method->name)` in [python_keywords.cpp](../src/python/python_keywords.cpp). The table key is `"Class.method"` first and then the bare `"method"`, and its names are the JSFL reference's parameter names without the `b`/`str` prefixes. A keyword matches either the name or `snake(name)`. Skipped positions remain `Value()` (undefined), which is what JSFL passes for an omitted argument. Errors are `TypeError`s:

| Situation | Message |
|---|---|
| Method has no table entry | `insertFrames() takes no keyword arguments` |
| Unknown keyword | `... has no parameter X; it takes num_frames, all_layers, frame_num_index` |
| Same slot given both ways | `... got X both by position and by name` |

To let a new model method take keywords, add a row to `table()` with its parameter names in JSFL order. Use `"Class.method"` when another class defines a method with the same name differently.

**Value conversion** (`toPy` / `fromPy`):

| `jsfl::Value` | Python |
|---|---|
| Undefined, Null | `None` |
| Boolean | `bool` |
| Number | `int` when integral and below 2^53, else `float` |
| String | `str` |
| Date (ms) | timezone-aware UTC `datetime` (`make_date`; `to_ms` back) |
| Object | instance of the class's type |
| Array | `list` (list or tuple coming in) |
| Map | `dict` (keys converted with `str()` coming in) |

Coming in, Phoenix also accepts `bytes` (converted to text with `str()`), anything `float()` accepts (`Decimal`, numpy scalars), and any iterable (`range`, `set`, generators), which becomes an array. Anything else becomes `str(obj)`. Recursion stops at depth 16.

**Identity.** `same()` compares underlying pointers, or `identity()` + `scope()` + class, so `fl.documents[0] == fl.getDocumentDOM()`. `hashInstance` uses the same key, so model objects work in sets and dicts.

**Lifetime and threads.** Every run increments `g_generation`. An `Instance` or `Bound` from an earlier run raises "belongs to a script run that has ended" (`objectOf`, `callBound`). Objects may be used only on the thread that runs the script (`requireRunThread`). On any other thread they raise. `trace` output from other threads is silently dropped, and `alert`/`confirm`/`prompt` raise.

**Errors.** `guarded()` turns C++ exceptions into `phoenix.Error` with `what()` as the message, and `std::bad_alloc` becomes "Phoenix ran out of memory.". After a failed run, `capture()` calls `describe()` in `kSupport` to fill `RunResult::message`, `file`, `line` and `stack`. The line it reports is the innermost traceback frame that is not under `sys.prefix` / `sys.base_prefix`, so the error points at the script's own line and not at library code.

### 1.6 A run (`Engine::run`)

1. Refuse to start if a run is already in progress ("A script is already running."), then call `initialize()`.
2. Create a `jsfl::Session(host, options.undoName)` (see [jsfl.md](jsfl.md)). Each run's changes become one undo step.
3. Take the GIL and set `g_inRun`, `g_runThread` and `g_host`.
4. Build fresh globals: `__builtins__`, `__name__ = "__main__"`, `__file__`, plus `fl` (the `kSupport.Flash` wrapper around `jsfl::makeFlash(session)`), `FLfile`, `alert`, `confirm` and `prompt`. `fl` and `FLfile` are also stored in the `phoenix` module, which is how imported modules reach them (`import phoenix; phoenix.fl`).
5. `begin()`: point `sys.stdout`/`sys.stderr` at an `Output` that sends whole lines to `Host::trace`, set `sys.argv = [path]`, and put the script's folder at the front of `sys.path`.
6. Start a `Watchdog`, compile and run the script, then catch errors.
7. Clean up: `end()` restores stdout, argv and sys.path, and `phoenix.fl` and `phoenix.FLfile` are reset to `None`. The globals dict is cleared, which breaks cycles, unless an event listener still refers to it (`keeps`). Then `readEvents()`, GIL release, `session.finish()` and `flushEvents()`.

`RunOptions` fields that Python honors: `undoName`, `timeLimit` and `poll`. `memoryLimit` applies only to the JSFL engine. A Python `MemoryError` is reported with `interrupted = true`.

### 1.7 Stopping scripts

`Guard` and `Watchdog`:

- Every 50 ms the watchdog thread takes the GIL and calls `Py_AddPendingCall(&check)`. It takes the GIL because Python before 3.13 notices pending calls from other threads only when its own thread reacquires the lock.
- `check()` runs on the script's thread between bytecodes. It calls `options.poll` (the window's progress and Stop dialog), then raises `KeyboardInterrupt("The script was stopped.")` if `Engine::stop()` set `_stop`, or `"The script took too long and was stopped."` once the deadline has passed.
- `Engine::stop()` is safe to call from any thread.
- Because the stop arrives as a normal `KeyboardInterrupt`, a script with a bare `except:` or `except BaseException:` catches it.

In the window, `ScriptRunner::runFile` wraps the run in `LongRun`. After about 1 s it shows a modal "Running X..." `QProgressDialog` whose Stop button calls `ScriptRunner::stop()`, which stops both engines. Time the user spends answering a script's own dialogs resets the clock. Headless runs use `--timeout` (default 600 s, `0` = none).

### 1.8 `fl.runScript` from Python

`Engine::run` installs `Flash::runScript` as a lambda:

- **`.py` target:** the file is compiled into a fresh dict that shares `__builtins__`, `fl`, `FLfile`, `alert`, `confirm` and `prompt` with the caller, with `__name__ = "__phoenix_script__"`. `session.scriptPath` is switched for the duration of the call. If a function name was given, that function is called with the converted arguments and its return value is converted back.
- **`.jsfl` target** (`Engine::runJsfl`): the call `session.settle()`s first, so Python's changes so far become their own undo step. It then runs the file on the window's `jsfl::Engine` (`setJsflEngine`) if that engine is idle, otherwise on a temporary one, with `RunOptions::callFunction` / `callArguments`. The remaining time limit is passed down, and `poll` forwards a Python Stop to `engine->stop()`. It settles again afterwards. A JSFL failure is raised in Python as `phoenix.Error(result.report())`. Model objects can be passed into JSFL but do not come back out: `RunResult::returned` holds plain values only. The test `python_run_script_runs_jsfl` shows that a returned layer arrives as `None`.

### 1.9 Event listeners

`kSupport.Flash` is a thin proxy around the native `fl`. It adds `addEventListener` / `removeEventListener` (and their snake_case forms), which register entries in `_listeners` as `(id, event, function, script globals, script undo name)`.

- After every run, `readEvents()` copies the set of listened events into `Engine::_events` (event → name of the first listening script). `listening()` reports whether any remain.
- `ScriptRunner::dispatchEvent(type)` calls both engines. `Engine::dispatch` returns immediately for events nobody listens to. If a run is in progress, the event is queued in `_pending` (consecutive duplicates are collapsed) and replayed by `flushEvents()`.
- A dispatch builds a fresh `fl` for the current moment, injects it and `FLfile` into each listener's globals, and calls the listeners with a 5 s deadline. Their changes form one undo step named after the first listening script. A listener's exception is traced as `"<event> listener: <traceback>"`, and the other listeners still run.
- Removing a listener clears its script's globals once no listeners remain from that script (`_release`). `forgetListeners()` drops them all.

### 1.10 Running Python scripts

| Entry point | Code path |
|---|---|
| Commands menu (user's `Configuration/Commands` folder; `*.py` listed when `PHOENIX_PYTHON`) and **Run Command...** | `ScriptRunner::fillCommandsMenu` → `runFile` |
| Script Editor (**Commands > Script Editor**) | `ScriptWindow::run` saves the script, or writes an untitled one to a temp file, then runs the path |
| `Phoenix script.py` while Phoenix is open | `ScriptRunner::handToRunning` drops a `.request` file that the running window picks up (`takeRequests`) |
| `Phoenix run-script script.py [docs...] [--timeout S]` | [command_line.cpp](../src/gui/command_line.cpp) creates a non-interactive `MainWindow`, then `ScriptRunner::runHeadless`. Trace goes to stdout, errors to stderr, and a failure exits with 1. |
| `Phoenix batch --python script.py <files or folders> [--save] [--out DIR] [--recursive] [--timeout S]` | `batchScript` → `ScriptRunner::runBatch` runs the script once per document. Exits with 2 if any document had problems. |

Within a run, `options.undoName` is the script file's base name.

**The out-of-process `phoenix` package** ([python/phoenix/\_\_init\_\_.py](../python/phoenix/__init__.py)) uses the same name as the embedded module, which can be confusing. `phoenix.Session` locates Phoenix (`exe=`, `PHOENIX_EXE`, the install the package came with, `%LOCALAPPDATA%\Phoenix\current`, `/Applications`, `PATH`, `build*/` in the source tree) and launches `Phoenix run-script _server.py --timeout 0`. It sets `PHOENIX_PYTHON` to its own `sys.executable` (unless already set) and sets `QT_QPA_PLATFORM=offscreen` when the offscreen plugin is present. [\_server.py](../python/phoenix/_server.py) runs inside Phoenix as one long script run, so objects stay valid between requests. There, `import phoenix` resolves to the embedded module. The server keeps objects in a number table and speaks a line-delimited JSON protocol (`op_get`, `op_set`, `op_call`, `op_python`, `op_jsfl`, `op_operation`, ...). Values are tagged `$object`, `$method`, `$dict` and `$date`. Printed output is redirected to a temp file and returned with each reply. The client releases numbers in batches (`release`) as its proxies are garbage-collected.

### 1.11 Platform notes

- **Windows:** `python3.dll` is preferred, and `LOAD_WITH_ALTERED_SEARCH_PATH` lets it find `python3X.dll` beside it. The Store `WindowsApps` aliases are skipped.
- **macOS:** framework and Homebrew paths are hard-coded, because a GUI app's `PATH` usually lacks `python3`. CI ([.github/workflows/tests.yml](../.github/workflows/tests.yml), [release.yml](../.github/workflows/release.yml)) builds the Python code on macOS but runs the tests with `PHOENIX_PYTHON=off`, so `test_python.cpp` skips itself there. The workflow comment says loading "has [not] been tried on a Mac". Treat macOS loading as untested.
- **Linux:** `dlopen` with `RTLD_GLOBAL`. The fallback relies on the loader finding `libpython3.N.so.1.0`.
- Free-threaded (`t`) builds are not supported, because the `PyObject` layout differs.

---

## 2. The operations layer (`ScriptApi`)

### 2.1 What it is

[src/gui/script_api.h](../src/gui/script_api.h) / [script_api.cpp](../src/gui/script_api.cpp). `ScriptApi` is a single registry of named operations. Each one has a JSON Schema for its arguments and returns a `Result {QJsonValue value; QImage image; QString error;}`. It runs on the GUI thread against the `MainWindow`'s front document (`_window._flaDocument`). It is a friend of `MainWindow`, and the helper `struct ScriptContext` (also a friend) wraps the window internals that operations need.

Two independent instances exist:

| Owner | `fromOutside` | `approveWrite` | Reached by |
|---|---|---|---|
| `McpServer::_api` | `true` | asks the user once per folder (auto-yes under `offscreen`) | MCP `tools/call`, `resources/read` |
| `JsflHost::_api` (created lazily in `callOperation`) | `false` | unset | `fl.phoenixCall` from JSFL / Python, and so `phoenix.Session.call` |

Because `approveWrite` is unset in the script host, **`export_image` and `render_from_data` always fail when called from scripts** ("The person did not allow writing there."). `begin_edit` group state is also per instance.

### 2.2 Addressing conventions

These come from the class comment and the `ScriptContext` helpers:

- `symbol`: a library symbol name. Work happens in its first timeline. When omitted, the current scene is used (`ScriptContext::timeline`).
- `layer`: a name or a 0-based index from the top (`layerIndex`). Some operations use other keys, such as `"above"`.
- `frame`: **1-based** (`frameIndex`, valid range 1..100000), unlike JSFL's 0-based indices.
- `index`: an element's position on its keyframe, 0 = bottom (`element`).
- Colors: `#RRGGBB` / `#RRGGBBAA` (`parseColor`, `colorText`).

### 2.3 The operations

They are registered in the constructor, in this order: `addReadOperations`, `addWriteOperations`, `addHelperOperations`, `addMotionGraphicsOperations` (the helper ones live under `addHelperOperations`). "writes" is the `Operation::writes` flag.

| Group | Name | writes | Required args |
|---|---|---|---|
| Read | `get_document`, `get_timeline`, `get_library`, `check_document`, `render_frame` (returns image) | no | none |
| Read | `get_elements` | no | layer, frame |
| Docs / view | `new_document`, `go_to` | no | none |
| Docs / view | `open_document` | no | path |
| Undo grouping | `begin_edit` (label), `end_edit` | no | label / none |
| Layers | `add_layer` (name), `set_layer`, `delete_layer` (layer) | yes | as shown |
| Frames | `insert_keyframe`, `insert_frames`, `remove_frames` | yes | layer, frame |
| Frames | `set_tween` (layer, frame, tween), `set_label` (layer, frame, label) | yes | as shown |
| Artwork | `add_shape` (path), `add_text` (text), `add_instance` (item) | yes | layer, frame + shown |
| Artwork | `modify_element`, `delete_element` | yes | layer, frame, index |
| Library | `convert_to_symbol` (layer, frame, name), `rename_library_item` (from, to) | yes | as shown |
| Export | `export_image` (path, `.png` only) | no | path |
| Motion graphics | `list_effects`, `list_node_types`, `get_node_graph` | no | none |
| Motion graphics | `set_effects` (layer, frame, index, list), `set_layer_compositing` (layer), `set_node_graph`, `set_template_values` (layer, frame, index), `rename_to_convention` (what, style) | yes | as shown |
| Motion graphics | `render_from_data` (rows, folder; mp4/mov/gif/png) | no | rows, folder |
| Helpers | `tidy_library`, `bounce` (layer), `lip_sync` (layer), `in_betweens` (layer, frame), `pose_cycle` (layer), `apply_palette` (colors) | yes | as shown |
| Helpers | `color_script` | no | none |

To see the live list with full descriptions and schemas, call `fl.phoenix.operations()` from JSFL. The authoritative source is the `add(...)` calls.

Some helper operations as examples of the pattern:

- `in_betweens` requires the next keyframe at `frame + duration` and a span of at least 2. Both keyframes must hold a single symbol instance (classic tween) or only shapes (shape tween). Inside one `EditFramesCommand` it sets the tween and its `acceleration = ease`, calls `fla::frames::convertToFrameByFrame`, and with `onTwos` clears every other keyframe. It returns the drawing count and `timelineJson`.
- `bounce` requires a single symbol instance on the keyframe. It computes landing and apex keys (plus a stretch key with `squash`), adjusts the transform scale and `ty` around the instance's lower edge, and sets classic tweens with ±100 eases, all inside one `EditFramesCommand`.
- `lip_sync` takes a mouth graphic instance. It parses cues (`fla::lipsync::parse`) or analyzes a library sound (`fla::lipsync::listen`). The mouth chart comes from the arguments, the symbol's `phoenixMouthChart` extra attribute, or `defaultChart`. It saves the chart back onto the symbol with a `SetPropertyCommand`.
- `tidy_library` moves loose items into `Symbols` / `Bitmaps` / `Sounds` folders with `AddFolderCommand` + `RenameLibraryCommand`. With `removeUnused` it also deletes unused symbols (`DeleteSymbolCommand`).

### 2.4 `ScriptApi::call`: undo and guards

```cpp
if (!found->writes) return found->run(arguments);          // reads: no undo step
if (_window.isReadOnlyDocument()) fail("This document is read-only ...");
if (grouped) { if (!groupTrouble().isEmpty()) fail("The edit begin_edit opened has ended: ..."); }
else if (fromOutside && stack.isInMacro()) fail("Phoenix is in the middle of another change; ...");
if (!grouped) stack.beginMacro("AI: " + title);
result = found->run(arguments);                              // pushes fla::Command objects
if (!grouped) { stack.endMacro(); stack.breakMergeChain(); }
context.refresh();                                           // MainWindow::refreshAfterScript
```

- Each write operation is one undo step named `AI: <title>`, unless `begin_edit` is open. In that case, everything until `end_edit` becomes one step named `AI: <label>`.
- `begin_edit` records the document and the stack's `index()`/`count()`. `groupTrouble()` detects that the front document changed or that something else closed the macro. `end_edit` adds a `note` if the history moved while the group was open (an undo or redo happened in between).
- `CommandStack` macros nest, and only the outermost one produces a history entry ([src/edit/command_stack.h](../src/edit/command_stack.h)). So an operation called from a script folds into the script's `jsfl::Session` step, as the user docs describe. The `fromOutside` check exists so that an MCP call arriving during a user drag or script is rejected and not merged into that change.
- On an exception the macro is still closed and the view refreshed. `ScriptError` (thrown by `fail()`) passes its message through unchanged. Any other `std::exception` is reported as `That failed: ...`.
- Arguments are **not** validated against the schema. Operations read them with `QJsonObject::value(...).toX(default)` and clamp ranges themselves.

### 2.5 Reaching operations from scripts

- `jsfl::Host::callOperation(name, argumentsJson, resultJson, error)` ([src/jsfl/host.h](../src/jsfl/host.h)) is a virtual function. The base implementation fails with "Phoenix's operations need the Phoenix window.", which is what test hosts get.
- `Flash.phoenixCall(name, argumentsJson)` ([src/jsfl/model_flash.cpp](../src/jsfl/model_flash.cpp)) calls it and returns JSON text.
- `JsflHost::callOperation` ([src/gui/script_runner.cpp](../src/gui/script_runner.cpp)) parses the arguments (they must be an object) and calls `ScriptApi::call`. With an empty name it returns the operation list as `[{name, title, description, arguments, changes}]`. Only `Result::value` is returned, so images from `render_frame` and `color_script` are dropped.
- The JSFL prelude in [src/jsfl/engine.cpp](../src/jsfl/engine.cpp) wraps this:

```js
fl.phoenix = {
    call: function (name, args) { return JSON.parse(fl.phoenixCall(String(name), JSON.stringify(args || {}))); },
    operations: function () { return JSON.parse(fl.phoenixCall("", "{}")); }
};
```

- Python does not get the `fl.phoenix` object, because the prelude is JavaScript. Use the raw method:

```python
import json
result = json.loads(fl.phoenix_call("in_betweens", json.dumps({"layer": "Arm", "frame": 1, "onTwos": True})))
```

  The keyword table has `Flash.phoenixCall: {name, argumentsJson}`, so `fl.phoenix_call(name=..., arguments_json=...)` also works. Out of process, `phoenix.Session.call("add_layer", name="X")` goes through `_server.op_operation`.

### 2.6 Adding an operation

1. Choose the group function in `script_api.cpp` (`addReadOperations`, `addWriteOperations`, `addHelperOperations` or `addMotionGraphicsOperations`) and call `add(name, title, description, properties, required, writes, run)`.
   - Build `properties` with `prop(type, description)`, `anyOf(...)`, `choice(values, description)` and the shared `kSymbolArg` / `kLayerArg` / `kFrameArg`.
   - `title` becomes the undo label (`AI: <title>`) and the MCP status-bar text.
   - `description` is what assistants read. State defaults ("1 when left out") and units.
2. In `run`, create `ScriptContext context{_window};` and resolve inputs with `context.timeline(args)`, `context.layer(...)`, `context.keyframe(...)`, `context.element(...)` and `context.symbolNamed(...)`. Report problems with `fail("...")`. Write the messages for the user and the assistant, and say what to do next.
3. Change the document **only** by pushing `fla::Command`s onto `context.stack()` (see `src/edit/*_commands.h` and `EditFramesCommand`). Never mutate the document directly, or undo breaks. Set `writes = true` for anything that pushes commands.
4. Return compact JSON that helps the caller check its work (counts, names, sometimes `timelineJson`). Put pictures in `Result::image`.
5. If it writes outside the document, call `approveWrite(path)` first and fail when that returns false.
6. Name destructive operations `delete_*` (or extend the `destructiveHint` rule in `McpServer::handle`).
7. Update the table in [website/ai-assistants.html](../website/ai-assistants.html) and add a CHANGELOG bullet.

The new operation then appears automatically in MCP `tools/list`, `fl.phoenix.operations()` and `phoenix.Session.operations()`.

---

## 3. The MCP server

### 3.1 Lifecycle and transports

[src/gui/mcp_server.h](../src/gui/mcp_server.h), [mcp_server.cpp](../src/gui/mcp_server.cpp), [src/main.cpp](../src/main.cpp), [src/gui/main_window.cpp](../src/gui/main_window.cpp).

| How started | What happens |
|---|---|
| Normal launch | The first `MainWindow` creates `McpServer` and calls `applySettings()`. If Preferences > *Let AI assistants connect (MCP)* (`QSettings mcp/enabled`, port `mcp/port`, default **47290**, clamped to 1024..65535) is on, it listens on `127.0.0.1:<port>`. |
| `Phoenix --mcp` (exactly two args) | Before any window exists, `McpServer::relayToRunning()` connects to the port if it is enabled and pipes stdin and stdout until either side closes. No window is created. If the connection fails, Phoenix starts normally without the start screen and calls `MainWindow::serveMcpOnStdio()` → `serveStdio()`. |
| `Phoenix --mcp --port N` | Session relay (`relayToRunning(N)`). It first sends `{"phoenixSession": $PHOENIX_MCP_TOKEN}`, then relays. If that Phoenix is gone, it exits with 1. It never opens a window. |
| **Commands > Open Claude Code** | `claude_code::prepare` ([claude_code.cpp](../src/gui/claude_code.cpp)) finds `claude` (on `PATH`, then installer locations) and calls `McpServer::openSession(token)`. That listens on the Preferences port if enabled, otherwise on a free port (`listen(0)`), and creates a UUID token. `writeConfig` writes `claude-code-<pid>.json` to AppLocalData (owner read/write only, deleted on `aboutToQuit`) with `{"mcpServers":{"phoenix":{"type":"stdio","command":<Phoenix exe>,"args":["--mcp","--port",N],"env":{"PHOENIX_MCP_TOKEN":token}}}}`. `ClaudeCodePanel` ([claude_code_panel.cpp](../src/gui/claude_code_panel.cpp)) runs `claude --mcp-config <file>` in a `TerminalView`, with the working directory set to the document's folder (or the home folder). |

The framing is JSON-RPC 2.0, **one message per line** in both directions. A trailing `\r` is stripped. A socket connection is closed if more than 64 MB arrives without a newline (`readLine`). Sockets are plain BSD/Winsock, because Qt Network is not part of the shipped Qt. The listener binds `INADDR_LOOPBACK` only, and on Windows uses `SO_EXCLUSIVEADDRUSE`.

**stdio details** (`serveStdio`): Phoenix duplicates the real stdout into `Shared::stdoutFd` and then points fd 1 at stderr, so stray `printf` output cannot corrupt the protocol. On Windows both streams are switched to binary mode.

**Threading** (`dispatch`): reader threads (one per socket, one for stdin) never touch the GUI. Each line is parsed and handed to the GUI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. The reader then waits on `Shared::answered` for the reply, or for `stopping`/`closing`. If it gives up, the `Pending` is marked `abandoned` and the GUI side skips it. `Shared` is held by `shared_ptr`, so readers can outlive the server. When the server closes, the stdin reader is detached because it cannot be woken.

### 3.2 Methods

`McpServer::handle` (GUI thread):

| Method | Behavior |
|---|---|
| (has `phoenixSession`) | If the token matches `_sessionToken`, the client is allowed without a prompt. No reply. |
| notifications (no `id`) | Ignored. |
| `ping` | `{}` |
| `initialize` | Takes the client name from `clientInfo.name` and calls `allow()` (consent). Negotiates `protocolVersion` from `2025-06-18`, `2025-03-26` and `2024-11-05`, falling back to the newest. Capabilities `{tools:{}, resources:{}}`, `serverInfo {name:"phoenix", title:"Phoenix", version}`, and an `instructions` string (frames from 1, look before changing, use `begin_edit`/`end_edit`). |
| `tools/list` | One tool per `ScriptApi::Operation`: `name`, `title`, `description`, `inputSchema = schema`, `annotations {readOnlyHint: !writes, destructiveHint: name starts with "delete" or == "tidy_library", openWorldHint: false}`. |
| `tools/call` | `callTool`: shows the title in the status bar, calls `ScriptApi::call`, and logs. Success returns `content: [{type:"text", text:<compact JSON>}]` plus `{type:"image", mimeType:"image/png", data:<base64>}` when `Result::image` is set, and `isError:false`. Failure returns a text item with the error and `isError:true` (a tool error, not a JSON-RPC error). |
| `resources/list` | `phoenix://document`, `phoenix://timeline`, and `phoenix://symbol/<name>` for every symbol. |
| `resources/read` | Maps to `get_document` / `get_timeline` / `get_timeline{symbol}`, and returns `application/json` text. |
| other | `-32601` |

Error codes used: `-32700` (line is not a JSON object), `-32001` (user did not allow), `-32002` (resource read failed), `-32003` (a modal dialog is up), `-32601` (unknown method).

Note that `readOnlyHint` comes straight from `writes`, so `new_document`, `open_document`, `go_to`, `begin_edit`, `export_image` and `render_from_data` are advertised as read-only even though they change state or write files.

### 3.3 Safety

- **Consent:** `allow()` shows an Allow / Don't Allow box naming the client. Allowed names are remembered (`_allowedNames`) until **Stop**. Under `QT_QPA_PLATFORM=offscreen` every client is allowed without asking ("whoever ran it that way chose to"). The Claude Code session token skips the prompt.
- **Modal guard:** while `QApplication::activeModalWidget()` is set, `tools/call` and `resources/read` are refused, and so is any request from a client that has not been allowed (`-32003`).
- **Concurrent edits:** `fromOutside` rejects a write while the user's own macro is open (section 2.4). Read-only documents are refused.
- **Files:** `approveWrite` asks once per folder (`_allowedFolders`). Operations never save the document.
- **Visibility:** a status-bar label `AI: <clients> — <activity>` and a **Stop** button (`stopAll`). Stop revokes every allowance, clears the session token (so the Claude Code panel's client has to be approved after that), and shuts down sockets.
- **Audit log:** `mcp.log` in `PHOENIX_SETTINGS_DIR` or AppLocalData. Each line is `ISO time \t client \t tool \t ok|failed \t args (first 400 bytes)`. At 4 MB the file is rotated to `mcp.log.1`.

### 3.4 Connecting assistants

User instructions are in [website/ai-assistants.html](../website/ai-assistants.html): a Claude Desktop `mcpServers` entry with `"args": ["--mcp"]`, `claude mcp add phoenix -- <Phoenix> --mcp`, and the Preferences switch that makes `--mcp` attach to the open window. Without that switch, a client-launched `Phoenix --mcp` gets its own window serving stdio.

---

## 4. Testing and debugging

### 4.1 Python

- [tests/test_python.cpp](../tests/test_python.cpp) uses `PythonHost`, an in-memory `jsfl::Host` with one document, its own `CommandStack`, and dialogs answered immediately. Each test calls `havePython()` first and **skips with a message** when `python::Engine::available()` fails, so a green run on a machine without Python proves nothing. Look for `skipped (no Python)` in the output. The tests cover output and dialogs, editing with one undo step, error file and line, stop and time limit, run-generation and thread checks, value conversion, `fl.runScript` of `.py` and `.jsfl`, keyword arguments, and listeners.
- Run them through the test binary's name filter (`main` in [tests/test_main.cpp](../tests/test_main.cpp) matches `argv[1]` as a substring): `phoenix_tests python_`. Use a private `TEMP` if suites run in parallel, because the fixtures write under `temp_directory_path()`.
- To pick an interpreter: `PHOENIX_PYTHON=C:\Python312\python.exe` (or a venv folder). To confirm the off path: `PHOENIX_PYTHON=off`.
- [tests/python/test_phoenix_module.py](../tests/python/test_phoenix_module.py) drives a built Phoenix through the `phoenix` package: `python tests/python/test_phoenix_module.py [--exe path]`. Put `platforms/qoffscreen.dll` beside the exe so no window appears. It covers sessions, errors, `python`/`jsfl` requests, save/open/export, `operations()`/`call()` (`test_phoenix_operations`), object release, and `batch --python`.
- Manual check: `Phoenix run-script my.py some.fla --timeout 0` prints trace to stdout and the error report with traceback to stderr.

### 4.2 Operations

No C++ unit test exercises `ScriptApi` directly, because it needs a `MainWindow`. The closest coverage is `test_phoenix_operations` in the Python module tests, which runs through `JsflHost::callOperation`. For a quick check, write a JSFL or Python script that calls `fl.phoenixCall` and run it headless with `run-script` against a test FLA. Remember that `export_image` and `render_from_data` refuse to run from scripts.

### 4.3 MCP

No automated MCP tests were found in `tests/`. Under `offscreen` the consent and approve-write prompts are skipped, so you can drive the stdio server by hand:

```sh
QT_QPA_PLATFORM=offscreen Phoenix --mcp some.fla
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","clientInfo":{"name":"test"}}}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_document","arguments":{}}}
```

The relay check in `main()` runs only when `--mcp` is the single argument (`argc == 2`). So `Phoenix --mcp` with no file, run while MCP is enabled in Preferences and another Phoenix is open, connects to that window. Disable the setting, or use a separate settings folder (`PHOENIX_SETTINGS_DIR`), for an isolated test. Replies appear on stdout. Everything else Phoenix prints goes to stderr. Check `mcp.log` to see which tool calls arrived and whether they succeeded. If a call returns `-32003`, a modal dialog is open in that window.
