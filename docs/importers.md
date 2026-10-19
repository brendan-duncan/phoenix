# Importers

This document covers everything Phoenix brings into an open document from a file that is not a Phoenix, FLA or SWF document of its own: raster images, sounds, layered PSD/PSB files, vector artwork (SVG, PDF, AI, EPS, PostScript), video, animatics (edit lists), scanned paper drawings, other documents' libraries, and extension packages. It also lists the smaller preset and settings importers.

Topics covered elsewhere:

- Opening FLA/XFL files, the FLA bitmap and sound encodings: [fla-format.md](fla-format.md)
- Reading SWF movies, including SWF taken in through Import: [swf.md](swf.md)
- The `.phx` container: [phx-format.md](phx-format.md)
- How the app, the edit layer and the command stack fit together: [architecture.md](architecture.md)
- The JSFL host interface that `document.importFile` calls into: [jsfl.md](jsfl.md)
- Python and MCP scripting: [python-and-mcp.md](python-and-mcp.md)
- Shapes, edges and fill styles that vector import builds: [geometry.md](geometry.md)

## Overview

### Where the code lives

| Area | Parser / reader (free of Qt) | GUI glue |
|---|---|---|
| Raster images, sounds | Qt `QImage`, miniaudio | [src/gui/media_import.cpp](../src/gui/media_import.cpp) (`MediaImport`) |
| Library items for bitmaps and sounds | [src/edit/library_media.cpp](../src/edit/library_media.cpp) (`media::newBitmap`, `AddBitmapCommand`, `AddSoundCommand`) | |
| PSD/PSB | [src/edit/psd_import.cpp](../src/edit/psd_import.cpp) (`psd::read`) | `MainWindow::importArtwork` in [src/gui/main_window.cpp](../src/gui/main_window.cpp) |
| SVG, PDF | [src/edit/vector_import.cpp](../src/edit/vector_import.cpp) (`vectors::readSvg`, `vectors::readPdf`, `vectors::toShape`), [src/edit/pdf_objects.cpp](../src/edit/pdf_objects.cpp) (`pdf::File`) | `MainWindow::importArtwork` |
| AI, EPS, PostScript | [src/edit/postscript_import.cpp](../src/edit/postscript_import.cpp), [src/edit/postscript_interpreter.cpp](../src/edit/postscript_interpreter.cpp) | through `vectors::readPdf` |
| Other documents (FLA, XFL, SWF) | [src/edit/document_import.cpp](../src/edit/document_import.cpp) (`documentImport::read`), [src/edit/library_transfer.cpp](../src/edit/library_transfer.cpp) | `MainWindow::importMedia`, [src/jsfl/model_import.cpp](../src/jsfl/model_import.cpp) |
| External libraries | `commands::load` in [src/gui/command_line.cpp](../src/gui/command_line.cpp), `transfer::copyItems` | `MainWindow::openExternalLibrary`, `MainWindow::copyLibraryItems` |
| Video | ffmpeg (external process) | [src/gui/video_import.cpp](../src/gui/video_import.cpp) (`VideoImport::extract`), `MainWindow::importVideo` |
| Animatics | [src/edit/edit_list.cpp](../src/edit/edit_list.cpp) (`editlist::parse`) | `MainWindow::importAnimatic` |
| Paper drawings | [src/edit/paper_cleanup.cpp](../src/edit/paper_cleanup.cpp) (`paper::cleanup`, `paper::vectorize`) | [src/gui/paper_import_dialog.cpp](../src/gui/paper_import_dialog.cpp) (`PaperImportDialog`, `PaperImport::run`) |
| Extension packages | [src/parser/extension_package.cpp](../src/parser/extension_package.cpp) (`extensions::read`, `install`, `remove`) | `ScriptRunner::installExtension`, `ScriptRunner::manageExtensions` in [src/gui/script_runner.cpp](../src/gui/script_runner.cpp) |

The parsers return plain structs (`psd::Image`, `vectors::Artwork`, `editlist::EditList`, `extensions::Package`) and know nothing about the document. The GUI code turns those into library items, layers and elements, always through commands on the edit context's `CommandStack` and inside one `beginMacro`/`endMacro`, so one import is one undo step.

### Entry points

File > Import (built in `MainWindow` menu setup in [src/gui/main_window.cpp](../src/gui/main_window.cpp)):

| Menu item | Shortcut | Slot | Handles |
|---|---|---|---|
| Import to Stage... | Ctrl+R | `MainWindow::importToStage` | everything in `kImportFilter` |
| Import to Library... | | `MainWindow::importToLibrary` | the same |
| Open External Library... | Ctrl+Shift+O | `MainWindow::openExternalLibrary` | `.fla .xfl .phx` |
| Import Animatic... | | `MainWindow::importAnimatic` | `.edl .xml` |
| Import Video... | | `MainWindow::importVideo` | `VideoImport::fileFilter()` |
| Import Paper Drawings... | | `PaperImport::open` | `.png .jpg .jpeg .bmp .tif .tiff .gif .webp` |

`kImportFilter` (an anonymous-namespace constant above `MainWindow::importToStage`) lists: images `png jpg jpeg gif bmp`, `psd psb`, vector artwork `svg ai eps ps pdf`, sounds `wav aif aiff mp3 flac ogg`, and `fla xfl swf`.

Other ways in:

- **Drag and drop** onto the window: `MainWindow::dragEnterEvent` / `dropEvent`. Documents (`.phx .fla .xfl`) open in a tab; `.zxp` installs; images, sounds and artwork (`MediaImport::isImage/isSound/isArtwork`) go to `importMedia`, to the stage at the drop point when dropped on the stage view, else to the library. With Alt held over the stage, images go to the reference board instead (`MainWindow::dropReferenceImages` in [src/gui/main_window_view.cpp](../src/gui/main_window_view.cpp)); they never enter the document. FLA/XFL/SWF are not accepted as media on a drop.
- **Paste**: the clipboard branch of the paste code in `main_window.cpp` writes `image/svg+xml` or `application/pdf` data to a temporary `Pasted.svg`/`Pasted.pdf`, or takes copied local file URLs, and calls `importMedia(paths, true, ...)`. A plain clipboard image becomes a bitmap plus an instance.
- **JSFL** `document.importFile(uri, importToLibrary)` in [src/jsfl/model_import.cpp](../src/jsfl/model_import.cpp) (`describeDocumentImport`). FLA/XFL/SWF are read in the JSFL layer itself through `documentImport::read`; everything else goes to `Host::importFile` ([src/jsfl/host.h](../src/jsfl/host.h)), which the app implements in [src/gui/script_runner.cpp](../src/gui/script_runner.cpp) by calling `MainWindow::importMedia` and reporting success when the library item count grew. Notes go to the output panel via `trace`. Video, animatics and paper drawings have no JSFL entry point. `document.importEmbeddedSWF` is covered in [swf.md](swf.md).
- **Command line**: `Phoenix --view <file>` (`MainWindow::viewFromCommandLine`) and `Phoenix --export-image <in> <out>` (`MainWindow::exportImageFromCommandLine`) accept artwork; both go through `MainWindow::openArtwork`, which makes a 1x1 document, runs `importMedia(..., true)`, then sizes the stage to the artwork's bounds and moves everything to the origin. Passing a `.zxp` as the file to open installs it ([src/main.cpp](../src/main.cpp)).
- **Library bitmap Properties**: Update and Import... call `MainWindow::reimportBitmap`, which replaces a bitmap's pixels (`SetBitmapPixelsCommand`, lossless encoding) from its `sourceExternalFilepath` or a newly chosen file. `MainWindow::updateBitmaps` does this for several at once.

### How notes reach the user

Readers collect human-readable notes (`psd::Image::notes`, `vectors::Artwork::notes`, `documentImport::Result::notes`), deduplicated where the reader has a `note()` helper. `MainWindow::importArtwork` prefixes each with the file name (`"<file>: <note>"`) and appends it to a `problems` list; `importMedia` shows the list in one `QMessageBox::warning` titled "Import to Stage" or "Import to Library", or hands it to the caller (`problemsOut`) for JSFL and the command line. A file that fails outright adds one line such as "`<file>` is not a file Phoenix imports."

### `importMedia` dispatch

`MainWindow::importMedia(paths, toStage, at, problemsOut)` runs every file inside one macro named "Import to Stage" or "Import to Library":

| Test (in order) | Path |
|---|---|
| `MediaImport::isImage` | `loadImage` + `AddBitmapCommand`; on the stage, a `BitmapInstance` at the stage's top-left, or centered on the drop point |
| `MediaImport::isSound` | `loadSound` + `AddSoundCommand`; when dropped (`at` set), `attachSound` puts it on the current layer's keyframe afterwards |
| `MediaImport::isArtwork` (`svg ai pdf eps ps psd psb`) | `MainWindow::importArtwork` |
| `documentImport::handles` (`fla xfl swf`) | `documentImport::read`, then a `SymbolInstance` of the main-timeline symbol |
| anything else | the "not a file Phoenix imports" note |

For images to the stage, `frame` is `PhoenixView::activeFrame()`; when the current layer is locked or hidden it is null, the images still go to the library and the status bar says so.

## Raster images

**Reader**: `MediaImport::loadImage` in [src/gui/media_import.cpp](../src/gui/media_import.cpp), decoding with `QImage`.

- A JPEG is stored as the original file bytes (`media::newBitmap(..., jpeg = true)`, attribute `isJPEG="true"`), exactly as FLA files keep JPEGs.
- Anything else is premultiplied to ARGB32 and encoded with `encodeLosslessBitmap` (attribute `originalCompressionType="lossless"`). See [fla-format.md](fla-format.md) for the encoding.
- Limit: width over 16383 (`0xffff / 4`) or height over 65535 is refused ("... is too large to import."). The same check applies to `MediaImport::bitmapFromImage`, used for PSD layers and images inside vector artwork. JPEGs skip this check because they are kept as files.
- The library name is the file name, made unique by `media::freeName` ("walk.png", "walk 2.png"...). `sourceExternalFilepath` and `sourceLastImported` are recorded so Update can re-read it.

**Image sequences**: in `importToStage`, a single picked image whose base name ends in digits triggers `MediaImport::sequenceOf`, which finds siblings with the same stem and extension (case-insensitive) and sorts them by number. If there are at least two, the user is asked whether to import them all; Yes calls `MainWindow::importSequence`, which reads every file first (so a bad file leaves no gap), then inserts one keyframe per image on the current layer starting at the playhead, each with one bitmap instance. A locked current layer stops it.

## Sounds

**Reader**: `MediaImport::loadSound` uses miniaudio ([src/gui/miniaudio_impl.c](../src/gui/miniaudio_impl.c) compiles it with `MA_NO_ENCODING` and `MA_NO_GENERATION`). It opens the file once to learn the channel count, then decodes to signed 16-bit at 44100 Hz, mono or stereo (more channels are downmixed to two by the decoder config).

