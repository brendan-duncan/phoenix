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
- [x] Restore a buildable Qt environment (see "Build environment" below)
  - Done 2026-10-10: the build no longer depends on an installed Qt. `cmake/PhoenixQt.cmake` downloads a trimmed Qt (made by `tools/qt/make_qt_archive.py`, about 40 MB for Windows x64) into `third_party/qt/` when none is found, with the version and hashes pinned in `cmake/QtArchives.cmake`. Checked by configuring and building from the archive alone with the installed Qt ignored. Archives for windows-arm64, macos, linux-x64 and linux-arm64 still have to be built on those machines and uploaded
- [x] Run the Qt archives workflow (`.github/workflows/qt-archives.yml`, added 2026-10-10) for 6.11.2 and merge its pin PR: it makes the archives for all five targets, builds Phoenix from each on the matching runner, publishes them to the `qt-6.11.2` release and pins the hashes. The macOS and Linux builds of Phoenix have never been tried, so the verify jobs there may surface portability work unrelated to Qt
  - Done 2026-10-10: the workflow made, verified and published all five archives on the `qt-6.11.2` release, and their hashes are pinned in `cmake/QtArchives.cmake`

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
- [x] Upgrade the unsaved-changes prompt to offer Save now that step 2 has
      landed. Save / Discard / Cancel, and Save that does not happen -- a
      canceled Save As, a declined lossy save, a failed write -- cancels the
      thing that asked, rather than discarding the changes anyway
- [x] File > New (`src/edit/new_document.h`, 11 tests). One scene, one layer,
      one empty keyframe, 550x400 at 24fps, shaped like a parsed document rather
      than a minimal stand-in so the view, the timeline and the writer all see
      what they would see from a file
  - The layer's `firstFrame` / `lastFrame` are bookkeeping the parser normally
      supplies and frame resolution relies on; a built document has to fill them
      in or nothing can be drawn on it
  - Verified end to end in the running application: new document, draw a
      rectangle, save, reopen, select what comes back. The written `.fla` also
      checks out against Python's `zipfile`
  - `adoptDocument` / `releaseDocument` now hold the one copy of "replace what
      is open", which New and Open both go through

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

- Flash uses CR for line breaks inside text runs, and XML normalizes a literal
  CR to LF on read. Those now go out as `&#13;`. Before the fix, 12 of the 74
  corpus documents came back with altered text and layer names
- `parseActionScript` read `element->GetText()` instead of
  `childElement->GetText()`, so frame ActionScript never actually loaded

### Old binary FLA files

- [ ] Flash CS4 and earlier saved .fla as a Microsoft compound document holding
      an undocumented binary format, not the zip of XML that CS5 and later
      write. Three corpus files are like that (Freya_Fairy_07.fla,
      Yard_Sale_03.fla, Pet_Cemetery_04.fla). Reading them would mean
      reverse-engineering that format; Animate converts them on open
  - 2026-10-10: the reader now recognizes them by their signature and says so ("a Flash CS4 (or earlier) document ... open it in Animate and save it again") instead of failing as a broken zip, and the file named at startup is opened once the window is up, so the message is not lost behind a window that does not exist yet

### Not written back yet

The writer records anything it cannot represent rather than dropping it in
silence (`XFLWriter::unsupported()`), and the GUI warns before a save that would
lose content. Across the corpus that is:

- [x] document scripts (`<scripts>`)
- [x] publish history
- [x] printer settings
- [x] swatch lists
  - Done 2026-10-10 for all four: the reader keeps each element's text as read (`ScriptList::xml`, `PublishHistory::xml`, `PrinterSettings::xml`, `SwatchList::xml`) and the writer copies it back into the document after the timelines, in Animate's order (PrinterSettings, publishHistory, swatchLists, scripts). Nothing edits them, so verbatim is exact; a document made in code, which has no text, is still reported as unsupported for them. Test in `test_xfl_writer.cpp`

These are all document metadata rather than geometry; they are carried
through untouched rather than modeled.

### Also outstanding

- [x] Parts the reader does not model were read past and dropped on save:
      sound items in the library (DOMSoundItem) and on frames (soundName,
      soundSync, inPoint44... and SoundEnvelope), extendedSwatchLists,
      brushdefinitions, fonts, variable-width stroke profiles
      (VariablePointWidth), and the "puppet" symbol type
  - Done 2026-10-10: each is kept as read and written back where it was: unmodeled document sections as XML text before or after the timelines as they came (`Document::extraSections`), media items other than bitmaps (`extraMedia`) with the data files they name under bin and any package file an Include names, such as PaintBrushDefinitions.xml (`extraFiles`), a frame's unmodeled attributes and children (`Frame::extraAttributes`, `extraChildren`, copied with the frame), a stroke's unmodeled children, and an unknown symbol type as spelled (drawn as a graphic). A blend mode of "normal" now reads as none, as Animate writes it, and an archive holding a document folder rather than the document (test_1_2.fla) is read from that folder. Test `xfl_keeps_what_it_does_not_model`; with PHOENIX_FLA_CORPUS=D:/fla the corpus round trip covers 84 documents with no differences and prints no unhandled elements (it failed on one before). Checked on disk: the Alvin file's sound item, frame sound settings and its .dat survive a save, and 520_Boogers_1104's brush definitions file is written back byte for byte
- [x] Audio playback: sounds play with the timeline
  - Done 2026-10-10: miniaudio 0.11.21 (public domain or MIT-0) is vendored under src/third_party/miniaudio, as the vendored Qt has no Multimedia. Sound items decode from their bin .dat as raw PCM (8 or 16 bit, mono or stereo, 5.5 to 44.1 kHz, resampled to 44.1 kHz) or as MP3; MP3 needs a valid MPEG frame header, since raw PCM can start with FF FF. Stream sounds follow the playhead (a scrub plays a frame of them), event sounds play out from their keyframe, start sounds do not restart while playing, stop silences the sound named, and in/out points and loops apply. Playback now runs by the clock at the document frame rate rather than at a fixed 24 fps, drops frames it cannot draw in time, and stops on the last frame. View > Mute Sounds (Ctrl+Alt+M). Test `test_audio_decode`; with PHOENIX_FLA_CORPUS=D:/fla all 11 corpus sounds decode to their recorded length (Milo MP3 within one frame). Checked with logging on the Alvin file: one stream voice per play, started at the matching offset on resume, at 24 fps. Not done: an Enter shortcut for play (it would take Enter from the pen and text tools)
  - Done 2026-10-10: sound waveforms and envelopes. A span with a sound draws its waveform in blue from the in point onward, a column per pixel at the loudest sample under it, as Animate does; the timeline gets the decoded sound from the audio player's cache. A frame's SoundEnvelope now shapes the volume: points at mark44 sample positions with left and right levels (32768 is full, a level left out is silence), joined by straight lines and held flat past either end, counting on through loops. Test `sound_envelopes_shape_the_volume` covers Milo_Kitty_01's fade-out and Yard_Sale_03_2's uneven channels. Checked on screen: the Alvin file's scratch track draws along its 1133-frame span from frame 8
- [ ] The frame grid and the layers list scroll up and down on their own, so after a wheel scroll over the grid the names no longer line up with their lanes, and the ruler scrolls out of view with the grid (seen 2026-10-10 on the Alvin file). They should share one vertical scroll, with the ruler fixed above it


- [ ] Verify Animate itself opens what we write. Round-tripping through our own
      reader proves self-consistency, not compatibility. The cubic `(...)` form
      is the likeliest sticking point: Flash stores a `q`/`p` quadratic
      approximation inside the parens that our reader skips and our writer omits
- [x] Save to a temporary file and move it into place, so an interrupted save
      cannot destroy the original
  - Done 2026-10-10 for .fla: written to `<name>.fla.saving` beside the original and moved over it once complete (a failed write or move leaves the original and removes the temporary). An XFL folder is still written in place

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
- [x] Toolbar buttons carry drawn icons (`src/gui/tool_icons.h`) rather than
      names. Ten small glyphs are not worth a dependency on an image set or a
      resource file to keep in step with the build, and drawing them means they
      scale to whatever size the toolbar asks for
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
    center and right -- and gets back one adjustment for the whole axis, so a
    shape shifts to meet a line rather than distorting because one edge snapped
  - Lining up with another object beats the grid at equal distance; the grid only
    wins when it is strictly closer
  - Candidates are the edges and centers of everything else on stage, plus the
    stage edges and center. The dragged objects are excluded, since something
    snapped to its own edge would never move
  - Candidates are gathered once at the start of a gesture, not per mouse move
  - Rotation does not snap the cursor: it is an angle, not a position, so
    snapping would fight the shift-key angle steps
  - Grid spacing, `objectsSnapTo` and `snapAlignBorderSpacing` were declared on
    `Document` but never read, so the grid silently always used the built-in
    default of 18. They are now parsed and written
- [x] `main.cpp` ignores `argv`, so opening a file from the shell or by dropping
      one on the executable does nothing -- it always reopens the most recent
      file instead
  - Done 2026-10-10: a path on the command line (which a drop on the executable is) opens instead of the most recent file, and a .fla or .xfl dropped on the window opens too. Checked: `Phoenix.exe D:/fla/bitmap_fill_test.fla` opens that file
- [x] Ruler guides. Animate snaps to these too, but nothing models them yet --
      no rulers, no guide objects in the document. Needs that feature first, and
      is not the same thing as the guide *layers* the parser already knows about
  - Done 2026-10-10: View > Rulers (ctrl+alt+shift+R) draws rulers in stage pixels along the top and left of the stage, ticks spaced to stay readable at any zoom; a guide is dragged out of a ruler, moved with the selection tool and dragged back into a ruler (or off the view) to remove, drawn in the document's guides color. View > Guides holds Show Guides (ctrl+;), Snap to Guides (ctrl+shift+;) and Clear Guides. The snapper takes guides as a third kind of target (`SnapKind::Guide`, test in `test_snapping.cpp`), ahead of objects at equal distance; inside a symbol they are carried into its space when that is a plain scale and shift
  - Not saved: no corpus file stores guides, so how Animate writes them is unknown and they live in the view for the session
  - Found along the way: snapping, the free transform box and the Properties panel's W, H, X and Y all measured bounds padded by 2 pixels for antialiasing, so a snapped object stopped 2 pixels short of the stage edge, a guide or another object, and a 355 pixel rectangle read as 359. `PhoenixView::elementBounds` now gives the art's own edges (a fill's outline, a stroke's outer edge); the padded bounds are kept for culling

Two things to tidy:

- [x] The stage selection and the document tree still track separately: the tree
      drives `PhoenixView::_selectedElement` for inspecting edges and paths
      inside a shape, while the stage drives `Selection`. They should be the one
      model, with the tree simply selecting finer-grained nodes
  - Done 2026-10-10: an element picked in the tree becomes the stage selection (a finer node, an edge or a path, selects its shape and is still shown inside it for inspection), and the stage selection is shown in the tree when the tree has built an item for it; the tree fills in lazily and is not unfolded for it. A guard stops the two from bouncing off each other
- [x] A click that only selects no longer moves anything. Snapping asks where
      the box would land rather than how far the cursor traveled, so running it
      at zero distance answered "on the nearest grid line" and shifted any
      object not already sitting on one -- a select became an edit, and the
      document came up modified. A press now has to pass the same drag threshold
      the marquee uses before it moves anything
  - Only showed up on artwork drawn by hand: files authored in Animate tend to
      sit on round coordinates already, where the snap was a no-op
- [x] A marquee tests element bounds, not geometry, so it catches a shape whose
      bounding box overlaps even when no part of the shape does
  - Done for merge artwork, see step 13

Also fixed along the way: `screenToScene` and `sceneToScreen` were dead code and
wrong -- they left out the centering that `paintEvent` applies, so any hit-testing
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
- [x] Fill/stroke properties panel: color swatches with alpha, stroke weight,
      and the polystar's sides and star mode. One `DrawingStyle`
      (`src/edit/drawing_style.h`) lives for the session, so the next shape picks
      up the last one's settings

### Rough edges

- [x] New objects go into the current frame of the first drawable layer, or one
      the file marks selected. There is no way to choose a layer yet, so
      `PhoenixView::activeFrame()` guesses. Needs layer selection in the timeline
  - Done: clicking a layer name in the layers panel makes it current
- [x] Drawing on a frame that has no keyframe should create one. Right now the
      object joins whatever frame is showing
  - Done 2026-10-10: `InsertKeyframeCommand` (`src/edit/frame_commands.cpp`, tests) cuts the span at the playhead and gives the new keyframe a copy of the span's contents, or stretches the last span up to a playhead past the end, undoably; `placeDrawnElement` inserts it inside the drawing's own undo step through `PhoenixView::keyframeForDrawing`, so every drawing tool gets it. The timeline repaints with every edit, and moving the playhead clears the selection as Animate does (which also drops a floating drawing into its frame). Checked on screen: drawing on frame 5 of a one-frame layer stretches the span and adds the keyframe, frame 1 shows no rectangle, and the saved file carries both frames. Text placed with the text tool still goes to the shown span's keyframe
- [x] Star mode is implemented but only polygon mode was checked on screen
  - Checked 2026-10-10: a five-point star
- [x] Rectangle corner radius and the oval's start/end angle and inner radius are
      in the data model and get written, but no tool sets them
  - Done 2026-10-10: the corner radius had its Properties row already; Start angle, End angle and Inner radius rows now set the oval primitive's arc and hole (checked on screen with a 270 degree arc and a 40% hole). The plain oval tool still draws whole ovals

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
  - A path whose last point repeats its first is recognized as closed, however it
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
    corner grows them along the line through its neighbors, a third of the way
    to the closer one
  - `EditablePath::closestPoint` sweeps coarsely then refines by bisection, so a
    click lands where the user aimed rather than on the nearest sample
- [x] Pencil (`D`) with Schneider curve fitting (`src/edit/curve_fit.h`, 10
      tests) and smooth / straighten / ink modes, with tolerance in the
      properties panel
  - Fit one cubic to the whole stroke by least squares, improve the
    parameterization with Newton-Raphson, and split at the worst point only if
    it is still outside tolerance. A sine drawn with sixty cursor points comes
    back as a handful of anchors
  - Straighten mode drops points that already lie near the line between their
    neighbors, keeping the corners
  - The end points are kept exactly: a stroke has to start and finish where the
    user's did

