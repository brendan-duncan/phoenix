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
- [x] First concrete mutation command, `SetElementTransformCommand`, driven by
      the transform tools (see step 3)
- [ ] Mutation API for the rest of the model (`Shape`, `Edge`, `Path`, `Frame`,
      `Layer`) and its commands -- still waiting on the tools that need it
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

Selecting things on the stage works. Transforming them does not yet.

- [x] Stage hit-testing (`PhoenixView::hitTest`): fill vs. stroke vs. anchor,
      with a tolerance in document units derived from a few screen pixels so
      picking feels the same at every zoom
  - Mirrors the draw traversal, so what is pickable is exactly what is visible:
    hidden and locked layers are skipped, guide and folder layers are not
    pickable, and a symbol instance reports itself rather than the artwork
    inside it
  - `drawShape` was split into a painter-free `buildShapePaths` plus a cached
    `shapePaths`, so hit-testing tests the same geometry the renderer draws
    instead of a second, drifting copy
  - `resolveLayerFrame` was pulled out of `drawLayer` for the same reason: one
    answer to "which frame is showing", used by both
- [x] `Tool` base class (`src/gui/tool.h`) and active-tool routing in
      `PhoenixView`, with per-tool cursors. A tool that declines an event lets
      the view fall back to panning, and middle-drag or alt-drag always pans
- [x] Overlay layer (`PhoenixView::drawToolOverlay`): selection bounds with
      corner grips, drawn at screen resolution so handles stay crisp under
      supersampling, and sized in screen pixels so they do not grow with zoom
- [x] Selection model (`src/edit/selection.h`, Qt-free, 10 tests): click,
      shift-click to toggle, marquee, click-empty to clear, Escape to clear.
      Holds `DOMElement` so the stage and the document tree can share one model
- [x] Selection-aware Edit menu: Select All, Deselect All, plus a count in the
      status bar
- [x] Toolbar with the Selection tool (`V`), as an exclusive action group ready
      for the tools that follow
- [x] Free transform: move, scale, rotate, skew of whole elements
  - `SetElementTransformCommand` (`src/edit/element_commands.h`, 7 tests) is the
    one edit underneath all four gestures -- only the matrix and the name differ
  - A drag applies its transform live and pushes a single command on release, so
    the gesture is one undo step. Merging exists for the other case, repeated
    arrow-key nudges, which should also collapse
  - Every frame of a drag recomputes from the transform the element had when the
    drag began, so a long drag does not accumulate rounding error
  - `SelectionTool` moves what is under the cursor, `FreeTransformTool` (`Q`)
    scales, rotates and skews -- the same split Animate makes between its arrow
    and free transform tools
  - The free transform box is carried by each gesture rather than recomputed as
    an axis-aligned box, so a rotated object keeps a rotated box with its handles
    on the corners
  - Shift constrains (uniform scale, 45 degree rotation steps), Escape abandons a
    gesture and puts everything back
  - Dragging only changes where things sit, so `invalidateBounds()` drops the
    bounds cache while the expensive path cache survives the drag
- [x] Snapping to the grid and to other objects (`src/edit/snapping.h`, Qt-free,
      14 tests), with View menu toggles and grid rendering
  - A gesture hands over the coordinates that are moving -- for a box, its left,
    centre and right -- and gets back one adjustment for the whole axis, so a
    shape shifts to meet a line rather than distorting because one edge snapped
  - Lining up with another object beats the grid at equal distance; the grid only
    wins when it is strictly closer
  - Candidates are the edges and centres of everything else on stage, plus the
    stage edges and centre. The dragged objects are excluded, since something
    snapped to its own edge would never move
  - Candidates are gathered once at the start of a gesture, not per mouse move
  - Rotation does not snap the cursor: it is an angle, not a position, so
    snapping would fight the shift-key angle steps
  - Grid spacing, `objectsSnapTo` and `snapAlignBorderSpacing` were declared on
    `Document` but never read, so the grid silently always used the built-in
    default of 18. They are now parsed and written
- [ ] Ruler guides. Animate snaps to these too, but nothing models them yet --
      no rulers, no guide objects in the document. Needs that feature first, and
      is not the same thing as the guide *layers* the parser already knows about

Two things to tidy:

- [ ] The stage selection and the document tree still track separately: the tree
      drives `PhoenixView::_selectedElement` for inspecting edges and paths
      inside a shape, while the stage drives `Selection`. They should be the one
      model, with the tree simply selecting finer-grained nodes
- [ ] A marquee tests element bounds, not geometry, so it catches a shape whose
      bounding box overlaps even when no part of the shape does

Also fixed along the way: `screenToScene` and `sceneToScreen` were dead code and
wrong -- they left out the centring that `paintEvent` applies, so any hit-testing
built on them would have been off by half the unused widget space. They are now
one `documentToWidget` transform shared by painting, picking and the zoom anchor.

## 4. Primitive tools

Done. Drawn shapes were saved to a .fla and round-tripped back with no
differences, so what the tools build is valid XFL, not just something that
happens to render.

- [x] `AddElementCommand` / `RemoveElementCommand` (`src/edit/element_commands.h`,
      8 tests). Ownership moves with the element: a `Frame` deletes what it holds,
      so while the element is out of the frame the command owns it, and dropping
      the history then frees it. Z-order is restored on redo, not appended
  - The commands take an optional `Selection` and drop the element from it
    whenever it leaves the document, so undo cannot leave the selection pointing
    at freed memory
- [x] Rectangle (`R`) and Oval (`O`), built as the `DOMRectangleObject` and
      `DOMOvalObject` primitives the format already has
- [x] Line (`N`) and PolyStar (`P`), built as ordinary shapes with an edge, since
      the format has no primitive for either
  - A polystar's outline closes by repeating its first point, which is what the
    fill reconstruction expects
  - Drawing a line with strokes switched off falls back to a hairline rather than
    creating an invisible object
- [x] All four share one `PrimitiveTool`: same press-drag-release gesture,
      differing only in what they build from the two corners
- [x] Shift constrains -- square, circle, and 45 degree line steps -- and the
      drag snaps to the grid and to other objects like any other gesture
- [x] Fill/stroke properties panel: colour swatches with alpha, stroke weight,
      and the polystar's sides and star mode. One `DrawingStyle`
      (`src/edit/drawing_style.h`) lives for the session, so the next shape picks
      up the last one's settings

### Rough edges

- [ ] New objects go into the current frame of the first drawable layer, or one
      the file marks selected. There is no way to choose a layer yet, so
      `PhoenixView::activeFrame()` guesses. Needs layer selection in the timeline
- [ ] Drawing on a frame that has no keyframe should create one. Right now the
      object joins whatever frame is showing
- [ ] Star mode is implemented but only polygon mode was checked on screen
- [ ] Rectangle corner radius and the oval's start/end angle and inner radius are
      in the data model and get written, but no tool sets them

## 5. Pen + subselection (object drawing mode)

Done. Bezier paths can be drawn with the pen or the pencil, and edited anchor by
anchor.

- [x] Authoring-side anchor model (`src/edit/editable_path.h`, 12 tests).
      `PathSegment` records only what a renderer needs: it cannot say whether an
      anchor's handles are linked, and it splits one conceptual anchor across two
      segments. `EditablePath` holds anchors with in and out handles and a smooth
      flag, and compiles down to segments when an edit finishes
  - Quadratics are raised to the cubic that draws the same curve, so one anchor
    model covers both
  - A path whose last point repeats its first is recognised as closed, however it
    was recorded, rather than ending up with two anchors on top of each other
  - A smooth anchor keeps one straight tangent through it: moving one handle
    swings the other to stay opposite, at its own length
- [x] Pen tool (`P`): click for a corner point, drag for a smooth one, alt-drag
      to break the tangent, click the first point to close, Enter to finish open,
      Escape to abandon. Nothing reaches the document until the path is finished,
      so an abandoned path leaves no trace and a finished one is one undo step
  - A closed path gets a fill, an open one is stroke only, since only a closed
    outline encloses an area
- [x] Subselection tool (`A`): shows every anchor and handle on the selected
      shape and drags them. Smooth points draw round, corners square. Handles are
      checked before anchors when picking, or a handle pulled back over its
      anchor could never be grabbed
- [x] `SetEdgeGeometryCommand`: a drag edits live and pushes one command on
      release. It stores the before and after anchors rather than patching a
      segment, because an anchor spans two segments and its handles live on both
      sides of the join
- [x] Tools that draw their own handles suppress the selection bounding box, so
      it does not sit on top of what the user is trying to grab
