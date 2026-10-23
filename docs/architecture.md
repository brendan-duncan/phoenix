# Phoenix architecture

This is the first note to read before working on Phoenix's source. It covers the layout of the
tree, the document model, how edits and undo work, how animation is evaluated, how the window and
the stage are built, the command line, the tests, and the conventions the code follows. Each
section describes the general shape and points to the files. The details are in the sibling notes
listed in [README.md](README.md).

Phoenix is a C++17 / Qt 6 application. The file formats it reads and writes natively are FLA (the
zipped XFL package), the XFL folder, the old binary FLA (read only), and its own `.phx` format.
Building is covered in [building.md](building.md).

## 1. The source tree

```
src/
  data/         the document model (namespace fla): DOMElement and its subclasses
  geom/         curve math, intersections, planar maps, fill tracing
  parser/       reading: XFL/FLA, binary FLA, SWF-as-document, zip, compound files
  writer/       writing: XFL XML, FLA zip, XFL folder, edge strings, document diffs
  phx/          the .phx format: container, wire encoding, read, write, dump
  edit/         commands, undo, selection, timeline edits, tweens, importers, exporters
  play/         Test Movie runtime: display list, ActionScript bindings, AVM1, canvas JS
  jsfl/         the JSFL engine (QuickJS) and its object model
  python/       Python scripting over the same object model
  util/         zip, PNG/GIF/APNG/EXR/SWF writers, LZMA, JSON, thread pool
  third_party/  vendored zlib, zstd, tinyxml2, miniaudio, libvterm
  gui/          everything Qt: MainWindow, PhoenixView, tools, panels, dialogs
  main.cpp      startup and command-line dispatch
resources/      files compiled into the app as Qt resources
tests/          phoenix_tests, the unit and round-trip tests
tools/          Python and JSFL scripts for checks, releases and the website
python/phoenix/ the Python package shipped next to the app
```

### Build targets and what is Qt-free

[cmake/PhoenixCore.cmake](../cmake/PhoenixCore.cmake) holds the only list of non-Qt sources. It
defines `phoenix_add_core(<target> <src> [FILES] [JSFL] [PYTHON] [PLAY])`, which builds an
**OBJECT library** from these groups:

| Group | Contents | Links |
|---|---|---|
| `PHOENIX_CORE_SOURCES` | `data/`, most of `edit/`, `geom/`, `parser/path_parser`, some `util/`, `writer/edge_writer`, plus `gui/audio_engine.cpp` and `gui/miniaudio_impl.c` (Qt-free, despite the folder) | — |
| `PHOENIX_CORE_FILE_SOURCES` | the readers and writers (`parser/`, `writer/`, `phx/`), importers in `edit/` that need them, zip/SWF/PNG code in `util/` | tinyxml2, zlib, zstd |
| `PHOENIX_CORE_JSFL_SOURCES` | `jsfl/` | QuickJS (`qjs`), `dl` |
| `PHOENIX_CORE_PYTHON_SOURCES` | `python/` (Python is loaded at run time, not linked) | `dl` |
| `PHOENIX_CORE_PLAY_SOURCES` | `play/`, plus the SWF and HTML5 publishers in `edit/` | libas3 (`as3_parser`), `qjs`; defines `PHOENIX_PLAY` |

The library has `AUTOMOC`, `AUTOUIC` and `AUTORCC` turned off. **Nothing in it may include Qt.**
The top-level [CMakeLists.txt](../CMakeLists.txt) creates `phoenix_core` with all groups (leaving
out `PYTHON` when `PHOENIX_PYTHON` is off) and then defines:

- **`Phoenix`**, the app (`qt6_add_executable`). It is built from `src/main.cpp` and the `src/gui`
  files listed in `SOURCES`/`HEADERS`, and links `phoenix_core`, Qt6 Widgets and Svg, Qt6
  Network when it is found (`PHOENIX_NETWORK`), libas3, libvterm and QuickJS.
- **`phoenix_tests`**, from [tests/CMakeLists.txt](../tests/CMakeLists.txt). It links the same
  `phoenix_core`, so each core source compiles once. You can also configure that file on its own
  with no Qt. It then builds `phoenix_test_core` with `FILES` only, and the JSFL, Python and play
  tests drop out.

Source lists are explicit, not globbed. A new core file goes into `PhoenixCore.cmake`, a new GUI
file into `SOURCES`/`HEADERS` in the top-level `CMakeLists.txt`, and a new test into
`TEST_SOURCES` (or one of the conditional lists) in `tests/CMakeLists.txt`. libas3 and QuickJS
come in through `FetchContent`. libas3 is pinned to a commit, and a sibling `../libas3` checkout
(or `PHOENIX_LIBAS3_SOURCE`) is used in its place when one exists.

### Dependency direction

```
            gui/  (Qt)  ───────────────────────────────┐
             │                                         │
   jsfl/  python/  play/                               │
       \     |      /                                  │
          edit/  ──── geom/                            │ all of these
           │  \                                        │ are phoenix_core,
   parser/ writer/ phx/  util/                         │ free of Qt
           │                                           │
         data/   (depends on nothing outside data/)  ──┘
```

These rules hold in the code:

- `data/` includes only its own headers. Every other directory builds on it.
- No core directory includes `gui/` except `edit/swf_event_sounds.cpp`, which includes the Qt-free
  `gui/audio_engine.h`.
