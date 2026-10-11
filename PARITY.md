# Phoenix Feature Parity — Animate, Harmony, Extensions

Goal: a studio producing TV animation in Adobe Animate can move to Phoenix
without losing files, scripts or workflow, and gains features from Toon Boom
Harmony and ones neither has. Compatibility with Animate comes first: every
feature here has to read and write FLA/XFL that Animate opens unchanged.

This tracks parity at the product level. Tool-by-tool authoring work, and the
detail of anything already done, lives in [TODO.md](TODO.md); items here point
to its steps rather than repeating them.

Status legend: `[ ]` todo · `[~]` in progress · `[x]` done

Started 2026-10-11 from a review of Phoenix against Animate and Harmony. The
Animate and Harmony feature lists are from knowledge of those products, not a
fresh check against their current releases.

---

## Parity matrix

A summary, kept up to date as items below land. ✅ has it · ◐ partial · ❌ missing

| Area | Animate | Harmony | Phoenix |
| --- | --- | --- | --- |
| FLA/XFL read and write | ✅ | ◐ SWF import | ◐ round-trips; Animate opening Phoenix's output not verified |
| Vector drawing (merge and object) | ✅ | ✅ | ✅ |
| Pressure and tilt brushes | ✅ | ✅ textured | ✅ |
| Variable-width strokes, Width tool, Paint Brush | ✅ | ✅ | ❌ |
| Symbols, graphic symbol swapping | ✅ | ✅ drawing substitution | ◐ no Frame Picker |
| Classic and shape tweens | ✅ | ✅ | ◐ shape tweens play, cannot be created |
| Motion tweens, Motion Editor | ✅ | ✅ function curves | ❌ |
| Bones, IK, deformers | ◐ IK, Asset Warp | ✅ | ❌ kept on save, not drawn |
| Layer parenting, rig hierarchy | ◐ | ✅ pegs | ❌ |
| Camera | ◐ 2D, layer depth | ✅ multiplane | ❌ |
| Lip-sync | ✅ | ✅ | ❌ |
| Effects and compositing | ◐ filters, blends | ✅ node graph | ◐ filters and blends render |
| Color palettes | ◐ swatches | ✅ linked palettes | ◐ swatches kept |
| X-sheet | ❌ | ✅ | ❌ |
| Render to sequences and video | ✅ | ✅ | ❌ SVG of one frame |
| Command-line render | ❌ | ✅ | ❌ |
| Scripting | ✅ JSFL, CEP | ✅ QtScript, Python | ❌ |
| Platforms | Win, Mac | Win, Mac, Linux | ◐ Win, Mac; Linux pending |

---

## 1. Animate compatibility — be a safe drop-in

Nothing else here matters if a studio's files come back damaged or play
differently.

### Verified round-trips

