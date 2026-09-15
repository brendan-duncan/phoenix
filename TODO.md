# Phoenix Authoring Tools — Roadmap

Goal: add Adobe Animate-style authoring tools (pen, subselection, primitives,
pencil/brush) to what is currently a read-only FLA parser and renderer.

Status legend: `[ ]` todo · `[~]` in progress · `[x]` done

---

## 0. Groundwork

- [x] Fix `Point::transform` using `m12` where it needed `m21` (disagreed with
      `Point::transformed`; latent under uniform scale, wrong under rotation/skew)
- [x] Fix `Rect::transform` aliasing bug (wrote `topLeft.x`, then read the new
      value when computing `topLeft.y`) and make it map all four corners so the
      result is a real AABB under rotation
- [x] Qt-free test harness (`tests/`), buildable standalone without Qt
- [ ] Extract a `phoenix_core` CMake static library (data + parser + edit, no Qt)
      so the app and the tests share one source list
- [ ] Restore a buildable Qt environment (see "Build environment" below)

## 1. Editing infrastructure

- [x] Undo/redo command stack (`src/edit/`), Qt-free — 26 tests
  - [x] `Command` base: redo / undo / name / merge
  - [x] `CommandStack`: push, undo, redo, clean-state tracking, observers
  - [x] `CompoundCommand` + begin/end macro for multi-step edits, nestable
  - [x] Command merging so a drag collapses into one undo step, with
        `breakMergeChain()` and macro boundaries ending the chain
- [x] `EditContext` (`src/edit/edit_context.h`): the mutable document plus its
      undo history and modified state. Qt-free
  - Note: an earlier plan here was to strip `const` from the document throughout
    the views. That turned out to be the wrong call -- of the 270 `const fla::`
    uses in `src/gui`, nearly all are locals in the draw path, which *should*
    stay const because drawing does not mutate. Only the document handle needs
    to be mutable, and tools reach it through `EditContext` instead.
- [x] Document "modified" state: `*` marker in the title, prompt before
      replacing or closing a modified document (File > Exit routed through
      `closeEvent` so it prompts too)
- [x] Wire Edit menu: Undo / Redo, enabled state and live action text
      ("Undo Draw Rectangle"), Ctrl+Z and Ctrl+Y / Ctrl+Shift+Z
- [ ] Mutation API on the data model (`Shape`, `Edge`, `Path`, `Frame`, `Layer`)
      and the concrete commands that drive it -- deferred until step 3/4, when
      the first tool defines what the commands actually need
- [ ] Upgrade the unsaved-changes prompt to offer Save now that step 2 has
      landed (it still offers only Discard / Cancel)

## 2. XFL serialization (write)

Done. Verified against a 74-document corpus: every one parses, serializes, and
re-parses to an identical tree.

- [x] XML writer for `DOMDocument.xml` and `LIBRARY/*.xml`
      (`src/writer/xfl_writer.*`, tinyxml2 `XMLPrinter`). Attributes matching the
      reader's default are omitted, the way Animate writes them
- [x] Edge-data string emitter (`src/writer/edge_writer.*`), the inverse of
      `path_parser.cpp`: `!` move, `|` line, `[` quad, `(...)` cubic,
      `S` / `FS` / `LS` style selects, decimal twips for whole values and
      `#` hex fixed point for fractions
  - A style select has to follow the move that opened its path, not lead it:
      the reader attaches it to the current path and silently drops one that
      arrives before any path exists
  - Unmodified edges are re-emitted from `Edge::data`, the text they were read
      with, so they round-trip exactly. The generator is for edited geometry
- [x] Uncompressed XFL folder output (`src/writer/xfl_folder_writer.*`),
      including the `.xfl` project marker
- [x] Zip writer for `.fla` (`src/util/zip_writer.*`, deflate via the vendored
      zlib, with a store fallback; no data descriptors or ZIP64). Output verified
      against an independent ZIP reader
- [x] `src/writer/xfl_content.*` builds the file list once, so the folder and
      `.fla` targets are the same content in different containers
- [x] Round-trip tests: parse → serialize → re-parse → compare trees, both
      hand-written cases and a real corpus
- [x] File > Save / Save As in the GUI, writing `.fla` or an XFL folder

Two bugs this turned up, both fixed:

- Flash uses CR for line breaks inside text runs, and XML normalises a literal
  CR to LF on read. Those now go out as `&#13;`. Before the fix, 12 of the 74
  corpus documents came back with altered text and layer names
- `parseActionScript` read `element->GetText()` instead of
  `childElement->GetText()`, so frame ActionScript never actually loaded

### Not written back yet

The writer records anything it cannot represent rather than dropping it in
silence (`XFLWriter::unsupported()`), and the GUI warns before a save that would
lose content. Across the corpus that is:

- [ ] document scripts (`<scripts>`)
- [ ] publish history
- [ ] printer settings
- [ ] swatch lists

These are all document metadata rather than geometry, and the reader keeps only
a partial model of each, so writing them faithfully means extending the reader
first.

### Also outstanding

- [ ] Verify Animate itself opens what we write. Round-tripping through our own
      reader proves self-consistency, not compatibility. The cubic `(...)` form
      is the likeliest sticking point: Flash stores a `q`/`p` quadratic
      approximation inside the parens that our reader skips and our writer omits
- [ ] Save to a temporary file and move it into place, so an interrupted save
      cannot destroy the original

## 3. Tool framework + selection

- [ ] Stage hit-testing: fill vs. edge vs. anchor, zoom-aware tolerance
- [ ] `Tool` base class, active-tool routing in `PhoenixView` mouse events
      (currently pan-only), per-tool cursors
- [ ] Overlay/handle rendering layer (extend `drawOverlayPoints`)
- [ ] Selection model: click, shift-click, marquee; selection-aware Edit menu
- [ ] Free transform: move, scale, rotate, skew of whole elements
- [ ] Toolbar UI + keyboard shortcuts
- [ ] Snapping: grid, guides, object snapping

## 4. Primitive tools

- [ ] Rectangle (reuse parsed `RectanglePrimitive`)
- [ ] Oval (reuse parsed `OvalPrimitive`)
- [ ] Line
- [ ] PolyStar
- [ ] Fill/stroke property panel

## 5. Pen + subselection (object drawing mode)

- [ ] Authoring-side anchor model (tangent linked/broken, corner vs. smooth) that
      compiles down to `PathSegment` — the current model stores only on-curve
      points plus controls, which cannot express handle state
- [ ] Pen: click for corner, drag for handles, alt-drag to break tangents,
      close on first anchor, resume from an endpoint
- [ ] Subselection: display and drag anchors + handles
- [ ] Add / delete / convert anchor point (de Casteljau splitting)
- [ ] Pencil with curve fitting (Schneider) + smoothing/straightening modes

## 6. Merge drawing model — the hard part

Animate's default mode maintains a planar map: every edge carries a left and a
right fill (that is what `fillStyle0` / `fillStyle1` already are). Drawing across
a fill splits both at intersections and rebuilds faces. Not started; decide
whether it is worth it only after 1-5 land.

- [ ] Evaluate boolean kernel: Skia `SkPathOps` (BSD, handles cubics directly)
      vs. Clipper2 (integer, polygon-only, would flatten to twip resolution)
- [ ] Planar map: curve-curve intersection, splitting, face extraction
- [ ] Per-half-edge fill attribution (`fillStyle0` / `fillStyle1`)
- [ ] Snap intersections to the twip grid (1/20 px) — Flash geometry is already
      twip-quantized, which is what makes an exact integer map tractable
- [ ] Paint bucket (flood fill over the map), ink bottle, eraser
- [ ] Selection tool edge-dragging (fill follows the edge)
- [ ] Stroke-to-outline conversion (needed by the brush tool)
- [ ] Consider rendering *from* the planar map, replacing the fill-stitching
      heuristic in `drawShape` (the `directedPaths.size() <= 2` special case and
      twip-rounded point matching). Likely fixes rendering bugs rather than adding risk

## Explicitly out of scope

Bones/IK, motion editor, asset warp, variable-width strokes, art/pattern brushes,
pressure/tilt input, text authoring. Each is a project in itself.

---

## Build environment

Qt 6.11.2 is installed at `C:/Qt/6.11.2/msvc2022_64`, with Visual Studio 18 and
MSVC 14.51. The hard-coded Qt path in `CMakeLists.txt` is gone: the build now
prefers `-DQt6_ROOT`, then `-DCMAKE_PREFIX_PATH`, then the `Qt6_ROOT` / `QTDIR`
environment variables, then the newest `C:/Qt/6.*/msvc*_64` it can find, and
fails with an actionable message if none of those turn up Qt.

Use the **Visual Studio generator**, not Ninja:

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

- [ ] The Ninja generator fails in the `libas3` dependency, which copies
      `as3_parser.pdb` after archiving; MSVC does not emit that PDB for a static
      library under Ninja, so the copy errors out. Phoenix's own code is fine.
      Fix belongs upstream in libas3 (guard the copy, or set
      `COMPILE_PDB_NAME` / `COMPILE_PDB_OUTPUT_DIRECTORY` on the target).
- The checked-in `build/` directory is stale: its cache still points at Visual
  Studio 2022 and Qt 6.10.2, neither of which is installed. Delete it and
  reconfigure with the command above.

### Running the tests without Qt

```
cmake -S tests -B build-tests -G Ninja
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```