- The layering inside the core is not strict. `parser/` and `writer/` include a few `edit/` headers
  (for example `edit/shape_tween.h`, `edit/phoenix_features.h`, `edit/tween_ease.h`), and `edit/`
  includes some `play/` and `parser/` headers. Treat `data/` as the base and `gui/` as the top, and
  keep new code as low as it can go.
- Python sits on top of JSFL: [src/python/python_engine.h](../src/python/python_engine.h) takes a
  `jsfl::Host` and a `jsfl::Engine`.

Namespaces: `fla` for the model and the editing core (with nested namespaces such as
`fla::timeline`, `fla::frames`, `fla::play`, `fla::display`), `phx` for the `.phx` code, `jsfl`
for scripting, and `commands` for the command line. The GUI classes and a few parsers
(`FLAParser`, `ZipReader`, `SwfImport`) are at global scope.

### resources/

Each folder becomes a Qt resource in `CMakeLists.txt`:

| Folder | Resource | What it is |
|---|---|---|
| [resources/as3](../resources/as3/README.md) | `:/as3` | The `fl.*` UI components as ActionScript 3 source, compiled by libas3 when a movie plays |
| [resources/html5](../resources/html5/README.md) | `:/html5` | The JavaScript runtime HTML5 publishing writes next to a page |
| `resources/templates` | `:/templates` | Start-screen starter documents as XFL folders with `template.json`; made by [tools/build_templates.py](../tools/build_templates.py) |
| `resources/tutorials` | `:/tutorials` | The interactive lessons (`lesson.json` and files) |
| `resources/cep` | `:/cep` | `adobe_cep.js` and `node.js`, the shims HTML extension panels start with |

## 2. The document model (`src/data`)

### The tree

Every model object derives from `fla::DOMElement`
([src/data/dom_element.h](../src/data/dom_element.h)). A `DOMElement` has a `parent` pointer, a
`visible` flag, a `domType()` that returns the `DOMElement::DOMType` enum, and a `domTypeName()`.
Subclasses provide a `staticDomType()` so that `findAncestorOfType<T>()` works. RTTI is not needed
to tell kinds apart: use `domType()`, `Element::elementType()` or `Resource::resourceType()`.

```
FLADocument                    filepath, fromBinary, fromSwf
└── Document                   stage size, frame rate, colors, grid, scene index...
    ├── timelines: Timeline*   the scenes
    │   └── layers: Layer*     Normal / Mask / Masked / Folder / Guide
    │       └── frames: Frame* keyframes, each with index + duration
    │           ├── elements: Element*
    │           ├── actionScript, morphShape (shape tween)
    │           └── tween settings, label, eases
    ├── symbolList: SymbolList → symbols: Symbol*  (each owns its timelines)
    ├── resources: Resource*   Bitmap and Symbol library items
    ├── folders: Folder*       library folders
    ├── swatchLists, scriptList, publishHistory, printerSettings
    └── extraSections / extraMedia / extraFiles   (kept unmodeled parts)
```

| Class | File | Notes |
|---|---|---|
| `FLADocument` | [fla_document.h](../src/data/fla_document.h) | Represents the file. Holds the path and a `Document*`. `fromBinary` means saving converts the file to the current format. `fromSwf` means the document was converted from a movie. |
| `Document` | [document.h](../src/data/document.h) | The `DOMDocument`. `currentSceneIndex()`/`currentScene()` give the scene shown. `reachable()` returns every object currently in the tree (see below). |
| `Timeline` | [timeline.h](../src/data/timeline.h) | A scene or a symbol's timeline: `name`, `layers`, ruler guides. |
| `Layer` | [layer.h](../src/data/layer.h) | `layerType`, `parentLayerIndex`/`parentLayer` (folder or mask parent), lock, outline, color, `frames`. |
| `Frame` | [frame.h](../src/data/frame.h) | One keyframe and its span: `index`, `duration`, `tweenType`, ease fields, `elements`, `actionScript`, `morphShape`. Frame numbers are zero-based. `kMaxFrames` caps the indexes. |
| `Element` | [element.h](../src/data/element.h) | The base class of everything placed on a frame: `transform`, `transformationPoint`, cached `bounds`. |
| `Symbol` | [symbol.h](../src/data/symbol.h) | A library symbol (a `Resource`): `symbolType`, `href` under `LIBRARY/`, 9-slice grid, `timelines`. |
| `Bitmap` | [bitmap.h](../src/data/bitmap.h) | A library bitmap. Its pixels are `LazyBytes` ([lazy_bytes.h](../src/data/lazy_bytes.h)), read the first time they are needed. |
| `SymbolList` | [symbol_list.h](../src/data/symbol_list.h) | `symbols` plus a name → symbol `symbolMap`. Use `link`/`unlink` to keep the map right when two symbols briefly share a name. |

### Element kinds

`Element::Type` names the kinds that can sit on a frame:

| Type | Class | File |
|---|---|---|
| `Shape` | `Shape`: `fills`, `strokes`, `edges`. `isDrawingObject` separates object drawing from merge artwork. | [shape.h](../src/data/shape.h), [edge.h](../src/data/edge.h) |
| `SymbolInstance` | `SymbolInstance`: `libraryItemName` and a resolved `symbol`, loop settings, color transform, filters, blend mode | [symbol_instance.h](../src/data/symbol_instance.h) |
| `Group` | `Group`: `members` | [group.h](../src/data/group.h) |
| `StaticText` | Static, dynamic and input text fields with their runs | [static_text.h](../src/data/static_text.h) |
| `BitmapInstance` | A placed library bitmap | [bitmap_instance.h](../src/data/bitmap_instance.h) |
| `Rectangle`, `Oval` | Primitive objects | [rectangle_primitive.h](../src/data/rectangle_primitive.h), [oval_primitive.h](../src/data/oval_primitive.h) |
| `Raw` | `RawElement`: an element Phoenix does not model (TLF text, components, video instances), kept as XML and not drawn, except a video instance, which `describeVideoInstance` reads so it can be drawn | [raw_element.h](../src/data/raw_element.h) |