### Rough edges

- [x] The pen cannot resume an existing open path from one of its endpoints
  - Done 2026-10-09, see the pen items in step 12
- [x] Deleting an anchor joins its neighbors with whatever handles they already
      had, rather than refitting the curve through the gap the way Animate does
  - Done 2026-10-09, see the subselection items in step 12
- [x] Picking a one-pixel stroke by clicking it is fiddly. Anchor editing was
      verified on a polygon; the tolerance may want widening for thin strokes
  - Noted 2026-10-10: the pick already allows half the stroke's weight plus four screen pixels at any zoom (`pickTolerance`, `hitTestElement`), which is Animate's reach, and an anchor under the cursor takes precedence over the stroke; nothing further to widen
- [x] **Dragging a corner no longer tears the outline apart.** A merged shape
      is written as one edge per piece, so every corner is shared between two
      or more edges. The tool tracked a single edge, so moving a corner moved
      one piece's endpoint and left the others behind -- the outline came apart
      and the fill collapsed into a triangle with a line hanging off it
  - An anchor drag now takes every anchor sitting at that point. A handle still
      moves alone, since a handle belongs to one curve and moving it
      disconnects nothing
  - Coincidence is judged to half a twip. The format stores twips, so anchors
      meant to be the same point agree well inside that, and Flash could not
      tell two closer points apart either
  - Moving a shared corner rewrites several edges, which is one thing the user
      did and so one thing to undo
- [x] Deleting an anchor has the same problem the drag had: it only edits the
      one edge, so removing a shared corner will still break the outline
  - Done 2026-10-09: a corner at the end of an open piece that meets the end of exactly one other open piece with the same styles is removed by joining the two pieces into one path (reversed as needed), refitting through the corner, and removing the second piece's path, as one undo step
- [x] Subselection edits the first path of each edge. A shape whose edge holds
      several paths shows them all but only the first is editable
  - Done 2026-10-09: every path of every edge is shown, grabbed, dragged, converted and deleted by its own index
- [x] Shape bounds are computed from anchor and control points, so the selection
      box can sit slightly inside a curve that bulges past its control polygon.
      Fine for culling, loose for a handle box
  - Done 2026-10-09: `calculateShapeBounds` takes each curve's ends and the points where it turns back in x or y (`Curve::extremaParameters`), so the box is tight on the curve

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
  - Paint order is the whole of the merge behavior: drawing a shape across
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
- [x] Chain neighboring pieces that share styles into one path. Correct as it
      stands -- one edge per piece, which the renderer stitches back into loops --
      but it writes far more edges than Flash would
  - Done 2026-10-09, see "Chain the output" in step 10
- [x] `ShapeMerger::merge` (`src/edit/shape_merge.h`, 12 tests): the whole
      behavior, at the data-model level. Outlines cut each other, the newer fill
      replaces the older where they overlap, a stroke across a fill divides it
      without erasing, and a seam between two regions of the same color vanishes
  - The two shapes number their styles independently, so the addition's fills
    and strokes are copied in and renumbered before anything looks at them
- [x] `MergeShapeCommand`: a merge rewrites the target completely, so undo keeps
      a copy of both sides rather than trying to reverse the operation. The merge
      runs once, when the command is built; redo and undo only swap a snapshot
      back, so going back and forth cannot drift
- [x] **Merging happens on deselect, not on draw.** A new drawing is its own
      object first: selected, sitting above the artwork, draggable from anywhere
      on it as one piece. Letting go of it is what folds it in. That is Animate's
      model, and it is what makes a shape repositionable right after drawing it
  - Starting another drawing commits the waiting one first, so it merges into
      what was under it rather than into the drawing being laid on top
  - A drawing merges only into shapes **below** it. Searching from the top of
      the z-order could pick something stacked above, which merged a shape into
      one drawn after it and produced nonsense geometry
  - `bakeTransform` (`src/edit/shape_transform.h`, 8 tests) folds a shape's
      transform down into its points, so a drawing that was dragged before being
      let go merges where it was left rather than where it was drawn. Merging
      reads raw edge coordinates and a drag lives in the transform
  - Edge coordinates are twips in the file but the parser converts, so a `Point`
      and a transform are both in pixels and the transform applies directly
  - Baking clears `Edge::data`. The writer prefers the text an edge was read
      with, so leaving it would save the geometry exactly where it used to be
  - Undo is one step for the merge: the target's snapshot and the drawing's
      removal go on as a single macro. Verified on screen -- one undo puts both
      separate rectangles back
  - Saving commits a floating drawing first, so saving with one selected and
      saving just after letting it go write the same file
- [x] **A fill drawn over something buries it, outlines included.** The merge
      resolved fills by paint order but carried every stroke through, so the
      older rectangle's edges kept drawing across the newer one. A stroke that
      came from the target and has the addition's fill on both sides of it is
      now dropped
  - Only the buried part goes: the older outline outside the new drawing stays,
      and the new drawing's own outline survives whatever it lies over
  - An outline drawn with no fill behind it still cuts without erasing, which is
      what lets a line dropped across a shape divide it
  - `rebuildShapeEdges` takes an optional `StrokeFilter` for this, so the rule
      lives with the merge rather than in the geometry
- [x] Ctrl held reaches for the selection tool whatever tool is active, so
      something can be moved without putting the drawing tool down. The gesture
      belongs to that tool until the button comes up, even if ctrl is released
      part way through
- [x] **Moving or reshaping a shape drops it in again, as drawing does.**
      Only a freshly drawn shape used to be waiting to be committed, so moving
      an existing one onto other artwork and letting go left it sitting on top,
      merging with nothing. Finishing a move, a free transform or an anchor drag
      now marks the shape, and deselecting commits it where it now sits
  - The commit takes on everything below it, not just the nearest: it merges
      into the one it lands on and cuts itself out of the rest
  - Several shapes can be waiting at once, since one drag moves a whole
      selection, and each is dropped where it ended up
- [x] **The cut happens when the drawing is placed, not when it is moved.**
      Drawing over artwork destroys what was under it there and then. The
      drawing sits exactly over the hole it just made, so nothing looks
      different until it is moved -- and then the hole is uncovered rather than
      appearing from nowhere. Drawing a rectangle over another and dragging it
      off now leaves the L-shaped notch, which is what Animate does
  - `ShapeMerger::subtract` is the merge run the other way: the same
      arrangement, but the covered faces are emptied instead of painted, and the
      cutter leaves nothing of its own -- no fill, and no outline round the hole
  - A stroke-only cutter takes nothing away, since it has no area. That is what
      keeps a line dropped across a shape dividing it rather than erasing it
  - Adding the drawing and cutting what it covers are one undo step
  - Deselecting without moving merges it straight back, reproducing the plain
      merge exactly: one shape, one outline, no seam
- [x] One shape per layer is correct and is what Animate writes -- checked
      against `rect_2.fla`, where two overlapping rectangles came back as a
      single `DOMShape` with two fills and three edges. What is still missing is
      selecting a *region* of that shape rather than all of it (see step 8).
      Re-selecting merged artwork and dragging still moves the whole thing
  - Done: regions are selected and dragged individually now
- [ ] Cutting leaves the cutter's fills and strokes in the target's style
      tables, unused. Harmless, but it grows the file
- [x] Wired into the tools. **Merge drawing is now the default**, as in Animate,
      with an Object Drawing toggle (`J`) to keep each drawing separate
  - The pen, pencil and shape tools all finish through one
      `placeDrawnElement`, so the mode is decided in a single place rather than
      three times over
  - A new shape merges into the topmost shape in the frame it touches; one drawn
      clear of everything becomes an object of its own
  - With merge mode on the rectangle and oval tools build plain shapes rather
      than `DOMRectangleObject` and `DOMOvalObject`. Those primitives are
      object-drawing by nature and cannot take part in a merge
  - Verified in the running application: two overlapping rectangles come out as
      one object, with the selection box spanning both
- [x] Paint bucket (flood fill over the map), ink bottle, eraser
  - Bucket and ink bottle done; eraser still to do
- [x] Selection tool edge-dragging (fill follows the edge)
  - Done, see step 13
- [x] Stroke-to-outline conversion (needed by the brush tool)
  - Done for the brush through QPainterPathStroker
- [ ] Consider rendering *from* the planar map, replacing the fill-stitching
  - 2026-10-09: the stitcher now traces region boundaries properly rather than taking the first path that happens to start where the last ended: every directed path has its fill on its right, so at a junction it keeps right (the most clockwise outgoing path, never a reversal), and a piece with the same fill on both sides goes in both directions. This fixed fills vanishing from regions bounded by a stroke running through a fill (a line across a rectangle, then a brush stroke across the line). Rendering from the map remains the thorough option
      heuristic in `drawShape` (the `directedPaths.size() <= 2` special case and
      twip-rounded point matching). Likely fixes rendering bugs rather than adding risk

## 7. Symbol editing -- the reason a loaded file looks read-only

Artwork in a real document lives inside library symbols. Across the corpus the
root timelines hold 31 shapes against 1415 symbol instances, so roughly
everything a file draws is one level down or more.

Clicking a symbol instance deliberately selects the instance rather than the
artwork inside it, which is what Animate does too. What Animate also has, and
Phoenix does not, is a way in: nothing enters a symbol, so the shapes inside
cannot be reached at all. Confirmed on screen -- clicking the character in
`Charlotte_06_2021.fla` selects the whole 800x550 instance, and double-clicking
does nothing.

The few shapes that do sit on a root timeline select and edit correctly, so this
is the missing feature rather than a fault in the tools.

- [x] An edit scope: which timeline the tools act on, as a stack, so entering and
      leaving nests properly
  - Done 2026-10-09: `PhoenixView::_scope`, a stack of (instance, timeline). The view's coordinate space becomes the scope's, so every tool, hit test and bounds works unchanged inside a symbol
- [x] Enter a symbol by double-clicking an instance, and leave it again
  - Done: double-click an instance to enter, empty stage to leave, or click a breadcrumb level
- [x] Draw the enclosing scope dimmed behind the symbol being edited, which is
      what makes edit-in-place readable
  - Done: the document draws at 30% and the scoped timeline over it at full strength
- [x] A breadcrumb showing the scope, and a way back to the root
  - Done, above the stage
- [x] Tools have to work in the symbol's coordinate space, not the stage's
  - Done through `documentToWidget`, which now runs through the instance transforms enteredS
- [x] The timeline panel should follow the scope rather than always showing the
      root
  - Done: both panels show the scoped timeline
- [x] Editing a symbol changes every instance of it. That is correct, but it
      needs to be visible, and the render cache has to notice
  - Visible: every instance draws the same timeline and the caches clear on edit

## 8. Matching Animate's behavior

The tools work, but they are not shaped like Animate's. The gap is one decision,
not a list of small ones: **selection is element-level, and Animate's is
region-level.**

Phoenix selects a whole `DOMShape`. Animate, on merge-drawn artwork, selects the
fill or the stroke segment you clicked. Clicking one of the two rectangles in
`rect_2.fla` selects both, because they are one shape -- in Animate you would get
the one fill. Everything below follows from that, and the planar map built in
step 6 is already the right structure to fix it: a face is what Animate would
select.

- [x] Select a face or an edge of the arrangement rather than the whole shape
  - Done 2026-10-09 (`region_split.h`): a click lifts the fill or stroke out as a floating shape (`SplitRegionCommand`); let go untouched the split is discarded from the history (`CommandStack::discardLast`) and the shape is whole again; moved or deleted, it is dropped back in like any shape
- [x] Draw a selected fill with Animate's stipple over the fill itself. The blue
      bounding box with corner handles is what Animate shows for objects, groups
      and symbols, so using it for everything makes merge-drawn artwork look
      like object drawing
  - Done: the lifted region is a shape of its own, so the dot screen covers exactly the selected fill or stroke
- [x] Double-click a fill to take its enclosing strokes with it; double-click a
      stroke to take every connected segment
  - Done
- [x] A marquee should cut geometry, not pick whole elements -- half a fill
      selected is half a fill you can drag away
  - Done, see step 13
- [x] Alt draws a rectangle or an oval from its center. The polystar already
      works from the center and a line has no center, so neither takes it
- [x] Hovering an edge with the selection tool should show the corner/curve
      cursor that says it can be bent (pairs with edge dragging in step 6)
  - Done
- [x] Ctrl-dragging should cut the dragged region out of what is under it. The
      move itself works; the cut does not, and cannot until selection is
      region-level. A drawing that has not merged yet has nothing to cut out of,
      and one that has merged is a single shape, so clicking it selects the
      whole thing rather than the face that was drawn
  - Ctrl-drag on an unselected edge now adds a corner, as in Animate; cutting a region out still waits on region selection
