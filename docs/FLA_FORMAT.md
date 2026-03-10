# FLA Format Specification

This document describes the Adobe Flash FLA format as parsed by the Phoenix project and how it is rendered.

## File Format Overview

FLA files are ZIP archives containing XML documents and binary assets. The primary parser entry point is `fla_parser.cpp`, which extracts and processes the following structure:

```
.fla (ZIP)
├── DOMDocument.xml    # Main document structure
└── LIBRARY/
    ├── Symbol1.xml   # Symbol definitions
    ├── Symbol2.xml
    └── image.png     # Bitmap assets
```

## Parsing Pipeline

### 1. fla_parser.cpp (Entry Point)

The `FLAParser::parse()` method:
1. Opens the ZIP file using `ZipReader`
2. Verifies `DOMDocument.xml` exists
3. Extracts and passes XML content to `DocumentParser`
4. Returns an `FLADocument` object containing the parsed hierarchy

### 2. document_parser.cpp (DOM Parsing)

The document parser builds an in-memory scene graph with the following structure:

```
FLADocument
└── Document
    ├── width, height, frameRate, version
    ├── timelines[]
    │   └── Timeline
    │       └── layers[]
    │           └── Layer (name, color, type, visible)
    │               └── frames[]
    │                   └── Frame (index, duration)
    │                       └── elements[]
    │                           ├── Shape (vector graphics)
    │                           ├── SymbolInstance
    │                           ├── Group
    │                           ├── BitmapInstance
    │                           ├── StaticText
    │                           ├── RectanglePrimitive
    │                           └── OvalPrimitive
    ├── symbolList
    │   └── symbols[]
    │       └── Symbol (name, timelines[])
    └── resources
        └── Bitmap (name, imageData)
```

#### Element Types

| Element | Description |
|---------|-------------|
| `DOMShape` | Vector graphics with fills and strokes |
| `DOMSymbolInstance` | Instance of a library symbol |
| `DOMGroup` | Grouping container for elements |
| `DOMBitmapInstance` | Reference to a bitmap resource |
| `DOMStaticText` | Static text elements |
| `DOMRectangleObject` | Rectangle primitive |
| `DOMOvalObject` | Oval primitive |

#### Fill Styles

- **SolidColor**: Hex color with optional alpha
- **LinearGradient**: Gradient with multiple color stops
- **RadialGradient**: Radial gradient with focal point

#### Stroke Styles

- **SolidStroke**: Continuous line
- **DashedStroke**: Dashed line pattern
- **RaggedStroke**: Variable width stroke
- **StippleStroke**: Stippled pattern
- **DottedStroke**: Dotted line

#### Layer Types

- **Normal**: Regular drawing layer
- **Mask**: Mask layer (controls visibility of masked layers)
- **Masked**: Layer affected by a mask
- **Folder**: Container layer
- **Guide**: Guide layer (not rendered)

#### Symbol Types

- **Graphic**: Static or animated graphic
- **Button**: Interactive button
- **MovieClip**: Animated movie clip

#### Loop Types

- **SingleFrame**: Display single frame
- **Loop**: Loop animation
- **PlayOnce**: Play once then stop
- **PingPong**: Play forward then backward

### 3. path_parser.cpp (Vector Path Data)

Path data is stored in a custom binary-like text format using special control characters:

#### Command Reference

| Command | Description | Format |
|---------|-------------|--------|
| `!` | Move to (new subpath) | `!x y` |
| `\|` | Line to | `\|x y` |
| `/` | Line to (alternate) | `/x y` |
| `[` | Quadratic curve | `[cx cy x y` |
| `(` | Cubic curve | `(x1,y1 x2,y2 x3,y3...)` |
| `S` | Style change | `S{n}` |
| `FS` | Fill style change | `FS{n}` |
| `LS` | Line style change | `LS{n}` |
| `;` | End/separator | `;` |

#### Coordinate System

- Coordinates are stored in **twips** (1/20 pixel)
- Can be decimal or hexadecimal (prefixed with `#`)
- Hex numbers use 24-bit signed integers (two's complement for negatives)

#### Example

```
!0,0|100,0|100,100|0,100
```

This creates a square path starting at origin.

## Rendering (phoenix_view.cpp)

The `PhoenixView` class renders the parsed document using Qt's QPainter.

### Rendering Pipeline

1. **Document Setup**
   - Calculate scale to fit document in viewport
   - Apply zoom and pan transformations
   - Support optional supersampling (2x) for anti-aliasing

2. **Timeline Rendering**
   - Process layers back-to-front
   - Handle mask layers (render mask first, then apply to content)
   - Support layer types: Normal, Masked, Folder, Guide

3. **Frame Selection**
   - Current frame = `firstFrame + player.currentFrame()`
   - Select frame based on frame index

4. **Element Rendering**

   | Element Type | Rendering Method |
   |--------------|------------------|
   | Shape | Connect path segments, apply fills and strokes |
   | SymbolInstance | Render symbol with color transforms |
   | Group | Recursively render members |
   | RectanglePrimitive | `drawRect()` |
   | OvalPrimitive | `drawEllipse()` |
   | StaticText | Render text runs with font styling |
   | BitmapInstance | Draw cached bitmap pixmap |

### Shape Rendering Details

The renderer handles complex vector shapes by:

1. **Collecting Directed Paths**
   - Extract paths from edges
   - Track fill style indices (fillStyle0, fillStyle1)
   - Associate fill/stroke styles by index

2. **Path Stitching**
   - Connect path segments that share endpoints
   - Use winding fill rule for complex shapes
   - Handle both forward and reverse path directions

3. **Style Application**
   - **Solid fills**: Direct color with alpha
   - **Linear gradients**: Two-point gradient with transform matrix
   - **Radial gradients**: Center-based with focal point offset

4. **Stroke Rendering**
   - Per-segment stroke application
   - Support for various stroke styles (solid, dashed, dotted)

### Color Transform

Symbol instances support color transformations:
- `tintMultiplier`, `tintColor`: Tint overlay
- `alphaMultiplier`, `alphaOffset`: Transparency
- `redMultiplier`, `redOffset`: Red channel
- `greenMultiplier`, `greenOffset`: Green channel
- `blueMultiplier`, `blueOffset`: Blue channel
- `brightness`: Brightness adjustment

### View Features

- **Zoom**: Mouse wheel zoom (0.1x to 10x range)
- **Pan**: Click and drag to pan
- **Anti-aliasing**: Optional supersampling (2x resolution)
- **Debug bounds**: Show element bounding boxes

## Data Model Summary

```
DOMElement (base)
├── Element
│   ├── Shape
│   │   └── Edge
│   │       └── Path
│   │           └── PathSegment (Move/Line/Quad/Cubic/Close)
│   ├── SymbolInstance
│   ├── Group
│   ├── BitmapInstance
│   ├── StaticText
│   │   └── TextRun
│   ├── RectanglePrimitive
│   └── OvalPrimitive
├── FillStyle
│   ├── SolidColor
│   ├── LinearGradient
│   └── RadialGradient
├── StrokeStyle (SolidStroke, DashedStroke, etc.)
├── Timeline
├── Layer
├── Frame
├── Symbol
├── Bitmap
└── Swatch
```