How shapes, edges and fills work is covered in [geometry.md](geometry.md).

### Ownership

The model uses raw pointers. **A parent owns its children** and deletes them in its destructor
(`Frame::~Frame` deletes its `actionScript`, `morphShape` and elements, for example). The owning
classes delete copy construction (`Frame(const Frame&) = delete`). Copies are made with
`cloneElement` ([src/edit/element_clone.h](../src/edit/element_clone.h)) and `timeline::cloneFrame`
([src/edit/timeline_edit.h](../src/edit/timeline_edit.h)). When a command takes an object out of
the tree, the command owns it until it is put back or the command is destroyed (see section 3).

`Document::symbolInstances` and `symbolInstancesMap` index the instances. `SymbolInstance::symbol`
is resolved after parsing. `breakSymbolCycles` and `symbolContains`
([symbol.h](../src/data/symbol.h)) guard against a symbol that contains itself. Every way of
placing a symbol checks `symbolContains`.

### Preserving what Phoenix does not model

An FLA saved by Phoenix should lose nothing. The model keeps unknown content at every level:

| Where | Field | What it keeps |
|---|---|---|
| Any `DOMElement` | `extraAttributes` | Attributes that are not modeled, in order, written back as read |
| Any `DOMElement` | `presentAttributes` | The names of modeled attributes the file wrote, so they are written back even when they equal Phoenix's default |
| `Element`, `Layer`, `Frame`, `Timeline`, `Resource`, strokes | `extraChildren` | Unmodeled child elements as XML text, written after the modeled ones |
| `Frame` | `otherEases`, `otherTweenType` | Per-property eases. Tween kinds such as `"motion object"` and `"IK pose"`, for which `tweenType` stays `None`. |
| `Layer` / `Symbol` / `SymbolInstance` | `otherLayerType`, `otherSymbolType` | Kinds spelled as read (`"ik container"`, `"puppet"`) |
| `SymbolInstance` | `extraFilters` | Filters Phoenix does not know |
| frame element list | `RawElement` | Whole unknown elements, in their place among the others |
| `Document` | `extraSections`, `extraMedia`, `extraFiles` | Unmodeled document sections (and whether each came before the timelines), non-bitmap media items, and package files carried through byte for byte |
| `DOMElement` / `Document` | `phxUnknown`, `phxUnknownChunks` | Fields and chunks from a newer `.phx` |

On the reading side, `keepAttributes` in
[src/parser/document_parser.cpp](../src/parser/document_parser.cpp) takes the list of modeled
attribute names and sorts each attribute into `presentAttributes` or `extraAttributes`. The writer
([src/writer/xfl_writer.cpp](../src/writer/xfl_writer.cpp)) emits them again through `writeKept`
and appends the `extraChildren`. `writeKept` skips any attribute the writer has already set. When
you model a new attribute, add it to the parser's modeled list as well. Otherwise the value as read
also stays in `extraAttributes`, and it comes back on save whenever the writer leaves the modeled
field out (at its default, for example).

Features of Phoenix's own that FLA has no place for are written as `phoenix…` attributes. The one
list of them, used for the save warning and `--convert`, is
[src/edit/phoenix_features.h](../src/edit/phoenix_features.h).
`fla::unsupportedContent` ([src/writer/xfl_content.h](../src/writer/xfl_content.h)) reports what
an FLA save cannot write. How the formats are read and written is covered in
[fla-format.md](fla-format.md), [phx-format.md](phx-format.md) and [swf.md](swf.md).

### Reading and writing, in one paragraph

`FLAParser::parse` ([src/parser/fla_parser.h](../src/parser/fla_parser.h)) opens a `.fla` zip or an
XFL folder. It sniffs a binary FLA or a SWF and converts it to XFL files first
(`BinaryFla::convert`, `SwfImport::convert`), then hands the XML to `DocumentParser`. Saving uses
`FLAWriter` ([src/writer/fla_writer.h](../src/writer/fla_writer.h)) or `XFLFolderWriter`, both on
top of the XML that `xfl_writer`/`xfl_content` produce. `.phx` uses `phx::load`/`phx::save`
([src/phx/phx_document.h](../src/phx/phx_document.h)). `commands::load` and `commands::convert` in
[src/gui/command_line.cpp](../src/gui/command_line.cpp) are a compact example of the dispatch.
Importing other formats (PSD, SVG/PDF/AI, images, sound, video) is covered in
[importers.md](importers.md).

## 3. Editing: commands, undo and selection (`src/edit`)

### Command and CommandStack

Every change to a document that should be undoable is a `fla::Command`
([src/edit/command.h](../src/edit/command.h)):

```cpp
class Command {
    virtual void redo() = 0;            // applies the edit; push() calls it the first time
    virtual void undo() = 0;            // exact inverse of redo()
    virtual std::string name() const;   // "Move", for "Undo Move"
    virtual int mergeId() const;        // -1: never merges
    virtual bool mergeWith(const Command* other);
    virtual size_t memoryUse() const;   // bytes of snapshots, for the history budget
    virtual void retargetSelection(Selection* from, Selection* to);
};
```