- [x] While ctrl is held the cursor should change to the selection arrow, and
      the selection bounds should draw even under a tool that hides them
  - The cursor part is done (the view shows the arrow tool's cursor while ctrl is down, except under the free transform tool, where ctrl is Distort). The bounds under a tool that hides them are still not drawn

---

Steps 9 to 13 come from a code review on 2026-10-08 that compared Phoenix with
VectorCraft (`D:/src/ref/vectorcraft`, MIT/Apache, a Rust Illustrator-class
editor). Only algorithms are taken from it, never code; VectorCraft paths are
relative to its `crates/` folder. Line numbers are as of `b9b78f1`. Items marked
**(confirmed)** were checked in the code; the rest are from reading it and
should be reproduced before fixing.

## 9. Bugs found in review -- fix first

- [x] **(confirmed) Clicking overlapping art on two layers picks the lower
      one.** Layers draw last to first (`phoenix_view.cpp:478`, so `layers[0]` is
      on top), but `hitTestTimeline` (`:1744`) walks them first to last and
      `hitTestElement` overwrites `result` on every hit (`:1838`). Walk top-down
      and return on the first hit, skipping any child whose bounds grown by the
      tolerance miss the point. Re-running the walk while skipping what was
      already found gives the whole stack under the cursor, for select-behind and
      ctrl-click cycling (VectorCraft `doc/src/hit.rs:74-177`)
  - Done: topmost first, returning on the first hit
- [x] **(confirmed) Editing an edge with several paths deletes the others.**
      `EditablePath::applyTo` (`editable_path.cpp:183`) deletes every path in the
      edge and writes back one. Replace only the path that was edited
  - Done: `applyTo` takes the path index and leaves the rest alone
- [x] **(confirmed) Pen handles come out lopsided.** `Anchor::setOutHandle`
      (`editable_path.cpp:84`) mirrors the in-handle at its *existing* length,
      which the first few pixels of the drag fix, so the incoming curve stays
      nearly straight. Recompute `in = anchor - (out - anchor)` on every drag
      event; alt held mid-drag freezes `in` (VectorCraft `tools/src/pen.rs:235`)
  - Done: the in-handle is the exact mirror on every drag event; alt freezes it
- [x] **(confirmed) Double-click with the pen adds a coincident anchor.**
      `pen_tool.h` promises double-click finishes, but `mouseDoubleClick` is not
      overridden, so `QWidget::mouseDoubleClickEvent` re-sends a press. Override
      it to finish the path
  - Done
- [x] **(confirmed) Curved shapes merge with the wrong fills.** See step 10
  - Not reproducible any more on 2026-10-09: ovals of radius 3 to 60 drawn over and cut out of a rectangle, whole and straddling its edge, all keep their fills (tests in `test_shape_merge.cpp`). The sampling it came from is gone anyway: see step 10
- [x] **(confirmed) Partially overlapping collinear segments are never split.**
      See step 10
  - Done, see step 10
- [x] Free transform skews the wrong way on the top and left edges.
      `free_transform_tool.cpp:234-240` shears by `delta/depth`, but the grabbed
      edge sits at a *signed* offset of `-depth` from the opposite midpoint.
      Divide by the signed lever, `k = dx / (handle.y - origin.y)`, and conjugate
      `[1, 0, k, 1]` by the origin (alt: the center). VectorCraft
      `tools/src/xform/free.rs:91-107`
  - Done 2026-10-09: the shear is divided by the signed lever from the still edge to the grabbed one, in the box's own frame, so every edge slides the way it is dragged, on a rotated box too. Verified on screen
- [x] Scaling a rotated object shears it: `adjustForDrag` (`:168-240`) builds
      scale and shear in page axes while the box it carries is rotated. Keep an
      oriented box (angle + rect in the turned frame), map the pointer into it,
      scale there and map back (VectorCraft `doc/src/orient.rs:15-85`,
      `tools/src/bbox.rs:232-254`, `tools/src/select.rs:304-314`). Shift should
      take the larger axis rather than averaging `|sx|` and `|sy|` (`:191`); alt
      scales from the center
  - Done 2026-10-09: the pointer is measured in the box's frame (`boxFrame`), scaled there and turned back, so a rotated box scales along its sides; shift takes the larger axis; alt scales about the center. Verified on screen
- [x] The free transform box goes stale: `_boxValid` is only cleared when the
      selection empties or the tool is deactivated (`:482-489`), so after undo,
      Select All or a tree selection the old box is drawn and used. Rebuild it
      from the selection on every press. A press outside the box also falls
      through to panning (`:337`), so `Q` cannot click-select or marquee
  - Done: the box remembers which transforms it was built for and is rebuilt when they change; a press outside it picks or clears
- [x] Free transform's move snaps the raw cursor (`:265-268`), so the object
      lands offset by where it was grabbed. Snap the box, as `SelectionTool` does
  - Done
- [x] A click with the subselection tool that does not drag still pushes "Move
      Anchor" and calls `markShapeEdited` (`subselection_tool.cpp:237-265`), so a
      click makes the document modified and queues a re-merge. Start a drag only
      past a 3 px threshold, as the selection tool does
  - Done
- [x] Subselection compares a document-space tolerance against shape-local
      coordinates (`:84, :100, :350`), so picking is off on scaled shapes. Use the
      existing `toleranceInLocalSpace`
  - Done
- [x] Subselection takes the first match, not the nearest (`:111`), so an earlier
      anchor beats a later handle; alt-insert takes the first edge in range, not
      the closest (`:151`). Pick the nearest anchor within tolerance, and a handle
      only when it is nearer than its own anchor (VectorCraft
      `tools/src/direct.rs:206-260`)
  - Done 2026-10-09 for anchors and handles: every anchor and handle within tolerance is a candidate, the nearest wins, and a handle only beats its own anchor when it is nearer. Alt-insert on the nearest edge is still the first in range
- [x] `isNearAnchor` (`phoenix_view.cpp:1881`) tests quadratic control points as
      well as on-curve points, so clicking near a control point reports an anchor
  - Done
- [x] A closed pen path always puts its fill on `fillStyle1` (`pen_tool.cpp:258`),
      while merging reads `forward ? fillStyle1 : fillStyle0`
      (`shape_geometry.cpp:137`). A path drawn the opposite way round to a
      rectangle, or one lobe of a figure-8, may lose its fill on merge. Choose
      the side from the sign of the area, or attribute through `attributeFills`
  - Done: chosen from the sign of the area
- [x] Masks: the mask pixmap uses the local `supersampleFactor`
      (`phoenix_view.cpp:457`, 1 with high-quality AA off and during gestures)
      while the content pixmap uses the file-static 2 (`:497`), so their sizes
      can disagree; `DestinationIn` only touches the mask's area, so unmasked
      content leaks into the rest of the pixmap
  - Done: both pixmaps are sized from the paint device

## 10. Geometry kernel robustness

The merge model sits on this, so these come before the tools in step 12.

- [x] **(confirmed) Fill attribution samples curves at 8 points.**
      `cycleContains` (`planar_map.cpp:268`) and `outlineContains`
      (`fill_attribution.cpp:10-43`) test against an 8-point polyline per curve,
      while `interiorPoint` (`:471`) steps only a quarter twip off the boundary.
      On a 50 px oval the chord sag is about 0.06 px, larger than that step, so a
      face just inside the oval tests as outside and `combine`
      (`shape_merge.cpp:334/339`) gives it the target's fill. All merge tests use
      rectangles. Fix by attributing **topologically**: every half-edge knows its
      source and direction, so read the target's fill on each side straight from
      its edges, and for the addition start at the unbounded face with winding 0
      and walk across face adjacencies adding +-1 for each addition edge crossed.
      No sampling at all. This also fixes hole assignment (`:373-405`), which uses
      the same inexact test. Add an oval-over-rectangle merge test
  - Done 2026-10-09: containment is exact now (`Curve::horizontalRayCrossings` splits a curve where its y turns and bisects each monotone run to the ray), used by the map's cycles, the attribution outlines and so every face and hole decision; a merge reads each face's fill off the bordering edge's own side where the shape has one there (`faceFillFromSources`), and only asks geometrically for a face bordered by the other shape alone; interior points try larger steps when a quarter twip lands outside. Oval-over-rectangle tests added, including a sweep of sizes and an oval straddling an edge
- [ ] Where a point test is still needed, use exact ray-curve roots, and pick
      interior points the robust way: flatten finely, scan about 9 horizontal
      lines offset off the grid, and return the middle of the widest span with
      non-zero winding (VectorCraft `pathops/src/planar.rs:721`)
  - Exact roots done 2026-10-09; the interior point still steps off an edge midpoint, now trying a quarter twip, a twip and four twips in turn
- [x] **(confirmed) Collinear overlap is not split.** `intersectSegments`
      (`intersect.cpp:51`) returns false for parallel segments, though
      `intersect.h:32-34` promises the ends of an overlapping stretch. With
      (0,0)-(10,0) and (5,0)-(15,0) neither is cut, so half-edges run through
      vertices not on them and the face walk breaks. Common: abutting rectangles
      of unequal size, a pencil stroke retracing itself. Split each segment at
      the other's endpoints when they lie on it
  - Done 2026-10-09: `splitAtCrossings` cuts a curve wherever another curve's endpoint lies on it, which covers overlaps and T-junctions; the overlapping stretch then deduplicates to one edge
- [x] T-junctions: the line-line path accepts only `t` in [0,1] with no slack
      (`intersect.cpp:161`) while the curve path allows 1e-6 (`:115`), and twips
      are not exact binary fractions, so an endpoint lying on a line can be
      dropped. Split any curve where another curve's endpoint lies within half a
      twip of it
  - Done, see the item above
- [x] One crossing can become two vertices: each piece end is rebuilt as
      `A.pointAt(t1)` and `B.pointAt(t2)` and rounded separately (`:154-155`).
      Give each crossing one vertex id that both curves resolve to, and weld
      vertices with union-find within a radius rather than by rounding into a
      cell (VectorCraft welds on a 4-epsilon grid checking neighboring cells,
      `pathops/src/planar.rs:500-517`). Snapping also moves vertices up to half a
      twip without re-checking for new crossings
  - Done: every cut carries the point it was made at and both pieces are pinned to it. Also found and fixed: the duplicate-piece key rounded midpoints to the twip grid separately, so a midpoint on a half-twip let both copies through and collapsed the faces round it -- which is what turned a brush stroke merged back over its own cut the color of the artwork under it
- [x] The parallel test is an absolute 1e-12 (`intersect.cpp:51`). After a
      rotation in `bakeTransform`, collinear lines then produce arbitrary
      crossings. Make it relative to the segments' lengths
  - Done 2026-10-09: relative to the product of the segments' lengths
- [x] Edges round a vertex are sorted by `atan2(tangentAt(0))`
      (`planar_map.cpp:218-220`), which is (0,0) for every retracted pen handle,
      and ties between tangent curves fall in arbitrary order, so `next` links
      the wrong edges. Sort by where each edge first crosses a small circle
      round the vertex (radius half the shortest edge chord there), falling back
      to P2/P3 for a retracted handle (VectorCraft `leaving_angle`,
      `pathops/src/planar.rs:176`)
  - Done 2026-10-09: the departure direction is where the curve first crosses a circle of half the shortest chord at the vertex (at least half a twip), found by sampling and bisection, with the end point and then the tangent as fallbacks (test with a retracted handle and two tangent curves)
- [x] Self-intersections: `selfIntersections` (`intersect.cpp:187-194`) only
      crosses [0,1/2] against [1/2,1], so a loop with both parameters on one side
      is missed (the one test is symmetric). A loop that is found is then dropped
      by `planar_map.cpp:161`, which measures the chord, which is zero for a
      loop. Measure the control polygon instead, and find self-intersections by
      splitting at inflections and extrema first
  - Done 2026-10-09: the curve is cut at its x and y extrema (`Curve::extremaParameters`), every pair of the monotone pieces is searched and hits at shared joins discarded (test against a brute-force polyline count, loops in either half); a loop piece back to its own vertex is kept by measuring `controlPolygonLength` (test: a looping cubic encloses one face)
- [x] `flatness()` (`curve.cpp:148`) measures distance to the infinite chord
      line, so a cubic whose controls lie on that line beyond the ends counts as
      flat and its overshoot is lost. Measure distance to the chord segment
  - Done 2026-10-09, with a test
- [x] Intersection results are capped at 64 (`intersect.cpp:102`) silently;
      raise it or fail loudly
  - Raised to 1024 on 2026-10-09; a real pair crosses at most nine times, so more is an overlap being chased
- [x] When two edges coincide the first source wins (`planar_map.cpp:171`), and
      target curves come first, so a new stroked shape sharing an edge with an
      unstroked fill loses its stroke there (affects `keepStroke`,
      `shape_merge.cpp:358`). Keep the newer source's stroke
  - Done 2026-10-09: the later source claims a coincident edge, direction flags following its own direction; a stroked rectangle drawn along an unstroked fill's edge keeps its stroke there (test)
- [x] Chain the output (replaces the "chain neighboring pieces" item in step 6).
      Each piece knows its source curve and parameter range, so walk through
      degree-2 vertices whose pieces share (left fill, right fill, stroke) and
      the same source, and rebuild each run as `subcurve(t0, t2)` of the original
      curve. That is VectorCraft's `exact_merge` (`pathops/src/boolean.rs:487`)
      without its nearest-point search; for pieces from different sources fall
      back to a least-squares refit with fixed end tangents, accepted only within
      tolerance (`tidy_segments`, `boolean.rs:410`)
  - Done 2026-10-09: half-edges carry `t0`/`t1` on their source curve; `rebuildShapeEdges` walks runs through vertices where exactly two drawn edges meet (dropped seam edges do not break a run) with equal styles and either the same source at a matching parameter or two collinear lines, and writes one edge per run as the source's subcurve pinned to the run's end vertices, or one line. The least-squares refit for cubic pieces from different sources is not done
- [ ] Chain cubic pieces from different sources (an earlier merge already cut the curve into separate edges) with a least-squares refit with fixed end tangents, accepted only within tolerance (`tidy_segments`, `boolean.rs:410`)
- [x] Bounding-box broad phase before curve-pair tests (`intersect.cpp:101`
      tests every pair); `setFaceFill` scans every edge (`:550`). Sort by x first;
      a sweep line only if big pencil shapes need it
  - Done 2026-10-09 for the pair loop in `splitAtCrossings`
- [ ] Drop sliver faces whose `2 * area / perimeter` is below about a twip
      (VectorCraft `is_sliver`, `pathops/src/boolean.rs:341`)
- [ ] Longer term, if the above still leaves robustness problems: the design in
      the `linesweeper` crate (MIT/Apache, `docs/sweep.typ`) is a Bentley-Ottmann
      sweep whose curves carry error bars; the sweep only stays epsilon-ordered,
      topology comes from that order and coordinates are computed afterwards, so
      a missed crossing cannot produce an inconsistent graph. VectorCraft sweeps
      in a sheared frame (y + x/32) so no common-angle edge is horizontal, and
      feeds open strokes in traced there and back with per-direction crossing
      counts so they never cancel (`pathops/src/planar.rs:30-90, 243`)
- [ ] Then render from the map (the step 6 item): keep a cached `PlanarMap` per
      shape, fill each face as its outer cycle plus holes, then draw the chained
      stroke runs. Fall back to the current stitching if the build fails, and
      check it against the 44/44 corpus agreement

## 11. Rendering fidelity

All in `phoenix_view.cpp` unless noted.

- [x] **(confirmed) Bitmap fills draw nothing:** `getFillBrush` returns
      `Qt::NoBrush` for them (`:1068`). Use `QBrush(QPixmap)` with the fill matrix
      as the brush transform for repeating fills, and clip to the shape for
      clipped ones. Honor the bitmap's `allowSmoothing` (bitmap instances
      always smooth, `:888-920`)
  - Done 2026-10-09: `BitmapFill` is parsed, written and cloned (`bitmapPath`, `bitmapIsClipped`, matrix), and drawn as a tiled `QBrush(QPixmap)` with the fill matrix over 20 as the brush transform. Library bitmaps are decoded through one cache (`libraryBitmap`), from the LIBRARY copy when there is one and otherwise from `bin/<bitmapDataHRef>`: JPEG as is, Animate's lossless `.dat` (magic 0x0503, row bytes, width, height, then the zlib stream in u16-length pieces, pixels A,R,G,B premultiplied) through `bitmap_decoder.cpp`. The `.dat` is kept on the bitmap and written back on save; the bitmap item's other attributes round-trip too. Instances honor `allowSmoothing`. Also found: `itemID` was read and written as `itemId`, so every library item lost its id on load and on save; fixed for symbols, folders and bitmaps
  - Still open: `bitmapIsClipped` fills tile like repeating ones; clip to one copy of the bitmap
