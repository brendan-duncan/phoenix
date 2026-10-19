# The .phx document format

A `.phx` file is Phoenix's native document. It is a chunked container. Each
chunk's content is protobuf wire format, which Phoenix encodes and decodes by
hand with no protobuf library. This page is for a developer who needs to add a
field, find out why a file reads wrong, or write a tool that reads `.phx`
files.

Related pages:

- [phoenix_format.proto](phoenix_format.proto): the schema, and the
  specification of every message.
- [website/phx-format.html](../website/phx-format.html): the user-facing page
  on the format (goals, measured performance, the Phoenix-only feature table).
- [architecture.md](architecture.md): the `src/data` model this format
  serializes.
- [fla-format.md](fla-format.md): the FLA/XFL side. Most `.phx` messages carry
  XFL detail so that an FLA export can reproduce the original.
- [geometry.md](geometry.md): edges, paths and the twip grid.

## Source map

| File | Role |
|---|---|
| [src/phx/container.h](../src/phx/container.h), [container.cpp](../src/phx/container.cpp) | Header, chunks, index, trailer, CRC-32, deflate. Free of Qt; knows nothing about what chunks hold. `ContainerWriter`, `ContainerReader`, `prepare()`, `PreparedChunk`, `ChunkInfo`. |
| [src/phx/wire.h](../src/phx/wire.h) | Protobuf wire codec: `phx::Writer`, `phx::Packed`, `phx::Reader`. |
| [src/phx/phx_schema.h](../src/phx/phx_schema.h) | Field numbers, one `namespace` per message (`phx::schema::Frame::Duration`, ...). Mirrors the proto by hand. |
| [src/phx/phx_codec.h](../src/phx/phx_codec.h) | Shared conversions: `packColor`/`unpackColor`, `kUnitsPerPixel`, `toUnits`/`fromUnits`. |
| [src/phx/phx_document.h](../src/phx/phx_document.h) | Public API: `phx::save`, `phx::snapshot` + `phx::write`, `phx::load`, `phx::loadMedia`, `phx::dump`, `SaveOptions`. |
| [src/phx/phx_write.cpp](../src/phx/phx_write.cpp) | `src/data` to messages (`encodeFill`, `encodeStroke`, `encodePath`, `encodeElement`, `encodeFrame`, `encodeLayer`, `encodeTimeline`, `encodeSymbol`, `snapshotUnguarded`, `writeUnguarded`), plus media dedup (`MediaTable`, `MediaJob`, `prepareMedia`). |
| [src/phx/phx_read.cpp](../src/phx/phx_read.cpp) | Messages to `src/data` (`readFill`, `readStroke`, `readPath`, `readShape`, `readKind`, `readElement`, `readFrame`, `readLayer`, `readTimeline`, `readSymbol`, `loadUnguarded`), lazy media (`MediaStore`, `loadMedia`). |
| [src/phx/phx_dump.cpp](../src/phx/phx_dump.cpp) | `phx::dump`, behind `Phoenix --phx-dump`. |
| [src/phx/parallel.h](../src/phx/parallel.h) | `parallelFor`: up to 8 threads, used to encode symbols and prepare chunks. |

Callers outside `src/phx`: `MainWindow` saves and opens documents
([src/gui/main_window.cpp](../src/gui/main_window.cpp); a file counts as a
Phoenix document when `phx::isPhx` sees the magic, whatever its extension),
the recovery autosave ([src/gui/autosave.cpp](../src/gui/autosave.cpp)), and
the headless commands ([src/gui/command_line.cpp](../src/gui/command_line.cpp),
[src/main.cpp](../src/main.cpp)).

## Container

Every integer in the header, the chunk headers and the trailer is
little-endian. Chunk content (protobuf) has its own encoding.

```
offset 0   +--------------------------+
           | file header, 32 bytes    |
32         +--------------------------+
           | chunk (8-byte aligned)   |  DOCU, SYMB..., MDIA..., unknown..., THMB, META
           | ...                      |
           +--------------------------+
           | INDX chunk               |  pointed to by the header and the trailer
           +--------------------------+
           | trailer, 16 bytes        |
EOF        +--------------------------+
```

