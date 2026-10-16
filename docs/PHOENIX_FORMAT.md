# The Phoenix document format (.phx)

Phoenix's own file format. FLA and XFL remain Phoenix's formats for
exchanging files with Animate; `.phx` is what Phoenix saves by default. Its
job is to hold everything Phoenix can do, including what Animate cannot
store, and to keep holding it as Phoenix grows.

The schema of the document model is `docs/phoenix_format.proto`. This page
describes the container, the encoding rules and the compatibility rules
around it.

## Goals

1. **Nothing lost.** Everything Phoenix reads from an FLA survives
   FLA → PHX → FLA: the modeled document, plus every attribute, element and
   package file Phoenix keeps without modeling. A `.phx` saved from an FLA
   can always be exported back to the same FLA.
2. **Phoenix's own features.** Variable width strokes, textured and bitmap
   brushes, linked palettes, reference video, review notes and whatever comes
   next are stored natively, not squeezed into XFL.
3. **Fast to open and save.** No XML to parse and no text geometry to
   tokenize; media is never decoded or re-encoded to save; the parts of a
   file can be read in parallel and media loaded only when shown.
4. **Embedded data.** Bitmaps, sounds, video, brush textures and fonts live
   inside the one file, in their original encodings.
5. **Compatible both ways.**
   - *Forward*: an older Phoenix opens a file from a newer one. It shows what
     it understands, keeps what it does not, and saves without dropping it.
   - *Backward*: every Phoenix opens every older `.phx`.
6. **Robust.** Damage is detected (checksums) and contained (one damaged
   chunk does not take the rest of the file with it); saving never leaves a
   half-written file in place of a good one.

## File layout

A `.phx` file is a header, a sequence of chunks, and an index of the chunks
at the end.

```
+----------------------+  offset 0
| header (32 bytes)    |
+----------------------+
| chunk                |
| chunk                |  ... in any order
| ...                  |
+----------------------+
| index chunk          |  located by the header and the trailer
+----------------------+
| trailer (16 bytes)   |
+----------------------+
```

All integers in the header, chunk headers, index and trailer are
little-endian.

### Header

| offset | size | field |
| --- | --- | --- |
| 0 | 8 | magic `89 50 48 58 0D 0A 1A 0A` (`\x89PHX\r\n\x1a\n`) |
| 8 | 2 | format major version |
| 10 | 2 | format minor version |
| 12 | 4 | flags (none defined; written 0, ignored when read) |
| 16 | 8 | offset of the index chunk |
| 24 | 4 | CRC-32 of bytes 0–23 |
| 28 | 4 | reserved, 0 |

The magic's high byte, CR LF, Ctrl-Z and LF catch transfers in text mode,
as PNG's do.

### Chunks

Every chunk starts on an 8-byte boundary (padding is zero bytes), so a
reader can map the file and use a media chunk's bytes in place.

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | type, four ASCII letters |
| 4 | 4 | flags |
| 8 | 8 | stored length (bytes after this header) |
| 16 | 8 | length once decompressed |
| 24 | 4 | CRC-32 of the stored bytes |
| 28 | 4 | chunk id (unique in the file) |
| 32 | … | stored bytes |

Flags: bits 0–3 compression (0 none, 1 zlib deflate; others reserved), bit 4
*critical*. Other bits are reserved: written 0, kept as read.

A **critical** chunk is one a reader must understand to show the document
correctly. A reader meeting a critical chunk type it does not know opens
the file read-only and says which feature it is missing. A reader meeting an
unknown chunk that is not critical skips it, and copies it unchanged when it
saves. (PNG makes the same split with the case of a letter; here it is a
flag, so a type name can be any four letters.)

### Index and trailer

The index is a chunk of type `INDX` whose content is a protobuf-encoded
`ChunkIndex` (see the schema): for every other chunk its id, type, offset,
lengths, flags and CRC, and for a media chunk its media id. With it a
reader seeks straight to what it needs without walking the file.