- [x] **(confirmed) Hairlines scale with zoom:** `MAX(weight, 0.5)` (`:933`)
      turns weight 0 into a 0.5 px document line. Flash hairlines are 1 device
      pixel: use a cosmetic pen
  - Done, see step 14
- [x] **(confirmed) Gradients are about 0.8% short:** the half-span is 1625 x 0.5
      (`:1014, :1043`), but the gradient square is +-16384 twips = +-819.2 px
  - Done 2026-10-09: 1638.4
- [x] `spreadMethod` (pad/reflect/repeat) is not parsed; map it to
      `QGradient::setSpread`. `interpolationMethod="linearRGB"` is not parsed;
      approximate it by expanding the stops into many sRGB stops
  - Done 2026-10-09 for `spreadMethod`: parsed, written, cloned and applied with `QGradient::setSpread` (reflect and repeat checked on screen). `interpolationMethod` is parsed and written back but still rendered as sRGB
- [x] Render `interpolationMethod="linearRGB"` by expanding the stops into many sRGB stops
  - Done 2026-10-09: eight stops per span computed in linear light (`addGradientStops`), fills and strokes alike
- [x] Gradient entries ignored their `alpha` attribute, so a fade to transparent was opaque; fixed 2026-10-09 in the parser (the writer already wrote it)
- [x] Gradient strokes ignore their matrix and use a fixed 0..100 line or a 50 px
      circle (`:965-994`)
  - Done 2026-10-09: the pen's brush is the same brush the fill would be (`getFillBrush`), matrix, spread and interpolation included; bitmap strokes come with it
- [x] Stroke `scaleMode` is parsed but ignored. Compute the pen width from the
      current transform's x/y scale for none/horizontal/vertical
  - Done 2026-10-09: `drawElement` keeps the element's transform relative to the stage (`_elementTransform`), and `scaledStrokeWeight` divides the weight by the square root of its area change for "none", putting one axis's scale back for horizontal/vertical. Cached pens hold the plain weight and are adjusted per draw (`penForHere`). Checked: a weight-4 "none" stroke inside a 2.5x instance measures 4 px
- [x] Caps, joints, miterLimit and pixelHinting are never parsed (`stroke_style.h`
      has only weight, scaleMode, fill), so Flash's default round caps and joins
      are lost
  - Done, see step 14
- [x] Strokes are drawn one edge at a time (`:1580-1604`), so every edge boundary
      gets a cap and corners never join. Chain consecutive edges of one style
      into a path first
  - Done, see step 14
- [x] Stroke styles map to Qt's fixed patterns (`:936-957`); the XFL dash1/dash2
      lengths, dotted spacing, ragged, stipple and hatched (no class at all) are
      ignored. A custom dasher walks each subpath by arc length, restarts per
      subpath, and turns zero-length dashes into dots shaped by the cap
      (VectorCraft `effects/src/stroke/dash.rs:47, 174, 311`)
  - Partly stale (noted 2026-10-10): dash1/dash2 and the dotted spacing are honored through Qt's dash pattern in pen-width units. Still open: ragged, stipple and hatched strokes are stand-ins (Qt's dash-dot patterns) rather than Animate's procedural looks, and hatched has no class
  - Done 2026-10-10 (`src/gui/stroke_effects.cpp`): ragged, stipple and hatched strokes are drawn from the stroke's centerline as filled marks, seeded from the geometry so they look the same on every paint: ragged wavers by its wave height and length (random patterns jitter, dotted ones break into dots), stipple scatters dots of its size, variation and density across the stroke's width, hatched lays marks across the line with its thickness, spacing, jiggle, rotation and length variation. Their settings are kept in `StrokeStyle::styleAttributes` and written back. `HatchedStroke` was not recognized at all, which made a shape using one fail to parse: MAD2_80Punkbed_characters_05.fla reported seven unhandled strokes and now loads them (its brows are hatched). Test file `D:/fla/stroke_styles_test.fla`; round-trip test in `test_xfl_writer.cpp`
- [x] Color transforms (`:723-755, :2772-2884`) render the instance into a
      stage-sized image with only its own matrix (`:736`), so the parent
      transform is dropped, content off the stage is clipped, and the result is
      1x and blurry when zoomed. Use an isolated offscreen sized to the
      instance's device-pixel bounds plus 2 px, drawn with the full transform
      shifted to that crop, then composited back (VectorCraft
      `render/src/group.rs:92-133, 253-262`). An alpha-only transform on a single
      shape can multiply brush and pen alpha instead of rendering an image
      (VectorCraft `render/src/lib.rs:762-790`)
  - Done 2026-10-09: an alpha-only transform is the painter's opacity, which scales each shape's own alpha as Flash does (overlapping shapes inside the symbol show through each other). Anything else renders into an offscreen cropped to the instance's device bounds (all frames of all layers, plus 2 px) with the full transform shifted to the crop, so it is sharp at any zoom, keeps the parent transform and is not clipped to the stage
- [x] Tint also blends alpha (`a = it*a + t*tintA`); Flash tint is per-channel
      multiply `1-t` plus offset `t*color` and leaves alpha alone. Brightness is
      done as an HSL lightness shift; it is also multiply+add (b > 0: multiply
      `1-b`, add `255b`; b < 0: multiply `1+b`)
  - Done 2026-10-09, and the arithmetic now runs on straight (unpremultiplied) color so offsets do not brighten translucent edges
- [x] Masks use the mask art's alpha; Flash masks use coverage only, so a
      semi-transparent mask fill should not fade the content. A mask inside a
      color-transformed symbol is sized from the widget `rect()` but drawn into
      the stage-sized image, so it is misaligned
  - Done 2026-10-09: while a mask layer renders (`_maskPass`) every fill and stroke is drawn opaque black and instance alpha is ignored, and the result is thresholded to whole pixels, so a 30% fill masks like a solid one and edges are not anti-aliased, as Flash's are not. The misalignment went with the cropped instance offscreen, whose painter carries the full transform
- [x] Motion tweens lerp matrix entries (`:662`), so rotation shrinks and skews.
      Decompose into translate, rotation, scale and shear, take the shortest
      rotation, and recompose (VectorCraft `doc/src/blend.rs:271-285`). Easing,
      custom ease, rotate-N-times and color-transform tweening are not parsed.
      Elements are paired by index (`:652`)
  - Done 2026-10-09 for the matrix: `blendTransforms` (`src/edit/transform_blend.cpp`, Qt-free, 4 tests) takes each matrix apart into translation, x/y scale and Animate's two skew angles, blends the angles the short way round and a flip as a negative scale, and puts it back together. A 0 to 90 degree tween is at 45 degrees and full size half way
- [x] Motion tween easing (`acceleration`), custom ease curves, `motionTweenRotate` with `motionTweenRotateTimes`, and color-transform tweening are not parsed or applied; elements are still paired by index
  - Done 2026-10-09 for easing and rotation: `acceleration` (and the `<Ease intensity>` that repeats it), `hasCustomEase` with the `<CustomEase>` bezier chain, `motionTweenRotate`/`motionTweenRotateTimes` and `motionTweenSync` are parsed and written back; `easeProgress` (`src/edit/tween_ease.cpp`, tests) blends straight progress with t squared or its mirror, or reads the custom curve; `blendTransforms` turns the way it is told with the extra turns. Also found: `motionTweenSnap` was read and written as `motionTWeenSnap`, so it was always lost; fixed. Checked on screen: eased in, the instance is a quarter of the way at half time; a clockwise one-turn tween is upside down at half time
- [x] Color-transform tweening (alpha, tint, brightness between keyframes) is not interpolated; elements are still paired by index
  - Done 2026-10-10: `blendColorTransforms` (`transform_blend.cpp`, test) blends every amount, multiplier, offset and the tint color, and an instance part way through a classic tween draws with the blend. Checked on screen: an alpha 1 to 0 tween is half transparent half way, a tint to red is half red. Elements are still paired by index
