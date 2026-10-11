# FLA / XFL Format Notes

What Phoenix knows about the format Adobe Animate (and Flash Professional CS5
and later) saves, learned from reading real files and making Phoenix's reader
and writer agree with them. Adobe has never published a specification, so
everything here is observation. Where a point rests on one sample, or was
written from memory and not yet checked against a file, it says so.

Units throughout: a **twip** is 1/20 of a pixel. Phoenix's model holds pixels
and converts on read and write.

---

## 1. Containers

A document comes in one of three containers:

| Container | What it is |
| --- | --- |
| `.fla` (CS5 and later) | A ZIP archive of the XFL package below |
| XFL folder | The same package as loose files, marked by a `<name>.xfl` file |
| `.fla` (CS4 and earlier) | A Microsoft compound document (signature `D0 CF 11 E0 A1 B1 1A E1`) holding an undocumented binary format of MFC-serialized objects. Phoenix converts it to XFL in memory and reads that; see [BINARY_FLA_FORMAT.md](BINARY_FLA_FORMAT.md) |

### Package contents

```
DOMDocument.xml           the document: settings, library index, scenes
LIBRARY/<path>.xml        one file per symbol, at its library path
LIBRARY/<path>            imported bitmaps kept in their original form
bin/<id>.dat              media data: bitmaps, sounds (see section 9)
bin/SymDepend.cache       Animate's dependency cache; safe to drop
META-INF/metadata.xml     XMP metadata
PublishSettings.xml       publish profiles
MobileSettings.xml        AIR / mobile settings
publishtemplates/         (folder, usually empty)
mimetype                  "application/vnd.adobe.xfl"
<name>.xfl                the project marker; contains "PROXY-CS5"
```

Animate writes entries in roughly that order with `LIBRARY/` first and
`mimetype` last, unlike ODF, which puts `mimetype` first. Directory entries are
present. Files are stored or deflated; there are no data descriptors.

### The ZIP central directory quirk

Many Animate-saved `.fla` files state a central directory size **54 bytes
larger** than the real one in the end-of-central-directory record. Readers that
derive the archive start from that size reject the file. Python's `zipfile`
reports "Bad magic number for central directory" for 45 of the corpus files.
Readers that use the stated offset and entry count are fine. The quirk shows up
in files from Flash CS5 through Animate 24, but not in every file a given
version saves. Phoenix's reader uses the offset and count and is unaffected.

### What Phoenix writes differently (to check)

These are differences between Phoenix's output and Animate's that have not yet
been tested against Animate opening the file:

- The `.xfl` marker contains `PROJECT_XFL`; every Animate marker in the corpus
  contains `PROXY-CS5`
- A Phoenix `.fla` has no `mimetype`, `META-INF`, `PublishSettings.xml` or
  `MobileSettings.xml` unless the document came from a file that had them
- A document made in Phoenix says `xflVersion="2.971"` and
  `creatorInfo="Phoenix"`; Animate 24 writes `xflVersion="23.0"`

## 2. DOMDocument.xml

The root is `<DOMDocument xmlns="http://ns.adobe.com/xfl/2008/">`. Notable
attributes:

| Attribute | Meaning |
| --- | --- |
| `width`, `height` | Stage size in pixels (default 550 x 400) |
| `frameRate` | Frames per second (default 24) |
| `backgroundColor` | Stage color |
| `currentTimeline` | 1-based index of the scene that was open |
| `xflVersion`, `creatorInfo`, `platform`, `versionInfo`, `majorVersion`, `buildNumber` | Which program saved it ("Saved by Animate Windows 24.0 build 3") |
| `gridSpacingX`, `gridSpacingY`, `objectsSnapTo`, `snapAlignBorderSpacing` | Grid and snap settings |
| `viewAngle3D`, `vanishingPoint3DX`, `vanishingPoint3DY` | 3D perspective |
| `nextSceneIdentifier` | Counter for new scenes |
| `layerDepthEnabled` | Layer Depth is on |

Attributes left at their default are omitted, and Phoenix's writer omits them
the same way.

Children, in the order Animate writes them:

| Element | Holds |
| --- | --- |
| `<folders>` | `DOMFolderItem` for each library folder (`name` is its path) |
| `<fonts>` | Embedded fonts |
| `<media>` | `DOMBitmapItem`, `DOMSoundItem`, video items |
| `<symbols>` | `<Include href="<path>.xml" itemIcon loadImmediate itemID lastModified/>` per symbol |
| `<timelines>` | One `DOMTimeline` per scene |
| `<PrinterSettings>`, `<publishHistory>`, `<swatchLists>`, `<extendedSwatchLists>`, `<scripts>` | Document metadata |

`<symbols>` may also include a package file that isn't a symbol, such as
`<Include href="PaintBrushDefinitions.xml"/>` for custom brushes.

Phoenix models the folders, bitmaps, symbols and timelines. It keeps
everything else (sound items, fonts, brush definitions, swatch lists,
printer settings, publish history, scripts) as the XML text it read and
writes it back in the same place.

## 3. Library

- An item's **name is its path**: `Characters/Bob/head`. The symbol's file is
  `LIBRARY/Characters/Bob/head.xml`, and its root is a `DOMSymbolItem` with
  `name`, `itemID`, `lastModified`, `symbolType` and an inner `<timeline>`
  holding one `DOMTimeline`
- Renaming or moving an item changes its path. Every reference follows it:
  `libraryItemName` on instances, `bitmapPath` on bitmap fills, `soundName` on
  frames, and the item's own file path
- `symbolType` on a symbol item: `graphic`, `button`, `movie clip`, and in
  newer files `puppet` and `puppetBase` (Asset Warp) and `ik container` (a
  shape with bones). Phoenix keeps a type it does not know as spelled and
  draws it as a graphic
- 9-slice: `scaleGridLeft`, `scaleGridTop`, `scaleGridRight`,
  `scaleGridBottom` on the symbol item
- Linkage (`linkageExportForAS`, `linkageClassName`...) is kept as read
- `DOMBitmapItem`: `name`, `href` (the original file under LIBRARY when there
  is one), `bitmapDataHRef` (the `.dat` under `bin/`), `allowSmoothing`,
  `quality`, `sourceLastImported`, `frameRight`, `frameBottom`
- `DOMSoundItem`: `name`, `soundDataHRef` (the `.dat`), `format`
  ("44kHz 16bit Stereo"), `sampleCount`, `exportFormat`...

## 4. Timelines, layers and frames

```
DOMTimeline name guides=...
└── layers
    └── DOMLayer
        └── frames
            └── DOMFrame
                └── elements
                    └── DOMShape, DOMSymbolInstance, DOMGroup, ...
```

### DOMTimeline

`name` is the scene name (or "Symbol N" inside a symbol). Ruler guides are
saved as XML inside the `guides` attribute, in this form (written from memory;
no corpus file has guides):

```xml
<guidelines><guideline direction="v">x</guideline><guideline direction="h">y</guideline></guidelines>
```

### DOMLayer

| Attribute | Meaning |
| --- | --- |
| `name`, `color` | Name, and the outline color used for highlights and outline view |
| `layerType` | `normal` (omitted), `folder`, `guide`, `mask`, `masked` |
| `parentLayerIndex` | 0-based index of the layer's parent: the folder it sits in, or the mask it is under |
| `visible`, `locked`, `outline`, `current`, `isSelected`, `autoNamed` | View state |
| `animationType` | `motion object` (an object motion tween layer), `IK pose` (an armature layer) |