`fla::CommandStack` ([src/edit/command_stack.h](../src/edit/command_stack.h)) is a linear history:

- **`push(cmd)` runs `cmd->redo()` and then stores it.** Callers build a command that describes
  the edit. They do not make the edit themselves and push afterwards. Pushing while undone steps
  exist drops the redo branch.
- **Merging.** If the previous command has the same non-negative `mergeId()` and `mergeWith`
  accepts, the new command is folded into it, so a drag becomes one undo step.
  `breakMergeChain()` (usually on mouse release) makes the next push start a new step.
- **Macros.** `beginMacro(name)` … `endMacro()` collect pushes into one `CompoundCommand`.
  Macros nest, and only the outermost one produces a history entry. An empty macro is discarded.
  `endMacro` also breaks the merge chain.
- `discardLast()` reverses and removes the last step as if it had never happened. It is used for
  edits that exist only to serve a selection.
- The clean state (`setClean`, `isClean`, `setDirty`) drives the "modified" mark. `setLimit` and
  `setByteBudget` (1 GiB by default) cap the history.
- `swapHistory` lets a document parked in a tab keep its own history. `setChangedCallback` is how
  the GUI hears about changes.

`fla::EditContext` ([src/edit/edit_context.h](../src/edit/edit_context.h)) pairs the open
`FLADocument` (borrowed) with its `CommandStack`. Drawing code takes `const` pointers to the
model. Tools reach the mutable document through the context.

### Command families

| File | Commands / functions |
|---|---|
| [property_command.h](../src/edit/property_command.h) | `SetPropertyCommand<T>`, a generic "set one field" command with optional merging. The Properties panel uses it for most fields. `ReplaceOwnedCommand<T>` swaps an owned object such as a fill. |
| [element_commands.h](../src/edit/element_commands.h) | `SetElementTransformCommand`, `AddElementCommand`, `RemoveElementCommand`, `GroupElementsCommand`, `UngroupCommand`, `MergeShapeCommand`, `SubtractShapeCommand`, `ConvertToSymbolCommand`, `BreakApartInstanceCommand`, and others |
| [timeline_edit.h](../src/edit/timeline_edit.h) | `namespace fla::timeline`: plain functions that edit a layer's frames in place (`insertFrames`, `insertKeyframe`, `removeFrames`, `setTween`, `setShapeTween`, `createMotionTween`, `pasteMotion`...), plus `EditFramesCommand`, which wraps any of them |
| [frame_commands.h](../src/edit/frame_commands.h), [frame_bake.h](../src/edit/frame_bake.h) | `InsertKeyframeCommand`. Frame-by-frame conversion, reverse frames, paste-inserting |
| [layer_commands.h](../src/edit/layer_commands.h) | `AddLayerCommand`, `RemoveLayerCommand`, `LayerEditCommand` (snapshot before/after) |
| [library_commands.h](../src/edit/library_commands.h) | Renaming, adding and deleting symbols, bitmaps and folders |
| [scene_commands.h](../src/edit/scene_commands.h) | Scene operations |

`EditFramesCommand` shows the pattern that recurs most often. It takes the affected layers, a name
and a lambda. On the first `redo()` it records each layer's frame state (`FrameState`: index,
duration, tween, motion XML among the extras), runs the lambda, and records the result. Later
undos and redos restore those records. Frames that the edit takes off a layer come back through the
`removed` vector, and the command owns them for as long as they might return.

### Selection and stale pointers

`fla::Selection` ([src/edit/selection.h](../src/edit/selection.h)) is an ordered list of borrowed
`DOMElement*` (an ordered list because pick order matters). It can hold whole elements (the stage)
or edges and paths (the document tree). It has a changed callback.

The model hands out raw pointers, so anything that remembers a model object has to tolerate undo
and redo taking that object out of the tree. The rule is to check against
`Document::reachable()`. `MainWindow::updateEditState` drops selected objects that are no longer
reachable, and `MainWindow::pruneToDocument` (run after undo and redo) clears the view's caches
and other holders of raw pointers. The JSFL session does the same with `Session::alive`.
`Command::retargetSelection` exists for histories that move between windows with their document.

### One edit path for the GUI and for scripts

Tools, panels, menus, JSFL, Python and the MCP server all change documents through the same
commands on the same `CommandStack`:

```
 Tool (gui/*_tool.cpp) ─┐
 Panel / menu action  ──┼──► CommandStack::push(Command) ──► Command::redo() mutates fla:: model
 JSFL / Python        ──┤          (beginMacro … endMacro
 MCP (script_api)     ──┘           = one undo step)
```

- Tools push directly. For example, `SelectionTool` wraps a move in `beginMacro("Move")` and
  calls `breakMergeChain()` when the gesture ends
  ([src/gui/selection_tool.cpp](../src/gui/selection_tool.cpp)).
- The Layers panel changes visibility, lock and outline directly because they are view state. It
  asks the window to carry out undoable layer edits through signals
  ([src/gui/layers_panel.h](../src/gui/layers_panel.h)).
- JSFL's `jsfl::Session::push` opens a macro named after the script on the first change to each
  document (`beginStep`) and pushes ordinary commands, so a whole script run is one undo step
  ([src/jsfl/session.cpp](../src/jsfl/session.cpp)). The object model's methods in
  `jsfl/model_*.cpp` construct `EditFramesCommand`, `AddLayerCommand`, `SetPropertyCommand` and the
  rest, the same commands the GUI uses.