**Into the document**: `AddSoundCommand` in [src/edit/library_media.cpp](../src/edit/library_media.cpp) writes a `DOMSoundItem` XML string into `Document::extraMedia`, a `SoundInfo` into `Document::sounds`, and the samples as raw little-endian PCM into `Document::extraFiles` under `bin/M <n> <time>.dat`; the format string is "44kHz 16bit Stereo" or "Mono". Import to Stage does not place a sound unless it was dropped; then `MainWindow::attachSoundTo` sets `soundName` on the keyframe under the playhead (inserting one if needed) as an event sound.

**Limits**: the whole file is decoded into memory. The filter lists `.ogg`, but the bundled miniaudio header only enables Vorbis when `stb_vorbis` is included, and no source in the tree includes it (see the questions at the end).

**Tests**: `jsfl_document_import_file` in [tests/test_jsfl_document.cpp](../tests/test_jsfl_document.cpp) exercises `importFile` through the test host, whose `importFile` ([tests/jsfl_test_host.h](../tests/jsfl_test_host.h)) fakes a `.wav` as silence; nothing in the suite decodes a real sound file through `loadSound`.

## PSD and PSB

### Reader

`psd::read(bytes)` in [src/edit/psd_import.cpp](../src/edit/psd_import.cpp) returns a `psd::Image` ([src/edit/psd_import.h](../src/edit/psd_import.h)): canvas size, a tree of `psd::Layer` (top first, as the Layers panel lists them), notes, and `ok`. It has no Qt dependency. A big-endian `Reader` clamps every read to the buffer and sets `failed` instead of running past the end; `Reader::endOf` keeps block lengths from wrapping.

Order of work:

1. Header: signature `8BPS`, version 1 (PSD) or 2 (PSB, where `Format::large` makes some lengths 8 bytes), channel count, size, depth, color mode.
2. Color mode data and image resources: skipped by length.
3. Layer and mask section: the layer info block (`readLayers`); then, after the global layer mask (skipped), tagged blocks are scanned for `Lr16`, `Lr32` or `Layr` only if the layer info block produced no records. 16- and 32-bit files store their layers that way. `longKey` lists the keys whose lengths are 8 bytes in PSB.
4. `readLayers` reads every record, then every record's channel data in record order. Each channel is decoded by `channelData` (compression 0 raw, 1 PackBits via `unpackBits`, 2 ZIP, 3 ZIP with prediction via `unpredict`) and reduced to 8 bits by `toEightBit`.
5. Groups are rebuilt from the section-divider records (see below), the list reversed to top first.
6. Only if no pixel layer came out, the flattened composite at the end of the file is read as one layer named "Background".

### Mapping into the document

`MainWindow::importArtwork` in [src/gui/main_window.cpp](../src/gui/main_window.cpp) handles `.psd`/`.psb` by extension, before the vector path.

Each pixel layer becomes a library bitmap through the `bitmapFor` lambda: its RGBA is copied into a `QImage`, the layer's opacity is multiplied into the alpha, and `MediaImport::bitmapFromImage` encodes it losslessly, named after the layer (or "Layer"), made unique by `freeName`. Bitmaps go to the library root, not a folder. A layer over the bitmap size limit adds the note "the layer `<name>` is too large." and is skipped.

**Import to Library**, or Import to Stage with no current layer in the timeline view: one bitmap per pixel layer, recursing into groups, hidden layers included. Nothing else is created: no layers, no symbols, no blend or clipping notes.

**Import to Stage** (current layer known from `TimelineView::currentLayer`):

- New layers are inserted at the current layer's index (index 0 is the top), each next one below the last, so the PSD stack lands above the current layer in its order. If the current layer is inside a folder, the top-level PSD layers go into that folder too.
- A PSD group becomes a folder layer (`newLayerFor(*timeline, true)`); its children follow it with `parentLayer` set to it.
- A pixel layer with normal blending gets a `BitmapInstance` on its first frame at `(left, top)` in canvas pixels, one PSD pixel to one stage pixel. The canvas's top-left sits at the stage's top-left, or the canvas is centered on the drop point. The stage size is not changed.
- A pixel layer with a mapped blend mode gets a new movie clip symbol "`<bitmap> Blend`" (numbered if taken) holding the bitmap at its origin, and a `SymbolInstance` of it with `blendMode` set, because the FLA model applies blend modes to movie clips only.
- `Layer::visible` is set from the PSD hidden flag, for pixel layers and folders alike.
- If any layer was clipped, one note: "layers clipped to the layer below came in unclipped."

Everything goes through `AddBitmapCommand`, `AddSymbolCommand` and `AddLayerCommand` inside the `importMedia` macro.

### Coverage table

