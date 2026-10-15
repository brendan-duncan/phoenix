# Binary FLA (Flash CS4 and earlier)

Before CS5, Flash saved .fla files in a binary format: a Microsoft compound
file (OLE2) whose streams hold MFC-serialized objects. Phoenix reads them by
converting them to XFL in memory (`src/parser/compound_file.cpp`,
`src/parser/binary_fla.cpp`) and parsing the result as any other document.

The format is undocumented. This description builds on the independent
reverse engineering in [fla-decoder](https://github.com/eddiemoore/fla-decoder)
(MIT, Ed Moore), corrected and extended by checking a Python prototype field
by field against the corpus documents that exist in both formats
(`Yard_Sale_03` / `Yard_Sale_03_2`, `Freya_Fairy_07` / `Freya_Fairy_07_2`).
All 183 streams of the three binary corpus files parse to their last byte.
Flash 8 (Contents 0x138), which has no twin in the corpus, was worked out
structurally from 20 production files (about 10,000 streams), every stream of
which now parses to its last byte; what is new in it is marked [S].

Confidence tags used below:
* **[X]** confirmed against an XFL twin. The counts are items compared, all
  matching unless a mismatch is stated.
* **[S]** structurally confirmed: the field size is right because every stream
  parses to its exact end, but its meaning is unknown or guessed.
* **[G]** guessed, with no evidence in the corpus.

## 0. Versions seen

When Flash saves, it copies unchanged Symbol streams verbatim. A single file
can therefore mix object schemas (Freya `Symbol 14` is MX 2004 era), so a
decoder must branch on the schema of each object, never on the file version.

Contents versions: 0x129 (MX 2004), 0x138 (Flash 8), 0x143 (CS3), 0x43 (CS4,
with CS3's layout [S]; its streams are named `P N <time>`, `S N <time>` and
`M N <time>`). Only a
few Contents records follow the file version rather than a schema of their
own (the ItemHeader tail, the profiles list); everything in the timeline
streams branches on schemas.

## 1. Container and primitives

* OLE2 compound file. Streams: `Contents`, `Page N` (scenes), `Symbol N`
  (library symbols), `Media N` (bitmaps and sounds).
* Little endian throughout.
* **CString** (MFC): `u8 n`. If n is 0xFF: `u16 n`; if that u16 is 0xFFFE the
  string is UTF-16, so read the length again (`u8`, or `FF`+`u16`, or
  `FFFF`+`u32`) and then n UTF-16 code units. Otherwise the string is n ANSI
  bytes. Empty strings are `00` (ANSI) or `FF FE FF 00` (Unicode).
* **wstr8**: `u8 n` + n UTF-16 units with no marker. Used for stream names in
  `Contents` when the record schema is at least 22 (CDocumentPage; 22 is
  Flash 8 [S]) or at least 6 (CMedia*). Older records use an ANSI string
  (`u8 n` + bytes).
* **color**: 4 bytes R, G, B, A. [X]
* **matrix**: 6 x s32: a, b, c, d as 16.16 fixed point, then tx, ty in twips
  (divide by 20 for px). [X] 260 instance matrices plus all shape matrices.
* **itemID**: `u32 time, u32 counter`, the XFL `itemID="tttttttt-cccccccc"`.
  (0, 0) means none. [X] every symbol, media item and folder.
* **CArchive object** (MFC `ReadObject`):
  * `u16 tag`.
  * `0x0000` means null.
  * `0xFFFF` declares a new class: `u16 schema, u16 len, ASCII name`. It
    appends the class and then the object to the load table.
  * `0x8000|i` is a new object of the class at load index i (appends one
    entry).
  * Any other value below 0x8000 is a back-reference to an object that was
    already loaded.
  * `0x7FFF` is followed by a `u32`: once the load table passes 0x7FFE
    entries, class and object indices no longer fit in 15 bits. Bit 31 set
    means a new object of the class at index `u32 & 0x7FFFFFFF`, clear a
    back-reference to that object. [S] Terrence `Page 1`, 2 MB.
  * An object can only refer back to one whose reading has finished; a
    reference to one still being read (itself or an ancestor) is damage.
  * The load table starts at index 1. The class-level schema was 1 everywhere
    (0 for CColorDef), so the real versioning is a schema byte inside each
    object.
  * Back-references matter. A folder layer is first serialized inline inside
    its child layer's `parent` field. When it shows up later in the page's
    layer list, that entry is only a back-reference. [X]

## 2. Timeline streams (`Page N`, `Symbol N`)

The stream is `u8 01`, followed by one `CPicPage` object.

### 2.1 CPicObj base (every CPic class)
```
u8  objSchema            1 or 5 seen
u8  objFlags             CPicShape: bit0 set = loose shape (XFL DOMShape), clear = DOMGroup [X]
ReadObject children...   until the null tag. Page: layers. Layer: frames.
                         Frame: elements. Group: members.
s32 x, s32 y             transformation point in parent px*20 (twips); INT_MIN,INT_MIN = unset
                         [X] 260 instances: equals matrix applied to the XFL transformationPoint
if objSchema >= 5: u8, u8    (always 0; the exact threshold is between 2 and 5) [S]
```

### 2.2 CPicPage (scene or symbol timeline)
```
CPicObj base (children = layers, stored BOTTOM layer first; XFL lists the top first) [X]
u8  pageSchema (3, 7)
u16  (2 seen)                                   [S]
if pageSchema >= 5: u16                         [S]
if pageSchema >= 7: u32                         [S]
u32 n; n x (u32, u32)   guide/ruler list, always empty in the corpus [S]
```

### 2.3 CPicLayer
```
CPicObj base (children = frames in time order)
u8  layerSchema (10, 11)
CString name                                        [X] 215 layers
u8  current        (the timeline's active layer)   [X]
u8  hidden         (1 means XFL visible="false")   [X]
u8  locked                                         [X]  (NOTE: hidden comes before locked)
u32 0xFFFFFFFF                                     [S]
color  layer outline color (#RRGGBB + ff)          [X] 215
u32 outline mode   (always 0 in the corpus)        [G]
u32 height multiplier (always 1)                   [G]
u8  layerType      0 normal, 1 guide, 3 folder     [X]  other values not in corpus
ReadObject parent  folder layer this layer belongs to (XFL parentLayerIndex) [X];
                   presumably also the mask/guide layer for masked/guided layers [G]
u8  1                                              [S]
u8  autoNamed      (XFL autoNamed)                 [X] 215/215 correlation
```
A folder layer has exactly one empty frame. XFL omits frames for folder
layers.

### 2.4 CPicFrame (a frame is a CPicShape that carries its own "merge drawing" shape)
```
CPicObj base (children = element objects, in z order bottom to top)
CPicShape body (2.5)    the frame's raw shape; XFL emits it as the first DOMShape when non-empty [X]
u8  frameSchema (22, 26)
u16 duration                                         [X] 370 frames
u16 keyMode         bit0 = classic motion tween. XFL keyMode = binary | (bit0 ? 0x4000 : 0x2000):
                    0x0600->9728, 0x1E00->15872, 0x1E01->24065 [X]
s16 ease (-100..100)         always 0 in corpus     [G]
u16 sound           Media N of the frame sound, 0 = none [X] 3 frames
u16 n; n x (u32 mark44, u16 level0, u16 level1)   sound envelope (32768 = full) [S]
u16 soundLoop  u8 soundSync (3 = stream [X])  u32 inPoint44  s32 outPoint44 (0x3FFFFFFF = end)
u16 soundZoom (0xFFFF = XFL soundZoomLevel 65535) [X]
CString label       always empty in corpus           [G] (best candidate position)
action block (2.9)  frame ActionScript               [X] 5 scripts in Freya Scene 1
u32 x3
ReadObject morph    CPicMorphShape (2.11) on the first keyframe of a shape tween, else null [S]
u32
ReadObject hints    CObList of CMorphHintItem (2.11): the tween's shape hints, else null [S]
CString             always empty                     [S]
u32 x3              (1,0,0)                          [S]
if frameSchema >= 24: u32, u32 customEase           [S]
if customEase: 6 x (u32 n; n x 3 x (f64 x, f64 y))  [S]
```
The custom ease curves are for the targets all, position, rotation, scale,
color and filters, in that order. Each point is its in handle, the point and
its out handle; XFL's `CustomEase` lists the same points without the first
in handle and the last out handle.
Tween type: shape tween when `morph` is non-null [S]; otherwise motion when
keyMode bit0 is set [X] (26 tweens); otherwise none. Flash 8 writes
frameSchema 23 with the 22 layout.

### 2.5 CPicShape body (also used inside CPicFrame)
```
u8  shapeSchema (2 or 6). Below, ">=3" is the threshold for the newer fields
    (proven only for 2 vs 6).
matrix
ShapeData:
  u8  shapeDataSchema (5)
  u32 edge count hint (approximate)                              [S]
  u16 nFills;   nFills x FillStyle
  u16 nLines;   nLines x LineStyle
  edges until a 0x00 flag byte
  if shapeDataSchema >= 5: s32 nCubics; nCubics x Cubic             [S]
```
**Cubic** (the curve the edges were made from, as drawn with a cubic tool):
```
4 x (s32 x, s32 y)    start, control, control, end, in 1/20 px (twips)
if shapeSchema >= 3:
  u8 n; n x (s32 x, s32 y, u16 kind)   the quadratic points standing for it,
                                         starting at the cubic's start; kind 1
                                         marks a control point
  u8 more              bit 0 and bit 1 each add one (s32 x, s32 y) after it;
                       the other bits vary from save to save (leftover memory)
```
[S] 43,958 records in the nine CS3/CS4 Gumball files parse to the end of
their streams. Flash 8 shapes (schema 2) have the four points alone.
**FillStyle**
```
color; u16 kind: low byte is the SWF fill type [X]
  0x00 solid
  0x10 linear, 0x12 radial, 0x13 focal [0x10/0x12 X]:
       matrix (maps the -16384..16384 twip gradient square, as in SWF/XFL) [X render]
       u8 nStops; if shapeSchema >= 3: 8 bytes (zero); nStops x (u8 ratio, color)
  0x40 repeat, 0x41 clipped, 0x42/0x43 non-smoothed bitmap:
       matrix (bitmap pixels -> twips, e.g. a=d=20) [X]; u16 Media N [X]
```
**LineStyle**
```
color; u16 width in twips [X 81 strokes]; 4 bytes stroke style (0 = solid) [S]
if shapeSchema >= 3: 6 bytes caps/joins (00 00 01 01 00 03: round, round, miter 3.0?) [G];
                     FillStyle (stroke fill) [S]
```
**Edge record** (all coordinates are in 1/256 twip, the same fixed point as
XFL's `#hex.hex` edge numbers) [X]: 76,964 segments matched XFL exactly,
0 mismatches.
```
u8 f; 0 = end
if f & 0x40: style change: (f & 0x80 ? 3 x u8 : 3 x u16) = line, fill0, fill1.
             1-based indices into the lists above; 0 = none. Bit 15 of the u16
             form = XFL edge selection flag ("S" mask); mask it off.        [X]
three deltas, type = (f>>0)&3, (f>>2)&3, (f>>4)&3:
     0 = (0,0), 1 = 2 x s16, 2 = 2 x s32, 3 = 2 x s16 << 7
from = previous 'to' + d1 (d1 != 0 starts a new subpath, XFL "!")
ctrl = from + d2;  to = from + d3
straight edge when (f & 0x0C) == 0 (no control point)
if shapeSchema >= 3 and straight: one extra byte, always 0 (3,113 seen)  [S]
```
XFL may store the same segment in the opposite direction. In that case
fill0 and fill1 are swapped.

### 2.6 Symbol instance: CPicSymbol (graphic), CPicSprite (movie clip), CPicButton
```
CPicObj base (children: none seen; point = transformation point)
u8  instSchema (10, 19)
matrix                                                         [X] 260
u16 firstFrame (0-based, graphics)                            [X] 179
u16 loop: 0 loop, 2 single frame (1 = play once [G])          [X] 179
u8  1                                                          [S]
color transform: 4 x (u16 mult 8.8, s16 add) in order alpha, red, green, blue  [X] alpha and brightness
u16 colorMode (0 none, 1 brightness, 4 alpha seen; 2 tint / 3 advanced [G]); s16 panel percent; color tint
CString   always empty                                         [S]
u32 symbol = N of the "Symbol N" stream (the library reference) [X] 260/260
if instSchema >= 19: 7 bytes 00 00 00 00 01 00 00              [S]
else if instSchema >= 14 (Flash 8): 3 bytes 00 00 00            [S]
CPicSprite only:
  u8 spriteSchema (6, 8); action block (2.9: onClipEvent script);
  CString instance name                                        [X] 39 names
  u32 2, u8, u32, u32, u32                                     [S]
  if spriteSchema >= 8: u8 1, u32 (per-file counter), CString component metadata XML
CPicButton only:
  u8 buttonSchema (11); action block (button script, e.g. "on (release) {...}") [X text];
  u8, CString (instance name [G]), u32
```

### 2.7 CPicText
```
CPicObj base; u8 textSchema (9, 13); matrix [X]
4 x s32 bounds twips: xmin, xmax, ymin, ymax.
  (XFL left/width differ by about 2 twips on each side.)
u8 1, u8 0, u32 0, u16 0, CString                      [S] (text kind static/dynamic/input
                                                         is probably here; only static in corpus [G])
runs until u16 nChars == 0:
   u8 runSchema (7 ANSI, 12 UTF-16 [X]); u16 size in twips [X];
   u8 n + font name (UTF-16 if runSchema >= 10)        [X] (XFL uses the PostScript face name)
   color [X]; attribute block of 23 bytes (rs 7), 24 bytes (rs 10, Flash 8 [S])
   or 35 bytes (rs 12):
      [0] flags  [2] bold [X]  [3] italic [G]  [7] align 0 left / 1 right / 2 center [X]
      [8] s16 lineSpacing twips [X]  [16] s16 letterSpacing twips [X rs7]
   nChars characters (ANSI or UTF-16; CR = line break) [X]
CString; u32; u32
if textSchema >= 11: CString;  >= 12: CString;  >= 13: u8 filterFlag (0) + u16
```

### 2.8 Groups
A CPicShape child with objFlags bit0 = 0 is an XFL DOMGroup [X 78]. Its own
shape is the first member; the CPic children follow. All member matrices are
in the parent coordinate space; the group matrix equals the matrix of its own
shape.

### 2.9 Action block (frames, sprites, buttons)
```
u32 version (1 = MX2004, 4 = Flash 8, 5 = CS3), u32 kind, u32 id/hash, u32 0
if version >= 5: u32 0, CString script        [X]
if version == 4: CString script               [S]
```
No MX 2004 file in the corpus has a script, so where a version-1 block keeps
its script is unknown.

### 2.10 CPicBitmap (a bitmap on the stage)
```
CPicObj base; u8 schema (2); matrix (bitmap pixels to the parent, tx/ty in
twips); u16 N of its "Media N"; u8 0                                   [S]
```
Written as a DOMBitmapInstance of the library bitmap.

### 2.11 Shape tweens
The first keyframe of a shape tween holds the tween's precomputed outline
pairs. Phoenix tweens from the keyframes itself, so it reads past these and
keeps only the tween type; the field meanings are partly guessed [S].
```
CPicMorphShape: u8 schema (2); matrix; matrix; 8 bytes;
                u16 n; n x ReadObject CMorphSegment;
                u16 (0); u16 nFills; nFills x (color, u16 kind 0 = solid);
                u16 nLines; nLines x (color, u16 width, 4 bytes)
                (no gradient or bitmap fill seen: Phoenix stops the stream there)
CMorphSegment:  16 bytes; s32 x, y start on the first keyframe; s32 x, y on the last;
                u16 n; n x ReadObject CMorphCurve
CMorphCurve:    s32 control x, y, anchor x, y on the first keyframe; the same on
                the last; u32 (1 = straight)                            36 bytes
CObList:        u16 n (0xFFFF: u32 n follows); n x ReadObject
CMorphHintItem: s32 x, y on the first keyframe; s32 x, y on the last; u32   20 bytes
```

## 3. Contents stream
```
u16 version (0x129 / 0x138 / 0x143); 13 / 17 / 25 zero bytes        [S]
scene list:  ReadObject CDocumentPage ... until null tag
u16 next Page number, u16 1, u16 next scene number
symbol list: ReadObject CDocumentPage ... until null tag
u16 next Symbol number
media list:  ReadObject CMediaBits / CMediaSound ... until null tag
u16 next Media number
document: u8, u16, rect (4 x s32 twips xmin, xmax, ymin, ymax) -> width/height [X];
          u16, u16, u8, u16, u16; 4 x u32 grid/snap spacing twips [X 18px]; 12 bytes;
          color background [X white]; color grid color [X #C0C0C0];
          u32 frame rate 16.16 [X 30 / 12]; 7 bytes
0x129: u16 n + n x (CString key, CString value)   publish settings
0x138, 0x143: same list (document properties), then u32 nProfiles x (u16 n + pairs)
       [S 0x138: one property, legacyLineSpacing; one profile of publish settings]
u32 -1, CString, CString, u32 1, u16 0
u16 nSolid + nSolid x CColorDef, u8, u16 nGrad + nGrad x CColorDef, u8    (swatch panel)
u32 3, u32 1, 4 x u32 (rect), 6 x u32
u32 nFolders x Folder   [X] all 8 folders with paths
0x138, 0x143: u16 0
u16 n x (CString name, ReadObject)   ("PublishQTProperties::QTSndSettings" -> CQTAudioSettings: u32)
trailer (not traced for 0x138): 17 bytes, CString, 14 bytes, CString, CString, 20 bytes;
  0x143 adds u32 x4, u16 x2, u32, u16, u32 x2, CString, u32, CString, u32 x2,
  CString x2, u32 x5, u8, u32 x2, then u16 n + ANSI "Saved by ..." (NUL terminated)
```
**ItemHeader** (shared by symbols, scenes, media and folders):
`u32 ver (5/6), u32 1, itemID parentFolder, u32 1, itemID self [X],
u32 0, Linkage, Source, if ver >= 6: u8, and in 0x143 files CString, u8`.
The tail follows the file: Flash 8 writes ver 6 with the u8 alone [S].
* Linkage: `u32 ver (3/5/7), u8, CString x3, if ver >= 5: u8; u32` [S]
* Source (linked/imported item): `CString source .fla path, CString source
  item href, u32 sourceLastModified, u32, u32, itemID sourceItemID`
  [X Yard "testure"]

**CDocumentPage** (one per scene or symbol):
```
u8 schema (17 / 22 / 24); stream name ("Page N" / "Symbol N"; ANSI, wstr8 from 22);
CString name [X]; u32 N (0 for scenes); u8 symbolType 0 graphic, 1 button [G], 2 movie clip [X 110];
CString; u32 1; ItemHeader; u32 lastModified [X]; CString x2; u32, u8, u32, u32;
Linkage; Source;
schema 17: 34 bytes (constant)
schema 24: u8, CString, u32, u8, CString, u32 x3, CString, 3 x (u8 3, CString, u32, u32),
           CString, u32, u8, u32, CString, u32 x4, CString, u32 x2, CString x2, u32,
           CString, u32, scale-9 rect 4 x s32 (INT_MIN = none) [G], 9 bytes
schema 22 (Flash 8) [S]: schema 24 without the first u8 and CString, without
           the u32 x4 and CString after the fifth CString, and without the
           last u32, scale-9 rect and 9 bytes:
           u32, u8, CString, u32 x3, CString, 3 x (u8 3, CString, u32, u32),
           CString, u32, u8, u32, CString, u32 x2, CString x2, u32, CString
```
The library path is the folder chain (from the parent folder itemID) plus
"/" plus the name [X].

**Folder**: `u32 ver (3/4), CString name, ItemHeader` [X]

**CMediaBits**:
```
u8 schema (5/6), stream "Media N", CString name [X], u16 N,
CString import path, u32 sourceLastImported [X], ItemHeader, u32 1, u16 3,
u8 quality [X], u8 allowSmoothing, u32 jpeg byte count (0 for lossless) [S]
```

**CMediaSound**: same first part as CMediaBits, then
`u32 1, u8 8, u16 format (bit0 stereo, bit1 16-bit, bits2-3 rate 5.5/11/22/44 kHz)
[X 6], u32 sampleCount [X 6], u32 0xFFFFFFFF, 11 bytes`; schema 6 adds a
CString (empty) [S].

**CColorDef**: `u8 schema (3/4), color, u16 kind; if gradient: matrix, u8 n,
(schema >= 4: 8 bytes), n x (u8, color); u16 x3 HSL (0..240)`

## 4. Media streams
A `Media N` stream has the same bytes as the XFL `bin/Media N.dat` file of
the same number: 11 of 16 Yard media are byte-identical, and the others are
the same format recompressed. Phoenix's existing `.dat` handling therefore
applies directly (`Bitmap::datData`).

* Lossless bitmap layout:
  `03 05, u16 rowBytes, u16 w, u16 h, 4 x s32 frame (twips:
  0, frameRight, 0, frameBottom) [X], u8 flags (bit0 alpha), u8 1`, then
  chunks of `u16 len + bytes` until len = 0. Together the chunks form one zlib
  stream of ARGB, premultiplied. [X]
* JPEG bitmaps start with `FF D8`.
* MP3 sounds are raw frames. Other sounds are raw PCM in the CMediaSound
  format.

## 5. Mapping to XFL (summary)
* DOMDocument: width, height, frameRate, backgroundColor come from Contents
  §3.
* Library item: CDocumentPage, plus the timeline in `Symbol N`.
* `DOMSymbolInstance libraryItemName`: the instance `u32 symbol`, looked up as
  the `Symbol N` library path.
* Layers: reverse the stored order. parentLayerIndex comes from the `parent`
  object.
* Frame index is the sum of the earlier durations.
* Elements: in the order stored, with the frame's own shape first. CPicShape
  maps to DOMShape (flags bit0) or DOMGroup. CPicSprite, CPicSymbol and
  CPicButton map to DOMSymbolInstance with symbolType movie clip, graphic and
  button. CPicText maps to DOMStaticText.
* Shapes: edges convert to XFL edge strings one-for-one (same 1/256-twip
  units, the same fill0/fill1/stroke indices).

## 6. Not covered by the corpus (still unverified)
* Which custom ease curves the frame uses (one for all, or one per
  property): seen only on untweened frames.
* Frame labels and ease: the Flash 8 files have labels ("wave", "Short
  bounce") and eases of -100 and 100 where 2.4 puts them, but no XFL twin
  to compare against [S].
* Shape tweens: the `CPicMorphShape` layout is structural only (2.11), and
  one with a gradient or bitmap fill has not been seen.
* Mask/guided layer type codes.
* Tint and advanced color modes.
* Dynamic/input text.
* Text filters.
* Bitmap instances (`CPicBitmap`): structural, and the matrix taken as
  pixels to the parent from one file [S].
* Video (`CMediaVideo`).
* MX 2004 scripts.
* `CPicButton` instance names.

A class not described here ends the stream it is in: MFC objects carry no
length, so nothing after it can be found. Phoenix keeps what came before it
(the layers before, and the layer it was in up to it), reads the other
streams, and says which parts are missing when the document opens.