- [ ] Open every corpus file Phoenix saves in Animate and confirm it loads with
      no repair prompt and looks the same (TODO step 2, "Verify Animate itself
      opens what we write")
- [ ] Make that a repeatable check: a script that saves the corpus through
      Phoenix, a JSFL script run in Animate that opens each and exports a
      frame strip, and an image diff against the originals
- [ ] Confirm the cubic `(...)` edge form Phoenix writes for edited geometry
      (Flash stores a quadratic approximation inside the parens)
- [ ] Confirm the timeline guides format, written from memory (TODO step 3)
- [ ] Files edited in Phoenix — drawing, symbols, tweens, library moves —
      opened in Animate, edited there, and reopened in Phoenix
- [ ] Old binary FLA (CS4 and earlier): read the compound-document format, or
      document a conversion route (TODO step 2)

### Playback correctness for what files already contain

- [ ] Object motion tweens (`motion object`, AnimationCore XML) play (TODO
      step 15)
- [ ] Camera layer: pan, zoom, rotation and color effects over time
- [ ] Layer depth (Animate's Layer Depth panel) with the camera
- [ ] Layer parenting: a layer follows its parent layer's transform (TODO
      step 15)
- [ ] Graphic symbol loop options (loop, play once, single frame, reverse,
      first frame) checked against Animate on real TV files
- [ ] Asset Warp (`puppet` symbols) drawn warped (TODO step 15)
- [ ] IK armature poses drawn and tweened (TODO step 15)
- [ ] Motion guides: classic tweens along a guide path, orient to path
- [ ] Shape hints steer shape tweens (TODO step 15)
- [ ] Clipped bitmap fills draw one copy rather than tiling (TODO step 11)
- [ ] Elements in classic tweens paired the way Animate pairs them, not by
      index (TODO step 11)
- [ ] Frame scripts that TV files commonly carry (`stop()`, `gotoAndPlay`)
      honored in preview playback; full ActionScript is not a goal

### Document structure

- [ ] Scenes: list, add, duplicate, delete, rename, reorder, and switch
      between them (check what exists today first)
- [ ] Edit Multiple Frames
- [ ] Distribute to Layers, Distribute to Keyframes
- [ ] Swap Symbol, and Swap Bitmap
- [ ] Find and Replace (symbols, colors, text, fonts)
- [ ] Deleting a used symbol along with its instances (TODO step 14)

## 2. Animation authoring at Animate's level

- [ ] Create Shape Tween: compute the MorphShape Animate writes (TODO step 15)
- [ ] Create Motion Tween, property keyframes, editable motion path
- [ ] Motion Editor: per-property curves and eases
- [ ] Convert to Frame-by-Frame Animation, Reverse Frames
- [ ] Copy and Paste Motion, motion presets
- [ ] Paste and Overwrite Frames, Paste in Place across frames
- [ ] Frame Picker panel for graphic symbols
- [ ] Auto Lip-Sync onto a graphic symbol's mouth poses (TODO step 15)
- [ ] Bone tool, Bind tool, poses and joint limits (TODO step 15)
- [ ] Asset Warp tool
- [ ] Layer parenting authoring view
- [ ] Camera tool (settle the C key with the Time Scrub tool)
- [ ] 3D Rotation and 3D Translation for movie clips
- [ ] Variable-width strokes drawn and written, the Width tool, the Paint
      Brush, art and pattern brushes (TODO step 13)
- [ ] Onion skin: draggable range markers, outline mode, anchor onion
      (TODO step 14)
- [ ] Custom ease presets, saved and shared

## 3. Production output

TV pipelines live on frame sequences and video.

- [ ] Export image sequence: PNG with alpha, at any size, for a frame range,
      a scene or the whole document
- [ ] Export video: MP4 and ProRes through FFmpeg as an optional external
      tool, with sound
- [ ] Export one frame as PNG, JPEG, SVG (SVG exists)
- [ ] Export per layer: each layer, or chosen layers, as its own sequence
- [ ] Export with and without the camera, and with the stage or the pasteboard
- [ ] Export sound as WAV
- [ ] Publish settings carried through from the document, so a file published
      from Animate exports the same from Phoenix
- [ ] Render queue: several documents and scenes in one batch
- [ ] SWF export, for pipelines that hand SWF to Harmony or other tools
      (decide whether it is worth it)

## 4. Scripting and extensions — Animate's SDK

Studios run their pipelines on JSFL. Running their existing scripts brings
their pipeline across with their files.

### JSFL engine

- [ ] Embed a JavaScript engine: QuickJS (MIT, small) rather than QJSEngine,
      which would bring in Qml
- [ ] Commands menu: run a `.jsfl` file, Run Command..., the user's Commands
      folder, Manage Saved Commands
- [ ] Output panel for `fl.trace` and script errors
- [ ] Command-line: `Phoenix.exe script.jsfl` runs it, as Animate does
- [ ] Every script runs as one undo step through the `CommandStack`
- [ ] Old SpiderMonkey dialect quirks scripts rely on (check E4X `XML` use in
      real scripts)

### JSFL object model

Built in order of what real scripts call. First collect studio and widely used
public scripts and log the API calls they make.

- [ ] Gather a JSFL corpus and an API call frequency table
- [ ] `fl`: documents, `getDocumentDOM`, `openDocument`, `createDocument`,
      `saveDocument`, `closeDocument`, `trace`, `configURI`, `configDirectory`,
      `browseForFileURL`, `runScript`, `outputPanel`
- [ ] `FLfile`: read, write, exists, copy, remove, list folder, create folder,
      `file:///` URIs
- [ ] `Document`: selection, scenes, `getTimeline`, `library`, align,
      distribute, arrange, group, break apart, convert to symbol, transforms,
      `exportPNG`, `exportSWF`, `save`, `width`, `height`, `frameRate`
- [ ] `Timeline`: layers, frames, `currentFrame`, insert and remove frames and
      keyframes, `createMotionTween`, `setSelectedFrames`, `copyFrames`,
      `pasteFrames`, `setFrameProperty`, `setLayerProperty`
- [ ] `Layer`, `Frame` (`elements`, `tweenType`, `name`, `labelType`,
      `soundName`, `actionScript`, `startFrame`, `duration`)
- [ ] `Element`, `Instance`, `SymbolInstance` (`libraryItem`, `firstFrame`,
      `loop`, color effects, filters, `name`), `BitmapInstance`, `Text` and
      `TextRun`
- [ ] `Library`, `Item`, `SymbolItem`, `BitmapItem`, `SoundItem`, `FolderItem`:
      add, select, rename, move to folder, delete, `editItem`, `itemExists`,
      linkage
- [ ] `Shape`, `Contour`, `Edge`, `HalfEdge`, `Vertex`, `Fill`, `Stroke`,
      `Matrix`, `Filter` — the planar map from TODO step 6 is the model under
      these
- [ ] `drawingLayer` for tools that draw previews
- [ ] Remaining objects as the frequency table calls for them

### Dialogs, tools and panels

- [ ] XUL dialogs (`fl.xmlPanel`, `document.xmlPanel`) built as Qt dialogs
- [ ] JSFL tools (`fl.tools`, `configureTool`, `mouseDown`, `mouseMove`,
      `mouseUp`, `setCursor`, `toolObj`) wrapped in Phoenix's `Tool`
- [ ] C-level extensibility: load DLLs that call into JSFL through the
      `mm_jsapi.h` function table
- [ ] CEP panels: read a ZXP's `manifest.xml` (host `FLPR`), show its HTML in a
      dock through WebView2 / WKWebView, and a `CSInterface` shim whose
      `evalScript` runs JSFL; events and theme colors
- [ ] CEP Node.js support, or a list of which panels work without it
- [ ] Extension installation and management (ZXP and folder install)
- [ ] Custom Platform SDK (C++ document types and publishers): not planned
      for TV; revisit if a studio needs it

### Phoenix's own scripting

- [ ] Python bindings over the same object model, for studios whose pipeline
      libraries (ShotGrid, ftrack, Kitsu) are Python
- [ ] Event hooks: document opened, saved, frame changed, before export

### Legal and naming

- [ ] Implement the API from its public behavior, without copying Adobe's
      documentation text or SDK headers
- [ ] Brief legal review before advertising "Animate extension compatible";
      describe compatibility without implying Adobe endorsement

## 5. Harmony parity, kept FLA-compatible

Harmony's strengths, built so that what Phoenix saves still opens in Animate.
Anything Animate cannot represent is baked on save and its live form kept in
metadata Animate ignores.

- [ ] X-sheet view of the timeline: graphic symbol exposures as columns,
      drawings as cells, editable
- [ ] Linked palettes: fills and strokes linked to palette colors, recolor
      every use at once, palette swaps (day, night); baked colors on save
- [ ] Line and color art separation for painting under clean lines
- [ ] Deformers beyond Animate's: curve and envelope deformation on symbols
- [ ] Master controllers: one slider driving several properties
- [ ] Multiplane camera with depth parallax, built on Animate's layer depth
- [ ] Function curves for every animatable property
- [ ] Textured and bitmap brushes
- [ ] Light table, and peg-style pivots on symbols
- [ ] Effects that Harmony composites (tone, highlight, glow with masks),
      rendered on export
- [ ] Storyboard and animatic import: an EDL or XML from Storyboard Pro or an
      editor becomes scenes or shot files with the audio placed

## 6. Phoenix's own features for TV production

What neither Animate nor Harmony offers.

- [ ] Headless command-line render: `phoenix render shot.fla --range 1-240
      --out frames/`, no window, Windows, macOS and Linux render nodes
- [ ] Batch operations from the command line: run a JSFL or Python script over
      a folder of FLA files
- [ ] FLA doctor: validate a file, report and repair damage, salvage what can
      be read from a corrupt or `RECOVER_` file
- [ ] Autosave and crash recovery
- [ ] Version control friendly FLA: deterministic writing (already true),
      saving as an XFL folder by default for studios that want it, Git LFS
      guidance
- [ ] Visual diff of two versions of a file, frame by frame and symbol by
      symbol
- [ ] Linked shared assets: a character rig in a master file pushed into every
      shot file that uses it, written as ordinary embedded symbols
- [ ] Shot breakdown: split an episode file into shot files by scene, label or
      camera cut
- [ ] Compositing passes on export: line, color, mattes, multi-layer EXR
- [ ] Lip-sync from audio offline (Rhubarb Lip Sync, MIT) with mouth charts
- [ ] Review notes: drawn and written notes on frames, kept on a guide layer
      that never exports
- [ ] Performance on large TV files, measured against Animate on the same
      files (see TODO "Performance")

## 7. Platform and studio deployment

- [ ] Linux release (TODO "Installer and updates")
- [ ] Windows arm64 and macOS Intel releases
- [ ] Windows code signing
- [ ] Site-wide configuration for studios: shared Commands, extensions,
      palettes and presets from a network folder
- [ ] Keyboard shortcut sets matching Animate's, importable from Animate's
      shortcut files
- [ ] Workspace layouts saved and shared

---

## Suggested order

1. Verified round-trips with Animate (section 1)
2. Playback correctness for camera, parenting, motion tweens (section 1)
3. Image sequence and video export, then the headless renderer (sections 3, 6)
4. JSFL engine and the most-called object model, with XUL dialogs (section 4)
5. Frame Picker and lip-sync (section 2)
6. CEP panels (section 4)
7. Harmony features and Phoenix's own (sections 5, 6), by studio demand