- The application side of scripting is `jsfl::Host` ([src/jsfl/host.h](../src/jsfl/host.h)), which
  supplies documents, `history(doc)`, `selection(doc)` and dialogs. The window implements it in
  [src/gui/script_runner.cpp](../src/gui/script_runner.cpp), and the tests implement it in
  [tests/jsfl_test_host.h](../tests/jsfl_test_host.h).
- [src/gui/script_api.h](../src/gui/script_api.h) is the JSON operation set the MCP server calls.
  Each change is named `"AI: <operation>"`, and `begin_edit`/`end_edit` group a request into one
  step.

When you add an editing feature, write the command (or `timeline::` function) in `src/edit` and
test it in `phoenix_tests`. Then call it from the GUI and from the scripting model. Details:
[jsfl.md](jsfl.md), [python-and-mcp.md](python-and-mcp.md).

## 4. The animation model

### Keyframes and spans

A layer's `frames` vector holds **keyframes only**. Each `Frame` covers frames
`[index, index + duration)`. The `timeline::` operations keep the vector in index order with no
gaps or overlaps. To find what a layer shows at frame *n*, find the last `Frame` with
`index <= n` and check that *n* is within its duration (`timeline::spanAt`,
`PhoenixView::resolveLayerFrame`). Past a layer's last span the layer shows nothing.

### Tween kinds

| Kind | Stored as | Evaluated by |
|---|---|---|
| **Classic tween** | `Frame::tweenType == TweenType::Motion` on the start keyframe. The end is the next keyframe. Ease is `acceleration` (-100..100), `customEase` (a Bezier chain), or `easeMethod` (a named preset). Rotation is `motionTweenRotate`/`motionTweenRotateTimes`. | `fla::easeProgress` ([tween_ease.h](../src/edit/tween_ease.h)), then interpolation of each element against its counterpart on the next keyframe |
| **Shape tween** | `TweenType::Shape`, with a `MorphShape` on the start keyframe (segments with A/B points and style indexes). `morphSignature` detects a stale morph and is not saved. | `morphedShape`, `blendFills`, `blendStrokes` ([shape_tween.h](../src/edit/shape_tween.h)). Morphs are built from the keyframes when a tween is made. See [geometry.md](geometry.md). |
| **Motion (object) tween** | `tweenType == None`, `otherTweenType == "motion object"`, and the `AnimationCore` document as a `<motionObjectXML…>` string in `Frame::extraChildren`. `timeline::motionOf(frame)` finds it. | `fla::MotionObject` ([motion_object.h](../src/edit/motion_object.h)) parses it into property curves and `MotionEase` time maps. `applyMotion` moves the stored transform. `rescale`, `truncate` and `addKeys` edit the XML text directly. |
| **IK pose** | `otherTweenType == "IK pose"` | [ik_pose.h](../src/edit/ik_pose.h), [parser/ik_tree.h](../src/parser/ik_tree.h) |

Motion tweens stay as XML in the model. Phoenix edits that XML with the text functions in
`motion_object.h`, and `EditFramesCommand` snapshots `extraChildren` so these edits undo. The
`timeline::fitMotionSpans` and `timeline::scaleSpans` functions keep motion keyframes in proportion
when spans change length.

### Graphic symbol loops

A graphic `SymbolInstance` plays its symbol's timeline in step with its parent, under `loopType`
(`LoopType`: `Loop`, `PlayOnce`, `SingleFrame`, `PingPong`, `LoopReverse`, `PlayOnceReverse`),
`firstFrame` and `lastFrame`. The single source of truth is the inline `fla::graphicFrame`
([src/edit/graphic_loop.h](../src/edit/graphic_loop.h)). The stage
(`PhoenixView::symbolFrame`), the baking code, HTML5 export and the behaviors all call it. Movie
clips play on their own clock, which is the runtime's job (section 5).

### Where frames are evaluated

There are two evaluators, and they have to agree:

1. **The stage, drawing in place.** `PhoenixView::resolveLayerFrame` picks the span for the frame
   shown, applies the loop type of the timeline being drawn, and computes `tweenProgress` with
   `easeProgress` and the next keyframe. `drawFrame(frame, nextTweenFrame, tweenProgress)` →
   `drawElement(element, tweenElement, progress)` → `drawShape(shape, tweenShape, ...)` then
   interpolates while it paints. A shape tween calls `morphedShape`, and a motion tween goes
   through `PhoenixView::motionOf`.
2. **Baking to copies.** `fla::frames::elementsAt(span, next, framesIn, frameRate, parent)`
   ([src/edit/frame_bake.h](../src/edit/frame_bake.h)) returns new `Element`s that show what a span
   shows *n* frames in. Edits that need concrete in-betweens use it: Convert to Frame-by-Frame,
   inserting a keyframe inside a tween (`timeline_edit.cpp`), HTML5 export, controllers and
   function curves. The caller owns the result.

[tools/consistency_check.py](../tools/consistency_check.py) renders the same frames through
`--export-image`, `--export-movie` and `play --image` and compares them, to catch drift between
these paths.

## 5. The GUI (`src/gui`)

### Main objects