- [x] Shape tweens force-close morph segments (`:101`) and rescale them to the
      lerped keyframe bounds (`:1626-1674`); only fill index 1 is used, gradients
      are not interpolated and strokes vanish mid-tween. Interpolate each morph
      segment and its fill/stroke indices directly; lerp gradients through a
      shared stop set (VectorCraft `doc/src/blend.rs:89-159`)
  - Done 2026-10-10 (`src/edit/shape_tween.cpp`, Qt-free, tests). The rescaling was there to hide a unit error: a MorphShape coordinate is twips over 256 ("#9.61" is 0x961 twips, one fraction digit weighs 1/16), and the parser read it as twips over 20, so the morph came out 12.8 times too small. The parser and writer are fixed (`formatMorphCoordinate`, round-trip tests). `morphedShape` builds the shape at the tween's progress: every point part way between its A and B positions, each taken through its own keyframe's transform to the stage and back through the drawn transform; one edge per segment with `fillIndex1` on its right, `fillIndex2` on its left and the stroke index (zero is none; a file whose segments carry no indices, as puppy.fla's do, takes the first fill and stroke); fills and strokes blended by index between the keyframes (colors, weights, and gradients stop by stop and matrix and all when alike). It draws through the ordinary path machinery, so strokes and every fill show. Checked on screen with shape_tween.fla at frames 0, 3 and 6
- [x] Oval primitives ignore start/end angle and inner radius (`:810`); rectangle
      corner radii are never parsed. See step 12 for the geometry
  - Done 2026-10-09: `ovalPrimitivePath` draws the sector from the start angle clockwise to the end angle with a notch to the center or, with an inner radius, a ring sector; a whole oval with an inner radius is a ring. Rectangle corner radii were already parsed and drawn (`roundedRectanglePath`). The primitive's hit test and bounds still use the whole oval
- [x] Filters are not implemented. Flash blur is an iterated box blur with
      separate blurX/blurY and quality = number of passes: use running-sum box
      blurs (VectorCraft `render/src/fx.rs:203-263`). Drop shadow and glow are the
      alpha silhouette blurred, tinted and offset, cached per object and moved,
      not re-rendered, on pan (`fx.rs:275-330`). Bevel is a shadow plus a
      highlight at opposite offsets; Adjust Color is a 4x5 color matrix
  - Done 2026-10-09: `Filter` (`src/data/filter.h`) is parsed and written for all seven filter elements (`enabled` included); `Filters::apply` (`src/gui/filters.cpp`) runs them in order on the instance's isolated offscreen, which is grown by `Filters::margin`. Blur is a running-sum box blur with quality passes (`src/edit/image_blur.cpp`, Qt-free, tests); drop shadow and glow are the silhouette moved, blurred and strengthened, with inner, knockout and hideObject; bevel is the coverage moved towards and away from the light, blurred, their difference lit or shaded, placed inner, outer or full; Adjust Color is brightness, contrast, saturation and hue as a color matrix. Filters are sized in the parent's pixels (zoom and parents, not the instance's own matrix). Gradient glow and bevel draw with the gradient's last color. Nothing is cached yet: a filtered instance is re-rendered on every paint
- [x] Gradient glow and gradient bevel should map the blurred coverage through their gradient rather than use its last color
  - Done 2026-10-10 (`filters.cpp`): a gradient glow maps its blurred, strengthened coverage through the gradient (ratio 0 at the outer edge, 1 at the object), inner and knockout as a glow; a gradient bevel maps the relief (how much an edge faces the light, -1 to 1) through the gradient with the flat surface at the middle, inner, outer or full as a bevel. Checked on screen with `D:/fla/effects_test2.fla`
- [x] Cache a filtered instance's image between paints and move it on pan rather than re-rendering it (VectorCraft `fx.rs:275-330`)
  - Done 2026-10-10: `_filterCache` keeps the filtered (or color-transformed) offscreen per instance with the transform it was rendered for; a paint that places the instance the same way apart from a shift, on the same frame and document revision, draws the kept image moved by the whole-pixel shift. Entries clipped by the device edge are not reused (a pan would reveal the cut), images over 4 Mpx are not kept, and the cache goes with `clearCaches`
- [x] Blend modes are not implemented. Formulas for multiply, screen, lighten,
      darken, difference, overlay and hard light are in VectorCraft
      `color/src/blend.rs:102-151`; add, subtract and invert are trivial. Layer
      needs an isolated group; Alpha and Erase are `DestinationIn` /
      `DestinationOut` against the parent layer
  - Done 2026-10-09 for multiply, screen, lighten, darken, difference, add, overlay and hardlight through Qt's composition modes on the isolated offscreen; `layer` is the isolation alone. `blendMode` is parsed and written
- [x] Blend modes subtract and invert (no Qt composition mode: composite by hand), and alpha and erase, which need the parent layer isolated so they can cut into it
  - Subtract and invert done 2026-10-10 (`Filters::composite`): invert differences the backdrop with white under the instance's coverage; subtract is invert, add, invert again, so the sum clamps at white and the difference at black. Checked on screen (effects_test2.fla: a blue square over white comes out orange under subtract and black under invert, over the red band dark red and cyan). Alpha and erase still draw plainly: they need the parent layer isolated
- [x] Blend modes alpha and erase, which need the parent layer isolated so they can cut into it
  - Done 2026-10-10: an instance with the Layer blend mode renders into its own offscreen already; while its contents draw, `_layerGroupDepth` says so, and an alpha instance inside composites with DestinationIn (clearing the group outside its image too) and an erase instance with DestinationOut (`Filters::composite`). Outside a Layer group Flash shows nothing of them, and neither does Phoenix. Checked on screen with `D:/fla/blend_layer_test.fla`
- [x] 9-slice scaling and cacheAsBitmap are not implemented
  - 9-slice done 2026-10-10: a symbol's `scaleGridLeft/Top/Right/Bottom` are parsed and written (`Symbol::scale9`); a movie clip instance of it that is scaled but not turned or skewed draws as nine cells, each the symbol drawn again under a clip with that cell's stretch, so the corners keep their size, the edges stretch one way and the middle both (`drawElement`). Instances with a color effect, filter or blend take the plain path through the offscreen and are not sliced. Checked on screen with `D:/fla/scale9_test.fla`. cacheAsBitmap is a runtime rendering hint with no visible effect in the authoring view, so it is out of scope
- [x] Text uses guessed font-name munging, `size*0.75`, one baseline and no line
      layout (`:812-887`)
  - Done 2026-10-09 in `text_layout.cpp`: PostScript names taken apart for family, bold and italic; size in pixels at the screen's dpi; lines, wrapping and per-run styles. See step 14

## 12. Improving the existing tools

### Selection and free transform

- [x] Arrow-key nudge (shift x10). `keyPress` handles only Escape
      (`selection_tool.cpp:236-271`); the merging in `CommandStack` was built for
      this
  - Done; repeated nudges merge into one undo step
- [x] Shift-drag constrains to 45 degrees. A shift-press currently toggles and
      never moves (`:146-152`); defer the toggle to release when nothing was
      dragged (VectorCraft `tools/src/select.rs:251-265, 359-365`)
  - Done; the shift toggle waits for a release without a drag
- [x] Alt-drag duplicates
  - Done for shapes
- [x] Cursor follows what is under the pointer (move, corner, curve, handle
      directions rotated by the box angle, `select.rs:381-453`)
  - Done 2026-10-09 for the free transform tool: move, scale (turned to the handle's axis), skew, rotate and distort cursors; see step 14
- [x] Draw snap feedback: `SnapResult.kind/target` (`snapping.cpp:23-63`) is
      never drawn
  - Done as the snap ring
- [ ] Snap handle drags properly: write the handle as `scale = c + s*e`, solve
      `s` for each target line, reject snaps that flip or collapse the box or
      move the handle more than 2x the tolerance, and prefer point targets
      (VectorCraft `tools/src/guides.rs:565-674`). Also snap to object vertices
      and edges, not only bounds (`guides.rs:429-484`)
- [x] The transformation point: `Element::transformationPoint` exists
      (`data/element.h:30`) but rotation always uses the box center (`:203, :509`)
      and the pivot cannot be dragged. Make it draggable, snap it to anchors and
      the center, and keep it while the selection is unchanged (VectorCraft
      `tools/src/xform/transform.rs:100-110, 190`)
  - Done 2026-10-09: the pivot comes from a single selected element's own transformation point (the box center for several, or a shape whose point was never set), is dragged as its own grip snapping to the box center and handles and then the grid, rides along with every gesture, is written back to the element so the file keeps it, and a double click returns it to the center. Rotation and alt-scaling turn about it. The box is also rebuilt before drawing when it no longer matches the selection (after an undo). It does not yet snap to anchors
- [x] Marquee should test geometry, not bounds (`elementsIn`,
      `phoenix_view.cpp:2045-2090`): reject on bounds; hit if any anchor is
      inside; hit if any segment crosses a rect edge; hit if the path is closed
      and the rect's center is inside it (VectorCraft `geom/src/hit.rs:28-54`).
      Cutting geometry is step 13
  - Done for merge artwork, which is cut and selected by region; objects are still picked by bounds

### Subselection

- [x] Anchor marquee, multi-anchor selection, nudge, and snapping the grabbed
      anchor with the others following (VectorCraft `tools/src/direct.rs:420-440`)
  - Done 2026-10-09 except snapping: the selection is a set of anchors; shift-click adds or removes, a drag on empty stage draws a marquee (shift adds), a drag on a selected anchor moves them all with every coincident anchor, the arrow keys nudge by one screen pixel (ten with shift) as one undo step, and Delete removes them all. Checked on screen
- [ ] `closestPoint` samples 32 points then bisects (`editable_path.cpp:250-290`).
      Solve exactly instead: for a quadratic the nearest point is a cubic
      equation; for a cubic, Newton from the best sample
- [x] Deleting an anchor should refit, not just join the neighbors
      (`editable_path.cpp:383-391`): sample both neighboring segments 16 times,
      keep the outer tangents, golden-section search over the parameter the
      removed point lands on minimizing the single-cubic fit error, and write the
      result into the neighbors' facing handles (VectorCraft
      `pathops/src/edit.rs:234-275`). Deleting a shared corner still has the
      problem the drag had (step 5)
  - Done 2026-10-09: when either side was curved, both segments are sampled 16 times and `CurveFit::fitSingleCubic` fits one cubic with the outer tangents kept (least squares for the handle lengths, then reparameterizing passes), written into the neighbors' facing handles; all-straight sides are joined by a line as before (test). Deleting a shared corner is still the open step 5 item

### Pen

- [x] Clicking the first anchor closes on press (`pen_tool.cpp:98-107`); preview
      the close instead and let the drag shape the closing in-handle as a mirror,
      alt breaking it (VectorCraft `tools/src/pen.rs:152-155, 226-231`)
  - Done 2026-10-09: the press marks the close and the release finishes it; dragging in between sets the first anchor's in-handle to the mirror of the drag, and its out-handle to the drag unless alt is held. The overlay shows the closing segment taking shape
- [x] Clicking the last anchor adds a duplicate; it should retract that anchor's
      out-handle, making it a corner (`pen.rs:157-161`)
  - Done 2026-10-09: the click retracts the out-handle and makes the anchor a corner; dragging on from it pulls a fresh out-handle alone
- [x] Resume an open path by pressing on either end of it, reversing first when
      it is the first anchor (`pen.rs:174-185`; replaces the step 5 item)
  - Done 2026-10-09: with the shape selected, the pen hovers either end of an open path with the plain pen cursor and a press picks the path up (mapped to the stage, reversed when picked up by its first anchor); finishing writes it back to the same edge as one `SetEdgeGeometryCommand`, closing it if the first anchor is clicked
- [x] Shift constrains anchors and handles to 45 degrees
  - Done 2026-10-09: the point's shadow on the nearest 45-degree ray from the last anchor (or the handle's anchor); the rubber band follows
- [x] Snap candidates are gathered at path start only (`:84`), so the path being
      drawn never attracts the cursor
  - Done 2026-10-09: gathered on every press, with the path's own anchors added

### Pencil and curve fitting

- [x] **Corners come out rounded.** Every fitted anchor is smooth
      (`curve_fit.cpp:266`) and every split gets a centered tangent (`:277`). The
      VectorCraft pipeline (`engine/src/cmd/draw2.rs:456-491`,
      `pathops/src/edit.rs:47-104`): drop samples closer than 0.3 x tolerance and
      pin the last to the release point; mark a corner where the turn exceeds
      about 100 degrees (`cos < -0.2`); build a Catmull-Rom spline (handles
      +-(next-prev)/6, none at corners and ends); then simplify: split where
      adjacent tangents turn more than 60 degrees, rotate a closed path to start
      on a corner, resample each run densely, fit it with its own end tangents,
      turn near-straight cubics into lines, and keep the input if the result has
      more anchors
  - Done 2026-10-09 the direct way rather than through a Catmull-Rom spline: `findCorners` marks a point a corner where the direction in and the direction out, each judged over a window of three tolerances (so one jittered sample is not a corner, and nothing within a window of the ends is), differ by more than 60 degrees (`Options::cornerAngle`), keeping the sharpest of a cluster; each run between corners is fitted on its own with its own end tangents and the corner anchor is left a corner; afterwards a cubic whose handles lie on its chord is written as a line. An L drawn freehand gives three anchors with a corner (test); a round bend stays smooth (test). Checked on screen with the pencil
- [x] The two-point base case (`:231-235`) leaves no in-handle and never sets the
      previous out-handle, putting a straight stub mid-stroke. Give it
      tangent-aligned handles of chord/3 (VectorCraft `pathops/src/fit.rs:52-55`)
  - Done 2026-10-09
- [x] One Newton pass, accepted even when worse (`:246-255`), and the result is
      not clamped (`:217`). Run up to 6 passes keeping the best, clamp t to
      [0,1], cap handles at 4x the chord (`fit.rs:77-95, 143, 174`)
  - Done 2026-10-09 (`refit`): up to six passes, each kept only when it lowers the worst error, parameters clamped and the ends pinned, handles capped at four chords
- [x] End tangents come from the first and last two raw samples (`:353-355`), so
      jitter hooks the ends. Estimate from points within a few pixels
  - Done 2026-10-09 (`tangentOver`): the direction to the first sample at least three tolerances away; a straight stroke with a jittered first sample fits as a straight line (test)
- [x] At the depth cap of 12 an out-of-tolerance fit is accepted silently
      (`:15, :257`)
  - Done 2026-10-09: at the cap the run's points are kept as corner anchors, which is within any tolerance, rather than a curve that strays
- [x] Tolerance and minimum spacing are in document units (`curve_fit.h:29,34`,
      `pencil_tool.cpp:81`), so strokes drawn zoomed in are over-smoothed. Work
      in screen pixels
  - Already the case: the pencil and the brush divide their screen-pixel tolerances by the zoom before fitting (`fitOptions`)
- [x] Straighten mode tests only point i against the line to i+1 (`:327-345`), so
      gentle arcs drift. Use Ramer-Douglas-Peucker (VectorCraft
      `pathops/src/edit.rs:107-133`)
  - Done 2026-10-09 (`simplifyRun`); a 90-sample half circle straightened at tolerance 2 stays within it (test)
- [x] Ink mode keeps every mouse sample as a corner (`pencil_tool.cpp:90-96`);
      fit with a small tolerance (about 0.5 px) instead
  - Done: ink mode fits at half a screen pixel
- [x] Close the stroke when its ends finish within about 15 px (or alt on
      release); extend a selected open path when a stroke starts at one of its
      ends; preview the fit live
  - Closing within 15 px done 2026-10-09 (Connect lines, Straighten mode only)
  - Extending and the live preview done 2026-10-09: a stroke that starts within reach of either end of the selected shape's open path is joined onto it on release (the path reversed when it was the first anchor, a corner at the join) as one `SetEdgeGeometryCommand`; while drawing, the fit as it stands is drawn over the faint raw stroke. Alt on release does not close
- [x] Shape recognition for Straighten mode (VectorCraft
      `geom/src/recognize.rs:239-312`): resample to 256 points by arc length; two
      or more sharp reversals is a scribble; a line if the largest deviation from
      the chord is under 8% and the length under 1.3x the chord; closed if the
      end gap is at most 25% of the size; find corners by splitting at the
      farthest point and simplifying each half at 12% of the size; 3 corners is a
      triangle, 4 near-right angles a rectangle (rotation snapped to 45
      degrees), otherwise an ellipse if its area fills its oriented box (from
      second moments) by more than 0.6. Snap to square or circle within 10%

### Primitives

- [x] Rectangle corner radius: the setback at a corner of angle theta is
      `r(1+cos)/sin` (r at 90 degrees), the arc handle `r * 4/3 * tan(theta/4)`,
      and the radius is clamped to half the shorter side (VectorCraft
      `geom/src/corners.rs:65-125`). Replaces the step 4 item
  - Done for the uniform radius a tool sets; per-corner radii on primitives render too
- [x] Oval start/end angle and inner radius: split the sweep into arcs of at most
      90 degrees, each with handle `r * 4/3 * tan(delta/4)`; trace the outer arc,
      then the inner arc reversed (or a line to the center), and close
  - Done 2026-10-09 for rendering (`ovalPrimitivePath`, Qt arcs); the oval tool still has no way to set the angles or inner radius
- [x] PolyStar with shift scales the radius by up to sqrt(2) and snaps the angle,
      because it squares the delta (`primitive_tool.cpp:136-140`). Shift should
      only hold the rotation upright. Arrow keys could change the side count
  - Done 2026-10-09: shift snaps the drag's angle to 45 degree steps and keeps its length as the radius. The arrow keys do not change the side count

## 13. New tools

All the merge-mode tools below are variations of `ShapeMerger::combine`
(`shape_merge.cpp:245-394`): each face's fill comes from the target and
addition maps, plus a rule for which strokes survive.

- [x] **Edge bending** (selection tool dragging an edge; pairs with step 6). At
      press find the nearest parameter t on the edge and clamp it to
      [0.05, 0.95]. For a quadratic move the control point by
      `delta / (2t(1-t))`, for a cubic both inner controls by `delta / (3t(1-t))`,
      which puts the curve exactly under the cursor (VectorCraft
      `engine/src/cmd/draw2.rs:687-717`). Fills follow because they share the
      edge. A straight edge first becomes a quadratic with its control at the
      midpoint. Corner/curve cursor on hover. Ctrl-drag inserting a corner has no
      VectorCraft counterpart
  - Done 2026-10-08 (`edge_bend.h`): drag a segment of an unselected shape to bend it, a corner to move it, ctrl-drag a segment to add a corner. Corner and curve cursors on hover. An unstroked fill's outline can be grabbed from just outside it
- [x] **Region selection** (step 8). A selection is a set of face and edge ids on
      a cached map; re-find them after a rebuild by interior point or by source
      plus parameter. Click: the face containing the point, or the nearest edge
      within tolerance (VectorCraft's Live Paint Selection,
      `tools/src/builder.rs:73-100, 715-765`). Double-click: a face plus its
      boundary edges, or every edge connected through degree-2 vertices with the
      same stroke
  - Done by lifting the region out rather than by holding face ids: see step 8
- [x] **Drag out and ctrl-drag cut.** Build a new shape from the selected faces'
      boundary half-edges (an edge between two selected faces keeps both
      fills). In the source, set those faces to no fill and drop edges left with
      the same fill on both sides -- `subtract` done topologically, with no
      re-intersection. Float the new shape as a pending drawing so
      commit-on-deselect merges it where it lands
  - Done as the natural consequence: dragging a lifted fill leaves the hole and drops it in where it lands
- [x] **Marquee and lasso cutting.** Insert the outline as a stroke-only cutter,
      which already divides without erasing, then select the faces inside. The
      lasso collects points at least 1 px apart and tests with even-odd
      point-in-polygon (VectorCraft `tools/src/xform/wand.rs:46-130`)
  - Done 2026-10-09: `RegionSplitter` arranges the shape together with the area's outline as unstyled curves, paints each divided face by sampling the shape alone, and lifts the faces and strokes inside out as one piece (`selectRegionsIn`). Objects the area touches are selected whole
- [x] **Brush** (paints fills). Fit the centerline as the pencil does, outline it
      with round caps and joins (in Qt, `QPainterPathStroker` then `simplified()`,
      then refit the outline), and merge it. Same-color seams vanish on their
      own. Modes are face rules: Normal = merge; Behind = target fill if any,
      else the addition; Fills = keep every target stroke; Selection = selected
      faces only; Inside = only faces in the target face under the press point.
      Variable width / pressure: flatten, offset each sample by
      `+-(w/2) * profile(t)` along the normal, miter or round the outer joins,
      join the sides with caps, and fill the ring non-zero (VectorCraft
      `effects/src/stroke/width.rs:158-217`)
  - Done 2026-10-09 (`brush_tools.cpp`, B): the fitted centerline is stroked with round caps and joins, resolved with `simplified()`, and each loop written with the fill on the side its direction implies. The modes done 2026-10-09 as a Brush mode field in Properties: Paint Fills merges with `MergeOptions::keepStrokes`, so lines survive under the paint (the pending drop carries the flag through to the merge on deselect); Paint Behind, Paint Selection and Paint Inside cut the stroke down first by arranging it with the outlines of the shapes under it (`RegionSplitter` with cutters) and keeping the regions where nothing is painted, where a selected fill is, or where the face the stroke began in is; verified on screen. Pressure done 2026-10-09: with Use pressure on, a tablet's pressure (read from Qt's tablet events, which still arrive as mouse moves) sets the width along the stroke, the outline built by offsetting the fitted centerline to either side and joining the sides with round caps; a mouse paints at full width. Not verifiable here without a tablet. The resolved outline is refitted with the curve fitter before it becomes edges (2026-10-09), so a stroke lands as a few cubics per loop rather than the hundreds of straight pieces the stroker and `simplified()` leave; a wavy stroke with ten corners saved as one edge of 47 cubics