### File header (32 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 8 | magic `89 50 48 58 0D 0A 1A 0A` (`\x89PHX\r\n\x1a\n`) | `kMagic` in container.cpp |
| 8 | 2 | major version | `kMajorVersion` = 1 |
| 10 | 2 | minor version | `kMinorVersion` = 0 |
| 12 | 4 | flags | written 0, not read |
| 16 | 8 | offset of the INDX chunk header | |
| 24 | 4 | CRC-32 of bytes 0 to 23 | |
| 28 | 4 | reserved | written 0, not read |

`ContainerWriter::open` writes 32 zero bytes first. `finish()` seeks back and
writes the real header once the index offset is known.

### Chunk header (32 bytes) and body

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | type, a fourcc: `fourcc("DOCU")` puts the first letter in the low byte, so the letters read in order on disk |
| 4 | 4 | flags |
| 8 | 8 | stored length (bytes of body after this header) |
| 16 | 8 | length after decompression |
| 24 | 4 | CRC-32 (zlib's) of the stored bytes |
| 28 | 4 | chunk id, assigned 1, 2, 3... by the writer in write order |
| 32 | n | stored bytes |

`ContainerWriter::writeChunk` zero-pads so that each chunk header starts on an
8-byte boundary.

Flags (`ChunkFlags`):

| Bits | Meaning |
|---|---|
| 0 to 3 (`kCompressionMask`) | 0 stored, 1 (`kDeflate`) zlib deflate. Other values fail `ContainerReader::content` with "uses a compression this Phoenix does not know". |
| 4 (`kCritical`, 0x10) | the reader must understand this chunk type to edit safely |

Compression is plain zlib (`compress2` at level 3) and nothing else. zstd
appears in the build but not in this format. `prepare()` deflates only when
asked, when the content is over 64 bytes and at most `0x7FFFFFFF` bytes, and
when deflating makes it smaller. Otherwise the chunk is stored. Decompression
goes through `inflateAll`, which works in pieces of 1 GB so sizes past 4 GB
are fine. Before inflating, `plausibleLength` rejects a declared length more
than about 1032 times the stored length, so a hostile header cannot make the
reader allocate a huge buffer.

### Index and trailer

The INDX chunk is always stored and critical. Its body is a `ChunkIndex`
message: field 1 repeats one `ChunkEntry` per chunk (`encodeIndex` /
`decodeIndex` in container.cpp). The index does not list itself.

| ChunkEntry field | Type | Content |
|---|---|---|
| 1 id | varint | chunk id |
| 2 type | fixed32 | fourcc |
| 3 offset | varint | offset of the chunk header |
| 4 stored_length, 5 length | varint | |
| 6 flags | varint | left out when 0 |
| 7 crc | fixed32 | |
| 8 media_id | varint | MDIA only |
| 9 symbol_name | string | SYMB only |

Trailer (last 16 bytes): index offset (8), `PHX!` (4), CRC-32 of the first 12
bytes (4).

### How `ContainerReader` opens a file

`open()` memory-maps the file (`MapViewOfFile` / `mmap`). `openMemory()` takes a
buffer instead. `openBytes()` then does the following:

1. Checks the magic and refuses a major version above `kMajorVersion`. It
   accepts any minor version.
2. If the header CRC is good, reads the index at the header's offset.
   `readIndex` checks the INDX type, its CRC, and that every listed chunk fits
   in the file.
3. Otherwise, or if that index fails, tries the trailer's offset. Success
   through the trailer with a bad header sets `recovered()`.
4. If both fail, `walkChunks()` steps through chunk headers from offset 32,
   skipping INDX chunks, until one does not fit. This sets `recovered()`. An
   empty walk is "damaged: no part of it could be read".

All bounds checks go through `fits()`, which is written so that
`offset + length` cannot wrap. `content()` checks the CRC before inflating, so
a damaged chunk only fails its own read.

### Saving safely

`ContainerWriter::open` writes to `<path>.saving`. `finish()` writes the index,
the trailer and the real header, flushes, calls `_commit`/`fsync`, closes the
file, and only then renames it over `<path>`. Any write failure, or the
destructor of an unfinished writer (`abandon()`), deletes the temporary file
and leaves the original as it was.

### Chunk types

| Type | Constant | Critical | Compressed | Content | Written by |
|---|---|---|---|---|---|
| `DOCU` | `kDocument` | yes | yes | `Document` | `writeUnguarded`, always first; the reader uses the first one only |
| `SYMB` | `kSymbol` | yes | yes | `Symbol`, one per library symbol, in symbol list order; the index carries `symbol_name` | |
| `MDIA` | `kMedia` | yes | only if the bytes are not already compressed | varint length + `MediaHeader`, then raw bytes | `prepareMedia` |
| `THMB` | `kThumbnail` | no | no | PNG | only when `SaveOptions::thumbnailPng` is set (the GUI save renders a 256-px-wide first frame; command-line and autosave saves have none) |
| `META` | `kMetadata` | no | yes | `Metadata` | always last before INDX |
| `INDX` | `kIndex` | yes | no | `ChunkIndex` | `finish()` |
| `XFLP` | `kXflPackage` | | | | declared in container.h but never written or read. The reader treats it as an unknown chunk. |

Write order in `writeUnguarded`: DOCU, then all SYMB, then all MDIA, then kept
unknown chunks, then THMB, then META, then INDX. Symbol and media chunks are
compressed and checksummed in parallel (`parallelFor` over `prepare` /
`prepareMedia`) and written in order.

## Wire encoding ([src/phx/wire.h](../src/phx/wire.h))

This is the protobuf wire format, a subset of it:

| Wire type | Writer | Reader |
|---|---|---|
| 0 varint | `uint`, `int64` (two's complement, so a negative int32 takes 10 bytes), `sint` (zigzag), `boolean` | `uint`, `int64`, `int32`, `sint`, `boolean` |
| 1 fixed64 | `fixed64`, `real` (IEEE double bits) | `fixed64`, `real` |
| 2 length-delimited | `bytes`, `string`, `message(field, nestedWriter)` | `string`, `bytes`/`size`, `message()` returns a sub-`Reader` |
| 5 fixed32 | `fixed32` (colors, fourccs) | `fixed32` |

- Groups (3, 4) and wire types 6 and 7 make `Reader::next()` fail. So do field
  number 0, field numbers above 2^29 - 1, and a truncated value. After a
  failure `failed()` is true and `next()` keeps returning false. The document
  reader turns this into `Context::damaged` and a warning, not an error.
- Packed repeated fields: build a `Packed` (`varint`, `sint`, `real`,
  `fixed32`) and call `writeTo(writer, field)`. Nothing is written when the
  list is empty. On reading, `varints()`, `reals()` and `fixed32s()` also
  accept a single unpacked value, as protobuf does.
- Nested messages are built bottom-up: encode the child into its own `Writer`,
  then `parent.message(field, child)`. Every encoder in phx_write.cpp returns
  a `Writer`.
- `Reader::keep(std::string&)` appends the current field's raw bytes, key
  included. `Writer::rawFields(std::string)` appends them back unchanged. This
  pair is how unknown fields survive a save (see below).

### Conventions in the messages

- proto2 defaults. A field left out has the default declared in the proto.
  The writer helpers in phx_write.cpp (`writeInt`, `writeSint`, `writeDouble`,
  `writeBool`, `writeString`, `writeColor`) take a `fallback` argument and
  skip the field when the value equals it. The reader assigns only the fields
  it finds, so an absent field keeps the C++ member's initial value. The
  proto default, the writer's `fallback` and the `src/data` member's default
  must therefore all be equal. A mismatch reads back wrong without any
  warning.
- Colors are a fixed32 of RGBA, red in the low byte (`packColor`).
- Matrices are `Matrix` (a, b, c, d, tx, ty doubles, with a and d defaulting
  to 1). `writeMatrix` leaves out an identity matrix entirely.
- Path geometry is on the FLA grid of 1/256 twip, which is 5120 units per
  pixel (`kUnitsPerPixel`). `encodePath` writes `segments` as packed varints
  `(pointCount << 3) | command` (0 move, 1 line, 2 quadratic, 3 cubic,
  4 close, the order of `fla::PathSegment::Command`). It writes
  `coordinates` as packed zigzag deltas x, y from the previous point, starting
  from (0, 0). `readPath` drops a segment with an unknown command or too few
  points, and marks the shape damaged.
- Other coordinates (element matrices, text boxes, morph points, transformation
  points) are plain doubles in pixels.
- Library references go by name (`library_item_name`, `bitmap_path`). Media
  go by media id.
- Enums are written as their `int` value and read through `readEnum`, which
  takes values below the known count and otherwise keeps the raw field and
  uses a fallback.

## Schema and field numbers

[phoenix_format.proto](phoenix_format.proto) is the specification.
[phx_schema.h](../src/phx/phx_schema.h) repeats its field numbers as
`enum : uint32_t` constants in a namespace per message, for example
`schema::Element::SymbolInstance = 22`. The codec uses only these constants,
never literals. The one exception is `ChunkEntry`, which container.cpp writes
with literal numbers 1 to 9. The two files are kept in step by hand, and
nothing checks that they agree.

Message tree, with the chunk that holds each root:

```
DOCU Document
  scenes*        -> Timeline
  folders*       -> LibraryFolder
  bitmaps*       -> BitmapItem (data_media, image_media -> MDIA ids)
  sounds*        -> SoundItem
  symbol_order*  (names; the symbols themselves are SYMB chunks)
  swatch_lists*  -> SwatchList
  package_files* -> PackageFile (media -> MDIA id)
  features*, required_features*
  xfl            -> XflDocument
SYMB Symbol
  timelines*     -> Timeline
Timeline -> layers* Layer -> frames* Frame -> elements* Element
Element: common fields 1-15, then a oneof kind at 20+:
  shape Shape (fills* IndexedFill, strokes* IndexedStroke, edges* Edge -> paths* Path)
  group Group (members* Element), symbol_instance, bitmap_instance, text, rectangle, oval, raw
```

Number ranges the proto sets aside:

| Message | Range | For |
|---|---|---|
| `Document` | 60 to 99 | Phoenix's own document settings (brushes 60, palettes 61, videos 62 so far) |
| `Element.kind` | 28 to 49 | new element kinds. `isKindField()` in phx_read.cpp treats every field from 20 to 49 as a kind field. |
| `Stroke` | 30 to 49 | Phoenix's own stroke kinds (brush = 30) |
| most messages | 15 | `xfl` (`XflDetail`) |

### `XflDetail`

Most messages that come from XFL have an `xfl` field holding an `XflDetail`:
`extra_attributes` (attributes Phoenix does not model, in read order),
`present_attributes` (modeled attributes the file wrote even at their
default), and `extra_children` (unmodeled child elements as XML text). These
map onto `fla::DOMElement::extraAttributes` and `presentAttributes` and the
various `extraChildren` vectors. `writeXfl` / `readXfl` handle them. Nothing
needs them to draw the document. They exist so that FLA, then `.phx`, then FLA
reproduces the package; see [fla-format.md](fla-format.md).

## Mapping `src/data` to messages

The writer and the reader walk the same tree, one function per message:

| Model (`src/data`) | Message | Encoder / decoder |
|---|---|---|
| `fla::Document` | `Document`, `XflDocument` | `snapshotUnguarded` / `loadUnguarded` |
| `fla::Symbol` | `Symbol` | `encodeSymbol` / `readSymbol` |
| `fla::Timeline`, `Layer`, `Frame` | `Timeline`, `Layer`, `Frame` | `encodeTimeline`... / `readTimeline`... |
| `fla::Element` subclasses | `Element` + kind | `encodeElement` + `encodeKind` / `readElement` + `readKind` |
| `fla::Shape`, `Edge`, `Path` | `Shape`, `Edge`, `Path` | `encodeShape`, `encodeEdge`, `encodePath` / `readShape`, `readEdge`, `readPath` |
| `fla::FillStyle` subclasses | `Fill` (oneof solid / linear / radial / bitmap) | `encodeFill` / `readFill` |
| `fla::StrokeStyle` subclasses | `Stroke` (`kind` picks `SolidStroke`, `DashedStroke`, ... `PaintBrushStroke`) | `encodeStroke` / `readStroke` |
| `fla::MorphShape` | `MorphShape` | `encodeMorphShape` / `readMorphShape` |
| `fla::Filter`, `ColorTransform` | `Filter`, `ColorTransform` | `encodeFilter`, `encodeColorTransform` / `readFilter`, `readColorTransform` |

Things to know when reading this code:

- **Kind first.** `readFill`, `readStroke` and `readElement` scan the message
  once to find the kind, construct the right subclass, then read the message
  again for the common fields. For `Element` the last kind field wins, as in
  protobuf. For `Fill` the first one wins.
- **Linking after reading.** `readTimeline` resolves `parent_layer_index` only
  to a layer above (index less than the layer's own), so a damaged index
  cannot make a cycle. `readLayer` recomputes `firstFrame`/`lastFrame`.
  `loadUnguarded` links symbol instances to symbols by name, then runs
  `fla::noteCenterPoints`, `fla::breakSymbolCycles` and `collectVideos`.
- **Symbol order.** The SYMB chunks can come in any order. `loadUnguarded`
  rebuilds `SymbolList::symbols` in `Document.symbol_order` order, then
  appends any symbol the order does not name. Symbols are found by
  `Symbol.name` inside the chunk; the index's `symbol_name` is not used for
  this. With 8 or more symbol chunks they are decoded on up to 8 threads
  (`std::async`), each with its own `Context`.
- **Shape style indices.** `Shape.fills` / `strokes` keep list order. Each
  entry also carries the index edges use for it (0 when the style is not in
  `fillsMap` / `strokesMap`).
- **Edge text is not stored.** `fla::Edge::data`, the original XFL edge string,
  is not part of the format. Only the decoded paths are. The FLA writer
  re-encodes edges when `data` is empty (`xfl_writer.cpp`). This is why the
  corpus test clears `data` on the original before comparing exports
  (`dropEdgeText` in [tests/test_phx.cpp](../tests/test_phx.cpp)).
  `Shape.cubic_edges` keeps the FLA's cubics records verbatim.
- **Limits.** Groups nested deeper than `kMaxDepth` (200) are dropped as
  damage. Frame indices and durations go through `fla::clampFrameIndex` /
  `clampFrameDuration`, and `last_frame` is capped at `fla::kMaxFrames`.
- **Exceptions.** `phx::load` and `phx::save` catch `std::bad_alloc` and every
  other exception and turn them into an `error` string. A hostile file cannot
  crash Phoenix through these entry points.

## Media

Every bitmap's `.dat` data and imported image, and every FLA package file
Phoenix does not model (sound data, `META-INF/...`, and so on), is one MDIA
chunk.

MDIA content layout:

```
varint  headerSize
bytes   MediaHeader (headerSize bytes)
bytes   the media's own bytes, in their original encoding
```

On save (`MediaTable::add`, `prepareMedia`):

- **References.** `BitmapItem.data_media` / `image_media` and
  `PackageFile.media` hold media ids. Ids are unique within the file and
  separate from chunk ids.
- **Dedup.** Bytes already on disk in a `.phx` (`fla::LazyBytes` with a
  `ByteLocation`) are deduplicated by location. Bytes in memory are
  deduplicated by FNV-1a 64 hash and then a byte comparison. The same content
  is stored once.
- **Copy as stored.** Media that still lie in a `.phx` keep their media id,
  and their stored bytes are copied without inflating or recompressing
  (`readStored` checks the CRC). New media take ids that avoid every reserved
  id. If the source file has gone or changed and the bytes were never loaded,
  the save fails ("Nothing was saved") rather than drop media.
- **Header contents.** The writer fills in `id`, `kind` (only when nonzero),
  `format` (from magic bytes: png, jpeg, gif, flash-lossless), `content_hash`
  and `package_path`. The other `MediaHeader` fields (`source_name`, `width`,
  `sample_rate`, ...) are defined but not written today.
- **Compression.** `alreadyCompressed()` sniffs PNG, JPEG, GIF, the FLA
  lossless `.dat` (`03 05`, zlib inside), ZIP, ID3/MPEG audio, Ogg, FLAC and
  ISO-BMFF (`ftyp`). Those are stored. Everything else is deflated.
- **Relocation.** With `SaveOptions::relocateMedia` (the default), every
  `LazyBytes` holder is pointed at the new file after the save, so the next
  save copies from it. The autosave sets it to false, because its recovery
  copy may be deleted.

On load (`MediaStore::read`): only each MDIA header is read, either from the
mapped bytes or by inflating the first 512 bytes (`inflateStart`). This gives
a `ByteLocation` with path, offset, lengths, flags, CRC, id and hash. The
mapping is released when `load` returns. `phx::loadMedia` later reopens the
file with `std::ifstream`, checks the CRC, inflates and strips the header the
first time something needs the bytes. Phoenix holds no handle on the file in
between, so a save can replace it.

## Compatibility

### Versions

The header has major 1, minor 0. A newer major is refused with "saved by a
newer Phoenix (format X.Y). Update Phoenix to open it." Any minor is accepted.
`loadUnguarded` has an empty branch for `minorVersion() > kMinorVersion`.
There are no migration steps yet.

### Unknown fields

Each decoder's `switch` ends in `default: r.keep(obj->phxUnknown)`. The
encoder for the same object ends in `w.rawFields(obj->phxUnknown)`. The
storage is `fla::DOMElement::phxUnknown`
([src/data/dom_element.h](../src/data/dom_element.h)), plus `phxUnknown` on
`fla::Filter` and `fla::TextRun`, which are not DOM elements. A field written
by a newer Phoenix therefore survives a load and save in an older one, as long
as its object is not deleted. `phx_keeps_what_a_newer_phoenix_wrote` checks
this for a `Document` field.

Gaps in this, as the code stands:

- **Kind-level fields move up.** Fields unknown inside an element's kind
  message (`Shape`, `Group`, `SymbolInstance`, `BitmapInstance`, `Text`,
  `RectanglePrimitive`, `OvalPrimitive`, `RawXml`) are kept in the same
  `phxUnknown` string as the `Element`'s own fields, because the C++ object is
  the same one. `encodeKind` never writes `rawFields`. `encodeElement` writes
  the whole string at the `Element` level. After a round trip, those fields
  sit in `Element` under their old numbers. This includes the unknown enum
  values `readEnum` keeps for `SymbolInstance.symbol_type`/`loop_type` and
  `Text.text_type`.
- **Some decoders drop unknowns.** These ignore unknown fields: `SoundItem`,
  `PackageFile`, `XflDocument`, `RawSection`, `CubicRecord`,
  `IndexedFill`/`IndexedStroke`, `KeyValue`, `Matrix`, `Vector2`,
  `Rectangle`, `Gradient`, `GradientStop`, `BitmapFill`, `WidthMarker`,
  `ColorTransform` and `XflDetail`. `SwatchList` keeps them on read, but its
  encoder does not write them back.
- **META and THMB are regenerated.** `Metadata` is not read. Every save
  writes a new META with only `application` and `modified`, and a THMB only if
  the caller supplies one.

### Unknown element and fill kinds

- **Element.** An element whose kind field is unknown (for example 28 to 49)
  becomes an empty `fla::Group`. The kind field's raw bytes go in
  `phxUnknown`. On save it is written as `group = 21` (empty) followed by the
  kept kind field. Since the last kind wins, a newer reader sees its own kind
  again.
- **Fill.** An unknown fill kind reads as a transparent `SolidColor`, with the
  field kept. On save the encoder writes `solid` before the kept field, and
  `readFill` takes the first kind, so a newer reader would then see the
  transparent solid.
- **Stroke.** An unknown stroke `kind` (above 6) draws as `SolidStroke` and is
  kept.

### Unknown chunks and required features

In `loadUnguarded`, the known types are INDX, DOCU, SYMB, MDIA, THMB and META.
Every other chunk is read into `Document::phxUnknownChunks` (type, flags,
content) and written back on save. It is recompressed through `add(..., true,
critical)`, so only the critical bit of its old flags survives. A critical
unknown chunk sets `readOnly`, with a warning naming the fourcc.
`MainWindow::loadFLAFile` then clears the document path, so Save asks for a
new name.

`Document.features` is recomputed on every save from
`fla::features::used(document)`
([src/edit/phoenix_features.h](../src/edit/phoenix_features.h)). Ids a newer
Phoenix listed that this build does not know are added back.
`Document.required_features` is passed through as read. Any id outside the
hard-coded `supported` set in `loadUnguarded` (today only
`"variable-width-strokes"`) makes the document read-only. Nothing in the
current writer adds required features of its own.

## Relationship to FLA

- **Phoenix-only features exist in both formats, in the same form.** Each
  feature in `fla::features::all()` is stored as `phoenix...` attributes on
  the relevant objects. In a `.phx` those attributes live in
  `XflDetail.extra_attributes`, exactly as in an FLA. The proto's
  `BrushDefinition`, `Palette`, `VideoItem`, `VideoInstance` and
  `Stroke.brush` are placeholders that the codec does not read or write yet.
  Placed videos are `RawXml` elements (`describeVideoInstance`). See
  [fla-format.md](fla-format.md) for the attribute side.
- **Only `.phx` keeps:**
  - fields and chunks from a newer Phoenix. `fla::unsupportedContent`
    ([src/writer/xfl_content.cpp](../src/writer/xfl_content.cpp)) reports
    "parts saved by a newer Phoenix" and unknown feature ids before an FLA
    export.
  - the `features` list.
  - the thumbnail.
  - exact integer path geometry, with no edge-string re-encoding.
  - deduplicated media.
- **FLA to `.phx` to FLA is meant to be lossless.**
  `phx_round_trips_real_documents_as_their_fla_would` requires the `.phx`
  copy of each corpus FLA to export a package identical to the original's
  export. FLA package files Phoenix does not model ride along as
  `PackageFile` + `MEDIA_PACKAGE_FILE` (or `MEDIA_SOUND` when a `SoundItem`
  names that path). `XflDocument.from_binary` records that the source was an
  old binary FLA.

## Adding a field, step by step

Example: a new per-layer setting.

1. **Proto.** In [phoenix_format.proto](phoenix_format.proto), add the field
   to `Layer` with the next unused number. Never reuse or renumber a number;
   reserve removed ones. Give a `[default = ...]` if zero/empty is not the
   natural default. Use `sint32` for values that are often negative, `fixed32`
   for colors, `double` for reals, and `packed = true` for numeric lists.
   Respect the reserved ranges above. A new element kind goes in 28 to 49 of
   the `kind` oneof.
2. **Schema constant.** Add `NewThing = N` to `namespace Layer` in
   [phx_schema.h](../src/phx/phx_schema.h). The number must match the proto
   exactly; nothing checks it for you.
3. **Model.** Add the member to `fla::Layer` in `src/data`, initialized to the
   proto default.
4. **Writer.** In `encodeLayer`
   ([phx_write.cpp](../src/phx/phx_write.cpp)), call the `write*` helper with
   the same default as `fallback`, before `w.rawFields(layer.phxUnknown)`.
   Nested messages: build a child `Writer` and `message()` it. Lists: `Packed`
   or repeated `message()`.
5. **Reader.** In `readLayer`
   ([phx_read.cpp](../src/phx/phx_read.cpp)), add a `case s::Layer::NewThing:`
   above `default`. Clamp anything that sizes an allocation or indexes an
   array. For an enum, use `readEnum` with the count of known values.
6. **Compatibility decision.** If an older Phoenix ignoring the field would
   only lose detail, you are done: it keeps the field through `phxUnknown`.
   If ignoring it would make the document draw wrong and an edit would corrupt
   it, it needs a required feature. Put an id in `phxRequiredFeatures` when
   the document uses it, and add that id to `supported` in `loadUnguarded`.
   Today's writer does not do this for anything, so this path is untested. A
   new chunk type needs a `fourcc` constant in container.h and an entry in
   `known` in `loadUnguarded`. Decide whether it is critical.
7. **FLA side.** If the setting should also survive FLA/XFL, give it a
   `phoenix...` attribute and register it in
   [src/edit/phoenix_features.cpp](../src/edit/phoenix_features.cpp). See
   [fla-format.md](fla-format.md).
8. **Tests.** Round-trip a document that uses the field through `phx::save` /
   `phx::load` in [tests/test_phx.cpp](../tests/test_phx.cpp), or in
   `phoenix_features_survive_xfl_and_phx` in
   [tests/test_phoenix_features.cpp](../tests/test_phoenix_features.cpp) for a
   feature. Also update the user page
   [website/phx-format.html](../website/phx-format.html) when it changes what
   files hold.

The format version stays 1.0 for additions. Bump the major version only for a
change older readers cannot follow.

## Inspecting a file

**`Phoenix --phx-dump file.phx`** ([src/main.cpp](../src/main.cpp),
`phx::dump`) prints the version, notes whether the index was rebuilt, and for
each chunk prints its type, id, offset, sizes, flags, media id and symbol
name. It then dumps the content's fields without the schema:

- Varints print with a zigzag reading when odd.
- fixed64 prints as a double plus hex.
- fixed32 prints as hex.
- Length-delimited fields print as text when printable (the first 200 bytes),
  as a nested message when they parse as one (up to depth 40), or as
  `<n bytes>`.
- MDIA prints its header and the data size. THMB prints the PNG size.

Because it is schema-free, it works on files from any version and shows
unknown fields.

**`protoc --decode`.** No protoc binary is part of the build. With one
installed, decode a chunk body after stripping the 32-byte chunk header and
inflating:

| Chunk | Message |
|---|---|
| DOCU | `phoenix.Document` |
| SYMB | `phoenix.Symbol` |
| META | `phoenix.Metadata` |
| INDX | `phoenix.ChunkIndex` |
| MDIA | not directly: skip the varint length first, and decode only `headerSize` bytes as `phoenix.MediaHeader` |

This script extracts every chunk's content by walking the chunks, as
`walkChunks` does:

```python
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
assert data[:8] == b"\x89PHX\r\n\x1a\n"
offset = 32
while offset + 32 <= len(data):
    ctype, flags, stored, length, crc, cid = struct.unpack_from("<4sIQQII", data, offset)
    body = data[offset + 32 : offset + 32 + stored]
    if len(body) < stored:
        break
    content = zlib.decompress(body) if flags & 0x0F == 1 else body
    open("%s-%d.bin" % (ctype.decode("latin-1"), cid), "wb").write(content)
    offset = (offset + 32 + stored + 7) // 8 * 8
```

Then run `protoc --decode=phoenix.Document docs/phoenix_format.proto < DOCU-1.bin`.

**Other tools:**

- `PHOENIX_PHX_PROFILE=1` makes `phx::load` print a time per phase to stderr:
  file, media, document, symbols.
- `Phoenix --convert in out` converts between `.phx`, `.fla` and XFL by the
  output's extension (`commands::convert`). For an FLA/XFL target it lists on
  stderr what is not written and which Phoenix features are kept as
  attributes.
- `Phoenix batch <folder> --convert phx` and `Phoenix doctor <file> --repair
  out.phx` also go through `phx::save` (`commands::save` in
  [command_line.cpp](../src/gui/command_line.cpp)).
- `Phoenix --roundtrip <document> <folder> [samples]` saves as `.fla` and
  `.phx`, reopens both and compares rendered sample frames. Run
  [tools/roundtrip_check.py](../tools/roundtrip_check.py) for a whole folder.

## Tests

All tests are in the `phoenix_tests` executable
([tests/CMakeLists.txt](../tests/CMakeLists.txt)). Its first argument is a
substring filter (`phoenix_tests phx_` runs these). The tests write to
`%TEMP%/phoenix_phx_tests` (`scratchPath`), so parallel runs need separate
TEMP folders.

| Test ([tests/test_phx.cpp](../tests/test_phx.cpp) unless noted) | Covers |
|---|---|
| `phx_wire_round_trips_every_kind_of_field` | `Writer`/`Reader` for every wire type, packed lists, truncated input sets `failed()` |
| `phx_container_finds_damage_and_recovers_its_index` | CRC failures, rebuilding the index via trailer and chunk walk |
| `phx_round_trips_a_new_document` | save/load of a new document |
| `phx_keeps_locked_guides` | `Document.guides_locked` |
| `phx_keeps_what_a_newer_phoenix_wrote` | unknown `Document` field (999), unknown ancillary chunk, critical unknown chunk making the file read-only |
| `phx_round_trips_real_documents_as_their_fla_would` | opt-in. Set `PHOENIX_FLA_CORPUS` to a folder of FLAs: each `.phx` copy must export the same FLA package. Also prints timings. |
| `phx_media_load_when_needed_and_are_copied_as_stored` | lazy media, stored vs deflated media, copy-as-stored on resave |
| `phx_snapshot_writes_the_document_as_it_was` | `snapshot()` + `write()` (the autosave path) |
| `phx_container_refuses_sizes_past_the_file` | implausible inflate sizes, wrapping offsets |
| `phx_damaged_documents_read_what_they_can` | over-deep groups, bad paths, clamped durations, damage warnings |
| `phoenix_features_survive_xfl_and_phx`, `drawing_guides_and_symmetry_survive_xfl_and_phx` ([tests/test_phoenix_features.cpp](../tests/test_phoenix_features.cpp)) | every Phoenix-only feature through `.phx` and XFL; `Document.features` lists them |

## Known discrepancies between proto and code

Check these before trusting `protoc` output:

- `phx_schema.h` has `Stroke::PaintBrushDefinition = 17` and
  `Symbol::ItemElement = 11`. The codec reads and writes both, but neither is
  in the proto, so protoc shows them as unknown numbered fields.
- The proto's `BrushDefinition`, `Palette`, `VideoItem` (`Document` 60 to 62),
  `VideoInstance` (`Element` 28), `Stroke.brush` (30) and the
  `MediaHeader` / `Metadata` fields named under [Media](#media) and
  [Unknown fields](#unknown-fields) have no constants in `phx_schema.h`, or
  are never written.
- The feature list in the proto's `FeatureRegistry` comment (four ids) is
  older than `fla::features::all()`. The source of truth is
  [src/edit/phoenix_features.cpp](../src/edit/phoenix_features.cpp).