```
MainWindow (QMainWindow)
├── EditContext _editContext   document + CommandStack
├── Selection _selection
├── Player* _player            playhead: currentFrame, playing (signals)
├── PhoenixView* _phoenixView  the stage (QWidget)
├── TimelineView* _timelineView  + LayersPanel, TimelinesPanel
├── tools (SelectionTool, FreeTransformTool, PrimitiveTool, ... as members)
├── panels in QDockWidgets (Properties, Library, Color, Swatches, Motion Editor,
│   X-sheet, Behaviors, Controllers, Output, ...)
├── ScriptRunner* _scripts     Commands menu, Output panel, JSFL/Python runs
├── McpServer*                 AI-assistant connection
└── _tabs: DocumentTab         parked documents, each with its own CommandStack,
                               view state, and AutoSave
```

- **`MainWindow`** ([main_window.h](../src/gui/main_window.h)) owns the documents and wires
  everything together. Its implementation is split across `main_window.cpp` and topic files
  (`main_window_view.cpp`, `main_window_share.cpp`, `main_window_inbetween.cpp`, ...).
  `updateEditState()` runs on every history change. It prunes the selection, refreshes the panels,
  sets the Undo/Redo text, and updates the title.
- **The Artist layout** (`main_window_artist.cpp`, View > Mode > Artist) is a mode of the window
  rather than a workspace: it saves the panels as they are, hides every dock and bar, and shows a
  tool strip of its own whose last two buttons open the timeline (a frame row over the
  `TimelineView`, borrowed from its dock in its phone view) and Properties as `StageDrawers`
  ([stage_drawers.h](../src/gui/stage_drawers.h), shared with the phone layout: frameless tool
  windows over the stage that a press elsewhere closes), then puts the saved layout back. Its edits (New Frame after the current
  span, holds, named eases, and keying a frame for a move with a classic tween on the span before)
  are Qt-free in [artist_edit.h](../src/edit/artist_edit.h). A press on the stage with the arrow or
  transform tool at a frame inside a span keys it first, inside a macro; a release that moved
  nothing discards the macro (`CommandStack::discardLast`) so a click leaves no keyframe behind.
  Artist and Phone modes also set `PhoenixView::setForgivingStage`: a left press a tool declines goes
  to the arrow's marquee rather than panning, and a drawing tool (`Tool::drawsOnStage`) pressed past
  the end of its layer gets a blank keyframe at the shown frame before it draws.
- **Document tabs.** Only one document is "shown" at a time (`_flaDocument`/`_editContext`). The
  others sit in `DocumentTab`s, and their histories are swapped in and out with
  `CommandStack::swapHistory`.
- **`Player`** ([player.h](../src/gui/player.h)) is only the playhead: `currentFrame` and
  `playing`, with change signals. It is not a movie runtime.
- **Panels.** Each panel is a widget in `src/gui` (`properties_panel`, `library_panel`,
  `layers_panel`, `timeline_view`, `color_panel`, `motion_editor`, `xsheet_panel`, `document_view`
  (the document tree)...). Panels that edit the document push commands onto the edit context's
  stack.

### PhoenixView: the stage

[src/gui/phoenix_view.h](../src/gui/phoenix_view.h) is the stage widget and the renderer. It
handles:

- **Coordinates.** `documentToWidget()` is the single mapping used for painting, hit testing and
  zoom anchoring. `scopeToDocument()` adds the transforms of the symbols being edited.
- **Edit scopes.** `_scope` is a stack of `EditScope`. Each level is a symbol entered in place
  (`enterSymbol`), a symbol edited on its own from the library (`editSymbol`, with no `instance`),
  or a group edited in place (`enterGroup`, which takes the group apart and puts it back as one
  undo step). `scopeTimeline()` is the timeline the tools work on. `exitScope`/`exitToDepth` leave
  scopes.
- **Hit testing.** `hitTest(documentPos, tolerance)` returns a `HitResult` with the element, its
  frame, the part hit (fill, stroke, anchor) and `elementToDocument`.
- **Tools.** `Tool` ([src/gui/tool.h](../src/gui/tool.h)) is a plain interface with mouse and key
  handlers that return `bool` (false lets the view fall back, for example to panning), plus
  `paintOverlay` (in document coordinates) and `deactivate`. One tool is active
  (`setActiveTool`). A modifier tool can take over temporarily (`setModifierTool`), for example
  the arrow while Ctrl is held. Subclasses live in `*_tool.h` files: `selection_tool`,
  `subselection_tool`, `free_transform_tool`, `pen_tool`, `pencil_tool`, `brush_tools`,
  `paint_tools`, `primitive_tool`, `text_tool`, `bone_tool`, `lasso_tool`, `three_d_tool`,
  `width_tool`, `camera_tool`, and `script_tool` (JSFL tools).
- **Rendering.** `paintEvent` → `drawDocument`/`drawStageContents` → `drawTimeline` →
  `drawLayer` → `drawFrame` → `drawElement` → `drawShape`, all with `QPainter`. Masks, layer
  effects, adjustment layers, blurs, onion skin, pegs, guides, rulers and safe zones each have their
  own `draw…` member. `renderFrame(frame, size, transparent[, area])` draws a frame the way a
  published movie shows it (guide layers hidden, locked layers shown, movie clips playing). Every
  export and the headless commands use it.
- **Caches.** Shapes are turned into `QPainterPath`s once and kept in `_pathCache`
  (`QMap<const fla::Shape*, PathCacheList>`, see `buildShapePaths`/`shapePaths`). There are also
  bounds caches (`_boundsCache`, `_symbolBoundsCache`, `_artBoundsCache`), a bitmap `QCache`,
  filter and blur caches, and static images reused while a gesture moves part of the stage
  (`StaticKey`). Caches are keyed by model pointers and do not watch the model. Code that changes
  what is drawn calls `clearCaches()` (or `forgetShape`, or the bounds-only variant), and
  `MainWindow::pruneToDocument` clears them after undo and redo. If you add an edit and the stage
  does not repaint correctly, a missing cache invalidation is the usual cause.