- [ ] **Pen input still missing.** Confirm on real hardware that Qt delivers
      tablet events to the stage (a driver that reports as a mouse gives a
      constant width). Tilt: Animate's Use Tilt rotates a non-round tip, so it
      needs brush tip shapes first (round only today). Pressure does not reach
      the pencil, eraser or paint brush, and nothing varies with pressure but
      width -- which matches Animate's classic brush. Pressure is sampled per
      mouse move, so a fast stroke gets few samples; interpolation between them
      is linear along the raw polyline
- [x] **Eraser.** Normal = `ShapeMerger::subtract` with the brush outline; Fills =
      keep all target strokes; Lines = keep fills, drop strokes inside;
      Selected Fills / Inside restrict as for the brush; Faucet clears the
      connected same-fill faces. For object-drawing strokes: sample each segment
      24 times against "inside the brush", bisect each in/out change 30 times,
      cut there, and rejoin a closed path's first and last runs (VectorCraft
      `engine/src/cmd/draw2.rs:329-441`)
  - Done (E) in normal mode through `SubtractShapeCommand`. The modes done 2026-10-09 as an Eraser mode field: Erase Fills subtracts with strokes kept, Erase Lines is a new `ShapeMerger::eraseStrokes` (every fill stays, strokes inside the cutter go), Erase Selected Fills works on the lifted selection only, Erase Inside cuts the pass down to the face it began in; the Faucet checkbox makes a click remove the fill region or the stroke under it whole, with its own cursor. Verified on screen. Drawing objects lose what the eraser covers too, fills and strokes alike, and stay objects (done 2026-10-09, verified on screen)
- [x] **Paint bucket.** Arrange the merge shapes under the click with strokes as
      cutting edges, take the face under the click, flood across edges with no
      stroke and the same fill on both sides, set the fill and rebuild. Gap
      closing (VectorCraft has none): find degree-1 vertices (dangling stroke
      ends), join each to the nearest endpoint or curve point within the gap
      size (about 3 / 8 / 20 screen pixels for small / medium / large), add the
      joins as invisible edges, rebuild, and fill only if the clicked face is now
      bounded. Keep the joins as fill boundaries without a stroke
  - Done 2026-10-09 (`paint_tools.cpp`, K): paints the face under the click, enclosed empty areas included, through `RegionSplitter::withFaceFill`. Gap closing done 2026-10-09: when the click lands in no bounded face, `RegionSplitter::gapJoins` joins each loose stroke end to the nearest other loose end, or to the nearest point on another piece, within the gap (3, 8 or 20 screen pixels for the Gap size setting's small, medium, large), the shape is arranged again with the joins as cutters, and the face is filled only if it is bounded now; the joins stay as fill boundaries without a stroke
- [x] **Ink bottle.** On a fill: flood its connected same-fill faces and give the
      new stroke to every boundary half-edge whose twin's face has a different
      fill. On a stroke: restyle the strokes connected through degree-2
      vertices. `rebuildShapeEdges` takes strokes from `sources`
      (`shape_geometry.cpp:162`), so it needs a per-half-edge stroke override
  - Done (S): strokes the outline of the fill under the click, or restyles the stroke clicked, through a per-piece `StrokeOverride` on `rebuildShapeEdges`. Does not yet flood across same-fill neighbors
- [x] **Convert Lines to Fills** (stroke to outline): stroke each chain, then
      union the outlines under non-zero. Offsetting (Expand Fill) arranges the
      fill, a ring stroked at width 2d, and open strokes at 2d: growing keeps
      their union, insetting keeps fill minus ring, and contours closer than d to
      the source are dropped to remove the stroker's inverted loops (VectorCraft
      `pathops/src/offset.rs`)
  - Done 2026-10-09 for Convert Lines to Fills (Modify > Shape, `lines_to_fills.cpp`): the stroked pieces of each stroke style are stroked together with the style's width, caps and joins, the outline is unioned under non-zero and each loop refitted into cubics, the strokes are dropped and the new fills merged over the shape, one undo step per shape. Expand Fill / Soften Fill Edges are not done
- [x] **Expand Fill / Soften Fill Edges**: offset the fill by arranging it with a ring stroked at width 2d (growing keeps the union, insetting keeps fill minus ring), dropping contours closer than d to the source (VectorCraft `pathops/src/offset.rs`)
  - Done 2026-10-10 (Modify > Shape > Expand Fill... and Soften Fill Edges..., `src/gui/fill_offset.cpp`). Each fill's region is taken from the stage's own stitching (`PhoenixView::fillRegions`), its outline stroked at twice the distance, and the ring's loops sorted by side: the loops outside the fill bound the grown fill, the loops inside (reversed) bound the shrunk one. No Qt area subtraction is used, since `QPainterPath::subtracted` handed back the wrong operand for these inputs. Soften makes a core at full color (the fill, or the fill shrunk by the whole distance) and then one band per step between successive offsets, each a ring (outer loop plus the previous loop reversed as a hole) with its alpha one step fainter; fills that are not solid are offset whole. The fills are stripped and the pieces merged under the strokes. The outline builder (`src/gui/shape_outline.cpp`, shared with Convert Lines to Fills) now fills a loop on its outside when it runs against the biggest loop, so holes are holes, and fills a bare polygon in with points every two units before fitting so a plain rectangle keeps its corners. The dialogs offer distance, steps and direction and remember them. Checked on screen on rect_2.fla: expand 20 grew the L-shaped region with rounded corners, inset 20 shrank it, soften 30 in 6 steps gave a fade outward (sampled 219, 146, 108, 73, 36 then full color across the edge) and inward
  - Rough edge: while a lifted region is still selected, the stage shows it as it was lifted, so the result of a Modify > Shape command on it appears once it is deselected
- [x] **Modify > Shape > Smooth / Straighten / Optimize.** Smooth pulls handles
      Catmull-Rom style (`+-(next-prev)/|next-prev| * d/3`) blended by an amount
      (VectorCraft `pathops/src/edit.rs:137-163`); Straighten is RDP; Optimize is
      the pencil's simplify. A smooth brush refits only the anchors inside it
      (`engine/src/cmd/draw2.rs:875-995`)
  - Done 2026-10-09 (`src/edit/path_tidy.cpp`, Qt-free, tests): Smooth blends every handle half way to the Catmull-Rom tangent, Straighten samples the path and reduces it with Ramer-Douglas-Peucker at two screen pixels, Optimize asks for a strength and refits the sampled path with the pencil's fitter, keeping the original when the fit would not be leaner. Each is one undo step per selected shape. The smooth brush is not done
- [ ] **Magic wand** for bitmaps broken apart is Animate's; VectorCraft's matches
      objects by color distance (default 32), stroke weight and opacity
      (`engine/src/cmd/wand.rs:104-161`) -- useful only as a "Select Same"

### No VectorCraft counterpart

MorphShape semantics, ragged/stipple/hatched strokes, 9-slice, the stipple
display for a selected fill, paint bucket gap closing, ctrl-drag to insert a
corner, and the lasso cutting fills all need designs of their own.

### Suggested order

1. Step 9 -- small fixes that remove the worst "the editor is broken" moments
2. Step 10 attribution, vertices and edge ordering, then render from the map
3. Edge bending and region selection
4. Pencil fitting with corners, then brush, eraser and paint bucket
5. Rendering fidelity and culling alongside

## 14. Authenticity review -- what still does not look or feel like Animate

From a second review on 2026-10-08, this time measured against Animate itself
rather than VectorCraft. Several step 9-13 items would push Phoenix toward
Illustrator; the notes below say where. Items marked **(confirmed)** were checked
in the code or against a real XFL document.

### Rendering

- [x] **(confirmed) Default caps and joins.** Only dotted strokes set round caps
      (`phoenix_view.cpp:955`); everything else gets Qt's square caps and bevel
      joins. Flash defaults to round caps and round joins. `caps`, `joints`,
      `miterLimit` and `pixelHinting` are never parsed, so explicit settings are
      lost too
  - Done 2026-10-08: `caps`, `joints`, `miterLimit` and `pixelHinting` are parsed and written; the pen defaults to round/round
- [x] **(confirmed) Hairlines.** Animate writes `solidStyle="hairline"` with no
      weight attribute, so the parser gives hairlines weight 1 and they scale
      with zoom. Read the attribute and draw a cosmetic 1 device pixel pen
  - Done: `solidStyle` is parsed and written; a hairline is a cosmetic zero-width pen
- [x] **(confirmed) Strokes are drawn per edge piece** (`:1580`), so merged
      artwork gets a cap at every vertex and corners never join. Chain same-style
      pieces end to end before stroking; independent of the map rewrite
  - Done: pieces of one style are chained end to end in `buildShapePaths`, and a run that returns to its start is closed
- [x] **Dashed and dotted strokes** ignore `dash1`/`dash2` and `dotSpace`
  - Done for dashed and dotted; ragged and stipple still use Qt patterns
- [x] **Selection highlight color.** Animate's default highlight color is the
      layer's outline color (Preferences > General > Highlight color > Use
      layer color). `Layer::color` is parsed and never used by the overlay,
      which draws a fixed cyan. Drawing objects, groups and symbols get a thin
      rectangle with no corner grips; the grips in `drawToolOverlay` (`:2349`)
      read as a transform box
  - Done: highlights use the layer color; objects get a thin box with no grips, the free transform tool draws its own box
- [x] **Selected merge shapes** show a dot stipple over the fill and along the
      stroke, not a bounding box. Falls out of region selection (step 8), but
      can be drawn at shape level now
  - Done at shape level (`drawShapeHighlight`): a 2 px dot screen in device pixels over fills and strokes
- [x] **(confirmed) Drawing objects do not exist in the model.** Animate writes
      an object-mode rectangle as `<DOMShape isDrawingObject="true">`, never as
      `DOMRectangleObject`; the primitive objects come only from the Rectangle
      Primitive and Oval Primitive tools, which are separate tools with live
      corner-radius handles. Nothing reads `isDrawingObject`, so a loaded
      drawing object is treated as merge artwork and will merge when moved
  - Done: `Shape::isDrawingObject` is read, written and set by Object Drawing mode; drawing objects never merge. Rectangle Primitive and Oval Primitive are separate tools now
- [x] `drawOverlayPoints` (`:1071`) draws cyan circles on every control point of
      a tree-selected shape. Useful for inspection; should be a debug toggle
  - Done: only with Show Bounding Boxes
- [x] **Timeline panel.** Keyframes are filled dots, empty keyframes hollow dots,
      spans gray with a hollow rectangle on the last frame, tween spans tinted
      with an arrow, the playhead red, frame numbers every five on the ruler
  - Done: dots, spans, tween tints and arrows, red playhead, 1-based ruler