| PSD feature | What Phoenix does |
|---|---|
| PSD (version 1), PSB (version 2) | Read. Other versions: "The file is not a Photoshop document Phoenix reads." |
| Raw, RLE (PackBits), ZIP, ZIP with prediction channel data | Read for layer channels. The flattened composite is read only when raw or RLE; a ZIP composite is ignored. |
| Grayscale, RGB, CMYK color modes | Read. CMYK is converted naively per pixel (`C * K / 255` etc. on the inverted ink values), with no ICC profile. |
| Bitmap, Indexed, Duotone, Lab, Multichannel | Refused with "Only RGB, CMYK and grayscale documents of 8, 16 or 32 bits are read; convert it in Photoshop first." |
| 8, 16, 32 bits per channel | Read, reduced to 8 bits: 16-bit by taking the high byte, 32-bit float clamped to 0..1 and raised to 1/2.2. Other depths (1-bit) refused with the note above. |
| Color mode data (palettes, duotone curves) | Skipped. |
| Image resources (resolution, ICC profile, guides, slices, layer comps, timeline/frame animation, and the rest) | Skipped wholesale. |
| Flattened composite | Used only when no pixel layer was read (a file saved without layers, or one whose layers were all empty). Becomes one layer "Background". Its alpha is the extra channel after the color channels when present. |
| Global layer mask | Skipped. |
| Layer bounds | Used for placement and size. Sizes past 300000 px a side or 2^28 pixels, or more pixels than the channel bytes could hold, are treated as damage and the layer is dropped. |
| Layer name | Pascal name, replaced by the Unicode `luni` name when present (UTF-16 to UTF-8, surrogate pairs handled). |
| Layer opacity | Multiplied into the bitmap's alpha (`bitmapFor`). |
| Hidden flag (flags bit 0x02) | Hidden layer or folder in the timeline. |
| Groups (`lsct`, `lsdk` section dividers) | Become folder layers. An end marker (3) opens a group, reading bottom-up; an open marker (1 or 2) closes it and supplies its name and flags. Unclosed groups are flattened into their parent. |
| Hidden group | Only the folder's own `visible` is cleared. `Layer::isVisible` ([src/data/layer.h](../src/data/layer.h)) does not inherit a folder's eye, so the group's layers still show. |
| Group opacity, group blend mode (including pass-through) | Ignored: folders get no opacity or blend. |
| Fill opacity, blending ranges, knockout, other tagged layer blocks | Ignored (only `luni`, `lsct`, `lsdk` are read; blending ranges are skipped by length). |
| Layer effects (`lfx2`, `lrFX`) | Ignored, silently. |
| Vector masks, real user mask (channel -3) | Ignored, silently. |
| User pixel mask (channel -2) | Applied: alpha is scaled by the mask inside the mask rectangle and by the mask's default color outside it. A disabled mask (mask flags bit 0x02) is not applied. Mask density and feather (the mask parameters block) and the other mask flags are ignored. |
| Clipping masks | Read (`Layer::clipped`); on Import to Stage one note says they came in unclipped. The layers are imported as ordinary layers. |
| Adjustment and fill layers | Records with empty bounds are left out with the note "Adjustment and fill layers, which have no pixels of their own, were left out." The same note fires for any empty pixel layer. A fill layer stored with pixels comes in as those pixels. |
| Text, shape and smart object layers | Come in as their stored (rasterized) pixels; no text, vector or link data is read. |
| Blend modes | Mapped by `blendName`: `norm`, `pass` to normal; `mul ` multiply, `scrn` screen, `over` overlay, `dark` darken, `lite` lighten, `diff` difference, `lddg` (linear dodge) add, `fsub` subtract, `hLit` hardlight. Every other key is drawn as Normal with one note per key: "A layer blend mode Phoenix has no like of (`<key>`) was drawn as Normal." Blended layers go inside a movie clip carrying the blend. |
| Truncated file | Whatever layers were read come in; if none were, "The document ended before its layers did." |

### Tests

[tests/test_psd_import.cpp](../tests/test_psd_import.cpp) builds PSDs in memory with a small `Writer`:

- `psd_layers_groups_and_rle_are_read`: group rebuilding, raw and PackBits channels, hidden flag, opacity, bounds.
- `psd_rejects_what_it_does_not_read`: bad signature, Lab mode refused with one note, `unpackBits`.
- `psd_damaged_sizes_are_not_given_memory`: oversized canvas, overflowing and over-claimed layer sizes, a composite claiming 65535 channels.
- `psd_deep_cmyk_large_and_zip_documents_are_read`: 16-bit high byte, CMYK inversion, PSB lengths, ZIP with prediction, `mul ` and `lddg` mapping, the unknown-blend note.

Not covered by tests: user masks, `luni` names, 32-bit float, `Lr16`/`Lr32` blocks, grayscale, the flattened composite's pixel values, and everything in `MainWindow::importArtwork` (layer order, folders, blend clips).

### Possible improvements