The trailer is the last 16 bytes of the file: the index offset again (8
bytes), then `50 48 58 21` (`PHX!`), then the CRC-32 of the first 12 bytes.
A file whose header was damaged can still be indexed from the trailer, and
a file whose index was damaged can still be read by walking the chunks from
the header.

## Chunk types

| type | critical | content |
| --- | --- | --- |
| `INDX` | yes | the chunk index |
| `DOCU` | yes | `Document`: settings, scenes, library folders and item list, swatches, publish settings; one per file |
| `SYMB` | yes | `Symbol`: one library symbol and its timelines; one chunk per symbol |
| `MDIA` | yes | the bytes of one media item: a length-prefixed `MediaHeader`, then the data. Package files an FLA carried that Phoenix does not model (sound data, publish settings, metadata) are media of kind `MEDIA_PACKAGE_FILE`, kept for export back to FLA |
| `THMB` | no | a PNG thumbnail of the first frame, for file browsers |
| `META` | no | `Metadata`: application, user, dates, free-form key/values |

Compression: `DOCU`, `SYMB` and `META` are deflated (they are
mostly small integers and strings and shrink well). `MDIA` is stored as is
when its data is already compressed (PNG, JPEG, MP3, AAC, Ogg, FLAC, video)
and deflated otherwise (raw PCM, Animate's lossless bitmap `.dat`). `THMB`
is never compressed.

A symbol per chunk is what lets a reader decode symbols on several threads
at once, and lets a save copy the bytes of the symbols that did not change.

## Encoding the model

Every message in `phoenix_format.proto` is encoded in the protobuf wire
format: a sequence of fields, each a key (field number and wire type, as a
varint) and a value (varint, 64-bit, length-delimited or 32-bit). Phoenix
encodes and decodes it with its own small codec (`src/phx/wire.h`); no
protobuf library is needed, and `protoc --decode` can read any chunk's
content given the schema, which makes the format easy to inspect and to
read from other languages.

Conventions:

- **Lengths, counts and indices** are varints; **signed values** that are
  often negative use zigzag (`sint32`/`sint64`).
- **Real numbers** are doubles (`fixed64`), except where noted.
- **Colors** are a `fixed32` holding RGBA, red in the low byte.
- **Matrices** are `Matrix` messages (a, b, c, d, tx, ty as doubles); a
  missing matrix is the identity.
- **Geometry** is on Animate's grid, 1/256 of a twip (1/5120 of a pixel):
  coordinates are integers in that unit, stored as zigzag deltas from the
  previous point in a packed list. A straight segment costs a few bytes
  rather than a few dozen characters of XFL, and the integers are exact, so
  nothing drifts on a round trip.
- **Library references** are by the item's name (its full library path), as
  in FLA; deflate makes the repetition cheap. Media are referenced by media
  id (`uint32`, unique in the file).
- **Defaults**: a field left out has its default (zero, empty, or the value
  the schema documents). Writers leave out fields at their default, which
  keeps files small and makes a newly added field cost nothing in files
  that do not use it.

### Kept XFL detail

Phoenix keeps parts of an FLA it does not model: unknown attributes and
elements, raw sections, the order attributes were written in, and package
files. Each message for something that came from XFL has an `xfl` field
(`XflDetail`) holding exactly those, so an export to FLA writes what the
original had. It is never needed to display the document; a reader that
never exports FLA can skip it.

### Phoenix-only features

Phoenix's own features are kept in FLA and XFL files too: each is stored as
attributes of its own (named `phoenix...`) on the document, symbols, layers,
frames, elements, fill and stroke styles or text runs, which other FLA
editors ignore and Phoenix reads back. `src/edit/phoenix_features.cpp` is the
one list of them: each feature's id, its name, the attributes that mark it,
and how another FLA editor shows a document that uses it. The test
`phoenix_features_survive_xfl_and_phx` writes a document using every one of
them to an XFL folder and to a `.phx` and checks that each comes back.

Phoenix uses the list to warn before an FLA or XFL save: the features are
kept, but another editor drops them if it saves the file. `Document.features`
in a `.phx` lists the ids of those the document uses, written from the
document at each save; ids a newer Phoenix wrote that this one does not know
are kept, and an FLA export lists them as possibly lost.

| id | Feature | Another FLA editor shows |
| --- | --- | --- |
| `textured-brushes` | Textured brushes | the grain baked into a bitmap fill, which can no longer be changed |
| `art-and-pattern-brushes` | Art and pattern brushes | plain strokes along the same lines |
| `bitmap-painting` | Bitmap painting | the painted canvas as an ordinary bitmap |
| `linked-palettes` | Linked palettes | the colors as they are, without the palettes or their links |
| `effect-layers` | Effect layers | hidden guide layers: the tone, highlight or glow is missing |
| `line-and-color-art` | Line and color art layers | ordinary layers |
| `review-notes` | Review notes | the notes as text on a guide layer, which is not exported |
| `multiplane-camera` | Multiplane camera | a guide layer: the shot is not framed, moved, colored or given depth |
| `pegs` | Pegs | ordinary symbols |
| `deformers` | Deformers | the instances without their bend |
| `behaviors` | Behaviors | the instances as keyed, without the motion their behaviors add |
| `shape-operators` | Shape operators | the shapes as drawn, without Trim Paths, Wiggle Path or Zig-zag |
| `text-animators` | Text animators | the text as typed, its letters still |
| `compositing` | Track mattes and adjustment layers | matte layers drawn plainly and adjustment layers without their blur or color |
| `instance-effects` | Effects on instances | the instances without their blurs, glows, distortions and wipes |
| `node-graph` | Node graphs | the layers stacked as usual, without the graph's effects, merges and mattes |
| `mask-edges` | Mask edges and layer modes | masks hard-edged and not inverted, layers drawn normally |
| `several-masks` | Several masks on a layer | each masked layer masked by its own mask layer only |
| `motion-blur` | Motion blur, echo and posterize time | the layers drawn sharp, every frame, without echoes |
| `templates` | Template fields | the symbols' text and colors as drawn, the same in every instance, nothing pinned to the text |
| `master-controllers` | Master controllers | the properties as keyed, without the sliders |
| `3d-rotation` | 3D rotation and depth | the movie clips flat, without their 3D turn |
| `parenting-options` | Layer parenting options | child layers taking on all of the parent's transform |
| `joint-limits` | Bone joint limits | bones that turn freely when posed |
| `shape-binding` | Bones bound in Phoenix | boned shapes as drawn; posing them may bend them differently |
| `shape-hints` | Shape hints | shape tweens without the hints steering them |
| `mouth-charts` | Lip sync mouth charts | the mouth frames as set, without the chart |
| `layer-transparency` | Layer transparency | the layers drawn solid |
| `see-through-stage` | See-through stage | an opaque stage color |
| `safe-zones` | Safe zones and aspect-ratio guides | the stage without them |
| `other-phoenix-settings` | Other Phoenix settings | the document without them |

## Versioning and compatibility

### Versions

The header carries a **major** and a **minor** version. This is version
**1.0**.

- A **minor** version adds things: new fields, new messages, new chunk
  types, new enum values. Nothing a reader of an older minor version relies
  on changes meaning. Readers accept every minor version of their major
  version.
- A **major** version is a change older readers cannot follow. It is
  expected to be rare (ideally never). A reader refuses a file of a newer
  major version and says which version of Phoenix is needed.

### Rules that make forward compatibility work

1. **Field numbers are forever.** A field is never renumbered, and a removed
   field's number is `reserved` in the schema and never used again.
2. **Meaning is forever.** A field never changes its meaning or its unit. A
   new meaning gets a new field.
3. **Unknown fields are kept.** A reader keeps every field it does not
   understand, as its encoded bytes, attached to the object it came from,
   and writes them back on save. A document from a newer Phoenix therefore
   comes back from an older one with the newer data intact (as long as the
   object itself was not deleted).
4. **Unknown chunks are kept.** Ancillary chunks of unknown type are copied
   on save; critical ones make the document read-only (see Chunks).
5. **Required features.** When a newer feature changes how something
   already understood must be drawn (so ignoring it would show the document
   wrong rather than just incomplete), the writer adds the feature's name to
   `Document.required_features`. A reader that does not know a listed name
   opens the file read-only and says what is missing. Features that only add
   (a new kind of library item, say) are not listed, so older readers still
   edit those files.
6. **Enums are open.** An unknown enum value is kept as its number and
   written back; the reader treats the object as its documented fallback
   (an unknown layer type draws as a normal layer, an unknown fill as no
   fill).

### Backward compatibility

A reader of a later version reads every earlier file: fields it finds
missing have their defaults, and anything whose default changed between
versions is converted by a migration step keyed on the file's version, run
once after reading. Version 1.0 is the first, so there are none yet.

An element of a kind the reader does not know is kept as an empty group
that carries the unknown kind with it, so it keeps its place in the frame
and is written back as it was.

## Embedded data

Each media item is one `MDIA` chunk: a `MediaHeader` (media id, kind,
format, the original file name and size, pixel size, sample format, a
content hash) followed by the bytes.

- **Original encodings are kept.** An imported PNG is stored as the PNG; a
  JPEG as the JPEG; an MP3 as the MP3. A bitmap that came from an FLA keeps
  Animate's `.dat` encoding so that an export to FLA can copy it byte for
  byte. Nothing is decoded or re-encoded to save.
- **Loaded when needed.** Opening a file reads only each media chunk's
  header (from its stored bytes, or the first few hundred bytes inflated):
  its id, size and hash. The bytes are read, checked and inflated the first
  time something shows them. A file with hours of reference video opens as
  fast as one without. Phoenix keeps no handle on the file between reads,
  so it can be replaced by a save.
- **Deduplication.** Media with the same content hash and bytes are stored
  once, and every item using them points at the same media id.
- **Cheap to save.** Media still as they were read from a Phoenix file are
  copied into the new one as stored, keeping their media id: never read
  into memory, inflated or compressed again. Other media already compressed
  are stored as they are, so saving them costs a checksum and a write. Chunks
  are prepared on several threads and written in order. After a save the
  document reads its media from the new file, so the next save copies all
  of them. If a file media were to be copied from has gone or changed (its
  checksum says so), the save stops rather than leave the media out.
- **Kinds** today: bitmap, sound, video, font, brush texture, other. A new
  kind is a minor version change.

## Saving safely

Phoenix writes the whole file to a temporary file in the same folder,
flushes it to disk, and renames it over the old one. A crash or a full disk
mid-save leaves the previous file untouched.

## Performance

Measured on the FLA files of the test corpus, Release build,
one run after another on the same machine:

| | FLA | PHX |
| --- | --- | --- |
| size | 177.6 MB | 169.0 MB |
| read | 1.49 s | 0.24 s |
| save, from FLA | | 1.49 s |
| save again, from PHX | | 0.96 s |

(88 files.) Most of the size is media, which both formats store
compressed; a read leaves media on disk until shown. The first read of a
file just written takes longer, the system scanning the new file; the
figure above is a read after that.

## Tools

- `Phoenix --phx-dump file.phx` prints the header, the index and every
  chunk's fields as text, without needing the schema (and so for files from
  any version).
- `Phoenix --convert in out` converts between `.phx`, `.fla` and XFL
  folders, by the output's extension, without a window; what an FLA cannot
  hold is listed.
- With the schema, `protoc --decode=phoenix.Document phoenix_format.proto`
  decodes a chunk's content (after removing the chunk header and inflating).
- `PHOENIX_PHX_PROFILE=1` makes Phoenix print how long each part of a read
  takes.
- `Phoenix --roundtrip file folder [samples]` saves a document as `.fla` and
  `.phx`, opens each again and compares sample frames with the original's;
  `python tools/roundtrip_check.py <corpus>` runs it over a folder of files
  and reports any that draw differently, with images of the differences.

## Planned features

The schema already has room for the features in TODO and PARITY that need
storage, so adding them is a minor version: textured and bitmap brushes
(`BrushDefinition`), art and pattern brushes, linked palettes, reference
video items and instances, review notes, camera and 3D properties, function
curves, deformers and master controllers. Each is listed with the field
numbers set aside for it in the schema.
