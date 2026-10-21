# SWF in Phoenix

Phoenix writes SWF movies (File > Export > Export SWF Movie, Publish, the
command line, JSFL `document.exportSWF`) and opens them as documents. This
page covers both directions, how they relate to Test Movie, and how the
code is tested.

Related deep dives: [architecture.md](architecture.md) for the overall
layering, [fla-format.md](fla-format.md) for the document model and XFL
files that SWF import produces, [actionscript.md](actionscript.md) for the
runtime in `src/play` and the libas3 compilers, and
[geometry.md](geometry.md) for how shapes are drawn before they become SWF
outlines.

## Map of the code

| File | Role | Qt? |
| --- | --- | --- |
| [src/util/swf_writer.h](../src/util/swf_writer.h), [.cpp](../src/util/swf_writer.cpp) | `swf::Writer` and `swf::Sprite`: the tag encoder. Character ids, shapes, morph shapes, bitmaps, sprites, buttons, text fields, placements, sounds, actions, ABC, compression. | No |
| [src/util/swf_sound.h](../src/util/swf_sound.h), [.cpp](../src/util/swf_sound.cpp) | SWF ADPCM encode and decode, MP3 frame scanning, cutting a soundtrack into per-frame stream blocks (`rawStream`, `adpcmStream`, `mp3Stream`). | No |
| [src/edit/swf_movie.h](../src/edit/swf_movie.h), [.cpp](../src/edit/swf_movie.cpp) | `fla::swfmovie::Movie`: the structured export. Turns timelines, symbols, masks, scripts and sounds into writer calls. Also `unstructured()`, `hasScripts()`, `holdsClips()`, `colorTransform()`, `filters()`, `blendMode()`. | No |
| [src/edit/swf_actionscript3.h](../src/edit/swf_actionscript3.h), [.cpp](../src/edit/swf_actionscript3.cpp) | ActionScript 3 for SWF: gathers class files and timeline classes, compiles them to ABC with libas3, writes DoABC and SymbolClass. Also translates the same input to JavaScript for HTML5 pages. | No |
| [src/edit/swf_event_sounds.h](../src/edit/swf_event_sounds.h), [.cpp](../src/edit/swf_event_sounds.cpp) | `fla::SwfEventSounds`: event, start and stop sounds as DefineSound and StartSound. | No (uses the audio decoder) |
| [src/gui/swf_export.h](../src/gui/swf_export.h), [.cpp](../src/gui/swf_export.cpp) | `SwfArt` (recorded paint items to shapes and bitmaps) and `SwfViewDrawer` (the `Drawer` the app uses, backed by `PhoenixView`). | Yes |
| [src/gui/main_window.cpp](../src/gui/main_window.cpp) | `MainWindow::exportSwf`: the one entry point. Picks structured or frame-by-frame output, sets up sound, runs the movie, writes the file. | Yes |
| [src/parser/swf_import.h](../src/parser/swf_import.h), [.cpp](../src/parser/swf_import.cpp) | `SwfImport::convert`: an SWF to XFL files (DOMDocument.xml, LIBRARY, bin). | No |
| [src/parser/fla_parser.cpp](../src/parser/fla_parser.cpp) | `FLAParser::parseUnguarded`: sniffs the SWF magic and routes the file through `SwfImport`. | No |

Everything under `swf::` and `fla::swfmovie` is free of Qt, which is what
lets the tests drive the exporter with a fake `Drawer`.

## Export

### Entry points

All roads lead to `MainWindow::exportSwf(path, size, error)`:

- File > Export > Export SWF Movie (the action built in `MainWindow`'s menu
  setup).
- Publish, when the SWF format is checked (the "flash" key in the publish
  settings). With the HTML format also checked, the page embeds the SWF with
  Ruffle loaded from unpkg at a pinned version.
- `Phoenix --export-movie in.fla out.swf` (`MainWindow::exportFromCommandLine`)
  and `Phoenix render in.fla --out out.swf` (`MainWindow::renderCommand`).
  Both print `_swfNotes` (what did not go in) to stderr.
- JSFL `document.exportSWF` ([src/jsfl/model_publish.cpp](../src/jsfl/model_publish.cpp)
  through the host in [src/gui/script_runner.cpp](../src/gui/script_runner.cpp));
  see [jsfl.md](jsfl.md).

`exportSwf` needs a live `PhoenixView`, because the art is drawn by the same
renderer the stage uses. Headless runs therefore construct a `MainWindow`
without showing it.

### Two kinds of output

```
                    fla::swfmovie::unstructured(document)
                                 |
            empty ---------------+--------------- a reason
              |                                       |
   structured movie                          frame-by-frame movie
   swfmovie::Movie + SwfViewDrawer           PhoenixView::recordFrame per frame
   sprites, buttons, names, masks,           every frame: remove all depths,
   scripts, labels, every scene in a row     place recorded shapes 1..n
   event sounds on their own timelines       event sounds via define()/frame()
                                             scripts dropped (with a note)
                                             only the scene shown
```

`unstructured()` returns why a document cannot keep its structure, or an
empty string. It walks `features::used(document)`
([src/edit/phoenix_features.h](../src/edit/phoenix_features.h)) and stops at
the first feature whose id is in a fixed list:

`effect-layers`, `line-and-color-art`, `multiplane-camera`, `pegs`,
`deformers`, `behaviors`, `compositing`, `instance-effects`, `node-graph`,
`mask-edges`, `several-masks`, `motion-blur`, `templates`,
`master-controllers`, `3d-rotation`, `depth-sorting`, `parenting-options`,
`bone-corrections`.