### Test Movie and the runtime

`TestMovieWindow` ([test_movie_window.h](../src/gui/test_movie_window.h)) makes a
`fla::play::MovieRuntime` ([src/play/movie_runtime.h](../src/play/movie_runtime.h), Qt-free) for
the document's scenes. It drives it with a `QTimer` at the frame rate and shows it in its own
`PhoenixView` through `setMovie()`. While a movie is set, the view draws clips in the state their
scripts leave them and sends mouse input to the movie. Display objects created by scripts are
drawn by `DisplayPainter` ([display_painter.h](../src/gui/display_painter.h)), which is also the
display engine's `Backend`. ActionScript 3 is compiled by libas3, ActionScript 1/2 runs in
`avm1_player`, and HTML5 Canvas JavaScript runs on QuickJS. See [actionscript.md](actionscript.md).

### Threads

The model is single-threaded. It belongs to the GUI thread, and nothing locks it. The places that
use other threads:

- `fla::parallelFor`/`parallelRows`/`parallelPixels` ([src/util/parallel.h](../src/util/parallel.h))
  run a persistent pool for data-parallel loops over disjoint ranges (image work and similar).
  `PHOENIX_THREADS=1` turns it off.
- `phx::parallelFor` ([src/phx/parallel.h](../src/phx/parallel.h)) splits `.phx` reading and
  writing across threads.
- `AutoSave` takes a `phx::snapshot` on the GUI thread and writes it on a `std::thread`
  (`phx::write` does not touch the document). It reports back with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- The MCP server reads its connections on threads and posts each request to the GUI thread the same
  way.
- The Python engine rejects calls into the object model from any thread other than the one the
  script runs on (`requireRunThread` in
  [src/python/python_engine.cpp](../src/python/python_engine.cpp)).

New work that touches `fla::` objects belongs on the GUI thread, or on a snapshot taken there.

## 6. Command-line modes and headless operation

`main()` ([src/main.cpp](../src/main.cpp)) checks the first argument before it builds the normal
window:

| Invocation | Handled in | What it does |
|---|---|---|
| `--phx-dump <file.phx>` | `main.cpp` → `phx::dump` | Prints a `.phx` file's structure, with no Qt app |
| `--convert <in> <out>` | `commands::convert` | Converts between `.phx`, `.fla` and an XFL folder by output extension. With `--for-other-apps` it bakes Phoenix-only features (`exportForOtherAppsFromCommandLine`). |
| `render`, `run-script`, `batch`, `doctor`, `diff`, `play`, `shots`, `links`, `benchmark`, `display-check`, `capture-window`, `capture-dialog`, `shortcuts`, `lessons`, `help` | `commands::isCommand` / `commands::run` in [command_line.cpp](../src/gui/command_line.cpp) | Pipeline commands. `kHelp` in that file documents every option. |
| `--export-image`, `--export-movie [--per-layer]`, `--roundtrip`, `--view` | `main.cpp` → `MainWindow::*FromCommandLine`, `renderSamples` | Older single-purpose modes |
| `--mcp [--port N]` | `main.cpp`, `McpServer` | Relays to a running Phoenix, or starts one for an AI assistant |
| `--tutorial <lesson>`, `<file>` | `main.cpp` | Normal start, opening a lesson or a file |

Most commands build `MainWindow window(QString(), nullptr, false)`. With `startup == false`
nothing opens by itself and no update check runs. The window is never shown, and its
`PhoenixView` renders with `renderFrame`. So the command line uses exactly the same renderer as the
app. `run-script` calls `ScriptRunner::runHeadless`, where alerts print, `confirm` answers yes,
and `prompt` returns its default.

Headless details from `main.cpp`:

- On Linux, a recognized command with no `DISPLAY`/`WAYLAND_DISPLAY` sets
  `QT_QPA_PLATFORM=offscreen` automatically. Elsewhere, set it yourself to keep windows off screen.
- On Windows, the export and command modes point `QT_QPA_FONTDIR` at `%WINDIR%\Fonts`, because the
  offscreen platform otherwise finds no fonts.