- [x] Add / delete / convert anchor point (9 tests)
  - Alt-click the outline inserts an anchor. De Casteljau subdivision means the
    curve is untouched by the insertion: the two halves describe exactly what
    the one segment did
  - Delete removes the anchor last clicked, refusing to go below two, which is
    the least that still draws something
  - Double-clicking an anchor converts it. A smooth point loses its handles; a
    corner grows them along the line through its neighbours, a third of the way
    to the closer one
  - `EditablePath::closestPoint` sweeps coarsely then refines by bisection, so a
    click lands where the user aimed rather than on the nearest sample
- [x] Pencil (`D`) with Schneider curve fitting (`src/edit/curve_fit.h`, 10
      tests) and smooth / straighten / ink modes, with tolerance in the
      properties panel
  - Fit one cubic to the whole stroke by least squares, improve the
    parameterisation with Newton-Raphson, and split at the worst point only if
    it is still outside tolerance. A sine drawn with sixty cursor points comes
    back as a handful of anchors
  - Straighten mode drops points that already lie near the line between their
    neighbours, keeping the corners
  - The end points are kept exactly: a stroke has to start and finish where the
    user's did

### Rough edges

- [ ] The pen cannot resume an existing open path from one of its endpoints
- [ ] Deleting an anchor joins its neighbours with whatever handles they already
      had, rather than refitting the curve through the gap the way Animate does
- [ ] Picking a one-pixel stroke by clicking it is fiddly. Anchor editing was
      verified on a polygon; the tolerance may want widening for thin strokes
- [ ] Subselection edits the first path of each edge. A shape whose edge holds
      several paths shows them all but only the first is editable
- [ ] Shape bounds are computed from anchor and control points, so the selection
      box can sit slightly inside a curve that bulges past its control polygon.
      Fine for culling, loose for a handle box

Two input bugs fixed along the way, both of which made a tool silently do
nothing rather than fail visibly:

- The pen's shortcut was `B`, which the "Show Bounding Boxes" toggle already
  owned. Qt fires neither key on a clash. The tools now use Animate's keys --
  `V` `A` `Q` `R` `O` `N` `Y` `P` `D` -- with `B` left to the bounding-box toggle
- Alt-drag was a pan modifier at the view level, so Alt never reached a tool.
  That broke the pen's alt-drag-to-break-tangent and alt-click-to-insert-anchor.
  Only middle-drag forces a pan now; Alt belongs to the tools, as in Animate

## 6. Merge drawing model — the hard part

Animate's default mode maintains a planar map: every edge carries a left and a
right fill (that is what `fillStyle0` / `fillStyle1` already are). Drawing across
a fill splits both at intersections and rebuilds faces.

Started. The geometry and the arrangement are in; fills are not yet attributed
to it, so nothing in the application uses it yet.

- [x] Kernel decision: **build the arrangement directly**, rather than adopting a
      boolean library
  - Both candidates return *paths*. What a merge model needs is the arrangement
    itself -- vertices, half-edges, faces -- plus a left and right fill on each
    half-edge. A boolean kernel gives output shaped wrong for that, and the fill
    attribution layer would still have to be written on top
  - Skia's `SkPathOps` is BSD and handles cubics, but vendoring Skia for one
    subsystem is enormous
  - Clipper2 is small and integer, which suits twips, but is polygon-only: every
    merge would flatten the beziers the pen and pencil produce, making stored
    geometry lossy on each edit
  - Revisit if the arrangement turns out to be harder to make robust than
    expected. The intersection layer is the part a library would have replaced,
    and it is now written and tested
- [x] `src/geom/curve.h`: one piece of path geometry, line or cubic, with
      evaluation, tangents, control bounds, splitting and subcurves. Quadratics
      are raised to cubics on the way in, so there is one curved case not two.
      A line keeps its inner controls on the chord, so the cubic formulae work
      unchanged and nothing has to special-case it
- [x] `src/geom/intersect.h`: curve-curve intersection and self-intersection
      (21 tests). Straight pairs are solved exactly; anything curved is found by
      subdivision against control-polygon boxes, halving until the pieces are
      flat enough to treat as segments
  - Slower than Bezier clipping, but it degrades into the exact line solver
    rather than into guesswork, and the line case is the overwhelmingly common
    one
  - Collinear overlap reports no crossing: running along together is not a
    crossing, and a point there would be a meaningless vertex in the map
  - Verified to resolve a crossing at twip scale, and mutation-checked --
    breaking the subdivision's parameter mapping fails four tests