It also returns a reason for a scene with a camera layer
(`camera::filmingOf`) and for any motion-object tween whose `MotionObject`
is 3D. The reason is the feature's title plus where it is used. When
`hasScripts()` is also true, the frame-by-frame export adds the note "The
movie's scripts are left out: it is drawn as frames of shapes for <reason>."

These are things Phoenix draws that a player's own rendering of clips could
not reproduce. When you add a Phoenix-only drawing feature, decide whether it
belongs in this list.

### The frame-by-frame movie

The simpler path, entirely in `exportSwf`:

1. `frames = _phoenixView->frameCount()` (the current scene only).
2. For each frame, `RemoveObject2` every depth placed last frame, then
   `PhoenixView::recordFrame(frame, size)` paints the frame into a
   `PaintRecorder` ([src/gui/paint_recorder.h](../src/gui/paint_recorder.h)).
   That gives `RecordedItem`s: each fill (strokes and text already turned into
   areas) with its brush, or an image with its transform, in painting order,
   with the clip in force and the composition mode.
3. Each item goes through `SwfArt::shapeOf` and is placed at depths 1..n with
   `Writer::place(id, depth, alpha, blend)`.

Masks come through as clips on the items (`RecordedItem::clip`), which
`shapeOf` intersects into the area, so nothing in this movie uses clip
depths. Whatever filters produce reaches the recorder as images, which become
bitmap fills.

### SwfArt: paint items to shapes

`SwfArt::shapeOf` ([src/gui/swf_export.cpp](../src/gui/swf_export.cpp)) is
shared by both paths:

- The area is `path` (intersected with `clip` when clipped), then
  `QPainterPath::simplified()`, so the outline does not cross itself. The
  writer fills each shape with one fill style on one side of every edge, so it
  relies on this.
- Fill mapping:

  | Brush | SWF fill |
  | --- | --- |
  | Image item | bitmap fill (clipped, smoothed), matrix from `imageTransform` |
  | `QLinearGradient` | linear gradient; the 1638.4 px gradient square mapped onto start and final stop |
  | `QRadialGradient` | radial gradient; focal ratio clamped to 0.99, which makes the writer pick DefineShape4 |
  | `Qt::TexturePattern` | repeating bitmap fill |
  | solid | solid fill, opacity folded into the color's alpha |

  Solids and gradients carry the item's opacity in their colors and return
  `alpha = 1`; images return the item's opacity as the placement alpha.
- `swfOutline` converts every cubic into four quadratics (the control is where
  the end tangents meet, or the chord midpoint when that point is too far
  away) and closes every subpath.
- Shapes and bitmaps are deduplicated by SHA-1: shapes over outline and fill
  fields, bitmaps over the premultiplied pixels and size. A shape drawn on 100
  frames is defined once.
- Composition modes map to SWF blend modes (Multiply, Screen, Lighten,
  Darken, Difference, Plus to Add, Overlay, HardLight); others stay Normal.

Bitmaps are always written as `DefineBitsLossless2` (32-bit ARGB,
premultiplied, zlib). There is no JPEG output path.

### The structured movie

`exportSwf` builds `swfmovie::Options`, a `SwfViewDrawer` and a
`swfmovie::Movie`, then calls `Movie::write(beforeShow)`. The callback runs
before each main-timeline `ShowFrame`; it updates the progress dialog (false
cancels) and writes that frame's soundtrack block.

#### Options

| Field | Set by `exportSwf` to | Effect |
| --- | --- | --- |
| `findClass` | the document's class path without Phoenix's built-in classes | ActionScript 2 class sources |
| `scaleX`, `scaleY` | export size over document size | main timeline content scaled; symbols are not |
| `hiddenLayers` | `publish.swfHiddenLayers` (the Include hidden layers setting) | main-timeline layers hidden by their eye go in |
| `actionScript3` | a lambda calling `writeActionScript3` | DoABC and SymbolClass for ActionScript 3 movies |
| `eventSound` | `SwfEventSounds::keySound` | DefineSound and SoundInfo for a keyframe's event, start or stop sound |

#### The Drawer contract

`Movie` never draws. It asks a `Drawer` for each layer frame:

```cpp
virtual std::vector<Piece> layerFrame(const Layer& layer, int frame, swf::Writer& writer) = 0;
virtual bool shows(const Timeline& timeline, const Layer& layer) const;
```

A `Piece` is one element of the frame, and is one of:

- art: `shapes` already written to the writer (id, alpha, blend each), drawn
  in the timeline's own space;
- `instance`: a movie clip or button, with the in-between `matrix`, `color`,
  `filters` and `blendMode` for this frame (so motion tweens arrive already
  evaluated);
- `text`: a dynamic text field with an instance name or variable, or an input
  field (`scriptedField()`);
- a graphic symbol that `holdsClips()`, with `graphicFrame` set to the frame
  of its own it shows.

`SwfViewDrawer::layerFrame` gets these from `PhoenixView::captureLayerFrame`,
which renders the layer frame and separates clips, buttons, scripted text and
clip-holding graphics from the art; the art goes through `SwfArt::shapeOf`.
The default `Drawer::shows` hides a main-timeline layer whose eye is off and
always shows symbol layers. The tests supply their own `SquareDrawer`.

`Movie` adds to what the drawer returns (`State::layerPieces`):

- Shape tweens become morph shapes (`morphPieces`, below).
- ActionScript 3 components (`DOMComponentInstance`, kept as raw elements)
  are added as movie clip pieces of their library symbol (`addComponents`).