- `PHOENIX_SETTINGS_DIR` puts settings in INI files in that folder (used by tests and by
  `capture-window`, which gets a throwaway folder so the user's layout is untouched).
- `LC_NUMERIC` is forced to `"C"` after `QApplication` starts, so numbers in files always use a
  dot.

`capture-window` and `capture-dialog` draw the window, a panel, a menu or a dialog to a PNG without
showing it. They produce the user guide's screenshots. As [building.md](building.md) notes, run
them without `offscreen`, which loses the theme. The user-facing reference is
[website/command-line.html](../website/command-line.html).

## 7. Testing

### The framework

`phoenix_tests` uses a small framework of its own, not GoogleTest or Qt Test:

```cpp
// An illustrative test, not one from the suite.
#include "test_util.h"

TEST(insertKeyframeSplitsSpan)
{
    fla::Layer layer(nullptr);
    // ...
    CHECK(layer.frames.size() == 2);
    CHECK_NEAR(value, 0.5);   // tolerance 1e-9
}
```

[tests/test_util.h](../tests/test_util.h) defines `TEST`, `CHECK` and `CHECK_NEAR`. A failed
check prints file and line and counts as a failure, but the test keeps running.
[tests/test_main.cpp](../tests/test_main.cpp) runs every registered test, or only those whose name
contains the first argument:

```sh
ctest --test-dir build -C Release -R phoenix_tests --output-on-failure
build/Release/phoenix_tests              # everything
build/Release/phoenix_tests Motion       # names containing "Motion"
```

The tests link `phoenix_core` and never Qt, which is one more reason the core must stay Qt-free.
Test files are named after the unit under test (`test_timeline_edit.cpp`,
`test_command_stack.cpp`, `test_jsfl_document.cpp`...). JSFL tests use the `Host` in
[tests/jsfl_test_host.h](../tests/jsfl_test_host.h) and data under `tests/data`
(`PHOENIX_TEST_DATA`). Play/publish tests are built only when libas3 is in the build. Python tests
skip themselves when no Python is installed.

### Corpus tests

Some tests run over real files when an environment variable points at them, and print `skipped`
otherwise:

| Variable | Used by |
|---|---|
| `PHOENIX_FLA_CORPUS` | `test_fla_writer`, `test_phx`, `test_shape_geometry`, `test_binary_fla`, `test_ik_pose`, `test_audio_decode`: open, save and round-trip each FLA in the folder |
| `PHOENIX_JSFL_CORPUS` | Running a folder of JSFL scripts |
| `PHOENIX_BINARY_FLA_LIST` (+ `PHOENIX_BINARY_FLA_REPORT`) | A sweep of old binary FLAs |

Round-trip tests write under `std::filesystem::temp_directory_path()`. When several test runs go
at once, give each its own `TEMP`/`TMP` (or `TMPDIR`), or they collide.

### Checks with the app (tools/)

These drive the built `Phoenix` executable headless and compare pixels:

| Tool | Checks |
|---|---|
| [tools/roundtrip_check.py](../tools/roundtrip_check.py) | Runs `Phoenix --roundtrip` on each file in a folder (save as `.fla` and `.phx`, reopen, compare sample frames). Writes `report.json`. Exits 1 on any difference. |
| [tools/consistency_check.py](../tools/consistency_check.py) | The same frames through `--export-image`, `--export-movie` and `play --image` must match (needs Pillow) |
| [tools/animate_check.py](../tools/animate_check.py) + `animate_check.jsfl` | Whether the original authoring tool opens Phoenix's saved FLAs and draws them as it drew the originals (prepare, run the JSFL there, compare) |
| `tools/animate_export_frames.jsfl` | Reference frames exported from that tool, for comparison with Phoenix's renders |

`Phoenix diff`, `Phoenix doctor` and `Phoenix benchmark` (section 6) are also useful while you
develop. The other scripts in `tools/` handle the website (`build_changelog.py`,
`sync_docs_version.py`, `deploy_site.py`), templates and lesson thumbnails, and releases
(`release/`, `qt/`, `ffmpeg/`).

## 8. Conventions

**Comments.** Headers document their API with `///` comments written as prose about behavior, and
refer to parameters with `\a name`. Comments say what something does and why, often in terms of
what the user sees ("Insert Frame (F5): the span covering \a index grows…"). Core headers say
"Free of Qt." where relevant. Implementation comments use `//`. Keep comments true when you change
code, and fix the matching note in `docs/` in the same commit ([README.md](README.md)).

**Naming.**

- Files: `snake_case.cpp/.h`, one main class or topic per file. Tests are `tests/test_<topic>.cpp`.
- Types: `PascalCase`. Functions and methods: `camelCase`. `enum class` with `PascalCase` values.
- Public data members in model classes have no prefix (`frame->duration`). Private members use a
  leading underscore (`_commandStack`, `_scope`).
- Model fields follow the XFL attribute names where one exists (`libraryItemName`,
  `motionTweenRotate`, `acceleration`). Fields holding unmodeled content use the prefixes `extra…` and
  `other…`. Flags that make a save write back what the file had are named `keep…`
  (`keepMatrix`, `keepColor`).

**Language and dependencies.** C++17. MSVC builds with `/utf-8` and `/Zc:__cplusplus`, so source
files may contain UTF-8 characters such as `°` and `×` in strings. Core code uses the standard
library, tinyxml2 and zlib, never Qt. GUI code uses Qt freely.

**Behavior rules worth knowing.**

- Never edit the model outside a `Command` if the change should be undoable. View state (layer
  visibility and lock, the current frame) is the exception.
- Anything that deletes or detaches model objects must clear the selection of them first
  ([selection.h](../src/edit/selection.h)). Anything that holds model pointers across edits must
  re-check `Document::reachable()`.
- Frame numbers in the model are zero-based. The UI and the scripting addresses in `script_api.h`
  count from 1.
- Numbers written to files are formatted in the `"C"` locale.
- Readers never throw to their callers (`FLAParser::parse`, `phx::load` return null plus an error
  string). Writers return `bool` plus an error string.

**Spelling and wording.** Use US spelling in code, comments and docs. In source and docs, describe
Phoenix's features in Phoenix's own terms. Name other products only for file formats (FLA, XFL, SWF,
JSFL) or for compatibility with what those files and scripts expect.

**Changelog.** When a user-facing change lands, add a short bullet to
[CHANGELOG.md](../CHANGELOG.md), which lists releases newest first. [tools/build_changelog.py](../tools/build_changelog.py) turns it
into the website's changelog page.