- Make a hidden group hide its children on import, for example by clearing `visible` on each descendant, since folder visibility is not inherited.
- Apply group opacity (multiply it into the children's alpha) and group blend modes (a movie clip per blended group).
- Read fill opacity (`iOpa`) and multiply it into the pixels, at least for layers without effects.
- Apply clipping masks, for example by masking the clipped layers' bitmaps with the base layer's alpha, or by using a mask layer.
- Read the ZIP-compressed flattened composite.
- Apply the resolution resource (pixels per inch) as an optional scale, and the ICC profile or at least a better CMYK conversion.
- Name the affected layers in the unmapped-blend note (it is per key today), or approximate the remaining modes where a close FLA blend exists.
- Tell adjustment/fill layers apart from empty pixel layers in the note, using their tagged blocks.
- Put a PSD's bitmaps in a library folder named after the file, as video import does for its stills.
- Read text layers (`TySh`) as text, and vector masks as masks or shapes.
- Add tests for masks, `luni`, 32-bit, `Lr16`, and the stage mapping.

## Vector artwork: SVG, PDF, AI, EPS, PostScript

### Data model

All vector readers fill a `vectors::Artwork` ([src/edit/vector_import.h](../src/edit/vector_import.h)):

| Field | Holds |
|---|---|
| `paths` | `Painted`: subpaths (move, line, cubic) in stage pixels, optional solid `fill`, optional `gradient` (linear or radial, stops, placement on the FLA gradient square, spread, focal point), optional solid `stroke` and `strokeWidth`, `evenOdd`, and `clips` (areas the paint is cut to) |
| `images` | `Image`: encoded PNG/JPEG bytes or straight RGBA, `inkCmyk` flag, an `href` for SVG images outside the file, and the placement of the unit square |
| `texts` | `Text`: one line, font family, size, bold, italic, color, placement of the baseline origin |
| `order` | everything in drawing order, bottom first, as (kind, index) |
| `width`, `height` | the page or canvas |
| `notes` | what could not be read |

### Dispatch

In `MainWindow::importArtwork`, `.svg` goes to `vectors::readSvg`; every other artwork extension (`ai pdf eps ps`) goes to `vectors::readPdf`, which:

1. Opens the bytes with `pdf::File` ([src/edit/pdf_objects.h](../src/edit/pdf_objects.h)): cross-reference tables and streams, object streams, filters Flate (with predictors), LZW, ASCIIHex, ASCII85 and RunLength; a damaged table is rebuilt by scanning for objects. Encrypted files are refused ("The file is encrypted.").
2. If that fails and `looksLikePostScript` (`%!`, or the EPS binary header `C5 D0 D3 C6`), hands off to `readPostScriptArt` in [src/edit/postscript_import.cpp](../src/edit/postscript_import.cpp). Otherwise the note is "The file is neither PDF nor PostScript (nor an Illustrator file)."
3. Takes the **first page only**. The page box is the first of `/ArtBox`, `/TrimBox`, `/CropBox`, `/MediaBox` found, default 612x792. One PDF point is one stage pixel; y is flipped so the page's top-left is the origin (`PdfRun::stage`).
4. Runs the page's content streams (`runContent`).
5. If the page drew no paths or images, tries the Illustrator private data on the page (`PieceInfo/Illustrator/Private/AIPrivateData*`, `illustratorPrivateData`), which may be plain, zlib-compressed after `%AI12_CompressedData`, or Zstandard-compressed after `%AI24_ZStandard_Data` (`readIllustratorPrivateData`), and reads it with the Illustrator-format reader.

`readPostScriptArt` handles EPS and pre-version-9 Illustrator files: an Illustrator 9+ EPS is read from the private Illustrator text it carries; an Illustrator-format file by its operators (`IllustratorReader`: paths, compound paths, colors, gradients, patterns, images, point/area/path text, layers, with hidden layers left out); anything else runs in the PostScript interpreter in [src/edit/postscript_interpreter.cpp](../src/edit/postscript_interpreter.cpp) (`readPostScript`) with the page taken from the bounding box.

### SVG coverage (`readSvg`, `walkSvg`)

| Feature | Handling |
|---|---|
| Parser | tinyxml2; parse failure gives "The file is not an SVG document." This is the only note SVG import produces. |
| Canvas | `width`/`height` (units px, pt, in, cm, mm; `%` ignored), `viewBox` mapped by the smaller scale from the top-left; `preserveAspectRatio` is not read. |
| Elements | `path` (all commands, arcs via `arcToCubics`), `rect` (with `rx`/`ry`), `circle`, `ellipse`, `line`, `polyline`, `polygon`, `text`/`tspan`, `image`, `use` (of elements and `symbol`); any other element (`g`, a nested `svg`) is walked as a group, with only its `transform` and styles applied (a nested `svg`'s own `viewBox` is not). |
| Skipped elements | `defs`, `clipPath`, `mask`, `symbol` (except through `use`), `pattern`, `marker`, `filter`, gradients, `style`, `metadata`, `title`, `desc`. Clipping paths and masks are therefore not applied. |
| Styles | Presentation attributes, then the `<style>` sheet's simple `.class` and element-name rules, then the `style` attribute. Properties read: `color`, `fill`, `stroke`, `stroke-width`, `opacity`, `fill-opacity`, `stroke-opacity`, `fill-rule`, `display:none`, `visibility:hidden`, `font-family`, `font-size`, `font-weight`, `font-style`. Not read: line caps, joins, miter limit, dashes. |
| Opacity | Multiplied into each path's fill and stroke alpha; group opacity is applied per path, not composited. |
| Gradients | `linearGradient`/`radialGradient` fills, with `href` inheritance, `gradientUnits`, `gradientTransform`, `spreadMethod`, focal point. A gradient on a stroke becomes its first stop's color. |
| Transforms | `matrix`, `translate`, `scale`, `rotate`, `skewX`, `skewY`; stroke width scaled by the transform's area scale. |
| Images | `data:` URIs in base64, or a file path resolved against the SVG's folder at import time. |
| Text | One `Text` per positioned `tspan` and one for the rest of the element's text; white space collapsed. |
| Guards | Recursion depth 32; 1,000,000 walked elements (`kMaxSvgElements`); a `use` that would repeat itself is skipped. |

### PDF content coverage (`runContent`)

| Feature | Handling |
|---|---|
| Paths | `m l c v y re h`, painting `f F f* S s B B* b b* n`, clipping `W W*`; `q`/`Q`, `cm`, line width. |
| Colors | `g rg k` / `G RG K`, and `sc scn SC SCN` in the space set by `cs`/`CS` (converted by `colorIn`). |
| Transparency | `gs` reads only fill and stroke alpha (`/ca`, `/CA`). Blend modes and soft masks in the graphics state are ignored. |
| Shadings | `sh` and shading patterns of type 2 (axial) and 3 (radial) become gradients; other types are left out with "Some shadings are of kinds Phoenix does not read and were left out." |
| Patterns | Tiling patterns are drawn tile by tile, cut to the shape (up to 2500 tiles per fill, 20000 per file); past that, "Pattern fills too large to draw tile by tile came in as plain colors." Other pattern kinds: "Pattern fills other than gradients and tiles came in as plain colors." |
| Form XObjects | Run recursively (depth 12, 20000 forms per file, then "Forms drawn past 20000 times were left out."). |
| Image XObjects | `DCTDecode` kept as the JPEG (CMYK flagged as ink unless `Decode` inverts it); unfiltered or Flate samples at 1, 2, 4 or 8 bits in gray, RGB, CMYK, Indexed, Separation or DeviceN, with an 8-bit `/SMask` as alpha. Images over 16 megapixels are subsampled. JPX, CCITT, JBIG2 and the rest: "Some images are of kinds Phoenix does not read and were left out." |
| Inline images (`BI`...`EI`) | Skipped: "Images written into the page's content were left out." |
| Text | `BT`, `Tf Tc Tw Tz TL Ts Tr Td TD Tm T* Tj ' " TJ`, decoded through each font's encoding or ToUnicode CMap (`readFont`); one `Text` per show operation; invisible (`Tr 3`, `7`) text is dropped. Fonts are mapped to a family name with bold/italic (`splitFontName`). |

### Mapping into the document

`MainWindow::importArtwork` walks `art.order`:

- **Paths**: shifted by the drop offset, then clipped. A clip whose area contains the whole path (with half the stroke width) is dropped, since files often clip to the page. Remaining clips are intersected with `QPainterPath`; a stroke-only path is first turned into its stroked outline (round caps and joins) filled with the stroke color. A path with both fill and stroke keeps its stroke, which then follows the cut outline. `vectors::toShape` then builds a drawing object (`Shape::isDrawingObject = true`) with one solid or gradient fill and one solid stroke; holes are decided by nesting depth, not by `Painted::evenOdd` (the flag is recorded but `toShape` does not consult it). Open filled subpaths get an unstroked closing edge. See [geometry.md](geometry.md) for edges and fill styles.
- **Images**: decoded with `QImage::loadFromData` (CMYK JPEG ink inverted back when `inkCmyk`), re-laid out when the file's width/height disagree with the JPEG's, or built from RGBA, or loaded from the SVG's `href`. Each becomes a library bitmap "`<file> Image`" (deduplicated) and a `BitmapInstance` scaled from the unit square. An image that cannot be found adds "an image it shows could not be found or read."
- **Text**: a `StaticText` with `autoExpand`, one run; the face name is the family with "-Bold", "-Italic" or "-BoldItalic" appended; the box top is 0.9 em above the baseline.

Import to Stage adds every element to the current frame and, when there is more than one, groups them with `GroupElementsCommand`, so the artwork arrives as one selected group. Import to Library creates a graphic symbol named after the file (deduplicated), with all elements on frame 1 of its single layer, ungrouped. If nothing drawable came out and no note was produced, the note is "`<file>` has no artwork to import."

Exceptions thrown by a reader (for example out of memory) are caught around `readSvg`/`readPdf` and reported as "could not be read."

### Tests

- [tests/test_vector_import.cpp](../tests/test_vector_import.cpp): SVG shapes and styles, arcs, PDF paths upright, `toShape` holes, SVG gradients/classes/`use`/images, PDF forms/shadings/opacity, PDF text and fonts, damaged functions/images/fonts, runaway programs and forms, self-repeating `use`, standard fonts without widths.
- [tests/test_postscript_import.cpp](../tests/test_postscript_import.cpp): `pdf::File` on compact and damaged files, Illustrator private data (zlib and Zstandard), the Illustrator format (layers, compound paths, gradients, clips, text, patterns, images, area/path text, Illustrator 11 text), PostScript programs, hostile programs, shadings, tiling patterns, image masks, binary data, embedded font widths, Illustrator EPS behind a binary header.
- The mapping in `MainWindow::importArtwork` (clipping, grouping, symbol creation) has no unit test.

## Other documents: FLA, XFL, SWF through Import

Import to Stage/Library and JSFL `importFile` accept `.fla`, `.xfl` and `.swf` (`documentImport::handles`). `documentImport::read` in [src/edit/document_import.cpp](../src/edit/document_import.cpp):

1. Parses the file with `FLAParser` (which reads SWF too; see [swf.md](swf.md)). Parser warnings become `Result::notes`.
2. Collects the source library's symbols, bitmaps and sounds. Video items are not in that list.
3. Moves the source's first timeline (first scene, or the movie) into a new movie clip symbol named after the file (or `symbolName`), numbered if the name is taken in the source. Other scenes are not imported.
4. Copies everything with `transfer::copyItems(..., Resolution::KeepBoth)`, so clashing names come in renamed ("Ball copy") rather than replacing the target's items, and returns the command plus the main symbol's name in the target.

`importMedia` then places a `SymbolInstance` of that symbol on the current frame (at the drop point, or the origin) for Import to Stage. The JSFL path in `importDocument` ([src/jsfl/model_import.cpp](../src/jsfl/model_import.cpp)) does the same at the origin. Tests: `jsfl_document_import_documents_and_movies` in [tests/test_jsfl_objects.cpp](../tests/test_jsfl_objects.cpp); `library_items_copy_between_documents_with_what_they_use` and `library_conflicts_replace_keep_both_or_skip` in [tests/test_library_transfer.cpp](../tests/test_library_transfer.cpp).

## External libraries

`MainWindow::openExternalLibrary` loads a `.fla`, `.xfl` (by its folder) or `.phx` with `commands::load`, keeps it in an `ExternalLibrary` (owned by `MainWindow::_externalLibraries`), and shows it in a floating `LibraryPanel` dock with `setExternal(true)`. Nothing enters the current document until the user copies:

- The panel's copy-to-document request, or its Copy (held in `MainWindow::_libraryClipboard`) followed by a paste into the main library: both end in `MainWindow::copyLibraryItems`.
- Dragging an item onto the stage: `MainWindow::placeLibraryItem` copies it first, then places it.

`copyLibraryItems` asks `transfer::conflicts` for name clashes and, if any, shows "Resolve Library Conflicts" with Skip (default; the copies use the target's existing items), Replace, or Keep both. `transfer::copyItems` ([src/edit/library_transfer.h](../src/edit/library_transfer.h)) brings each item with everything it uses (`withDependencies`), as one undo step. Closing the dock releases the file.

Related: linked symbols kept in step with a master file are in [src/edit/linked_assets.h](../src/edit/linked_assets.h) (`links::Link`, stored as the symbol's `sourceFlashFilepath`/`sourceLibraryItemHRef`).

## Video

`MainWindow::importVideo` with [src/gui/video_import.cpp](../src/gui/video_import.cpp):

- Accepted extensions (`kVideoSuffixes`): `mp4 mov m4v avi mkv webm flv wmv mpg mpeg`. Not accepted by drop, paste or JSFL.
- Requires `ffmpeg` (or `ffmpeg.exe`) next to the Phoenix executable (`MovieExport::ffmpegPath` in [src/gui/movie_export.cpp](../src/gui/movie_export.cpp)); otherwise `MovieExport::missingFfmpeg` is shown.
- An options dialog offers "Shrink to the stage width" and "Import its sound, as a stream", both on by default.
- `VideoImport::extract` runs ffmpeg into a `QTemporaryDir`: JPEG stills at the document frame rate (`-vf fps=<rate>[,scale='min(<width>,iw)':-2] -q:v 3`), then optionally the audio as 16-bit stereo 44.1 kHz WAV. Progress is parsed from ffmpeg's `Duration:` and `time=` output; Cancel kills the process. A video with no sound is not an error.

Into the document, in one "Import Video" macro:

- A library folder "`<video name> frames`" (`AddFolderCommand`), and one JPEG bitmap per still (`MediaImport::loadImage`), each with `sourceExternalFilepath` set to the video.
- A new guide layer named after the video, locked, added at the bottom of the edited timeline, with one keyframe per still holding its bitmap instance. Guide layers are left out of publishing and export, which the dialog tells the user.
- The WAV, if any, as a library sound "`<video name>.wav`" set on the first keyframe with `soundSync="stream"`.

Limits: every frame is a separate full JPEG in the document; there is no embedded video item. Video import has no unit test.

## Animatics (edit lists)

`MainWindow::importAnimatic` reads a cut exported from a storyboard tool or an editor through `editlist::parse` in [src/edit/edit_list.cpp](../src/edit/edit_list.cpp), which picks the XML reader when the first non-blank character is `<`:

- **CMX 3600 EDL** (`parseEdl`): `TITLE:`, event lines with eight or more fields (record in/out and source in from the last four timecodes), `* FROM CLIP NAME:` and `* SOURCE FILE:` comments. Tracks starting with `A` are audio (`A2` is track 2); every video event is track 1. Timecodes are counted at the document's frame rate (the EDL does not say), and the cut is shifted so its first event starts at frame 0. `timecodeFrames` accepts `:`, `;` and `.` separators but always counts non-drop frames at the rounded rate, so drop-frame timecode is not compensated.
- **Final Cut Pro 7 XML** (`parseXml`, `xmeml`): the first `sequence` (also under `project/children`), its `rate/timebase` (with `ntsc` meaning x1000/1001), `clipitem`s of each video and audio track with `start`, `end`, `in`, `name` and `file/pathurl`. If the list's rate differs from the document's, all times are scaled and the report says so.

Into the current timeline (the symbol being edited, or the current scene):

- A "Shots" layer at the top with one keyframe per event of video track 1, labeled with `editlist::shotName` (the clip name without extension), for File > Export > Export Shots.
- A "Board" layer at the bottom with each shot's still (`png`, `jpg`, `jpeg`, found as the listed file, by its name beside the list, or as `<shot name>.<ext>` beside it), fitted and centered on the stage.
- One "Audio `<n>`" layer per audio track, with each clip's sound as a stream from its source in-point (`inPoint44`).

Other video tracks are ignored. A report lists counts and up to twelve missing files. Test: `an_edl_and_a_final_cut_xml_read_as_shots_and_sounds` in [tests/test_edit_list.cpp](../tests/test_edit_list.cpp) (parser only).

## Paper drawings

File > Import > Import Paper Drawings opens `PaperImportDialog` ([src/gui/paper_import_dialog.h](../src/gui/paper_import_dialog.h)); `PaperImport::run` does the import.

- **Input**: one picture, several picked files, or the numbered sequence a picture belongs to (`MediaImport::sequenceOf`). Any `QImage`-readable format.
- **Cleanup** (`paper::Settings`, [src/edit/paper_cleanup.h](../src/edit/paper_cleanup.h)): even out the paper, auto levels, line darkness threshold (with Find Line Darkness, `paper::suggestThreshold`), speck removal, gap closing, smoothing, line color. A live preview shows the cleaned lines and the share of the page they cover. Settings persist in `QSettings` under `paper/`.
- **Tracing**: each image is first scaled to at most 1600 px on its longer side (`PaperImportDialog::workImage`), cleaned (`paper::cleanup`) and traced (`paper::vectorize`) with the chosen curve fit (`trace::kCurveFits`) into a merge shape (not a drawing object).
- **Placement**: a new layer "Paper Drawings" (inserted at the current layer's index), or the current layer unless it is locked or a folder. Keyframes from the start frame every `hold` frames, the last one held for `hold` frames. Each page is fitted to the stage or kept a scan pixel to a stage pixel, and either positioned by its page (keeps pegged drawings registered) or centered on its lines.
- Everything is traced before anything is added, so an unreadable file leaves no gap; such files and drawings with no lines are reported.

Tests: [tests/test_paper_cleanup.cpp](../tests/test_paper_cleanup.cpp) covers the cleanup and vectorizing; the dialog and placement are untested.

## Extension packages (.zxp, .mxi, CSXS)

**Entry points**: Commands > Manage Extensions... (`ScriptRunner::manageExtensions`: Install Package for `.zxp`/`.zip`, Install from Folder, Remove, Open Folder), dropping a `.zxp` on the window, opening a `.zxp` (`MainWindow::openDocumentPath`), or passing one on the command line ([src/main.cpp](../src/main.cpp)). All call `ScriptRunner::installExtension`, which calls `extensions::install` with the Configuration folder and `<settings>/Extensions`.

**Reading** (`extensions::read` in [src/parser/extension_package.cpp](../src/parser/extension_package.cpp)): the package is a ZIP (through `ZipReader`) or a folder. The shortest-path `.mxi` wins; otherwise the shortest-path `CSXS/manifest.xml`; otherwise the error "This package has neither an .mxi file nor a CSXS/manifest.xml...".

- **`.mxi` packages** (`Package::Kind::Files`): name, version, id (falls back to name), author, description, and each `<file source destination>`. Sources are relative to the `.mxi` and found case-insensitively. Destinations under `$flash`, `$userflash`, `$animate` or `$useranimate` map to the matching subfolder of Phoenix's Configuration folder (`configDestination`); any other destination is kept with the extension only.
- **CSXS manifests** (`Package::Kind::Panels`): bundle id/name/version, each `Extension`'s main page, script path, menu name, UI type, sizes, icon, Node.js flags (`--enable-nodejs`, `--mixed-context`), `AutoVisible` and `StartOn` events, and the host list. Every file is kept; none is placed in the Configuration folder. The panels are hosted by [src/gui/cep_panel.cpp](../src/gui/cep_panel.cpp) (see [jsfl.md](jsfl.md)).

**Installing** (`extensions::install`): an installed package of the same id is removed first. Every file (except `META-INF/`) is copied to `Extensions/<id>/package/`; for `.mxi` packages, placed files are also copied into the Configuration folder (flattened to the leaf name under their destination). A record `phoenix-extension.txt` stores the metadata, panels, hosts and each placed file with an FNV-1a hash. All paths go through `safePath`, which rejects absolute paths, drive letters and `..`.

Notes shown after installing:

- the host list does not include `FLPR`: its panels may expect scripts Phoenix does not run;
- panels ask for Node.js: which Node modules Phoenix provides and which it does not;
- the package placed `.swf` files: SWF panels are kept but not run, JSFL commands and tools work;
- some files were meant for places outside the Configuration folder;
- a placed file under `Tools/`: the tools join the toolbox when Phoenix starts again (added by `installExtension`).

**Removing** (`extensions::remove`) deletes each placed file only if its hash still matches (a file the user has edited stays), then the extension folder.

Tests: [tests/test_extension_package.cpp](../tests/test_extension_package.cpp) (`extension_packages_install_their_files_and_remove_them`, `extension_packages_read_html_panels_from_their_manifest`, `extension_packages_keep_to_their_folder_and_record`).

## Smaller importers

These read files into settings or panels rather than into document content.

| What | Where | Formats |
|---|---|---|
| Swatches: Add Colors / Replace Colors | `SwatchesPanel::importColors` in [src/gui/swatches_panel.cpp](../src/gui/swatches_panel.cpp), `swatches::readAct`/`readGpl` | `.act`, `.gpl` |
| Linked palette | [src/gui/palettes_panel.cpp](../src/gui/palettes_panel.cpp) | `.phxpal` |
| Brush presets | [src/gui/brush_properties_dialog.cpp](../src/gui/brush_properties_dialog.cpp), `brushpresets::fromSetText` | `.phxbrushes` |
| Brushes from another document | [src/gui/brush_library_dialog.cpp](../src/gui/brush_library_dialog.cpp) | `.fla .xfl` |
| Motion presets (files or a folder) | `MotionPresetsDialog::importFiles` in [src/gui/motion_presets.cpp](../src/gui/motion_presets.cpp) | `.xml` |
| Eases | [src/gui/ease_curve_dialog.cpp](../src/gui/ease_curve_dialog.cpp) | `.json` |
| Keyboard shortcuts | [src/gui/shortcuts_dialog.cpp](../src/gui/shortcuts_dialog.cpp) | Phoenix `.json`, and XML/TXT shortcut files |
| Themes | [src/gui/theme_editor.cpp](../src/gui/theme_editor.cpp) | Phoenix theme files |
| Workspaces | Import Workspace... in `main_window.cpp` | Phoenix workspace files |
| Reference images | `MainWindow::dropReferenceImages`, Add Reference Image in [src/gui/main_window_view.cpp](../src/gui/main_window_view.cpp) | images, kept on the reference board, not in the document |

## Open questions for maintainers

- `.ogg` is in `kImportFilter`, `kSoundSuffixes` and the animatic sound search, but the bundled miniaudio only decodes Vorbis when `stb_vorbis` is included, and no source in the tree includes it. Ogg imports probably fail with "could not be read as a sound."
- `IllustratorReader` in [src/edit/postscript_import.cpp](../src/edit/postscript_import.cpp) declares `_sawClip` and reports "Clipping masks were not applied: what they hide shows." when it is set, but nothing sets it, and the reader does apply clips (`clips()`). The note looks dead.
- In the PSD stage path, `document->symbolList` is created outside any command when a blended layer needs a symbol; undoing the import leaves the empty list behind.