- Graphics that hold clips are opened: their layers, bottom first, are
  captured at `graphicFrame`, each part's matrix composed with the graphic's,
  its color chained through `Piece::within`, and its `path` recording where
  it sits inside the graphic. This keeps a named clip inside a graphic
  reachable by scripts. A graphic with a mask layer in it is not opened; it
  stays art.

`State::sameAsBefore` skips the drawer for a frame that must look like the
one before: same keyframe, no tween, no parent layer, and only static
elements (graphics of one frame, recursively; clips and buttons play on their
own). It is turned off for the whole movie when the document uses the
`shape-operators` or `text-animators` features, which animate art without a
tween.

#### Characters

Characters are defined on first use and cached by key in `Movie::State`:

| Source | Cache | SWF |
| --- | --- | --- |
| Movie clip symbol | `sprites` by `Symbol*` | `DefineSprite`, written by recursing `writeTimeline` into a `swf::Sprite`; plus `DefineScalingGrid` for a 9-slice clip and `DoInitAction` for `#initclip` |
| Button symbol | `buttons` by (symbol, instance script) | `DefineButton2`, plus `DefineButtonSound` |
| Several shapes, or one faded or blended | `artSprites` by shape ids, alphas, blends | one-frame `DefineSprite` placing each shape |
| Mask layer frame with more than one piece | `artSprites` keyed `"mask ..."` | one-frame `DefineSprite` |
| Button state with a mask | `stateSprites` | one-frame `DefineSprite` per state |
| Scripted text field | `fields` by `StaticText*` | `DefineEditText` |
| Font of a field's first run | `fonts` by face and style | `DefineFont2` with no glyphs (a device font) |
| Shape tween | `morphs` by first keyframe | `DefineMorphShape` |

Definition tags always go into the writer's main tag stream, even while a
sprite's timeline is being built into a `swf::Sprite` (which holds only
control tags). They are emitted before the placement that first uses them.

A symbol that is already being written (`writing`) is not entered again; the
problem "X is inside itself: left out there" is recorded. Recursion into
graphics stops at depth 32.

#### Depths, layers and masks

For each timeline, `writeTimeline` first captures every usable layer's pieces
on every frame, then `plan()` assigns depths:

```
layer list (top first)         depth bands (bottom first)
  mask  M ----------------+      1   M  (clipDepth = 4)
  masked A (parent = M)   |      2-3 A  (2 slots: A shows at most 2 pieces)
  masked B (parent = M)   |      4   B
  plain  C                       5   C
```

- Guide and folder layers are skipped, and so are hidden layers unless
  `usable()` lets them in (`hiddenLayers`, or the drawer says they show).
- Each layer gets a band as deep as the most pieces it shows on any frame,
  counted per (element index, path inside opened graphics); each such slot
  keeps its depth across frames.
- A mask layer gets one depth just under the first of its masked layers. Its
  placement carries `clipDepth` = the top depth of all layers masked by it.
  The mask's pieces are placed as one character (`maskSprite`): the shape
  itself when it is one art piece, else a sprite of them. On the main
  timeline the mask's matrix carries `scaleX`/`scaleY`.

Then each frame is diffed against the one before (`std::map<int, Shown>`):

- A depth whose character, name or clip depth changed, or that is empty now,
  gets `RemoveObject2`.
- A new or replaced depth gets a full `PlaceObject2`/`PlaceObject3`
  (`placementOf`).
- Otherwise a move (`PlaceObject2` with the move flag) carrying only the
  fields that changed: matrix (tolerance 1e-5 on the linear part, 0.025 px on
  translation), color transform (compared at 1/256), filters (by
  `filtersKey`), blend, ratio. Nothing changed, nothing written.

So a motion-tweened clip is placed once and moved every frame; it keeps its
own playhead. Between scenes every depth is removed, so a scene starts empty.

The writer chooses `PlaceObject3` (code 70) when a placement has filters, a
non-normal blend or bitmap caching; otherwise `PlaceObject2` (26).

#### Instances: color, filters, blend

- `colorTransform()` folds tint, then brightness, then the advanced
  multipliers and offsets into one SWF multiply-and-add. Within opened
  graphics, the graphic's color is applied after the part's (`then`).
- `filters()` maps enabled filters: drop shadow, blur, glow, bevel, gradient
  glow, gradient bevel directly (angle to radians, quality to passes 1 to 3,
  bevel type to inner/onTop); Adjust Color becomes a 4x5 color matrix built
  the way Phoenix adjusts color (saturation about luminance weights 0.3086,
  0.6094, 0.0820, hue rotation about the gray axis, contrast about 128, then
  brightness). Phoenix's own Color Matrix and Convolution filters are written
  as SWF's color matrix (6) and convolution (5) filters.
- `blendMode()` maps the file's names (`normal`, `layer`, `multiply`, ...,
  `hardlight`) to SWF numbers 1 to 14.

#### Buttons

`State::buttonFor` captures frames 0 to 3 (up, over, down, hit) of each
usable layer, plans depths as for a timeline, and makes one `ButtonRecord`
per (character, depth, matrix, color, filters, blend), merging identical
records across states. Without a hit frame, up records also get the hit bit.

A button with a mask layer cannot clip inside `DefineButton2`, so each state
that shows something becomes its own one-frame sprite with the mask as a
clip depth inside it, and the record places that sprite at depth 1.

`on()` handlers on the instance are compiled with
`as3::avm1::Compiler::compileHandlers` into `BUTTONCONDACTION`s:

| Handler | Condition |
| --- | --- |
| `press` | OverUpToOverDown |
| `release` | OverDownToOverUp |
| `releaseOutside` | OutDownToIdle |
| `rollOver` / `rollOut` | IdleToOverUp / OverUpToIdle |
| `dragOver` / `dragOut` | OutDownToOverDown / OverDownToOutDown |
| `keyPress "<Key>"` | key code in bits 9 to 15 (`ButtonCondition::key`) |