- [x] Planar map (`src/geom/planar_map.h`, 15 tests): every crossing becomes a
      vertex, every piece between crossings a pair of opposite half-edges, and
      every enclosed region a face
  - Two rectangles laid over each other come out as three regions, which is the
    case merge drawing exists for
- [x] Snap to the twip grid (1/20 px). Flash geometry is already twip-quantized,
      so this turns "are these two points the same" from a tolerance question
      into a dictionary lookup, which is what keeps the topology consistent
- [x] Faces with holes. A shape drawn inside another leaves a hole, not a
      separate region, and disconnected boundaries mean several cycles walked
      the wrong way round -- only one of them is the outside
  - Which face a boundary sits in is decided by a ray cast from a point a
    quarter twip off the edge. A vertex of the cycle will not do: it lies on the
    boundary, where the cast can answer either way. That was a real bug, caught
    by the touching-rectangles test
- [x] Coincident edges are merged. Two shapes sharing an edge each contribute
      their own copy, and collinear overlap is deliberately not a crossing, so
      keeping both would leave a zero-width sliver between them
- [x] Per-half-edge fill attribution (`src/geom/fill_attribution.h`, 11 tests).
      Each face gets the fill of the last drawn outline covering it, and each
      half-edge records the fill of the face it borders -- the fill on its left.
      A half-edge and its twin together are what the format stores as
      `fillStyle0` and `fillStyle1`
  - Paint order is the whole of the merge behaviour: drawing a shape across
    another does not stack them, it replaces what was underneath inside the new
    outline and leaves the rest
  - An outline carrying no fill still cuts, and an open stroke cuts without
    being a region at all: it has no inside, so it splits faces and takes no
    part in attribution. That is what lets a line drawn across a fill divide it
    into halves that can be dragged apart
- [ ] Intersection is tested pairwise, which is quadratic. Fine for the tens of
      curves a shape holds; a sweep line if that ever stops being true
- [x] `src/geom/shape_geometry.h`: a shape's edges become curves carrying the
      styles the file recorded for them, which is what feeds the arrangement from
      a real document
- [x] **The side convention, settled by data rather than guessed.** The format
      records a fill for each side of an edge but never says which side is which.
      Reading eight real documents and checking which way round makes every face
      agree with itself: `fillStyle1` is the fill on the **left** of the edge's
      direction, `fillStyle0` the one on the **right**. Agreement was 44 faces
      out of 44, against 3 out of 44 for the opposite reading
  - That it agrees at all is the stronger result: the arrangement reproduces the
    topology real Flash files encode, not merely something plausible
- [x] Convert the arrangement back into `Edge` objects, so an edit can be
      written to the document (`rebuildShapeEdges`). Each pair of opposite
      half-edges becomes one edge carrying the fill from each side
  - An edge with the same thing on both sides separates nothing and is left out,
    whether that is two empty sides or the same fill on each. That is exactly how
    the seam vanishes where two shapes merged into one region
  - Verified on real data: 36 shapes from six documents went through the
    arrangement and back without changing which fills they use
- [ ] Chain neighbouring pieces that share styles into one path. Correct as it
      stands -- one edge per piece, which the renderer stitches back into loops --
      but it writes far more edges than Flash would
- [x] `ShapeMerger::merge` (`src/edit/shape_merge.h`, 12 tests): the whole
      behaviour, at the data-model level. Outlines cut each other, the newer fill
      replaces the older where they overlap, a stroke across a fill divides it
      without erasing, and a seam between two regions of the same colour vanishes
  - The two shapes number their styles independently, so the addition's fills
    and strokes are copied in and renumbered before anything looks at them
- [x] `MergeShapeCommand`: a merge rewrites the target completely, so undo keeps
      a copy of both sides rather than trying to reverse the operation. The merge
      runs once, when the command is built; redo and undo only swap a snapshot
      back, so going back and forth cannot drift
- [ ] Wire it into the tools. The pen, pencil and shape tools still always add a
      new element; merge mode needs a toggle (Animate's `J`) and, when it is on,
      the rectangle and oval tools have to produce shapes rather than the
      primitive object types
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