- [x] **Layers panel.** Eye, lock and outline-color swatch columns; the current
      layer highlighted with a pencil icon. Currently one eye column and a
      "(Hidden)" suffix on the name
  - Done: eye, lock and color columns; clicking a name makes the layer current, which `activeFrame` follows
- [x] Text: `size * 0.75` is the right px-to-pt conversion at 96 dpi but
      `setPixelSize` would be exact at any dpi
  - Done 2026-10-09 in `text_layout.cpp`: the point size is `size * 72 / dpi` of the screen, so a 12 px run draws 12 px at any dpi. Text is laid out properly now too: a 2 px gutter, lines broken at returns, fixed-width boxes wrapped at spaces, each run in its own font and color, bounds measured from the layout rather than a fixed 200x50. Face names are taken apart the PostScript way (ArialMT, TimesNewRomanPS-BoldItalicMT) for family, bold and italic

### Interaction

- [x] **Subselection anchors.** Animate draws every anchor as a small hollow
      square, filled when selected; it does not show smooth versus corner by
      shape. There is no double-click-to-convert: Alt-drag an anchor with the
      subselection tool pulls handles out of a corner point. Alt-click to insert
      an anchor is not Animate's either -- anchors are added by clicking a
      segment with the pen or the Add Anchor Point tool, and removed by the
      Delete Anchor Point tool or by hovering an anchor with the pen
  - Done: hollow squares, filled when selected; alt-drag a corner pulls out handles, alt-click a curve point makes it a corner. The double-click convert and the alt-click insert are gone; the pen adds and removes anchors
- [x] **Pen cursors.** x for a new path, o for closing, + and - over segments and
      anchors, ^ for convert. The plain crosshair cannot show any of this
  - Done: x, o, + and -. With no path started the pen hovers the selected shape: a click on an anchor removes it, on a segment adds one. ^ (convert) is drawn but not wired; the subselection tool's alt-drag converts instead
- [x] **Snap ring**, not guide lines: a small circle under the cursor while snap
      to objects is on, which grows when the drag locks onto a target
  - Done for the selection and shape tools
- [x] **Selection cursors.** Arrow with a small box (move), corner (bend a
      corner) or arc (bend a curve). Belongs with edge bending as a first-class
      item, since the hover state is how the merge model is discoverable
  - Done: move box, corner and arc, see step 13
- [x] **Pencil smoothing** is a 0-100 value applied in screen space, not a
      document-unit tolerance. Straighten mode depends on the "connect lines"
      and "recognize shapes" drawing settings
  - Done: 0-100 in screen pixels; ink mode fits at half a pixel. Connect lines and Recognize shapes done 2026-10-09 as checkboxes, for Straighten mode (`shape_recognition.cpp`): a stroke whose ends come within 10 screen pixels is closed, the seam anchor dropped; a closed stroke with four square corners near the axes becomes the exact rectangle, and a roundish one (many short sides, every raw point about as far from the middle) becomes the oval through its extent. Verified on screen
- [x] **Free transform** has Distort (ctrl-drag a corner) and Envelope modes for
      merge shapes. Handles are hollow white squares, the transform point a
      white circle, and the cursor changes to rotate, skew and scale arrows by
      region
  - Handles and transform point done. Cursors done 2026-10-09: a move box inside, a double arrow on a handle turned to the handle's axis (so it follows a rotated box), skew arrows along an edge, a curling arrow in the rotate band outside a corner; shown on hover and held through the drag (`cursors.cpp`, `free_transform_tool.cpp`). Distort done 2026-10-09: ctrl-drag a corner handle with only shapes selected and the corner moves on its own, the box becoming a quadrilateral, and every point of the geometry is warped by the bilinear map from the starting box to the new one (`warpShape` in `shape_transform.cpp`); ctrl+shift tapers by moving the neighboring corner the opposite way along their edge; one undo step per shape, Escape abandons, verified on screen. Ctrl belongs to the free transform tool now rather than calling up the temporary arrow (`Tool::usesControlKey`). Envelope done 2026-10-09: a Transform setting in Properties (Free Transform, Distort, Envelope); Envelope puts eight anchors round the box, a corner and a midpoint on each side, each with a handle towards either neighbor, and the shapes are warped from the straight box the session began with onto the Coons patch those eight cubics make; anchors carry their handles, handles bend the sides, one undo step per drag, and the session lasts until the selection or the box changes. Straight runs are cut into short cubics first (`prepareForWarp`) so they can bend, for Distort too. Distort has its own cursor now, an arrow with a leaning box, shown with ctrl held or the Distort option on
- [x] **Rectangle tool** changes the corner radius with Up and Down during the
      drag
  - Done: Up and Down during the drag (shift x10), plus a Corner radius field in Properties. Rectangle primitives carry per-corner radii, which are now parsed, written and drawn
- [x] Ctrl-drag on an unselected edge adds a corner; a plain drag of a selected
      region already cuts it out. The step 8 "ctrl-dragging should cut" item is
      really "ctrl is the temporary arrow tool", which exists
  - Done: the selection tool's ctrl-drag on an outline makes a new corner (`selection_tool.cpp`)

### Missing basics

- [x] **(confirmed) Delete does nothing** with an object selected; only the
      subselection tool handles the key, for anchors
  - Done: Delete and Backspace on the stage, Edit > Clear in the menu
- [x] Plain wheel scrolls the stage, shift-wheel scrolls sideways, ctrl-wheel
      zooms. Phoenix zooms on the plain wheel by 1.15 and has no keyboard zoom.
      Animate: ctrl+= / ctrl+- zoom, ctrl+1 100%, ctrl+2 fit frame, ctrl+3 show
      all, ctrl+4 400%, ctrl+8 800%
  - Done, with View > Magnification carrying the presets
- [x] Space held pans; Hand (H) and Zoom (Z) tools. Zoom clicks in by 2x at the
      point, alt-click out, drag a marquee to zoom to it
  - Done