`onClipEvent` on a button is a recorded problem. A symbol's
`trackAsMenu="true"` sets the menu flag. Because the handlers are part of
`DefineButton2`, a button is cached per (symbol, script) pair. On the main
timeline the handlers compile with the scene-aware options of the scene the
instance is on.

Button sounds: for each state frame that is a keyframe at exactly that index
on a visible, non-guide, non-folder layer, `eventSound` is asked for its
sound, and `DefineButtonSound` gets them in transition order: up's sound for
over-up to idle, over's for idle to over-up, down's for over-up to over-down,
hit's for over-down to over-up.

#### Text

Static text is art: the recorder turns glyphs into fills. A dynamic field
with a name or variable, or an input field, becomes `DefineEditText`
(`fieldFor`):

- The box is the element's `left/top/width/height` widened by a 2 px gutter
  on each side.
- Font, size, color, alignment and leading come from the first text run. The
  face is cleaned for the player to find among system fonts: a `-Style`
  suffix and a trailing `MT` are removed, and Bold/Italic in the name or the
  run's attributes set the style flags. `_sans` is the fallback.
- The text of all runs is concatenated, with `\n` as `\r`.
- `variableName`, `lineType` (multiline, multiline no wrap, password),
  `isSelectable`, `border`, `renderAsHTML`, `maxCharacters` and `autoExpand`
  map to the field flags; anything but input is read-only.

The instance name (`fieldName()`: `instanceName`, else the `name` attribute)
goes on the placement.

#### Shape tweens as morph shapes

`morphPieces` replaces the drawn in-between of a shape tween with a
`DefineMorphShape` placed with a ratio. `morphFor` builds it from the
keyframe's `MorphShape` (`ensureMorph`, see [geometry.md](geometry.md)):

- Fills: solid, linear and radial gradients without a focal point. Any other
  fill (bitmap, focal radial) makes the whole tween fall back to drawn frames.
  An end fill of another type or stop count takes the start fill unchanged.
- Lines: only solid strokes with a solid fill; width scaled by the shape's
  transform unless the stroke's scale mode is `none`.
- Segments become quadratic edges at both ends; `Writer::defineMorphShape`
  writes every edge as a curve, so start and end records always match.
- The ratio is the eased progress (`easeProgress` with the keyframe's
  acceleration or custom ease) times 65535.

Mask layers never morph.

#### Frame labels

On each frame, the first layer from the top with a keyframe starting there
and a non-empty name that is not a comment gives a `FrameLabel`; `anchor`
labels set the anchor byte. There is no `DefineSceneAndFrameLabelData`: the
scenes are concatenated into one main timeline (see below).

#### Scenes

`Movie` writes `document.timelines` one after another. `sceneStarts` records
each scene's first frame, and the AVM1 compile options get `scenes` (name to
start), `sceneStart`, `nextScene` and `prevScene`, so `gotoAndPlay("Game", 2)`,
`nextScene()` and `prevScene()` compile to absolute `GotoFrame`s. The test
`swf_movie_plays_scenes_one_after_another` shows the result.

### ActionScript 1 and 2

The compiler is libas3's AVM1 backend (`avm1/avm1.h`, namespace
`as3::avm1`); libas3 lives beside Phoenix (see [actionscript.md](actionscript.md)).

| Source | Compiler call | Tag |
| --- | --- | --- |
| Frame script | `compileFrameScript` | `DoAction` in the timeline (main or sprite), top layer first |
| `#initclip` in a symbol's frame 0 | `compileFrameScript().initActions` | `DoInitAction` for the sprite; several layers concatenated |
| `on()` / `onClipEvent()` on a movie clip | `compileHandlers` | `CLIPACTIONS` on the placement |
| `on()` on a button | `compileHandlers` | `BUTTONCONDACTION`s in `DefineButton2` |
| ActionScript 2 class files | `classMembers`, then `compileClass` | an empty sprite exported as `__Packages.<Class>` with a `DoInitAction` |
| Linkage class | `compileScript("Object.registerClass(...)")` | `DoInitAction` on the exported sprite |

Details:

- Clip events map to `swf::ClipEvent` flags; each `keyPress` becomes its own
  record with its key code (named keys like `<Left>` map through
  `keyCode()`). An event clips do not have is a recorded problem.
- Hidden layers that are left out take their frame scripts with them.
- `actionScript2Classes()` finds the classes to compile: it scans every
  script and every `linkageClassName` for identifiers and dotted paths (each
  prefix tried), keeps those starting with a capital letter that `findClass`
  can find, and scans those sources in turn. `writeClasses` first collects
  each class's members (so methods resolve names), qualifies short superclass
  names within the package, compiles each, and emits them so that a class
  comes after what it `needs`.
- Symbols with `linkageExportForAS="true"` and a `linkageIdentifier` are
  written up front and listed in one `ExportAssets` so `attachMovie` finds
  them from frame 1.
