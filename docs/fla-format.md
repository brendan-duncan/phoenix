# FLA and XFL in Phoenix

How Phoenix reads and writes FLA and XFL documents: the containers, the XML as the
parser models it, the edge strings, the media files, the old binary FLA, and how a
save keeps what Phoenix does not understand.

The user-facing format notes, with corpus counts and the observations behind them,
are [website/fla-format.html](../website/fla-format.html) and
[website/binary-fla-format.html](../website/binary-fla-format.html). This note is
about the code. Where the two disagree, the code is right and the page needs fixing
(see [Known gaps](#known-gaps-and-traps)).

Related notes:

- [architecture.md](architecture.md): the document model as a whole, editing and undo
- [geometry.md](geometry.md): how edges become filled regions, merge drawing, shape tweens
- [phx-format.md](phx-format.md): Phoenix's own `.phx` format
- [swf.md](swf.md): SWF import, which reuses the XFL reader (below)
- [importers.md](importers.md): bringing bitmaps, sounds and other media into a document

Units: a twip is 1/20 pixel. The model holds pixels; the parser divides edge
coordinates by 20 on read and the writer multiplies on write. Matrix `tx`/`ty`,
text boxes and stroke weights are already pixels in the XML.

## Where the code lives

| Part | Code |
| --- | --- |
| Entry point: sniff the file, pick a container, parse | [src/parser/fla_parser.cpp](../src/parser/fla_parser.cpp) `FLAParser` |
| ZIP, folder and in-memory file access | [src/parser/zip_reader.cpp](../src/parser/zip_reader.cpp) `ZipReader`, [src/util/zip_archive.cpp](../src/util/zip_archive.cpp) `ZipArchive` |
| XML to model | [src/parser/document_parser.cpp](../src/parser/document_parser.cpp) `DocumentParser` |
| Edge strings, hex numbers, morph points | [src/parser/path_parser.cpp](../src/parser/path_parser.cpp) `PathParser` |
| IK tree reading | [src/parser/ik_tree.cpp](../src/parser/ik_tree.cpp) `ikBones`, `parseIKCoordinate` |
| Old binary FLA | [src/parser/compound_file.cpp](../src/parser/compound_file.cpp) `CompoundFile`, [src/parser/binary_fla.cpp](../src/parser/binary_fla.cpp) `BinaryFla::convert` |
| Damage checks, repair and salvage | [src/parser/fla_doctor.cpp](../src/parser/fla_doctor.cpp) `fla::doctor` |
| Model to XML | [src/writer/xfl_writer.cpp](../src/writer/xfl_writer.cpp) `XFLWriter` |
| Edge strings out | [src/writer/edge_writer.cpp](../src/writer/edge_writer.cpp) `EdgeWriter` |
| The package's file list | [src/writer/xfl_content.cpp](../src/writer/xfl_content.cpp) `XFLContent::build`, `unsupportedContent` |
| `.fla` and folder output | [src/writer/fla_writer.cpp](../src/writer/fla_writer.cpp) `FLAWriter`, [src/writer/xfl_folder_writer.cpp](../src/writer/xfl_folder_writer.cpp) `XFLFolderWriter`, [src/util/zip_writer.cpp](../src/util/zip_writer.cpp) `ZipWriter` |
| Part-by-part document comparison | [src/writer/document_diff.cpp](../src/writer/document_diff.cpp) `compareDocuments` |
| Bitmap `.dat` | [src/data/bitmap_decoder.cpp](../src/data/bitmap_decoder.cpp) |
| Sound `.dat` | [src/gui/audio_engine.cpp](../src/gui/audio_engine.cpp) `decode`, `parseFormat` |
| Model classes | [src/data/](../src/data/) (`fla::Document`, `Timeline`, `Layer`, `Frame`, `Shape`, ...) |

Everything in `src/parser`, `src/writer` and `src/data` listed here is free of Qt.
The XML library is tinyxml2 (`src/third_party/tinyxml2`).

## Opening a file

`FLAParser::parse(path)` wraps `parseUnguarded` in a `try`/`catch`: any exception
(`std::bad_alloc` included) becomes `errorString()` and a null result, so a damaged
file never takes Phoenix down. `warningString()` carries what of a document that did
open could not be read (used by the binary FLA and SWF paths).

`parseUnguarded` reads the first eight bytes and branches:

| First bytes | Container | What happens |
| --- | --- | --- |
| SWF signature (`SwfImport::isSwf`) | Published movie | `SwfImport::convert` builds XFL files in memory; `ZipReader::openMemory` serves them (see [swf.md](swf.md)) |
| `D0 CF 11 E0 A1 B1 1A E1` | Compound file (Flash CS4 and earlier) | `CompoundFile::read`, then `BinaryFla::convert` into XFL files in memory, then `openMemory` |
| Anything else | ZIP archive, or an XFL folder | `ZipReader::open(path)` |

So every route ends in the same `DocumentParser` reading `DOMDocument.xml` through a
`ZipReader`. The resulting `fla::FLADocument` records `fromBinary` or `fromSwf`
([src/data/fla_document.h](../src/data/fla_document.h)).

### ZipReader's three modes

[src/parser/zip_reader.h](../src/parser/zip_reader.h) hides where the files come from:

- **Archive**: `ZipArchive` over the `.fla`. File data is read on demand.
  `lazyFile(name)` returns a `fla::LazyBytes` ([src/data/lazy_bytes.h](../src/data/lazy_bytes.h))
  that extracts the entry the first time it is needed, holding a `shared_ptr` to the
  archive. Bitmaps and sounds of a production file are not all in memory at once.
- **Folder**: a path that is a directory, or a `.xml` / `.xfl` file inside one (the
  parent directory is used). The directory must hold `DOMDocument.xml`. Names are
  checked by `withinFolder` so an `href` cannot climb out with `..` or an absolute
  path. `getFileList()` in this mode lists only the folder's top-level files.
- **Memory**: a `std::map<path, bytes>` from a converter.

If an archive has no top-level `DOMDocument.xml`, `parseUnguarded` looks for
`<folder>/DOMDocument.xml` and calls `setRoot("<folder>/")`, so a ZIP of an XFL
folder opens too.

### The ZIP reader and damaged archives

`ZipArchive::open` ([src/util/zip_archive.cpp](../src/util/zip_archive.cpp)) finds
the end-of-central-directory record, takes the directory's **offset and entry
count** from it (from the Zip64 record when the 32-bit fields are `0xFFFFFFFF` /
`0xFFFF`), and then `parseCentralDirectory` reads headers until the count is reached.
It never uses the stated directory size. That is what makes Animate files whose
end record overstates the directory size (by 54 bytes in the corpus, per the website
page) open, where readers that derive the archive start from the size reject them.

Other defenses, covered by `zip_archive_survives_damaged_directories` in
[tests/test_zip_archive.cpp](../tests/test_zip_archive.cpp):

- The entry loop also stops at the most headers that could fit before the end of the file.
- `extractFile` refuses an entry whose compressed size runs past the end of the file
  before allocating for it, and checks the local header signature.
- Only methods 0 (stored) and 8 (deflate) are extracted.

A file too damaged for this (a truncated save with no central directory at all) is
the job of `fla::doctor::salvage`, which reads entries from their local headers into
an XFL folder ([src/parser/fla_doctor.h](../src/parser/fla_doctor.h); `Phoenix doctor <file>`
on the command line).

## The package

What an XFL package holds, and what Phoenix does with each part:

| File | Read as | Written by |
| --- | --- | --- |
| `DOMDocument.xml` | `DocumentParser::parse` | `XFLWriter::writeDocument` |
| `LIBRARY/<href>` (a symbol's `.xml`) | `parseSymbolInclude`, one per `<Include>` | `XFLWriter::writeSymbol`, at `XFLWriter::symbolHref` |
| `LIBRARY/<href>` (a bitmap as imported: PNG, JPEG...) | `Bitmap::imageData` (lazy) | Carried through from `imageData` |
| `bin/<bitmapDataHRef>` | `Bitmap::datData` (lazy) | Carried through from `datData` |
| `bin/<soundDataHRef>`, other `*DataHRef` media | `Document::extraFiles` | Carried through |
| `bin/SymDepend.cache` | Ignored | Not written |
| `PublishSettings.xml`, `MobileSettings.xml`, `META-INF/*`, `mimetype`, anything else | `Document::extraFiles` (`FLAParser::keepUnreadFiles`) | Carried through byte for byte |
| `<name>.xfl` at the top level | Ignored | Written afresh as `<name>.xfl` containing `PROXY-CS5` |
| `bin/phoenix_texture_*` | Ignored | Made again (textured paint, below) |

`keepUnreadFiles` runs only for real FLA/XFL input (not for binary or SWF
conversions). It builds the set of files the model already accounts for
(`DOMDocument.xml`, every symbol's `LIBRARY/` file, every bitmap's `LIBRARY/` and
`bin/` file, `extraFiles` already collected by the parser) and keeps every other
non-directory entry as a lazy `extraFiles` entry. Because a folder lists only its top
level, files in subfolders of a folder document reach `extraFiles` only when the
parser named them (sound data through `parseMedia`, an `Include` in an unmodeled
section) or, for reference-board pictures, by the path the board gives
(`fla::references::of`).

On save, `XFLContent::build` adds `META-INF/metadata.xml` (empty) and `mimetype`
(`application/vnd.adobe.xfl`) when the document did not carry its own. Entries go out
in this order: `DOMDocument.xml`, the `.xfl` marker, symbol files, baked texture
tiles, bitmaps (`LIBRARY/` copy then `bin/` data), `extraFiles`, then the two
defaults. `ZipWriter` deflates each entry and stores it instead when deflate would not
make it smaller. `FLAWriter` names the marker after the file's stem;
`XFLFolderWriter` after the folder name (or the name passed in).

Paths come from the file being edited, so they are checked:
`XFLContent::isSafeRelativePath` rejects absolute paths and `..`; a symbol or bitmap
with an unusable path fails the save, and an unsafe `extraFiles` path is skipped.
`XFLWriter::fileSafeHref` replaces `<>:"\|?*`, control characters, and a trailing
dot or space on any path part with `_`, so a library name Windows cannot hold as a
file name still saves. The symbol keeps its real `name` inside its file.

## DOMDocument.xml

Root `<DOMDocument xmlns="http://ns.adobe.com/xfl/2008/">`. `parseDocument` in
[src/parser/document_parser.cpp](../src/parser/document_parser.cpp) reads the
attributes into `fla::Document` ([src/data/document.h](../src/data/document.h)) and
dispatches on each child:

| Child | Model |
| --- | --- |
| `<folders>` of `DOMFolderItem` | `Document::folders` (`fla::Folder`: `name` is the library path, `itemID`, `isExpanded`) |
| `<media>` | `DOMBitmapItem` to `Document::resources` (`fla::Bitmap`); everything else (sounds, videos) as XML text in `Document::extraMedia`, plus `Document::sounds` / `Document::videos` summaries |
| `<symbols>` of `<Include href>` | `Document::symbolList` (`fla::SymbolList`), each symbol read from `LIBRARY/<href>` |
| `<timelines>` of `DOMTimeline` | `Document::timelines`: the scenes |
| `<publishHistory>`, `<PrinterSettings>`, `<scripts>` | XML text on `PublishHistory`, `PrinterSettings`, `ScriptList` |
| `<swatchLists>` | `SwatchList` per list: solid swatches modeled, the whole list's XML kept in `SwatchList::xml` and written back from it |
| anything else (`<fonts>`, `<extendedSwatchLists>`, `<brushdefinitions>`...) | `Document::extraSections` as XML text, with `beforeTimelines` set when it came before `<timelines>`; any `<Include href>` inside is loaded into `extraFiles` |

Modeled root attributes include `width`/`height` (default 550 x 400), `frameRate`
(24), `backgroundColor`, `currentTimeline` (1-based scene index, as text),
`xflVersion`, `creatorInfo`, `platform`, `versionInfo`, `majorVersion`,
`buildNumber`, grid, guide and snap settings, `viewAngle3D`, `vanishingPoint3DX/Y`,
`nextSceneIdentifier`, and the three `playOptions*` flags (true unless the file says
otherwise). Phoenix's own: `phoenixStageAlpha` (stage transparency, written as the
alpha of `backgroundColor`), `phoenixDrawingGuides`, `phoenixSymmetry`.

`writeDocument` writes children in this order: `folders`, `fonts` (pulled out of
`extraSections`), `media` (bitmaps, then `extraMedia`), `symbols`, the
`extraSections` that came before the timelines, `timelines`, then `PrinterSettings`,
`publishHistory`, `swatchLists`, `scripts`, and the remaining `extraSections`. A
document with no `xflVersion` is written with `23.0`, no `creatorInfo` with
`Phoenix`, no `currentTimeline` with `1`.

After parsing, `DocumentParser::parse` resolves each `SymbolInstance::libraryItemName`
through `SymbolList::symbolMap`, fills `Document::symbolInstancesMap`, calls
`noteCenterPoints`, cuts symbol cycles with `breakSymbolCycles` (the instance that
closes a loop keeps its `libraryItemName` but loses its `symbol` link, so it saves as
read and draws as missing), and collects video items.

## Library items

### Symbols

Each `<Include href="Folder/Name.xml" itemIcon loadImmediate itemID lastModified/>`
loads `LIBRARY/<href>`. The root must be `DOMSymbolItem` or `DOMComponentItem` (a
component; recorded in `Symbol::itemElement` and written back as the same root):

```xml
<DOMSymbolItem name="Characters/Bob/head" itemID="..." lastModified="..." symbolType="graphic">
  <timeline>
    <DOMTimeline name="head">...</DOMTimeline>
  </timeline>
</DOMSymbolItem>
```

`fla::Symbol` ([src/data/symbol.h](../src/data/symbol.h)):

- `name` (the library path), `itemId`, `lastModified`, `href` (kept, not derived:
  usually `name + ".xml"` but not always). `XFLWriter::symbolHref` falls back to
  `name + ".xml"` for a symbol made in this session.
- `symbolType`: no attribute means movie clip; `button`, `graphic`. Anything else
  (`puppet`, `puppetBase`, `ik container`...) is drawn as a graphic with the spelling
  kept in `otherSymbolType`.
- `scaleGridLeft/Top/Right/Bottom`: the 9-slice guides; `scale9` is set when
  `scaleGridLeft` is present.
- `includeAttributes`: the `Include` element's other attributes, written back on it.
- Other root attributes (linkage, `linkageClassName`...) go to `extraAttributes`;
  children other than `<timeline>` (such as `persistentData`) to
  `Resource::extraChildren`.

A missing `LIBRARY/` file, unparseable XML or an unexpected root makes
`parseSymbolInclude` return false, which fails the whole document parse.

### Bitmaps

`DOMBitmapItem` becomes `fla::Bitmap` ([src/data/bitmap.h](../src/data/bitmap.h)):
`name`, `itemID`, `href`, `bitmapDataHRef`, `sourceExternalFilepath`,
`sourceLastImported`, `allowSmoothing`, `quality`, `frameRight`, `frameBottom`.
`imageData` is `LIBRARY/<href>` and `datData` is `bin/<bitmapDataHRef>`, each lazy and
each only when present. A bitmap with neither is not modeled: its item is kept in
`extraMedia` instead. An item marked `phoenixTextureBake="true"` is skipped (see
textured paint below).

### Sounds and videos

Not modeled as library resources. The item element is kept whole in `extraMedia`,
every `*DataHRef` attribute's `bin/` file and an `href`'s `LIBRARY/` file go to
`extraFiles`, and a `Document::SoundInfo` (`name`, `format`, `dataPath`) is recorded
for playback. `collectVideoItems` reads `DOMVideoItem` entries in `extraMedia` into
`Document::videos`.

## Timelines, layers, frames

```xml
<DOMTimeline name="Scene 1" guides="...">
  <layers>
    <DOMLayer name="Layer 1" color="#4F80FF" current="true" isSelected="true">
      <frames>
        <DOMFrame index="0" duration="10" tweenType="motion" acceleration="-50">
          <tweens><Ease target="all" intensity="-50"/></tweens>
          <elements>...</elements>
        </DOMFrame>
      </frames>
    </DOMLayer>
  </layers>
</DOMTimeline>
```

### DOMTimeline

`fla::Timeline` ([src/data/timeline.h](../src/data/timeline.h)): `name`,
`layerDepthEnabled`, and ruler guides. The `guides` attribute holds XML of its own,
`<guidelines><guideline direction="v">x</guideline>...</guidelines>`;
`parseGuides` reads it into `guidesX` / `guidesY`, and when it does not parse the
attribute is kept as text instead. The writer emits positions with up to two decimals.
Other attributes and children (other than `<layers>`) are kept.

### DOMLayer

`fla::Layer` ([src/data/layer.h](../src/data/layer.h)). Layers are listed top to
bottom: `layers[0]` draws on top.

| Attribute | Field | Notes |
| --- | --- | --- |
| `name`, `color` | `name`, `color` | The writer always writes `color`, black included |
| `layerType` | `layerType` | `normal` (absent), `folder`, `mask`, `masked`, `guide`; another spelling goes to `otherLayerType` and draws as normal |
| `parentLayerIndex` | `parentLayerIndex`, `parentLayer` | 0-based index of the folder or mask above; -1 for none |
| `visible`, `locked`, `outline`, `isSelected`, `autoNamed`, `current` | same names | `autoNamed` defaults to true |

`parseLayers` links `parentLayer` only to an earlier layer in the list, and a layer
whose parent is a `mask` layer becomes `Masked`, because files often write masked
layers as `normal` with a `parentLayerIndex`.

Layer parenting (a parent layer that moves its children, as opposed to folder
membership) is not modeled by the parser: `layerRiggingIndex` on the parent layer and
`parentLayerIndex` on each child keyframe stay in `extraAttributes`, and editing code
such as [src/edit/layer_commands.cpp](../src/edit/layer_commands.cpp),
[src/gui/layers_panel.cpp](../src/gui/layers_panel.cpp) and
[src/edit/html5_export.cpp](../src/edit/html5_export.cpp) reads them from there.

`parseFrames` computes `Layer::firstFrame` / `lastFrame` from the spans.

### DOMFrame

`fla::Frame` ([src/data/frame.h](../src/data/frame.h)). A `DOMFrame` is a span:
`index` (0-based) and `duration` (default 1). There are no elements for the frames in
between. Both are clamped by `clampFrameIndex` / `clampFrameDuration` to
`kMaxFrames` (2^24) so `index + duration` cannot overflow.

| Attribute or child | Field |
| --- | --- |
| `keyMode` | `keyMode` (kept as text, meaning unknown) |
| `name`, `labelType` | Frame label and its kind (`name`, `comment`, `anchor`) |
| `tweenType` | `motion` and `shape` to `TweenType`; others (`motion object`, `IK pose`) to `otherTweenType` |
| `motionTweenSnap` (also the misspelling `motionTWeenSnap`), `motionTweenSync` | `motionTWeenSnap`, `motionTweenSync` (the writer writes `motionTweenSnap`) |
| `motionTweenRotate`, `motionTweenRotateTimes` | Classic tween rotation |
| `acceleration` | -100 to 100 |
| `hasCustomEase` + `<tweens><CustomEase target="all">` of `<Point x y>` | `hasCustomEase`, `customEase`, `customEaseName` |
| `<tweens><Ease target="all" method intensity>` | `hasEase`, `easeMethod`; `intensity` fills `acceleration` when that attribute was 0 |
| `<tweens>` children for a target other than `all`, or of another kind | `otherEases` as XML text |
| `<Actionscript><script>` | `actionScript->code` (the `<script>` text, usually CDATA) |
| `<MorphShape>` | `morphShape` (shape tween data, below) |
| `<elements>` | `elements` |
| anything else (`<SoundEnvelope>`, `<motionObjectXML>`, `<IKTree>`, `<betweenFrameMatrixList>`...) | `extraChildren` as XML text |
| other attributes (`soundName`, `soundSync`, `inPoint44`, `outPoint44`, `poseLocations`, `isIKPose`, `parentLayerIndex`...) | `extraAttributes` |

Higher layers interpret several of these kept parts without the parser modeling them:
[src/edit/frame_sound.h](../src/edit/frame_sound.h) reads and writes a frame's sound
attributes and `SoundEnvelope`; [src/edit/motion_object.h](../src/edit/motion_object.h)
parses the `AnimationCore` document in `motionObjectXML`;
[src/parser/ik_tree.cpp](../src/parser/ik_tree.cpp) and
[src/edit/ik_pose.cpp](../src/edit/ik_pose.cpp) read `IKTree` text. If you add a
feature over one of these, keep the data in the kept XML or attributes so a save that
never touched it writes it back unchanged.

`writeFrame` writes, in order: the modeled attributes, `<tweens>` (an `Ease` when there
is a preset, `hasEase`, or a non-zero `acceleration` on a tween; the `CustomEase` when
there is one, in place of the `Ease`; then `otherEases`), `<Actionscript>`,
`<MorphShape>` (only on a shape tween), the kept attributes, the kept children except
`IKTree` and `betweenFrameMatrixList`, `<elements>`, and finally those two, which
the FLA stores after the elements.

### Shape tween morphs

`<MorphShape><morphSegments><MorphSegment startPointA startPointB strokeIndex1
strokeIndex2 fillIndex1 fillIndex2><MorphCurves controlPointA anchorPointA
controlPointB anchorPointB isLine/>` maps to `MorphShape` / `MorphSegment` /
`MorphCurves` ([src/data/morph_shape.h](../src/data/morph_shape.h)).
`<morphHintsList>` is kept as `MorphShape::hintsXml`.

Morph coordinates are `"x, y"` pairs in the same number syntax as edges but scaled by
256: `PathParser::parseMorphPoint` returns the value times 256 / 20 pixels
(`#9.61` reads as 0x9.61 = 2401/256, so 2401 twips). `EdgeWriter::formatMorphCoordinate`
writes the inverse: whole part = twips / 256 as 24-bit two's complement hex, then two
hex digits of remainder (one when the low digit is 0).

`parseLayer` and `writeLayer` both call `ensureMorph(frame, end)` from
[src/edit/shape_tween.h](../src/edit/shape_tween.h) for every shape tween keyframe
with a following keyframe, so a stored morph is kept until either keyframe's shape
changes and then rebuilt. See [geometry.md](geometry.md).

## Elements

`parseElementByType` dispatches on the element name:

| XML | Model class | Notes |
| --- | --- | --- |
| `DOMShape` | `fla::Shape` | `isDrawingObject="true"` for Object Drawing; else merge-mode artwork |
| `DOMSymbolInstance` | `fla::SymbolInstance` | Also appended to `Document::symbolInstances` |
| `DOMGroup` | `fla::Group` | `<members>` read recursively |
| `DOMStaticText`, `DOMDynamicText`, `DOMInputText` | `fla::StaticText` | `textType` set from the tag |
| `DOMBitmapInstance` | `fla::BitmapInstance` | `libraryItemName`; other children kept |
| `DOMRectangleObject` | `fla::RectanglePrimitive` | `x`, `y`, `objectWidth`, `objectHeight`, four corner radii, `<fill>`, `<stroke>` |
| `DOMOvalObject` | `fla::OvalPrimitive` | `x`, `y`, `objectWidth`, `objectHeight`, `startAngle`, `endAngle`, `innerRadius`; other attributes (`closePath`...) kept |
| `DOMTLFText` | `fla::StaticText` via `convertTlfText` | Converted to classic text when its `<markup>` `TextFlow` parses; saved as classic text |
| anything else (components, `DOMVideoInstance`, TLF that did not convert) | `fla::RawElement` | Whole XML in `RawElement::xml`, written back in place, not drawn (a video instance gets bounds and `videoItem` from `describeVideo`) |

Every element goes through `parseElement` first ([src/data/element.h](../src/data/element.h)):

- `isSelected`, `isFloating`, `lockFlag` (to `isLocked`), `isVisible` (to `visible`).
- `<matrix><Matrix a b c d tx ty/></matrix>`: missing attributes take identity
  values; `tx`, `ty` in pixels. `keepMatrix` records that the file wrote one, so an
  identity matrix the file wrote is written back.
- `<transformationPoint><Point x y/></transformationPoint>`: the pivot, with
  `keepTransformationPoint` likewise.

### Symbol instances

`parseSymbolInstance` reads `libraryItemName`, `name`, `blendMode` (`normal` is
stored as empty), `firstFrame`, `lastFrame`, `symbolType` (absent means movie clip;
unknown types to `otherSymbolType`), and `loop` or the older `loopType` (`loop`,
`play once` (default), `single frame`, `ping pong`, `loop reverse`,
`play once reverse`). The writer always writes `loop` on a graphic instance.

- `<color><Color .../></color>` to `colorTransform` (multipliers, offsets,
  `brightness`, `tintMultiplier`, `tintColor`); `keepColor` when the element was
  present even with no effect.
- `<filters>`: `BlurFilter`, `DropShadowFilter`, `GlowFilter`, `BevelFilter`,
  `GradientGlowFilter`, `GradientBevelFilter`, `AdjustColorFilter` to `fla::Filter`
  ([src/data/filter.h](../src/data/filter.h)); an unknown filter is kept in
  `extraFilters` and written back after the known ones.
- Other children (a button's or clip's `Actionscript`) to `extraChildren`.
- A 3D-placed instance has `matrix3D` (16 numbers) among its kept attributes and no
  `<matrix>`. The parser derives the 2D `transform` with `SymbolInstance::transformOf3D`
  and moves the transformation point (stored in the parent's space) into the
  instance's own. On save `writeElementCommon` writes the point back in parent space
  and `SymbolInstance::keptAttributes` rewrites `matrix3D` if the instance moved.

### Text

`StaticText` ([src/data/static_text.h](../src/data/static_text.h)) has `left`,
`top`, `width`, `height`, `autoExpand`, `instanceName`, `lineType`, and `runs` from
`<textRuns><DOMTextRun><characters>` + `<textAttrs><DOMTextAttrs>` (`alignment`,
`aliasText`, `autoKern`, `letterSpacing`, `lineHeight`, `size`, `bitmapSize`, `face`,
`fillColor`; other attributes kept per run). Empty runs are kept: they carry
paragraph formatting.

Two whitespace traps, both handled:

- tinyxml2 drops text that is only whitespace, so `<characters> </characters>` would
  lose its space. `keepBlankCharacters` rewrites such runs as character references
  before parsing (`DOMDocument.xml` and every symbol file).
- Line breaks inside runs are CR. A literal CR in XML reads back as LF, so the writer's
  `toText` turns every CR in the printed document into `&#13;`. The printer only emits
  LF for its own formatting, so every CR is data.

## Shapes

```xml
<DOMShape isDrawingObject="true">
  <fills>
    <FillStyle index="1"><SolidColor color="#FF0000"/></FillStyle>
  </fills>
  <strokes>
    <StrokeStyle index="1">
      <SolidStroke weight="2" caps="none"><fill><SolidColor/></fill></SolidStroke>
    </StrokeStyle>
  </strokes>
  <edges>
    <Edge fillStyle1="1" strokeStyle="1" edges="!0 0|2000 0|2000 2000|0 2000|0 0"/>
    <Edge cubics="!0 0(;...);"/>
  </edges>
</DOMShape>
```

`fla::Shape` ([src/data/shape.h](../src/data/shape.h)) holds `fills` / `strokes` in
read order and `fillsMap` / `strokesMap` keyed by the `index` attribute; edges refer
to styles by those indexes. The writer iterates the maps, so styles go out in index
order.

**Fill styles** (`parseFillStyle`): `SolidColor` (`color`, `alpha`; other attributes
such as a palette link kept), `LinearGradient`, `RadialGradient` (`<matrix>`,
`spreadMethod`, `interpolationMethod`, `<GradientEntry color alpha ratio>` sorted by
ratio, `focalPointRatio` on radial), `BitmapFill` (`bitmapPath`, `bitmapIsClipped`,
`<matrix>`). A `BitmapFill` with `phoenixSolidColor` is a texture tile Phoenix baked;
it reads back as the `SolidColor` it stood for.

**Stroke styles** (`parseStrokeStyle`): `SolidStroke`, `DashedStroke`,
`RaggedStroke`, `StippleStroke`, `DottedStroke`, `HatchedStroke`, `PaintBrushStroke`
([src/data/stroke_style.h](../src/data/stroke_style.h)). Modeled attributes:
`weight` (default 1), `scaleMode`, `solidStyle` (`hairline`), `caps`, `joints`,
`miterLimit` (3), `pixelHinting`, `dash1`/`dash2` (6), `dotSpace` (3),
`paintBrushDefinition`; the procedural parameters (`pattern`, `waveHeight`,
`waveLength`, `dotSize`, `variation`, `density`, `hatchThickness`, `space`,
`jiggle`, `rotate`, `curve`, `length`) go to `styleAttributes` by name. The stroke's
`<fill>` holds a fill style. Other children, notably `<VariablePointWidth>` of
`<WidthMarker position left right type>`, are kept in `StrokeStyle::extraChildren`,
and a variable-width profile is also read into `widthProfile`.

**Fill sides**: `Edge::fillStyle0` is the fill on the right of the edge's direction
and `fillStyle1` on the left, in y-down space. Absent attributes read as -1; the writer
leaves -1 out. How faces are assembled from edges is in [geometry.md](geometry.md).

### The edges string

`parsePath` reads an `<Edge edges="...">` with `PathParser::parse`
([src/parser/path_parser.cpp](../src/parser/path_parser.cpp)) into an `fla::Edge`
([src/data/edge.h](../src/data/edge.h)) of `Path`s of `PathSegment`s, and keeps the
original text in `Edge::data`.

| Token | Meaning | Segment |
| --- | --- | --- |
| `!x y` | Move to | `Move` (or none, see below) |
| `\|x y` | Line to | `Line` |
| `/x y` | Line to (same as `\|`) | `Line` |
| `[cx cy x y` | Quadratic to | `Quad` (control, end) |
| `Sn` (or `sn`) | Style number after a move, meaning unknown | `Path::styleIndex` |
| `FSn`, `LSn` | Accepted, not seen in Animate output | `Path::fillStyleIndex`, `lineStyleIndex` |
| `(` ... `)` | A cubic run, rarely in `edges` | `Cubic` via `parseCubicCurve` |
| `;` | Separator, skipped | |

Rules the parser applies:

- A `!` to the point where the previous segment ended continues the current path
  instead of starting a new one (Animate writes `!a|b!b|c` for joined pieces). Points
  are compared after rounding to whole twips (`pointsMatch`).
- On a `!` that does start a new path, a `Close` segment is appended to the previous
  path if it ended at its own start. The format has no close token; `EdgeWriter`
  writes nothing for `Close`.
- A line or curve before any move fails the edge (`errorString`); `parseEdges`
  then fails the shape, and the shape is left out of its frame.
- An unknown character stops parsing that string, keeping what was read.
- `Path` holds a single `styleIndex`, so a second `S` in the same path overwrites the
  first (documented by `mid_path_style_changes_are_lost_by_the_reader` in
  [tests/test_edge_writer.cpp](../tests/test_edge_writer.cpp)).

`parsePoint` stores the original token text in `Point::xs` / `Point::ys`.

### Numbers

`PathParser::parseNumber`, shared with morph points and `cubics`:

- Decimal: optional `-`, digits, optional `.digits`. `2000`, `-850`, `2081.5`.
- Hex: `#` then hex digits, optionally `.` and fraction digits. The integer part is a
  24-bit two's complement value (above 0x7FFFFF reads as negative). The fraction is
  `digits / 16^count`, so `#11C2.6A` is 0x11C2 + 0x6A/256 and `#A.8` is 10.5.
  `#FFF973.D` is -1677 + 13/16. More than 15 hex digits read as 0.
- A damaged number (a bare `-`) reads as 0 rather than throwing.

`EdgeWriter::formatCoordinate` ([src/writer/edge_writer.cpp](../src/writer/edge_writer.cpp))
snaps to 1/256 twip, writes whole twips in decimal, and fractional ones as hex fixed
point with two fraction digits (negatives as six-digit 24-bit two's complement). Past
the 24-bit range it writes the nearest whole twip in decimal, which has no limit.

### Cubics

Shapes drawn with cubic tools carry extra `<Edge cubics="...">` elements with no style
attributes. The `edges` strings are the quadratic approximation that is drawn; the
`cubics` text is for editing the curves again. Phoenix does not parse them for
drawing: `parseEdges` stores each as XML text in `Shape::cubicEdges` together with the
number of `edges` elements before it, and `Shape::readEdgeCount` remembers how many
edges were read.

`Shape::cubicsStillMatch()` is true while the edge count is unchanged and every edge
still has its original `data`. Only then does `writeShape` put the kept cubics
records back in their original places. Once any edge is edited they would describe
something else, so they are dropped.

For an edge with no `data` (made or edited in Phoenix), `writeShape` calls
`EdgeWriter::writeEdgeAsAnimate`: each `Cubic` segment is split (`cubicToQuads`)
until each quadratic is within half a twip, the quadratics go into `edges`, and the
cubic itself goes into a following `<Edge cubics>` record in the short form:

```
!x0 y0(;c1x,c1y c2x,c2y x3,y3q x0 y0Qcx cyqx yQ...);
```

Whole twips in decimal; `q` marks a point on the curve, `Q` a control point.
`EdgeWriter::writeEdge` (no cubics list) instead writes the cubic inline as
`(c1 c2 end)`, which `parseCubicCurve` reads back; it is used by tests and callers
that need an exact string rather than an Animate-style file.

Style selects (`S`, `FS`, `LS`) are written after the move that opens the path,
because the reader attaches them to the path that move started.

## Media `.dat` formats

### Bitmaps

`bin/<bitmapDataHRef>` is one of ([src/data/bitmap_decoder.h](../src/data/bitmap_decoder.h)):

- **JPEG**: starts `FF D8 FF` (`isJpegData`); any image library reads it.
- **Lossless**: `isLosslessBitmapData` checks u16 `0x0503` at byte 0. Little-endian
  header: u16 magic, u16 bytes per row, u16 width, u16 height, then the frame in twips
  (four u32: 0, width x 20, 0, height x 20 as Phoenix writes it), then two bytes. From
  byte 26, pieces of u16 length + bytes until a zero length; joined, they are one zlib
  stream of rows. 4 bytes per pixel is A, R, G, B with color premultiplied; when the
  row is only 3 x width bytes it is R, G, B, opaque.

`decodeLosslessBitmap` does not trust the header with memory: it refuses more than
2^28 pixels, more than 1 GiB of rows, or a claimed size the compressed stream could not
inflate to, and keeps a short stream's partial rows. `encodeLosslessBitmap` writes
4-byte rows deflated at best compression in 2048-byte pieces.

When a bitmap has both `LIBRARY/<href>` and a `.dat`, either can be drawn from, and a
save writes both back as they were.

### Sounds

`bin/<soundDataHRef>` is MP3 or raw PCM described by the item's `format`
(`"44kHz 16bit Stereo"`). `parseFormat` in
[src/gui/audio_engine.cpp](../src/gui/audio_engine.cpp) maps 5, 11, 22 and 44 to
5512, 11025, 22050 and 44100 Hz, accepts 8 or 16 bits, and takes `Stereo` for two
channels. `looksLikeMp3` accepts an `ID3` tag or a full MPEG audio frame header
(version, layer, bitrate and sample rate fields all valid), not just the sync bits,
because raw PCM can begin `FF FF`.
Sound data is never re-encoded on save: it is an `extraFiles` entry.

## The binary FLA (Flash CS4 and earlier)

The layouts are documented field by field in
[website/binary-fla-format.html](../website/binary-fla-format.html); this is how the
code is arranged.

**Container.** `CompoundFile::read` ([src/parser/compound_file.cpp](../src/parser/compound_file.cpp))
is a self-contained MS-CFB reader: header, FAT (109 header entries plus the DIFAT
chain), directory, mini FAT and mini stream. It returns every stream by name, with
storages as `Storage/Stream` prefixes. Chains are guarded against loops, the directory
tree is walked iteratively, and the total stream bytes are capped at twice the file
size plus 1 MiB, so a hostile file cannot blow up memory. Streams are `Contents`,
`Page N` (scenes), `Symbol N` (library symbols) and `Media N` (bitmaps, sounds,
videos); CS3/CS4 may name timeline streams `P N <time>` / `S N <time>`.

**Objects.** [src/parser/binary_fla.cpp](../src/parser/binary_fla.cpp) has a
`Reader` over one stream that implements MFC `CArchive::ReadObject`: class
declarations (`0xFFFF`), new objects (`0x8000 | class`), back-references, and the
32-bit `0x7FFF` escape, with a load table per stream. Each MFC class name maps to a
reader function in two tables: timeline classes (`CPicPage`, `CPicLayer`,
`CPicFrame`, `CPicShape`, `CPicRect`, `CPicSymbol`, `CPicSprite`, `CPicButton`,
`CPicText`, `CPicBitmap`, `CPicVideoStream`, `CPicVideo`, `CPicMorphShape`,
`CMorphSegment`, `CMorphCurve`, `CObList`, `CMorphHintItem`) and `Contents` classes
(`CDocumentPage`, `CMediaBits`, `CMediaSound`, `CMediaVideoStream`, `CMediaVideo`,
`CColorDef`, `CQTAudioSettings`). Readers branch on each object's own schema byte,
not the file version, because Flash copies unchanged streams verbatim. Every failure
throws `ParseError` with the stream name and offset.

**Conversion.** `BinaryFla::convert`:

1. Reads `Contents` (`readContents`): version, scene list, symbol list, media list,
   document settings, folders. A failure after the version is known is recorded and
   reading continues; without a version it is fatal.
2. Reads every stream the scenes and symbols name, plus any `Page ` / `Symbol `
   stream, each starting with `u8 1` and a `CPicPage`. On a `ParseError` the page is
   kept as far as it was read (the layers before the failure, and the one it was in up
   to the failure) and the failure goes into the warning.
3. `Writer::write` emits `DOMDocument.xml`, `LIBRARY/<path>.xml` per symbol and
   `bin/<media>.dat` per media stream into the in-memory file map. Library paths come
   from the folder chain. Layers are reversed (binary stores bottom first). Frame
   indexes are running sums of durations. Shape edges convert one for one (the binary
   stores the same 1/256-twip units; bit 15 of a style index, the selection flag, is
   masked off). A `Media N` stream's bytes become `bin/Media N.dat` unchanged; an
   embedded video becomes a `DOMVideoItem` that records only where it came from.

The XFL parser then reads that map like any other document. Because MFC objects carry
no length, a class Phoenix does not know ends its stream; the rest of the document
still opens and `FLAParser::warningString()` says what is missing.

What converts (from the comment in [src/parser/binary_fla.h](../src/parser/binary_fla.h)):
stage size, frame rate, background; folders, symbols and their timelines; scenes;
normal, guide and folder layers; frame durations, classic tweens and eases, labels,
scripts (including MX-era normal-mode action lists turned into ActionScript), sounds;
shapes and groups with solid, gradient and bitmap fills and strokes; symbol instances;
placed bitmaps and videos; rectangle primitives (as the drawing objects they draw as);
shape tweens (as the tween type only, since Phoenix tweens from the keyframes); static
text; JPEG and lossless bitmaps, sounds and embedded video. CS4 drawing objects are not
yet read.

A document opened this way has `FLADocument::fromBinary` set. `keepUnreadFiles` is not
run for it, and saving writes the current XFL-based format; there is no binary writer.

Tests: [tests/test_binary_fla.cpp](../tests/test_binary_fla.cpp) builds compound files
and MFC streams by hand (`compound_file_reads_streams_from_sectors_and_the_mini_stream`,
the damage tests, Flash 5 / MX / Flash 8 / CS4 cases) and compares a binary corpus file
with its XFL twin (`binary_fla_reads_as_its_xfl_twin`). `binary_fla_sweep` is opt-in:
`PHOENIX_BINARY_FLA_LIST` names a file of paths and `PHOENIX_BINARY_FLA_REPORT` gets
one line per document, for comparing results across many files before and after a
change.

## Round-trip fidelity

The rule: anything the parser reads but does not model is kept as it came and written
back in its place. Saving an unedited document must not lose an element or attribute.

### What is kept, and where

| Mechanism | Holds | Written back by |
| --- | --- | --- |
| `DOMElement::extraAttributes` | Unmodeled attributes, name and value, in read order | `writeKept` after the modeled attributes; one the writer already set wins |
| `DOMElement::presentAttributes` | Names of modeled attributes the file wrote | `setString` / `setInt` / `setDouble` / `setBool` / `setColor` write them even at the default (`wasPresent`) |
| `extraChildren` on `Element`, `Layer`, `Frame`, `Timeline`, `Resource`, `StrokeStyle` | Unmodeled child elements as XML text | `appendRawXml`, after the modeled children |
| `RawElement::xml` | A whole element of an unmodeled kind | `writeElement`, in its place among the frame's elements |
| `Shape::cubicEdges` | `<Edge cubics>` records with their positions | `writeShape`, while `cubicsStillMatch()` |
| `Edge::data` | The original `edges` string | `writeShape`, unless cleared by an edit |
| `Frame::otherEases`, `SymbolInstance::extraFilters`, `MorphShape::hintsXml` | Single-property eases, unknown filters, shape hints | Their writers |
| `otherSymbolType`, `otherLayerType`, `otherTweenType` | Enum values Phoenix does not model, as spelled | Written in place of the enum |
| `keepMatrix`, `keepTransformationPoint`, `SymbolInstance::keepColor` | The file wrote an identity matrix, origin pivot or empty color | Written back even though default |
| `Document::extraSections`, `extraMedia`, `PrinterSettings::xml`, `PublishHistory::xml`, `ScriptList::xml`, `SwatchList::xml` | Whole document sections | `writeDocument` in Animate's order |
| `Document::extraFiles` | Package files by path, lazily read | `XFLContent::build`, byte for byte |
| `Symbol::includeAttributes` | The `Include` element's attributes | `writeDocument` |

`keepAttributes(element, {modeled...}, into)` in
[src/parser/document_parser.cpp](../src/parser/document_parser.cpp) is the helper that
fills `extraAttributes` and `presentAttributes`. The writer's side is `PresentScope`,
which makes the node being written and its `presentAttributes` current for the
`set*` helpers, and `writeKept`. Kept XML text is printed compactly by `elementText`
and re-parsed by `appendRawXml`.

### What the writer regenerates

The writer never copies `DOMDocument.xml` or a symbol file through. It rebuilds each
from the model with tinyxml2, so:

- Attribute order within an element is modeled attributes first, then kept ones.
  Whitespace and formatting are tinyxml2's. A round trip preserves content, not
  bytes.
- Defaults are left out as the format leaves them out, unless `presentAttributes`
  says the file wrote them. Some attributes are always written because files always
  carry them (a layer's `color`, a graphic instance's `loop`, a frame's `index`).
- Numbers go through `formatDouble`: integers as integers, others as the shortest
  `%.Ng` (6 to 17 digits) that reads back as the same double.
- Colors: `parseHexColor` rounds the alpha to the nearest 1/255 step; `setColor`
  writes `#RRGGBB` and the alpha separately.
- Edges are written from `Edge::data` when present (exact), regenerated otherwise.
- `MorphShape` is rebuilt by `ensureMorph` when stale, written from the model otherwise.
- TLF text becomes classic text.

`XFLWriter::note` collects descriptions of what could not be written (missing raw XML
for printer settings and similar, a non-bitmap resource); `XFLContent::build` adds
bitmaps with no data. `fla::unsupportedContent(document)` runs the whole build without
writing and adds Phoenix features or `.phx` parts this build does not know.
`MainWindow::confirmLossySave` ([src/gui/main_window.cpp](../src/gui/main_window.cpp))
shows that list before an FLA or XFL save, and `confirmPhoenixFeatures` warns when the
document uses Phoenix-only features ([src/edit/phoenix_features.h](../src/edit/phoenix_features.h)).
`MainWindow::writeDocumentFile` writes a `.fla` to `<path>.saving` and renames it into
place, so a failed save leaves the original alone; any other non-`.phx` path is
written as an XFL folder.

### Phoenix's own attributes

Features the format has no place for travel as attributes whose names start with
`phoenix`, listed in one place in [src/edit/phoenix_features.h](../src/edit/phoenix_features.h)
(`features::all`). Other editors ignore them. Ones the parser and writer handle
directly include `phoenixStageAlpha`, `phoenixDrawingGuides` and `phoenixSymmetry` on
the document; the rest ride in `extraAttributes` and are read by the code that owns the
feature.

Textured paint is the one feature written in a form other editors can show: with
`XFLWriter::setBakeTextures(true)` (which `XFLContent::build` always sets), a textured
shape's solid fills are written as repeating `BitmapFill`s of a baked tile, carrying
`phoenixSolidColor` / `phoenixSolidAlpha`. The tiles are `DOMBitmapItem`s marked
`phoenixTextureBake="true"` with data under `bin/phoenix_texture_*`, added by
`withBakedMedia` after every symbol is written. On read, the parser turns such a
`BitmapFill` back into its `SolidColor` and skips the tile items and files, so they are
made fresh on each save.

### Checking round trips

| Check | What it compares |
| --- | --- |
| Unit tests in [tests/test_xfl_writer.cpp](../tests/test_xfl_writer.cpp) | Parse, write, parse again; `TreeComparer` compares the two models. Many cases target one feature (`xfl_keeps_what_it_does_not_model`, `xfl_writes_an_armature_pose_after_the_frame_elements`, `xfl_text_keeps_a_run_of_only_spaces`, `xfl_color_alpha_rounds_to_the_nearest_step`...) |
| `xfl_round_trips_real_documents` (opt-in) | Every `.fla` or XFL folder in `PHOENIX_FLA_CORPUS`: parse, write with `XFLFolderWriter`, re-parse, `TreeComparer`. Reports `unsupported()` descriptions |
| `xfl_save_keeps_every_element_and_attribute_of_real_documents` (opt-in) | The files themselves: every package file comes back, and in every XML file each element and attribute occurs at least as often as before (`countXml`). Catches what the parser never kept, which a model comparison cannot see |
| `ik_bones_are_kept_on_save`, `ik_symbol_armatures_and_poses_are_kept_on_save` (opt-in) | Named corpus files with IK data |
| [tests/test_edge_writer.cpp](../tests/test_edge_writer.cpp) | Number formatting, edge strings, cubics records, morph coordinates, damaged numbers |
| [tests/test_fla_writer.cpp](../tests/test_fla_writer.cpp) | ZIP writer through the reader, a new document packed as Animate does, bitmaps and sounds saved and read back |
| [tests/test_bitmap_decoder.cpp](../tests/test_bitmap_decoder.cpp) | Lossless `.dat` decode, encode, damage refusal |
| [tests/test_zip_archive.cpp](../tests/test_zip_archive.cpp) | Damaged central directories; folder documents |
| [tools/roundtrip_check.py](../tools/roundtrip_check.py) | Rendering: runs `Phoenix --roundtrip <file> <folder> [samples]` on every `.fla`/`.phx` in a folder, which saves as `.fla` and `.phx`, reopens each, and compares sample frames pixel by pixel. Headless (`QT_QPA_PLATFORM=offscreen`); writes `report.json` and images of differences; exits 1 on any difference |
| `Phoenix diff <before> <after>` | `fla::compareDocuments`: builds both packages with `XFLContent::build` (ignoring `META-INF/`), compares part by part (scenes, each symbol, each media file, with a changed-line count), then compares rendered frames |

The opt-in tests are skipped unless `PHOENIX_FLA_CORPUS` points at a folder of files.
They write scratch output under the system temp directory (`phoenix_roundtrip`,
`phoenix_keeps`), so give each concurrent test run its own `TEMP`.

## Adding a field

1. Model it on the class in `src/data/` with the format's default as the member's
   default.
2. Read it in the matching `parse*` function in
   [src/parser/document_parser.cpp](../src/parser/document_parser.cpp), and **add its
   name to that function's `keepAttributes` list**. Otherwise it is read and also kept
   in `extraAttributes`; `writeKept` skips a name the writer already set, but a field
   edited back to its default would then be resurrected from the kept copy.
3. Write it in the matching `write*` function in
   [src/writer/xfl_writer.cpp](../src/writer/xfl_writer.cpp) with the `set*` helper
   and the same default, inside the element's `PresentScope`.
4. If it is a child element, remove it from the path that sends unknown children to
   `extraChildren`, or it will be written twice.
5. Add a case to [tests/test_xfl_writer.cpp](../tests/test_xfl_writer.cpp), extend
   `TreeComparer` if the field should take part in model comparison, and run the
   opt-in corpus tests.
6. Consider the other formats that carry the same model: [phx-format.md](phx-format.md)
   for `.phx`, [swf.md](swf.md) for export.

## Known gaps and traps

- **Missing symbol files fail the open.** `parseSymbolInclude` returns false for a
  missing or malformed `LIBRARY/` file and the whole parse fails; `Phoenix doctor` is
  the recovery route.
- **Unhandled children inside modeled containers are dropped**, with a `!!!! Unhandled`
  line on stderr: for example a non-`Edge` child of `<edges>`, a non-`DOMFrame` child
  of `<frames>`, a non-`DOMLayer` child of `<layers>`, a non-`script` child of
  `<Actionscript>`, an unknown `<MorphShape>` child, an unknown `DOMTextRun` child.
  The corpus test `xfl_save_keeps_every_element_and_attribute_of_real_documents` is
  what notices these.
- **A failed element is dropped whole.** `parseElements` and `parseGroup` skip an
  element whose parser returned null. For a shape that happens when an edge string has
  a line before any move, or when a `<FillStyle>` or `<StrokeStyle>` holds a fill or
  stroke element `parseFillStyle` / `parseStrokeStyle` does not know.
- **`S` style numbers**: one per path; a mid-path change is lost on read.
- **Folder documents** list only top-level files; package files in subfolders survive
  only when something names them.
- **Website drift**: [website/fla-format.html](../website/fla-format.html) says Phoenix
  writes `PROJECT_XFL` in the marker, omits `mimetype` and `META-INF` for new
  documents, and writes `xflVersion="2.971"`. The code writes `PROXY-CS5`, adds both
  files, and defaults `xflVersion` to `23.0`.