- [x] Lasso (L), Eyedropper (I) and Text (T) tools
  - Eyedropper done (I): samples a solid fill or stroke and hands over to the bucket or the bottle. Lasso done (L): cuts merge artwork along the loop and takes what is inside, shift adds. Text done 2026-10-09 (T): a click opens an auto-expanding box, a drag a fixed-width one that wraps; typing, Return, Backspace, Delete, arrows, Home and End; Escape, a click elsewhere or a change of tool commits as one undo step (Add for a new text, Edit Text for a changed one, Delete Text for one emptied); clicking an existing text with the tool edits it in place with the caret at the click. The box is drawn as Animate draws it, a thin outline with a round handle for a growing box and a square one for a fixed width. Font and size sit in Properties, the color is the fill color, and the face is written as a PostScript name (ArialMT). Single-key tool shortcuts are held off while a box is open. Verified on screen and in a saved file (DOMStaticText with &#13; line breaks). Range selection, per-character styling, the box handle and the three text types done 2026-10-09: drag, shift with the arrow keys, a double-click on a word or ctrl+A select a range, drawn as a translucent band; typing replaces it; the font, size and fill color in Properties restyle the selection, splitting the runs where needed and joining them again where they match; dragging the box's handle sets a fixed width (and double-clicking it lets the box grow again); a Text type field makes a field static, dynamic or input, which is written as DOMStaticText, DOMDynamicText or DOMInputText with instanceName and lineType carried through, and a dynamic or input field shows a dotted outline on the stage. Verified on screen
- [x] Edit menu: Cut, Copy, Paste, Paste in Place (ctrl+shift+V), Duplicate
      (ctrl+D), Group (ctrl+G), Break Apart (ctrl+B), Convert to Symbol (F8),
      Arrange (ctrl+up/down)
  - Cut, Copy, Paste in Center, Paste in Place and Duplicate done for shapes. Group (ctrl+G), Ungroup, Break Apart (ctrl+B, groups and drawing objects) and Arrange (ctrl+up/down, shift for front/back) done 2026-10-09 in a Modify menu; a group is picked as a whole and moves by its members. Convert to Symbol (F8) done 2026-10-09: the selection moves into a new library symbol with its registration at the selection's top left and an instance takes its place, undoable. The Convert to Symbol dialog done 2026-10-09: name (offered the next free "Symbol N"), type (Movie Clip, Button, Graphic) and a three-by-three registration grid like Animate's; type and registration are remembered between uses, a blank or clashing name is refused. Converting a region lifted out of a shape works too (verified on screen 2026-10-09: F8 on a selected fill, edit in place, save, reopen; the saved file carries LIBRARY/Symbol 1.xml and a DOMSymbolInstance). Grouping and converting no longer flush a lifted region back into its source first, and the command stack now reports isExecuting() so selection observers ignore changes a command makes while it runs. Break Apart on a symbol instance done 2026-10-09: the instance is replaced by deep copies of what its symbol shows (the first frame for a movie clip or button, the instance's first frame for a graphic), gathered bottom layer first, each carrying the instance's transform; copied merge artwork floats selected and settles into the artwork underneath on deselect, drawing objects and nested instances keep to themselves; undoable, verified on screen. The instance's color effect is baked into the copies' colors on 2026-10-09 (tint, brightness, the advanced multipliers and offsets, on solid fills, gradient stops, strokes and text)
- [x] Arrow-key nudge (shift x10), shift-drag constrain, alt-drag duplicate --
      also in step 12
  - Done: nudge, shift-drag constrains a move to 45 degree steps, alt-drag moves a copy (`selection_tool.cpp`)
- [x] The Properties panel is one long form of spin boxes for every tool at
      once. Animate's inspector has Tool, Object, Frame and Doc tabs, collapsible
      sections (Position and Size, Color and Style, Shape Options...) and
      hot-text fields that scrub when dragged and open for typing when clicked
  - Done 2026-10-10 (`src/gui/properties_panel.cpp`, `src/gui/hot_text.cpp`). Tabs: Tool shows the active tool's icon and name with only its own rows (Color and Style for the drawing tools, Tool Options for the rest); Object shows the selection (Shape, Drawing Object, Rectangle or Oval Primitive, Graphic, Movie Clip or Button with an instance name field, Text, Group, Bitmap, or a mixed selection) with Position and Size (W, H with a lock-aspect chain, X, Y, applied live and recorded as one Move or Resize step), a shape's first solid fill and stroke (color, alpha, weight; applied as a Fill Color, Fill Alpha, Stroke Color or Stroke Size step), Smoothen and Straighten buttons, an instance's Color Effect (None, Brightness, Tint, Alpha, Advanced), Blending, Looping (options and first frame) and its filter list, a text's face, size, color and type; Frame shows the span under the playhead on the current layer with Label (name, type), Tweening (tween, ease, rotate and times, snap, sync) and the span's facts; Doc shows stage size, frame rate and stage color. Every Object, Frame and Doc edit is an undo step (`SetPropertyCommand` and `SetColorCommand` in `src/edit/property_command.h`, tests). The panel follows the selection as Animate's does: an object shows Object, nothing selected shows Doc with the arrow or Tool with another tool, a tool change shows Tool, a frame click shows Frame. Frame labels (`name`, `labelType`) and instance names are now parsed and written. Clicking a frame cell in the timeline makes its layer current. Hot text: drag to scrub (shift x10, ctrl x0.1), click to type, Return commits, Escape cancels. Checked on screen on effects_test.fla: dragging X moved the instance, typing W resized it with the aspect kept, Undo named "Resize", the Frame tab showed a classic tween's ease and rotation, and the tool tabs for the rectangle, brush and oval primitive showed their rows

### Panels and toolbar

Added 2026-10-10: the toolbox, the layers list and the timeline strip were
laid out for convenience rather than to match Animate.

- [x] **Toolbox.** Animate docks its tools in one vertical column at the left
      of the stage, in groups: selection and subselection, free transform,
      lasso; pen, text, line, the shape tools together (rectangle, oval, the
      two primitives, polystar), pencil, brush; paint bucket with the ink
      bottle, eyedropper, eraser; hand, zoom. A slot whose siblings are hidden
      shows a small triangle, a long press opens them and the chosen sibling
      takes the slot. Under the tools sit the stroke and fill swatches and the
      options of the active tool. Phoenix has a horizontal bar across the top
      with every tool in a row and the swatches only in Properties
  - Done 2026-10-10: a vertical toolbar at the left (`setupToolBar`), one slot per tool or group; a group slot is a QToolButton with a delayed flyout (rectangle, oval, rectangle primitive, oval primitive, polystar; paint bucket with ink bottle) and the chosen tool takes the slot, as a long press does in Animate. Every tool keeps its key through the window's action list. The stroke and fill swatches sit under the tools and share the style with the Properties panel; Object Drawing (J) sits last. Checked on screen: the flyout opens on a long press and picking Oval puts it in the slot
- [x] **Layers list header and buttons.** Animate's list has a header strip
      whose eye, padlock and outline square toggle every layer at once, a
      column for each under it, a pencil marker on the current layer, and
      under the list New Layer, New Folder and Delete buttons. Names are
      edited by double-click. Phoenix's header is empty and layers can only
      be hidden, locked and made current
  - Done 2026-10-10 (`layers_panel.cpp`, rewritten with drawn glyphs): the header's eye, padlock and outline square toggle every layer, a pencil marks the current layer, the color square toggles outline view, double-click edits a name, and New Layer, New Folder and Delete sit under the list. Adding and deleting go through `AddLayerCommand` and `RemoveLayerCommand` (`src/edit/layer_commands.cpp`, tests): a new layer is named Layer_N with the next of Animate's layer colors, spans the longest layer and goes above the current one; deleting a folder takes its contents and renumbers the folder links; a rename is a Rename Layer step that also clears autoNamed. The list rebuilds only when it no longer matches the document (so an undo shows), keeping which folders are open. A timeline keeps at least one layer. Checked on screen on effects_test.fla: new layer, new folder, rename, delete, two undos
  - Regression fixed 2026-10-10: opening a second document crashed, since releasing the old one dropped the undo history, whose change hook had the layer list refresh from a document already freed. The timeline view and the tree now let go of the document before it is released, and the grid gets its document before the list hands it rows
- [x] **Outline mode.** The color square on a layer toggles outline view: the
      layer's artwork draws as thin lines in the layer color with no fills,
      and the square goes hollow. DOMLayer's `outline` attribute is neither
      parsed nor written
  - Done 2026-10-10: `Layer::outline` is parsed and written; while such a layer draws, `PhoenixView::_outlineColor` holds its color and every fill and stroke is drawn as a hairline in it, text and bitmaps as their boxes, and instances skip their color effects, filters and blends so the lines stay visible. Masks are never outlined. Checked on screen: the band row drew as red outlines
- [x] **Timeline footer.** Under Animate's frames sit the current frame
      number, the frame rate and the elapsed time beside the transport and
      onion skin buttons; Phoenix shows only the transport buttons

- [x] **Collapsible outliner.** The document tree at the left of the stage
      takes space Animate does not give it; it should fold away
  - Done 2026-10-10: a fold arrow at the left of the breadcrumb row, View > Outliner (ctrl+alt+O) and dragging the splitter to the edge all hide it; the arrow or the menu brings it back at the width it had. Whether it is shown and its width are remembered between sessions

- [x] **New Document dialog.** File > New made a 1920x1080 document at
      24 fps with no choice; Animate asks, with presets by purpose
  - Done 2026-10-10 (`src/gui/new_document_dialog.cpp`): categories down the left (Character Animation, Social, Game, Education, Ads, Web, Advanced) with Animate's presets as tiles drawn in the stage's shape (Full HD, HD, 4K, Standard, Instagram, Story, ad sizes, PAL, NTSC, 23.976 film and more), and details on the right: width, height, a frame rate list that also takes a typed rate, and the stage color. Picking a tile fills in the details; editing them shows Custom; double-clicking a tile creates straight away. The last choice is remembered, and the new stage is fitted to the view. Checked on screen: Social > Instagram Story, then Advanced > 4K UHD created a 3840 x 2160 document at 30 fps
  - Done 2026-10-11: the dialog opens on the last document created (category, size, frame rate and stage color); the first time, it opens on HD 1080p (1920x1080 at 30 fps) under Advanced rather than Character Animation's Full HD at 24 fps. Checked on screen with the saved settings cleared and with them in place

### Timeline editing, onion skin, library

Added 2026-10-10: the panels looked like Animate's but the timeline could
not be edited, frames were only made as a side effect of drawing, and there
was no library or onion skin.

- [x] **Frame commands.** Insert Frame (F5), Insert Keyframe (F6), Insert
      Blank Keyframe (F7), Remove Frames (shift+F5), Clear Keyframe
      (shift+F6), Create Classic Tween and Remove Tween; frame selection in
      the grid (click, drag a block across layers, shift-click to extend);
      dragging a keyframe; a right-click menu on the frames
  - Done 2026-10-10: the operations are Qt-free in `src/edit/timeline_edit.cpp` (tests in `test_timeline_edit.cpp`) and run through `EditFramesCommand`, which records each layer's frames before and after and owns the frames taken off, so any edit is one undo step. The Insert menu holds Timeline > Frame/Keyframe/Blank Keyframe and Classic Tween; Edit > Timeline holds Remove Frames, Clear Keyframe and Remove Tween; the same actions sit on the frames' right-click menu. A command works on the selected block of frames, else the current layer at the playhead. A selected keyframe is dragged to move it, staying between its neighbors; the last keyframe keeps its length. Checked on screen on rect_2.fla: F5 at frame 10, F6 at 5, a classic tween, the keyframe dragged to 7, two undos, and frames 3 to 7 removed
  - Not done: Create Shape Tween, since a shape tween needs the MorphShape Animate computes and Phoenix only reads; dragging a block of several frames
  - Copy, Cut and Paste Frames done 2026-10-10 (ctrl+alt+C/X/V, in Edit > Timeline and the frames menu): copying takes each selected layer's spans cut to the range, deep copies with their elements, tweens, shape tween data and scripts (`timeline::cloneFrame`, `copyFrames`); pasting at the selection's first frame replaces as many frames as were copied, a span running past them keeping its remaining frames, layers filled top down from the first selected one. Cut removes the frames after copying. Tests in `test_timeline_edit.cpp`; checked on screen on rect_2.fla
- [x] **Onion skin.** The onion skin buttons beside the transport and the
      range markers in the ruler; the frames either side of the playhead
      drawn faded (Animate tints before and after differently)
  - Done 2026-10-10: a toggle beside the transport and View > Onion Skin (alt+shift+O), kept in step; two frames before and two after are drawn under the playhead's frame, each rendered alone into an image (so its own shapes do not darken each other), washed blue before and green after, and faded more the further away it is. The renderer draws them through a frame override (`PhoenixView::shownFrame`), which also keeps them out of the filtered-instance cache. Locked layers are left out, as in Animate. The ruler brackets the range. Checked on screen on effects_test.fla (the spin, linear and eased tweens at frame 4) and the Alvin file at frame 36
  - Not done: dragging the range markers, and Animate's outline onion mode
- [x] **Library panel.** A panel listing the symbols and bitmaps in their
      folders; dragging an item to the stage places an instance, and
      double-clicking a symbol edits it
  - Done 2026-10-10 (`src/gui/library_panel.cpp`): a Library dock in a tab beside Properties (the right dock's tabs now sit on top, as in Animate) lists the document's folders, symbols and bitmaps with their kind and a count, folders kept open across rebuilds and the list rebuilt when an edit changes the library (Convert to Symbol). Dragging an item onto the stage places an instance centered on the drop in the active frame, one Add Instance undo step, through `placeDrawnElement` so it gets a keyframe where needed and is selected. Double-clicking a symbol edits it on its own: a scope with no instance, the document put away and the symbol's registration point at the stage's center (`PhoenixView::editSymbol`). A symbol's own type (`symbolType` on DOMSymbolItem) is now parsed and written, so the library and new instances show it. Checked on screen: scale9_test.fla's two movie clips listed, Panel9 dragged to the stage and double-clicked to edit, and the Alvin file's 96 items in their folders
  - Preview pane done 2026-10-10: the selected symbol's first frame (or the bitmap) drawn at the top of the panel on the stage color, fitted and never enlarged past twice its size (`PhoenixView::libraryPreview`). Checked on the Alvin file's symbols
  - Item menu done 2026-10-10 (right-click in the library): Rename edits a symbol's name in place and renames every instance of it (`RenameSymbolCommand`), refused with a message when the name is taken; Duplicate copies the symbol deeply as "name copy" (`library::cloneSymbol`, `AddSymbolCommand`); Delete takes out a symbol with no instances (`RemoveSymbolCommand`) and says how often a used one is used instead; New Folder adds "Untitled Folder N" inside the folder clicked (`AddFolderCommand`). All undoable, Qt-free in `src/edit/library_commands.cpp` with tests. A rename asked for from inside the tree's edit is deferred until the editor closes, since the rebuild it causes deleted the item mid-edit and crashed. Checked on screen on scale9_test.fla
  - Not done: renaming folders and bitmaps, moving items between folders by dragging, deleting a used symbol along with its instances

### Suggested order, weighted for authenticity

1. Caps, joins, hairlines, stroke chaining
2. Highlights in the layer color, the stipple, the snap ring, cursors
3. Delete, scroll and zoom keys, Hand and Zoom tools, the drawing-object flag
4. Step 9 fixes and the attribution rewrite, which region selection needs
5. Edge bending and region selection -- the one change that makes merge drawing
   feel like Animate's

## Performance

Measured on `COM01_034_alvin_and_the_monks_v2.fla` (1260 symbol instances),
dragging, at a maximized window on a 3856x1568 display:

| build | per frame |
| --- | --- |
| Debug, before | ~270 ms |
| Release, before | 37 ms |
| Release, after | ~5 ms while dragging |

  - Done 2026-10-10: the transport strip shows the current frame, the frame rate and the time into the animation at its left, as Animate's footer does
- [x] Stop supersampling while a gesture is running. High quality rendering
      draws the stage into an offscreen image at 2x in each direction, which at
      that window size is a 97 MB buffer allocated, filled and scaled down for
      **every frame**. It accounted for about 84% of the paint cost. It is now
      dropped for the duration of a gesture and the sharp frame is drawn once
      the gesture ends, which is what Animate does
  - A wheel gesture has no release to end it, so a 120 ms settle timer does
      instead
- [ ] Debug builds are ~7x slower than Release. Worth knowing before reading
      anything into a timing taken from the default build
- [x] The whole stage is redrawn on every mouse move, and nothing caches the
      static part of the scene, so a drag repaints 1260 instances to move one
      thing. Caching everything except the moving object is the next real win:
      render the static layers into an image once per (document revision, zoom,
      center, size) and draw only the moving object and overlays on top
  - Done 2026-10-10: while a gesture is on and something is selected (or a drawing is still floating), `paintEvent` splits the stage around the moving elements. Every visible element of the edited timeline's own frames is numbered in drawing order (`DrawPhase` in `drawFrame`; symbol insides and mask coverage are never split); a Scan pass finds where the moving ones fall, then what stacks below them is rendered into `_belowImage` (with the stage background) and what stacks above into `_aboveImage` (with the border), keyed on zoom, pan, size, frame, scope depth, grid and `_revision` (bumped by `clearCaches`). Each drag frame draws the below image, the moving elements and anything stacked between them live, the above image, then the overlay. An element above the moving ones whose blend mode (or one inside its symbol or group) needs the backdrop is drawn live too, since what it blends with moves (`needsBackdrop`). Checked on the Alvin corpus file at frame 40: drag frames took ~10 ms against ~17 ms for a full non-supersampled paint and 70-100 ms for the supersampled settle (Debug build); stacking matches mid-drag and after release, and the effects file keeps its blend modes and masks mid-drag. A move only calls `invalidateBounds`, so the images survive the drag; free-transform's envelope warp calls `clearCaches` per move and so rebuilds them each time
- [x] **There is no culling.** `_visibleRect` is computed (`phoenix_view.cpp:311`)
      but nothing reads it, so `drawElement` draws everything. Skip an element
      whose cached bounds, padded by 2 px, miss the view, and skip leaves smaller
      than a quarter pixel. `getElementBounds` is already cached, so this is cheap
  - Done 2026-10-09 in `drawFrame`: an element's cached bounds are taken through the painter's transform to the device (so it works inside symbols and offscreens too), and it is skipped when, padded by 2 px, they miss the device or are under a quarter pixel. Tweened frames are not culled since their bounds move. Symbol bounds now cover every frame of every layer so an animated instance is not culled by its first frame
- [ ] Cache stroke outlines per zoom octave (key on the power of two nearest the
      scale), so panning and small zooms reuse them
- [x] Bitmap instances scan the library linearly on every draw
      (`phoenix_view.cpp:888`); look them up in a map
  - Done 2026-10-09: `libraryBitmap` caches the decoded pixmap by name, so the scan happens once per bitmap per document
- [x] Color-transformed instances and mask layers allocate a stage- or
      widget-sized image per frame. Size the offscreen to the content's
      device-pixel bounds instead (see step 11)
  - Done 2026-10-09 for instances (the isolated offscreen is cropped to the instance on the device, see step 11). Mask layers still render into device-sized pixmaps
- [x] Mask layers render into device-sized pixmaps; crop them to the mask's device bounds
  - Done 2026-10-10: the mask's coverage and each masked layer's content are rendered into pixmaps the size of the mask art's device bounds (padded 2 px, clipped to the device) and composited back at that offset; a mask that reaches nothing on the device hides its layers without any pixmap
- [x] `getElementBounds` is cached, but `gatherSnapCandidates` walks every
      element on the stage at the start of each drag to collect snap targets
  - Done 2026-10-09: the walk's result (element, bounds) is kept in `_snapBounds` and thrown away with the bounds cache, so a drag only filters it

## Installer and updates

- [x] Windows x64 installer and auto-update through Velopack
  - Done 2026-10-10: `cmake/PhoenixVelopack.cmake` links Velopack's C library (pinned, static); `src/gui/updater.cpp` checks the GitHub releases a few seconds after start-up and from Help > Check for Updates, downloads in the background, and offers to restart, else installs on quit. `cmake --install --component Phoenix` stages the app with the Qt it needs, `tools/release/pack.py` packs it, and `.github/workflows/release.yml` publishes a release for each `v*` tag. Checked locally: Setup installed 0.1.0, which found 0.1.1 in a local feed (`PHOENIX_UPDATE_SOURCE`), downloaded its 21 KB delta and installed it on quit. The "Restart Now" path and the workflow itself have not been run yet
- [x] macOS release (Apple silicon): `Phoenix.app` with its icon, packed by `pack.py` into a `.pkg` installer and portable zip, signed with the Developer ID certificates and notarized
  - Done 2026-10-11: the release workflow's macOS job imports the certificates from repository secrets into a keychain of its own and stores the notary credentials there; vpk signs with hardened runtime and notarizes and staples both the app and the .pkg. A trial run (0.2.0-test.1, not published) was accepted by the notary service and passed `spctl`. Not yet run on a Mac: installing, starting, and updating
- [ ] macOS on Intel: a universal build would need Velopack's two static libraries joined with `lipo` and a universal Qt
- [ ] Linux release: an AppImage with `vpk [linux] pack`; the deploy script needs checking there, since `qt_generate_deploy_app_script` deploys differently on Linux
- [ ] Windows arm64 release: the `windows-arm64` target exists in `pack.py`, but the cross kit has not been built in CI
- [ ] Code signing on Windows (Azure Trusted Signing through `vpk pack --azureTrustedSignFile`), so SmartScreen stops warning about the installer
- [ ] Pin libas3 to a commit rather than `main`, so a release tag always builds the same thing

## Explicitly out of scope


Bones/IK, motion editor, asset warp, variable-width strokes, art/pattern brushes,
tilt input and brush tip shapes. Each is a project in itself. (Pressure for the
brush and static, dynamic and input text authoring were done on 2026-10-09; see
step 13 and step 14.)

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

Ninja needs the compiler on `PATH` already, so run that from a Developer Command
Prompt. Dropping `-G Ninja` uses the default generator instead, which finds MSVC
on its own but is multi-config, so the build and the test run then need
`--config Debug` and `-C Debug` respectively. See the README for the detail.