- Compile errors are recorded per location ("Scene 1, layer actions, frame
  1: ...") and the rest of the movie still goes in.

### ActionScript 3

`Movie`'s constructor decides whether the movie is ActionScript 3, as Test
Movie does: the publish settings' version (`play::actionScriptVersion`), but
a version-3 file whose scripts look only like AS1/2 (`setProperty(`,
`onClipEvent`, `_root.` and so on, or any instance script) is treated as AS2,
and a file with no version whose scripts look like AS3 (`import flash.`,
`addEventListener`, `:void` ...) is treated as AS3 (`scriptsLookLike`).

For an AS3 movie, `Movie::write`:

1. calls `Writer::setActionScript3(true)` (sets the AS3 bit in
   FileAttributes) and collects `components(document)`;
2. writes every movie clip that has frame scripts, a linked class or a
   component on it, and every button with a linked class (without handlers),
   collecting their character ids;
3. calls `Options::actionScript3(writer, sprites)`, which in the app is
   `writeActionScript3`;
4. writes the main timeline. Frame scripts are not written as `DoAction`.

Without the callback, an AS3 movie with scripts goes out without them and a
problem says so.

`writeActionScript3` ([src/edit/swf_actionscript3.cpp](../src/edit/swf_actionscript3.cpp))
compiles with `as3::abc::compile` and writes:

- one `DoABC` (code 82, lazy flag set) named after the document, or `frame1`;
- one `SymbolClass` (code 76): id 0 for the main timeline's class, and each
  written symbol's class. A class the ABC does not define and the player does
  not provide is a note, not an entry.

What the compiler is given (`Compiler::gatherAll`):

- For each movie clip or button with frame scripts or a linked class: a
  timeline class. A linked class found on the class path (or a player class)
  is used as is; if the symbol also has frame scripts, a generated class
  (`as3::abc::timelineClassName(documentName, symbol, index)`) extends it and
  a note says so. Otherwise the generated or linked name extends
  `linkageBaseClass`, defaulting to `flash.display.MovieClip` or
  `SimpleButton`. Component symbols' own timelines are not scanned.
- The main timeline, all scenes as one: `<document>_fla.MainTimeline`, or the
  `DocumentClass` when it exists and the timeline needs no frame code. A
  document class with no file gets a generated class of that name and a note.
- Stage instances declared as public slots of the timeline class when
  `AS3AutoDeclare` is on (and always for generated component names like
  `__id0_`), skipping members the base class already declares.
- Component parameters (`parametersAsXML`) as code that sets each value
  between `componentInspectorSetting = true` and `false`.
- Class sources reached from the code, transitively, ordered so a superclass
  or interface comes before what extends it; `include` resolved through
  `ActionScript3Options::readFile`; `AS3ConfigConst` as conditional
  compilation constants.
- Component skins the library lacks are drawn in Phoenix's plain look and
  named in a note.

If compilation fails there is no `DoABC` and no `SymbolClass`; the notes
carry file and line, or scene/symbol, layer, frame and line. The same
gathering feeds `translateActionScript3` for HTML5 publishing, which the tests
compare against the SWF path.

In the app, `as3.findClass` searches the full class path including Phoenix's
built-in classes (for example the `fl.*` components), while the AS2
`findClass` excludes them.

### Sound

Two independent mechanisms, both following the publish settings
(`fla::publish::read`, [src/edit/publish_settings.h](../src/edit/publish_settings.h)).
User-level behavior is described in website/sound.html.

#### Stream sounds: one soundtrack

In `exportSwf`, when the timeline has audio and `soundCompression` is not
`none`:

1. `TimelineAudio::mixdown` mixes the stream sounds. For a structured movie
   with several scenes, each scene is made current in turn and the mixes are
   concatenated.
2. If anything is audible, it is encoded:
   - `mp3`: rate at least 11025 Hz, LAME via `MovieExport::encodeMp3`, then
     `swf::mp3Stream(..., delay = 1105)`. Without an encoder it falls back to
     ADPCM and the status bar says so.
   - `raw`: `swf::rawStream`.
   - otherwise ADPCM: `swf::adpcmStream` with `soundAdpcmBits`.
3. `Writer::soundStream` writes `SoundStreamHead2` before the first frame;
   each frame's `beforeShow` writes that frame's `SoundStreamBlock`.

`mp3Stream` deals whole MP3 frames to movie frames so the decoded sound keeps
up with the playhead; each block starts with its sample count and a seek
telling the player where this movie frame's sound begins within the block.
The 1105-sample encoder delay goes in as the stream's latency.

#### Event, start and stop sounds

`SwfEventSounds` turns a keyframe sound whose sync is `event`, `start` or
`stop` into `DefineSound` plus `StartSound`:

- `soundOf(name)` defines each library sound once. Format: the publish
  settings' event format, unless the library item sets its own
  (`exportFormat`/`exportBits`) and `overrideSounds` is off. An item marked
  `exportNative` that is an MP3 at 11, 22 or 44 kHz is copied frame for
  frame. MP3 is encoded with a seek of 1105 samples in the header; without an
  encoder, ADPCM (and `mp3Missing()` reports it). Raw is 16-bit little-endian.
  `none` writes no sound.
- `soundInfo(key)` builds the `SOUNDINFO`: stop for `stop`; `noMultiple` for
  `start`; loop count (0, meaning 65535, for loop forever); in and out points
  from the frame's trim; the volume envelope unless it is flat.

How they are placed depends on the export:

- Structured: `Options::eventSound` is `keySound`. `State::startSounds` writes
  `StartSound` inside the timeline the keyframe is on, so a clip's sound is
  in its sprite and a stopped clip starts none. Main-timeline hidden layers
  are heard when `hiddenLayers` is set; hidden layers inside symbols are
  silent (even though they draw, per `Drawer::shows`).
- Frame by frame: `define(writer, frames)` plans the current scene; each
  movie clip with sounds (or clips with sounds inside it, up to 16 levels)
  becomes a sprite containing only those `StartSound`s, placed at depths
  counting down from 65535 while the clip's keyframe lasts, so it plays and
  loops along with the clip's span. `frame(writer, f)` emits each frame's
  sounds, places and removes.

The streamed mixdown leaves event sounds out, so nothing is heard twice.

### Tag writing

`swf::Writer` ([src/util/swf_writer.cpp](../src/util/swf_writer.cpp)) keeps
one tag buffer and a running character id (`_nextId`, from 1). Coordinates
are pixels in the API and twips (x20) in the file.

The constructor writes `FileAttributes` (flags 0; `setActionScript3` patches
the AS3 bit 0x08 in place) and `SetBackgroundColor`. `finish(compress)`
appends `End`, prepends the header (stage rect, frame rate as 8.8, frame
count) and writes `CWS` with zlib level 9 (`compress2`), or `FWS`
uncompressed. The version byte is always 10.

| Call | Tag (code) |
| --- | --- |
| `defineShape` | DefineShape3 (32), or DefineShape4 (83) for a focal radial gradient |
| `defineMorphShape` | DefineMorphShape (46) |
| `defineBitmap` | DefineBitsLossless2 (36) |
| `defineSprite` | DefineSprite (39) with the `Sprite`'s tags and an End |
| `defineButton` | DefineButton2 (34) |
| `defineButtonSound` | DefineButtonSound (17) |
| `defineFont` | DefineFont2 (48), no glyphs |
| `defineEditText` | DefineEditText (37) |
| `defineScalingGrid` | DefineScalingGrid (78) |
| `defineSound` | DefineSound (14) |
| `place(Placement)` | PlaceObject2 (26) or PlaceObject3 (70) |
| `place(id, depth, alpha, blend)` | PlaceObject2, or PlaceObject3 with a blend |
| `remove` | RemoveObject2 (28) |
| `frameLabel` | FrameLabel (43) |
| `doAction` / `doInitAction` | DoAction (12) / DoInitAction (59) |
| `exportAssets` | ExportAssets (56) |
| `doABC` / `symbolClass` | DoABC (82) / SymbolClass (76) |
| `soundStream` / `soundBlock` | SoundStreamHead2 (45) / SoundStreamBlock (19) |
| `startSound` | StartSound (15) |
| `showFrame` | ShowFrame (1) |
| `tag(code, body)` | anything else, raw |

Shape encoding specifics worth knowing before you change it:

- A `DefineShape3` from `defineShape` has exactly one fill style and no line
  styles; the first move record selects fill 0 = style 1. Strokes reach the
  writer already turned into areas.
- Gradient matrices map the SWF gradient square (32768 twips across) onto the
  shape, and `matrix(body, m, 20.0)` scales only the translation. Bitmap fill
  matrices use scale 1, since bitmap pixels are twips in a fill matrix. At
  most 15 gradient stops are written.
- Straight edges with one zero delta use the short horizontal/vertical form.

`swf::Sprite` mirrors the control-tag subset (place, remove, startSound,
frameLabel, doAction, showFrame) into its own buffer. The tag header encoder
(`appendTag`) uses the short form under 63 bytes and the long form otherwise.

### What a SWF cannot hold

Besides the `unstructured()` list, these are recorded in `Movie::problems()`
or `_swfNotes` and shown in the status bar or printed on the command line:

- scripts that do not compile, with their location;
- a symbol inside itself;
- an event a movie clip or button does not have; `#initclip` outside a movie
  clip's first frame; `onClipEvent` on a button;
- a component whose symbol is missing from the library;
- AS3 compile notes, missing skins, symbol classes not in the ABC;
- MP3 not available (sound fell back to ADPCM).

There are no player version, password or protection settings.

## Import

Opening a `.swf` goes through `FLAParser::parseUnguarded`
([src/parser/fla_parser.cpp](../src/parser/fla_parser.cpp)): if the first
bytes satisfy `SwfImport::isSwf` (`FWS`, `CWS` or `ZWS`), the whole file is
passed to `SwfImport::convert`, which produces an in-memory map of XFL files.
A `ZipReader` opened on that map is parsed like any XFL document (see
[fla-format.md](fla-format.md) and [importers.md](importers.md)). The
document is marked `fromSwf`; `MainWindow` adopts it untitled as `<name>.fla`
so a save never overwrites the movie, and shows the conversion report.

```
.swf --SwfImport::convert--> { DOMDocument.xml, LIBRARY/*.xml, bin/*.dat,
                               bin/abc/N.abc, PublishSettings.xml }
     --ZipReader::openMemory + DocumentParser--> fla::Document (fromSwf)
```

### Reading the file

`Converter::run`:

- Decompresses: zlib for `CWS`, LZMA ([src/util/lzma.h](../src/util/lzma.h))
  for `ZWS`.
- Reads the stage rect and frame rate (8.8); the header's frame count is
  skipped, since frames are counted from ShowFrame tags.
- Looks for a runtime shared library preloader: a `DefineBinaryData` whose
  `SymbolClass` name ends in `__Content__` and holds an SWF. That inner
  movie is converted instead (up to 4 levels), with a note in the report.
- Reads the tags into a `TimelineData` for the main timeline, then
  `finish()` writes the XFL, and `keepActionScript3()` keeps compiled AS3.

### Tags read

Control tags (`readTags`, for the main timeline and each sprite):

| Tag | Use |
| --- | --- |
| ShowFrame (1) | snapshot the display list as a frame |
| PlaceObject, PlaceObject2, PlaceObject3 (4, 26, 70) | matrix, color, name, ratio, clip depth, filters, blend, clip actions |
| RemoveObject, RemoveObject2 (5, 28) | remove a depth |
| SetBackgroundColor (9) | stage color |
| FrameLabel (43) | labels |
| DefineSceneAndFrameLabelData (86) | scenes and labels (main timeline) |
| DoAction (12), DoInitAction (59) | kept compiled, plus a comment in the frame script |
| DoABC (72, 82) | abcFile kept |
| StartSound (15) | event sound use; StartSound2 (89) is left out |
| SoundStreamHead/2 (18, 45), SoundStreamBlock (19) | streamed soundtrack |

Definition tags (`define`):

| Tags | Becomes |
| --- | --- |
| DefineShape 1 to 4 (2, 22, 32, 83) | shapes with solid, gradient and bitmap fills, strokes with caps and joins |
| DefineMorphShape/2 (46, 84) | a shape on each frame it changes, or a shape tween (below) |
| DefineSprite (39) | movie clip symbol |
| DefineButton/2 (7, 34), DefineButtonSound (17) | button symbol, its handlers and state sounds |
| JPEGTables (8), DefineBits/JPEG2/3/4 (6, 21, 35, 90), DefineBitsLossless/2 (20, 36) | bitmaps |
| DefineSound (14) | sound item |
| DefineFont/2/3 (10, 48, 75), DefineFontInfo/2 (13, 62), DefineFont4 (91) | glyphs for static text; font 4 keeps only name and style |
| DefineText/2 (11, 33) | static text as glyph outlines |
| DefineEditText (37) | dynamic and input text fields |
| DefineVideoStream (60), VideoFrame (61) | video item of its size and length; pictures not decoded |
| DefineScalingGrid (78) | 9-slice grid |
| ExportAssets (56), SymbolClass (76) | symbol names and linkage; SymbolClass id 0 is the document class |

A fixed list of tags (Protect, EnableDebugger, ScriptLimits, Metadata,
FileAttributes, DefineFontAlignZones, CSMTextSettings and others) is skipped
silently. Anything else, and DefineBinaryData, is counted in the report's
"Left out" list. Guards cap placements (8 million), sprite nesting (64) and
font records against damaged files.

Images the converter cannot decode itself (JPEG with alpha, PNG or GIF inside
DefineBitsJPEG2) go through the decoder `main.cpp` installs with
`SwfImport::setImageDecoder`.

### How a SWF becomes a document

`timelineElement` builds each timeline's layers, top first:

1. An `Actions` layer when any frame had DoAction/DoInitAction: the frame
   gets a comment script and a `CompiledActions` element.
2. A `Labels` layer.
3. One `Sound` layer per concurrent event sound, and a `Soundtrack` layer
   with the stream (`soundSync="stream"`) on its first frame.
4. One layer per depth and part it plays. A placement with a clip depth
   makes a `mask` layer, and the depths it covers on that frame become
   layers masked by it (`parentLayerIndex`). Movies reuse depths, a mask on
   some frames and plain art on others, so each depth gets a layer for each
   part (named `Depth 3 Mask`, `Depth 3 Masked` when it has more than one),
   holding only the frames it plays that part on. A layer showing only video
   is hidden.

Filters come back as the FLA has them. A color matrix becomes Adjust Color
when some brightness, contrast, saturation and hue make that matrix as
Phoenix draws Adjust Color (`adjustColorOf`, fitted once per matrix);
otherwise it, and a convolution, become Phoenix's own Color Matrix and
Convolution filters, which an FLA keeps as `phoenixColorMatrix` on an
`AdjustColorFilter` and `phoenixConvolution` (with `phoenixMatrixX`,
`phoenixMatrixY`, `phoenixDivisor`, `phoenixBias`, `phoenixPreserveAlpha`,
`phoenixClamp`, `phoenixEdgeColor`, `phoenixEdgeAlpha`) on a `BlurFilter` of
no blur, so other FLA editors show the art without them.

Within a depth, frames are grouped into spans where the placement does not
change. With `Options::recognizeTweens` (the default), spans whose matrix and
color change exactly as a classic tween would (linear or eased) become a
classic tween, and a morph shape whose ratio runs on evenly becomes a shape
tween; a move to the same place extends the previous span. Everything else
stays a keyframe per change.

A shape placed once (with no color transform) is written inline; one placed
more often becomes a graphic symbol. Sprites and buttons become symbols, also
when nothing places them. Several scenes come back when
`DefineSceneAndFrameLabelData` lists more than one; `sliceOf` cuts the main
timeline, and a stream goes with the scene it starts in.

The document root records `creatorInfo="Phoenix (imported from SWF)"`.

### Scripts after import

Compiled code cannot become source, but it still runs:

- AS1/2: `CompiledActions` elements on frames and instances hold each block
  as hex (`DoAction`, `DoInitAction` with its symbol, `ClipAction` with
  events and key, `ButtonAction` with conditions) and the SWF version.
  `play::compiledActionsIn` ([src/play/avm1_player.h](../src/play/avm1_player.h))
  reads them back and `MovieRuntime` runs them. If there is no
  PublishSettings.xml, one saying ActionScript 2 is added.
- AS3: each DoABC body goes to `bin/abc/1.abc`, `2.abc`, ... and a
  PublishSettings.xml with version 3 and the root `DocumentClass`. The parser
  keeps those files in `extraFiles`; [src/gui/movie_hooks.cpp](../src/gui/movie_hooks.cpp)
  hands them to the runtime with `setCompiledScripts`.

The report lists what could not come back: tweens that are not clean, layer
names, library folders, symbol names the movie did not export, guide layers,
scripts as source.

## Test Movie is not a SWF

Control > Test Movie (`MainWindow::testMovie`) opens a `TestMovieWindow`
that plays the document itself with `fla::play::MovieRuntime`, not an
exported SWF. The SWF exporter's job is to produce a file that a standalone
player runs the same way, and several decisions follow Test Movie on
purpose: the AS2/AS3 choice in `Movie`'s constructor, the class path used by
`findClass`, Include hidden layers (which applies to both), and sounds
starting on the timeline they belong to. See [actionscript.md](actionscript.md)
for the runtime.

The two meet in tests: `swf_actions_play_as_the_document_does` plays the
document with `MovieRuntime`, publishes it, imports the SWF, plays the
imported document (running the compiled AVM1 blocks) and requires the same
trace output.

## Tests and inspecting output

Tests are in the `phoenix_tests` executable; the argument is a substring
filter (see [building.md](building.md)):

```
build/Release/phoenix_tests swf_
```

| File | Covers |
| --- | --- |
| [tests/test_swf_writer.cpp](../tests/test_swf_writer.cpp) | the writer's header, shapes, bitmaps and frames (`readMovie`, `shapePoints` decode them) |
| [tests/test_swf_movie.cpp](../tests/test_swf_movie.cpp) | `swfmovie::Movie`: clips, buttons, names, labels, scripts, masks, tweens, hidden layers, scenes, AS2 classes and linkage, compile errors, `unstructured`, color and filters, AS3 to ABC, document classes, opened graphics, morph shapes, sounds on timelines and buttons; most round-trip through `SwfImport` |
| [tests/test_swf_actions.cpp](../tests/test_swf_actions.cpp) | compiled AS1/2 kept on import and run by the runtime, matching the document |
| [tests/test_swf_sound.cpp](../tests/test_swf_sound.cpp) | ADPCM, stream blocks, MP3 dealing, StartSound info, `SwfEventSounds` on scenes and clips, library compression |
| [tests/test_swf_import.cpp](../tests/test_swf_import.cpp) | import of hand-built SWFs: shapes, bitmaps, sounds, scenes, damaged headers, self-nested sprites, tween recognition, morphs, video, preloaders |

Useful helpers, all file-local in the tests (copy what you need):

- `SquareDrawer` (test_swf_movie, test_swf_actions): a `Drawer` that turns
  each element into a 10 px square and interpolates classic tweens linearly,
  so `Movie` runs without Qt or a view.
- `movieOf(document, &problems, options, &frames)`: runs `Movie` with a
  `SquareDrawer` and returns the compressed file.
- `tagsOf(file)`: inflates a `CWS` file and returns every tag as
  `{code, body, sprite}`, recursing into `DefineSprite` so control tags
  inside a sprite carry its id.
- `actionsOf(tags)`: every `DoAction` disassembled with
  `as3::avm1::disassemble`, by sprite. Expected listings look like
  `"Stop\nEnd\n"` or `"GotoFrame 4\nPlay\nEnd\n"`.
- `abcOf(tags)` and `symbolClassesOf(tags)`: the DoABC body and SymbolClass
  entries; the ABC can then be checked with `as3::abc::read`,
  `as3::abc::verify` and `as3::abc::disassemble` (class lines such as
  `" shapes:Box extends flash.display:MovieClip"`, slots such as
  `"slot box slot 0 : shapes:Box"`).
- `namesPlaced(tags, sprite)`: instance names placed in a timeline.
- `movieWithSounds`: an `eventSound` stub that defines a tiny raw sound per
  name, for checking where `StartSound` and `DefineButtonSound` land.

### Comparing with Ruffle

[Ruffle](https://github.com/ruffle-rs/ruffle) (MIT/Apache-2.0) plays SWF
closely to Flash Player, and two scripts use it as a reference. Both run
Phoenix headless and need Ruffle's source (`RUFFLE_SOURCE`, default
`D:/src/ruffle`).

- `tools/ruffle_tests.py` plays Ruffle's regression SWFs
  (`tests/tests/swfs`, about 4,500: AVM1, AVM2, timelines, filters, text)
  with `Phoenix play`, compares the trace with each test's `output.txt`
  (what Flash Player traced) and the last frame with its expected images,
  and prints a summary by folder. A run takes about two minutes. Tests that
  play recorded input, AIR-only and JPEG XR tests are skipped. Keep a
  report.json as a baseline and pass it as `--baseline` after a change: the
  script lists what newly fails and newly passes, and exits 1 on anything
  newly failing.
- `tools/ruffle_compare.py` renders sample frames of SWF movies with Ruffle's
  exporter (built once with `cargo build --release -p exporter` in Ruffle's
  source) and with Phoenix (the imported timeline, or Test Movie with
  `--mode play`), scores how much they differ away from edges, and lists the
  movies that differ most, with side-by-side pictures. Point it at a folder of
  real movies to find import bugs.
- `tools/flash_player_check.py` plays movies in Adobe's Flash Player debug
  projector (`flashplayer_32_sa_debug.exe`, in `build/flashplayer` or at
  `FLASH_PLAYER`) and compares their trace with Test Movie's: for a document,
  its exported SWF in Flash Player against the document in `Phoenix play`;
  for an SWF, Flash Player against Phoenix playing the imported movie. The
  projector runs on a desktop of its own, so no window shows; it reads the
  script's mm.cfg but logs to `%APPDATA%\Macromedia\Flash Player\Logs`, so
  movies run one at a time, in real time.

To look at a real export, write it with `Phoenix --export-movie file.fla
out.swf` (headless with `QT_QPA_PLATFORM=offscreen`), then decode it with the
same approach as `tagsOf`, or reopen it in Phoenix: import reports what it
could not read, and a structure that survives the round trip (clips, names,
masks, labels, tweens) is a good sign the export is right. Remember that
`finish(true)` compresses; `finish(false)` gives an uncompressed `FWS`
that is easier to read in a hex view.