Masked layers are often written as `normal` with `parentLayerIndex` pointing at
a mask layer. A layer's type has to be worked out from its parent's
(`document_parser.cpp`). Layers are listed top to bottom: `layers[0]` draws on
top. Layer parenting (Animate 2019's parent layer, not the folder link) has not
been seen in a file yet.

### DOMFrame

A frame element is a **span**: `index` (0-based start) and `duration`.
Keyframes are the spans themselves; there are no in-between frame elements.

| Attribute | Meaning |
| --- | --- |
| `index`, `duration`, `keyMode` | Position, length, and a bit set Animate uses internally |
| `name`, `labelType` | Frame label: `name`, `comment` or `anchor` |
| `tweenType` | `motion` (classic), `shape`, `motion object`, `IK pose` |
| `acceleration` | Classic ease, -100 to 100; repeated as `<tweens><Ease intensity>` |
| `hasCustomEase` | A `<tweens><CustomEase>` of `<Point>`s gives a bezier chain from (0,0) to (1,1) |
| `motionTweenRotate`, `motionTweenRotateTimes` | `auto`, `none`, `clockwise`, `counter-clockwise`, and the extra turns |
| `motionTweenSnap`, `motionTweenSync`, `motionTweenOrientToPath`, `motionTweenScale` | Classic tween options. Old files also spell it `motionTWeenSnap` |
| `soundName`, `soundSync`, `inPoint44`, `outPoint44`, `soundLoopMode`, `soundLoop` | A sound placed on the frame. `soundSync` is `event` (omitted), `start`, `stop` or `stream` |

A frame may also carry `<Actionscript><script>` (frame script, as CDATA),
`<SoundEnvelope>` (volume points, see section 9), `<MorphShape>` (shape tween
data, section 7) and `<motionObjectXML>` (section 7).

## 5. Elements

| Element | Notes |
| --- | --- |
| `DOMShape` | Fills, strokes and edges (section 6). `isDrawingObject="true"` makes it an Object Drawing shape that never merges. Without it, it is merge-mode artwork |
| `DOMGroup` | `<members>` holding elements |
| `DOMSymbolInstance` | `libraryItemName`, `symbolType` (of the instance; can differ from the item's), `name` (instance name), `loop`, `firstFrame`, `blendMode`, `cacheAsBitmap`, `<matrix>`, `<transformationPoint>`, `<color>`, `<filters>` |
| `DOMBitmapInstance` | `libraryItemName`, `<matrix>` |
| `DOMStaticText`, `DOMDynamicText`, `DOMInputText` | `<textRuns>` of `DOMTextRun` with `<characters>` and `<textAttrs><DOMTextAttrs>`. Dynamic and input text add `instanceName` and `lineType` |
| `DOMRectangleObject` | Rectangle Primitive: `objectWidth`, `objectHeight`, `x`, `y`, `topLeftRadius`... and a stroke and fill |
| `DOMOvalObject` | Oval Primitive: plus `startAngle`, `endAngle`, `innerRadius`, `closePath` |

An Object Drawing rectangle is a `DOMShape isDrawingObject="true"`, not a
`DOMRectangleObject`. The primitive elements come only from the Rectangle and
Oval Primitive tools.

### Matrix and transformation point

`<Matrix a b c d tx ty/>` with `tx`, `ty` in pixels. A missing attribute takes
the identity value. `<transformationPoint><Point x y/>` is the pivot in the
element's own space.

### Loop

`loop` on a graphic instance: `loop`, `play once`, `single frame`. The corpus
uses only these three (`single frame` 5609 times, `loop` 1631, `play once` 3).
Animate also has reverse playback options whose spelling has not been seen.
Phoenix accepts an older `loopType` attribute and a `ping pong` value of
unknown origin.

### Color effect

`<color><Color .../></color>`:

| Attributes | Effect |
| --- | --- |
| `brightness` (-1 to 1) | b > 0: each channel x (1-b) + 255b. b < 0: x (1+b) |
| `tintMultiplier`, `tintColor` | Each channel x (1-t) + t x tint. Alpha untouched |
| `alphaMultiplier` | Alpha alone |
| `redMultiplier`, `redOffset`, ... `alphaOffset` | Advanced: multiply then add, per channel |

The arithmetic runs on straight (unpremultiplied) color.

### Filters

`<filters>` holds any of `DropShadowFilter`, `BlurFilter`, `GlowFilter`,
`BevelFilter`, `GradientGlowFilter`, `GradientBevelFilter` and
`AdjustColorFilter`, applied in order, each with `enabled`. Blur is an
iterated box blur: `quality` is the number of passes, and `blurX` and `blurY`
are separate.

### Blend modes

`blendMode`: `normal` (also omitted), `layer`, `multiply`, `screen`,
`lighten`, `darken`, `difference`, `add`, `subtract`, `invert`, `alpha`,
`erase`, `overlay`, `hardlight`. `alpha` and `erase` only show inside a
`layer` group. Outside one, Flash draws nothing for them.

## 6. Shapes

```xml
<DOMShape>
  <fills><FillStyle index="1"><SolidColor color="#FF0000"/></FillStyle></fills>
  <strokes><StrokeStyle index="1"><SolidStroke weight="2">...</SolidStroke></StrokeStyle></strokes>
  <edges>
    <Edge fillStyle1="1" strokeStyle="1" edges="!0 0|2000 0|2000 2000|0 2000|0 0"/>
    <Edge cubics="!0 0(;...);"/>
  </edges>
</DOMShape>
```

Style indexes are 1-based; 0 or absent means none.

### Fill sides

An edge carries the fill on each side of it. **`fillStyle1` is the fill on the
left of the edge's direction and `fillStyle0` the one on the right**, in a
y-down space. The format never says this. It was settled by arranging eight
real documents into faces and checking which reading makes every face agree
with itself: 44 of 44 faces agreed one way, 3 of 44 the other.

Merge-mode artwork is a planar map. Edges meet at shared points, and a face is
whatever the edges around it agree on. A shape's edges are pieces that end at
each meeting point, so a corner is shared by two or more `Edge` elements. A
shape doesn't need to be stored as closed outlines.

### The `edges` string

The geometry the shape is drawn with. Lines and quadratics only:

| Token | Meaning |
| --- | --- |
| `!x y` | Move to |
| `\|x y` | Line to |
| `/x y` | Line to (same as `\|`; appears 29 times in the corpus) |
| `[cx cy x y` | Quadratic curve to |
| `Sn` | A number Animate records after a move. Seen as S1, S2, S4, S6. Its meaning is unknown. Phoenix keeps it with the path and writes it back after the move it followed (writing it before the move loses it) |

Phoenix's reader also accepts `FSn` / `LSn`, which haven't been seen in Animate's
output.

A move to the point where the last segment ended continues the same path.
Animate writes `!a|b!b|c` for pieces that join.

**Numbers** are twips, written two ways:

- Decimal: `2000`, `-850`, occasionally with a fraction (`2081.5`)
- Hex fixed point: `#` then the whole twips in hex, `.` and up to two hex
  digits of fraction: `#11C2.6A` is 0x11C2 + 0x6A/256. A single digit counts in
  sixteenths. Negative values are 24-bit two's complement: `#FFF973.D` is
  -1677 + 13/16 = -1676.1875

Animate mostly writes fractional values in hex, but decimal fractions occur
too. Phoenix writes whole values in decimal and fractional ones in hex,
snapping to the 1/256-twip grid so what it writes reads back as the same
number.

### The `cubics` string

Shapes drawn with the pen, brushes and other cubic tools carry the original
cubic curves too, as extra `<Edge cubics="...">` elements with no style
attributes. Only the editor uses them. The `edges` strings are a quadratic
approximation that's drawn and published. Some files hold a lot of these:
Charlotte_06_2021 has 87,744 and the Alvin file 49,730.

```
!873 -1566(873,-1566;608,-1639 317,-1675 0,-1673q873 -1566Q475 -1676q0 -1673);
```

`(` opens a run: the start point, `;`, then each cubic as control 1, control 2
and end point. After `q`/`Q`/`p`/`P` come the quadratic points it was
approximated with. `)` closes the run, optionally followed by an end point,
then `;`.

**Phoenix today:**
- Reads only `edges`, skips `cubics` and doesn't write them back. A save
  drops the cubic curves, which Animate would need to edit the artwork as
  cubics again
- Writes edited cubic geometry into `edges` in the `(...)` form, which
  Animate's `edges` strings never contain

Both are open compatibility problems.

### Fill styles

| Element | Notes |
| --- | --- |
| `SolidColor` | `color` (#RRGGBB, default black), `alpha` (default 1) |
| `LinearGradient`, `RadialGradient` | `<matrix>` maps a gradient square of +-16384 twips (+-819.2 px). `<GradientEntry color alpha ratio>` stops, ratio 0 to 1. `spreadMethod`: pad (omitted), `reflect`, `repeat`. `interpolationMethod="linearRGB"` interpolates in linear light. Radial adds `focalPointRatio` |
| `BitmapFill` | `bitmapPath` (library path), `bitmapIsClipped` (false tiles), `<matrix>` in twips: a = 20 is the bitmap at its own size |

### Stroke styles

`SolidStroke`, `DashedStroke`, `DottedStroke`, `RaggedStroke`,
`StippleStroke`, `HatchedStroke`, each with a `<fill>` holding a fill style.

| Attribute | Notes |
| --- | --- |
| `weight` | Pixels. Missing means 1, except for a hairline |
| `solidStyle="hairline"` | A 1-device-pixel line at any zoom, with no `weight` |
| `caps` | Round (omitted), `none`, `square` |
| `joints` | Round (omitted), `miter`, `bevel`, with `miterLimit` |
| `scaleMode` | `normal`, `horizontal`, `vertical`, `none`: how the weight follows the instance's scale |
| `pixelHinting` | Snap to whole pixels |
| `dash1`, `dash2` | Dash and gap, for DashedStroke |
| `dotSpace` | DottedStroke spacing |

Ragged, stipple and hatched strokes have their own procedural parameters
(wave height and length, dot size, density, hatch thickness, spacing,
jiggle...). A stroke may carry a `<VariablePointWidth>` of `<WidthMarker>`s
for variable width. Phoenix keeps these but doesn't draw them.

## 7. Tweens

### Classic tweens

On a keyframe with `tweenType="motion"`. The in-betweens aren't stored. They
are computed between this keyframe and the next one's elements.

- Matrices are taken apart into translation, scale and Animate's two skew
  angles, blended (rotation the short way, plus `motionTweenRotateTimes`
  turns), and put back together. Blending matrix entries directly shrinks a
  rotating object
- Progress follows `acceleration` (straight progress blended with t² or its
  mirror) or the `CustomEase` curve
- Color effects blend amount by amount
- Motion guides: a guide layer above the tweened layer bends the path (not yet
  modeled)

### Shape tweens

On a keyframe with `tweenType="shape"`. Animate stores the tween it computed as
a `<MorphShape>` on the frame:

```
<MorphShape><morphSegments><MorphSegment startPointA startPointB strokeIndex1 strokeIndex2 fillIndex1 fillIndex2>
  <MorphCurves controlPointA anchorPointA controlPointB anchorPointB isLine/>
```

Each point exists in an A form (start) and a B form (end). A morph coordinate
is **twips over 256**. `#9.61` is 0x961 = 2401 twips, so one fraction digit
weighs 1/16 of a unit, not 1/256. Reading these as ordinary twips makes the
tween 12.8 times too small. `<morphHintsList>` holds shape hints.

New shape tweens need a MorphShape computed for them. Animate's own runs one
segment per closed outline (fill on the left, so `fillIndex2` holds it),
starting at the outline's leftmost point. Phoenix builds one the same way
(`buildMorph` in `src/edit/shape_tween.cpp`) for a tween it creates, and
again whenever either keyframe's shape is edited; a stored morph is kept
until then.

### Object motion tweens

`tweenType="motion object"` on the frame and `animationType="motion object"`
on the layer. The frame's `<motionObjectXML>` holds an `AnimationCore`
document: property curves (`Motion_X`, `Motion_Y`, rotation, scale, skew,
color, filters) of `<Keyframe>`s in TimeScale units, a `<TimeMap>` ease and a
transformation point offset. Phoenix keeps this verbatim but doesn't play it.
template_robot.fla has 12.

### IK armatures

From one sample, ik_bones.fla (Animate 24, a shape with four bones):

- The armature layer has `animationType="IK pose"`. Its pose keyframes have
  `tweenType="IK pose"`, `isIKPose="true"`, `poseLocations`, `keyMode="9732"`
  and an empty `betweenFrameMatrixList`
- An `<IKTree name treeName physicsEnabled>` holds a root `<IKNode>` whose
  `<ChildNode>`s nest one per bone: `boneName`, the joint location in hex fixed
  point, `xArray`/`yArray`/`angleArray`, a `worldMatrix`, and a `<State>` per
  pose (duration, angle in radians, x and y in twips)
- `<IKState>` entries give the pose durations
- `<IKContour>` is the shape's outline (`dataPoints` in hex fixed point, a
  matrix, its stroke and fill). `connectionIndices` and `connectionData` bind
  outline points to bones with weights, in groups of four floats ending in a
  weight of 1 or 0.5
- The boned shape becomes a library symbol of `symbolType="ik container"`
- `persistentData` keeps `ikBoneCount`, `ikTreeCount` and `ikNodeCount`

Not yet sampled: bones between symbol instances, and how poses tween (two
poses). Phoenix keeps all of it on save and draws the shape as stored.

### Asset Warp

Symbols of `symbolType="puppet"` (and `puppetBase`) hold a mesh warped by pins.
template_bike_girl.fla has 24, template_robot.fla 14. The storage hasn't been
read yet. Phoenix keeps them and draws them unwarped.

## 8. Text

- `characters` holds the text. Line breaks are CR, which XML turns into LF on
  read, so a writer has to emit `&#13;`. Without that, 12 of the first 74
  corpus documents came back with altered text and layer names
- `DOMTextAttrs`: `face` is a PostScript name (`ArialMT`,
  `TimesNewRomanPS-BoldItalicMT`), `size` is in pixels, plus `fillColor`,
  `alignment`, `letterSpacing`, `lineSpacing`, `autoKern`, `aliasText`...
- A text box grows to fit unless it has a fixed width. The box has a 2-pixel
  gutter

## 9. Binary data under bin/

### Bitmaps

A `DOMBitmapItem`'s `.dat` is one of:

- **JPEG**: a plain JPEG stream (FF D8 FF), for photo-compressed bitmaps
- **Lossless**: a 26-byte header, then a zlib stream split into pieces. The
  header is u16 `0x0503`, u16 bytes per row, u16 width, u16 height, then the
  size in twips. The pieces start at byte 26, each a little-endian u16 length
  and that many bytes, until a zero length. Joined, they inflate to rows of
  pixels: 4 bytes A, R, G, B with color premultiplied, or 3 bytes R, G, B when
  the row is only 3 x width

When the original file is also kept under `LIBRARY/` (`href`), either form
can be used.

### Sounds

A `DOMSoundItem`'s `.dat` is MP3 (it starts with a valid MPEG frame header),
or raw PCM described by the item's `format`: "5kHz" (5512 Hz), "11kHz", "22kHz"
or "44kHz", "8bit" or "16bit", "Mono" or "Stereo". Raw PCM can begin with
FF FF, so recognizing MP3 takes a full header check, not just the sync bits.

A frame's `<SoundEnvelope>` holds `<SoundEnvelopePoint mark44 level0 level1>`:
a sample position at 44.1 kHz and the left and right levels (32768 is full; a
level left out is silence). The volume runs in straight lines between points
and holds flat past either end. `inPoint44` and `outPoint44` trim the sound in
the same units.

## 10. Rendering rules that are part of the format

These aren't written anywhere in a file, but a file only looks right when
they're followed:

- Strokes default to round caps and round joins
- Same-style stroke pieces are joined end to end before stroking, so a
  corner where two edges meet is a join, not two caps
- Masks use coverage only: a 30% fill masks like a solid one, and mask edges
  aren't anti-aliased
- An alpha-only color effect fades each shape inside the instance on its own
  (overlapping shapes show through each other). Any other effect applies to
  the instance as a whole
- 9-slice applies to movie clips scaled without rotation or skew
- Gradients and bitmap fills on strokes follow the stroke's own fill matrix

## 11. Open questions

- Does Animate accept Phoenix's `.xfl` marker text and a package with no
  `mimetype`? (section 1)
- What does `S` in an `edges` string mean? (section 6)
- The guides attribute format (section 4)
- The spelling of the reverse loop options (section 5)
- Camera layers, layer depth, layer parenting and 3D rotation: no samples yet
- Asset Warp mesh and pin storage (section 7)
- IK between symbol instances, and IK pose tweening (section 7)
- What `keyMode` encodes

## 12. Where Phoenix implements this

| Part | Code |
| --- | --- |
| ZIP and folder reading | `src/parser/zip_reader.cpp`, `src/util/zip_archive.cpp` |
| XML to model | `src/parser/document_parser.cpp` |
| `edges` and morph coordinates | `src/parser/path_parser.cpp`, `src/writer/edge_writer.cpp` |
| Model to XML | `src/writer/xfl_writer.cpp`, `src/writer/xfl_content.cpp` |
| `.fla` and folder output | `src/writer/fla_writer.cpp`, `src/writer/xfl_folder_writer.cpp`, `src/util/zip_writer.cpp` |
| Bitmap `.dat` | `src/data/bitmap_decoder.cpp` |
| Sound `.dat` | `src/gui/audio_engine.cpp` |
| Fill sides and faces | `src/geom/planar_map.cpp`, `src/geom/fill_attribution.cpp` |
| Classic tween blending and easing | `src/edit/transform_blend.cpp`, `src/edit/tween_ease.cpp` |
| Shape tweens | `src/edit/shape_tween.cpp` |
