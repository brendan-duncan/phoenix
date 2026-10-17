# The HTML5 runtime

Plain JavaScript that plays a movie Phoenix publishes as HTML5 (File >
Publish with the HTML5 format, or `Phoenix render movie.fla --out
page.html`). It is the `flash.*` API (`flash.display`, `flash.events`,
`flash.geom`, `flash.text`, `flash.media`, `flash.filters`,
`flash.utils`, `flash.ui`, `flash.net`, `flash.system`) over one canvas,
written so that ActionScript 3 translated to JavaScript runs on it
unchanged: translated classes extend the runtime's classes, and the
movie's timelines are the runtime's MovieClips. No build step and no
dependencies; it runs in any current browser.

The folder is built into Phoenix as a Qt resource (`html5` in
CMakeLists.txt), so `player.js` here is `:/html5/player.js` in the app.
Publish joins the runtime's files into one `phoenix.js` beside the page.

## What Publish writes

For a page `movie.html`:

| File | What it is |
|---|---|
| `movie.html` | The page: a `<div id="phoenix-stage">`, the scripts, and `phoenix.start(...)` |
| `phoenix.js` | The runtime: libas3's `as3.js` (see "Translated ActionScript"), then this folder's files joined in this order: `core.js`, `geom.js`, `events.js`, `display.js`, `graphics.js`, `filters.js`, `render.js`, `bitmap.js`, `timeline.js`, `text.js`, `media.js`, `utils.js`, `player.js` |
| `phoenix-createjs.js` | `createjs.js`, for an HTML5 Canvas document only: CreateJS over the runtime, for the document's JavaScript frame scripts |
| `phoenix-as2.js` | For a movie whose scripts are ActionScript 1 or 2: libas3's `avm1.js` then `as2.js` (see "ActionScript 1 and 2") |
| `movie.content.js` | The movie: `phoenix.content({...})` with its symbols, timelines, shapes and text (see "Content") |
| `movie_assets/` | Its pictures (`imageN.png` or `.jpg`) and sounds (`soundN.mp3`, or `.wav` where Phoenix has no MP3 encoder) |
| `movie.as.js` | An ActionScript 3 movie's scripts translated to JavaScript (see "Translated ActionScript"), and `movie.as.js.map`, its source map back to the `.as` files and frames. Not there for a movie without ActionScript, whose page asks for none |

The page works from a web server or opened from the disk. From the disk,
sounds play through audio elements, as Web Audio cannot read local files,
and `BitmapData.draw` of a library picture may leave a picture whose pixels
the page cannot read back.

## Pages

```js
phoenix.start({container: "phoenix-stage", scale: "fit", actionscript: "movie.as.js"});
```

`phoenix.start(settings)` returns a promise of `phoenix.player`. Settings:

- `container`: the element (or its id) the canvas goes in; `canvas`: a
  canvas to draw on instead.
- `content`: a movie object; by default the last `phoenix.content` call's.
- `base`: where the movie's files are, before their relative paths.
- `scale`: `"fit"` (the stage fills the container, keeping its shape, as
  the stage's `scaleMode` asks) or `"none"` (the stage at its own size).
- `actionscript`: a translated ActionScript file to load before the movie
  starts (left out or `false` for none). A file that is not there is no
  error.
- `frame`: play to that frame (from 1) at once and hold it there.
- `trace`: an element (or id) to write `trace` output, console output and
  errors into.

The page's address can ask for the same things: `#frame=12`, `#trace`,
`#scale=none`. Phoenix's own checks use them to compare a published page
with Phoenix's render of the same frame in a headless browser.

`phoenix.player` has `tick()` (one frame and draw), `pause()`, `play()`,
`frame`, `stage`, `root`, `draw()`, `errors()` and `pagePoint(x, y)` (where
a point of the stage is on the page, for tests that send the pointer
there).

## The runtime

| File | Holds |
|---|---|
| `core.js` | The package tree (`phoenix.ns`, as3.js's), class registration (`phoenix.defineClass`, `phoenix.define` for functions), `trace`, `getTimer`, `phoenix.unsupported` |
| `geom.js` | `Point`, `Rectangle`, `Matrix`, `ColorTransform`, `Transform` |
| `events.js` | `EventDispatcher` (capture, target and bubble phases; priorities; weak references accepted), `Event`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`, `TextEvent`, `ErrorEvent`, `IOErrorEvent`, `TimerEvent`, `ProgressEvent` |
| `display.js` | `DisplayObject`, `InteractiveObject`, `DisplayObjectContainer`, `Sprite`, `Shape`, `Stage`, `LoaderInfo` and the constant classes (`BlendMode`, `StageScaleMode`...) |
| `graphics.js` | `Graphics`, the whole drawing API, and how its fills and strokes are drawn |
| `filters.js` | `BlurFilter`, `DropShadowFilter`, `GlowFilter`, `BevelFilter`, `GradientGlowFilter`, `GradientBevelFilter`, `ColorMatrixFilter`, `ConvolutionFilter` |
| `render.js` | The renderer: the display list onto the canvas, with layers for filters, masks and blend modes, timeline masks, 9-slice scaling, scroll rects |
| `bitmap.js` | `Bitmap`, `BitmapData` |
| `timeline.js` | `MovieClip`, `SimpleButton`, `Scene`, `FrameLabel`, and `phoenix.Graphic` (a graphic symbol's object) |
| `text.js` | `TextField`, `TextFormat` and the text constants |
| `media.js` | `Sound`, `SoundChannel`, `SoundTransform`, `SoundMixer` over Web Audio |
| `utils.js` | `Timer`, `flash.utils` functions, `Keyboard`, `Mouse`, `URLRequest`, `URLLoader`, `navigateToURL`, `SharedObject` (local storage), `Capabilities`, `ExternalInterface`, `Loader` (pictures) |
| `player.js` | Loading, the frame loop, the pointer and keys, the canvas's layout, input fields, frame sounds, and the hooks for translated ActionScript |
| `createjs.js` | CreateJS for HTML5 Canvas documents (not part of `phoenix.js`) |
| `as2.js` | ActionScript 1 and 2's classes over the runtime (not part of `phoenix.js`; in `phoenix-as2.js`) |

Each frame runs as Flash Player runs one: every playing MovieClip on the
stage moves on a frame; `enterFrame` goes to every object listening (on
the stage or not); the new frames' objects are made and `frameConstructed`
goes out; frame scripts run, parents before children; `exitFrame`; `render`
when `stage.invalidate()` asked for it; then the stage is drawn. A script's
`gotoAndPlay` or `gotoAndStop` makes the frame's objects at once and runs
its script once the running script is done.

The canvas is the stage's size times the page's scale and the screen's
pixel ratio, so it is sharp on any screen. Vector art is drawn with the
canvas's paths; a color transform goes into the colors themselves, so
only filters, masks and blend modes need a layer of their own.

The language's own classes (`int` and `uint`, `Vector`, `Dictionary`,
`ByteArray`, `XML`, the errors, `is` and `as`, `getDefinitionByName`,
`getQualifiedClassName`, `describeType`) are as3.js's, which comes first in
`phoenix.js`; the runtime keeps `flash.utils`' timers and `getTimer` so that
what they call is reported and the stage draws after it.

Not there yet: `Loader` for SWF files; 3D (`z`, `rotationX`, `rotationY`,
`matrix3D`); `Video`; `DisplacementMapFilter` and shaders; text selection
with the pointer. What the runtime does not do is said once in the console
(`phoenix.unsupported(what, consequence)`, "Phoenix HTML5: ..."), and a
`flash.*` or `fl.*` class it does not have at all is said when translated
code first asks for it; using it then throws ReferenceError #1065, as a
player without the class would.

## Translated ActionScript

Publish translates an ActionScript 3 movie's scripts with libas3
(`as3::js::translate`, from the classes SWF publishing compiles: the
document class, the classes the library links to and what they reach,
Phoenix's own `fl.*` components, each timeline with frame scripts as a
class, the components' inspector parameters) into `movie.as.js`:

```js
// libas3's bundle: each class defined when first used.
as3.define("com.studio.Hat", () => { ... class Hat extends MovieClip { ... } ... });
as3.define("movie_fla.MainTimeline", () => { ... });
as3.runScripts();

// The movie's classes: its main timeline's, and its symbols'.
phoenix.actionscript(function (movie) {
    movie.documentClass(as3.use("movie_fla.MainTimeline"));
    movie.linkClass("Hat", as3.use("com.studio.Hat"));   // by library name
});
//# sourceMappingURL=movie.as.js.map
```

The translated classes follow libas3's construction protocol, and so do
the runtime's: a JavaScript constructor only makes the object (a display
object's symbol and first frame included), and `$construct(...args)` is
the ActionScript constructor, run for the class actually made; a
subclass's `super(...)` is `super.$construct(...)`. The runtime's classes
whose state is their own properties (events, geometry, filters, text
formats) get a `$construct` that makes one with the arguments and takes
its properties (`phoenix.adoptClass`, which `phoenix.defineClass` applies);
`EventDispatcher`, `Bitmap`, `SimpleButton`, `Sound`, `Timer` and
`URLLoader` have their own. Every class the runtime defines is registered
with as3.js (`as3.register`), so translated code finds it by name, and
`getQualifiedClassName` names it.

After the modules run, every library item whose linkage class translated
code defines (a symbol, a sound, a bitmap) is made as that class, unless a
module bound the symbol to a class under it (its frame scripts' class).

A translated file (or any other script) calls `phoenix.actionscript(module)`
once or more. Each `module(as3)` runs after the movie's content has loaded
and before its main timeline is made, so the classes it registers are the
ones the timelines make their objects with. The object it is given has
as3.js's `as3` as its prototype, so code inside a module that names its
parameter `as3` still finds `as3.define` and `as3.use`.

```js
phoenix.actionscript(function (as3) {
    var MovieClip = as3.ns("flash.display").MovieClip;
    var Event = as3.ns("flash.events").Event;

    class Hat extends MovieClip {
        constructor() {
            super();                         // the symbol's first frame is made here
            this.addFrameScript(0, this.frame1);
            this.addEventListener(Event.ENTER_FRAME, this.spin.bind(this));
        }
        frame1() { this.stop(); }
        spin(e) { this.rotation += 5; }
    }
    as3.defineClass("com.studio.Hat", Hat);  // as3.ns("com.studio").Hat from now on
    as3.linkClass("com.studio.Hat", Hat);    // made for the library symbol linked as com.studio.Hat

    class Main extends MovieClip {
        constructor() {
            super();
            trace(this.stage.stageWidth, this.hat instanceof Hat);
        }
    }
    as3.defineClass("Main", Main);
    as3.documentClass(Main);                 // the main timeline is made as a Main
});
```

What `as3` has:

- `as3.ns(name)`: the package object for `name` (`"flash.display"`,
  `"com.studio"`, `""` for the top level), made when first asked for. The
  runtime's classes and the movie's share the one tree, so a qualified
  name always finds its class by walking it (`flash` is also a global).
- `as3.defineClass(qualifiedName, cls)`: puts `cls` in its package, and
  makes `getQualifiedClassName` and `getDefinitionByName` know it.
- `as3.classByName(qualifiedName)`: a class by its name, or null.
- `as3.linkClass(name, cls)`: binds `cls` to the library item whose
  linkage class (or, failing that, library name: its path in the library,
  folders and all, as the FLA names it) is `name`: a symbol (its
  instances, on the timelines or made with `new`, are `cls` objects, and
  `new cls()` makes one with the symbol's frames), a sound (`new cls()` is
  a `Sound` playing it) or a bitmap (`new cls()` is a `BitmapData` of its
  pixels). Returns false when there is no such item.
- `as3.documentClass(cls)`: the main timeline's class: the document class,
  or the class made for the main timeline's frame scripts. Without it, a
  class defined under the document class's name (the content's
  `documentClass`) is used.
- `as3.frameScripts(symbol, scripts)`: frame scripts for a timeline with no
  class of its own: `symbol` is a library name or linkage (`""` for the
  main timeline), `scripts` an object of functions by frame number from 0;
  each runs with the clip as `this`, as `addFrameScript` would have it.
- `as3.library()`: the library's symbols: `{name, linkage, type}`, `type`
  being `"mc"`, `"graphic"` or `"button"`.
- `as3.trace`, `as3.flash`, and `as3.stage` and `as3.root` once the movie
  runs.

Every linkage class the library names exists before the modules run, as an
empty subclass of `MovieClip` (of `SimpleButton` for a button, of the
symbol's base class when it names one; `Sound` and `BitmapData` for sounds
and bitmaps), so translated code can refer to one it does not define; a
module's own class takes its place with `linkClass`.

How the classes behave, as ActionScript has them:

- Construction. A class's objects get their symbol from the class they
  were made as (the runtime looks at `new.target`), so a subclass of a
  linked class makes the same symbol. The timeline's instance name and
  matrix are set before the constructor runs, and the symbol's first
  frame's children are made inside `super()`: once it returns, named
  children are properties of `this` (declared stage instances). The
  document class's constructor finds `stage` set.
- Frames are numbered from 1 in `gotoAndPlay`, `gotoAndStop` and
  `currentFrame`, and from 0 in `addFrameScript`. Labels and scenes work as
  they do in Flash (`currentLabel`, `currentFrameLabel`, `currentLabels`,
  `scenes`, `currentScene`, `nextScene`).
- A frame script runs once each time its frame is entered.
- Display objects a script changes (`x`, `scaleX`, `transform.matrix`,
  `alpha`, `transform.colorTransform`) stay as the script left them; the
  timeline no longer moves them. A timeline object a script removes stays
  removed until a frame places it anew.
- Errors thrown by listeners, frame scripts and constructors are reported
  (to the console, and to the trace element) and the movie goes on.

The ActionScript top level that is not `flash.*` (`int`, `uint`,
`Vector`, `XML`, `Dictionary`, `ByteArray`, `is` and `as`) is as3.js's;
`trace` from translated code goes through `as3.output`, which the runtime
sends where its own `trace` goes.

## ActionScript 1 and 2

A movie whose scripts are ActionScript 1 or 2 has them translated by libas3
(`as3::js::as2`, see its README) into `movie.as.js`, one module:
`avm1.movie(function ($, movie) {...})`, its frame scripts, instances'
handlers, `#initclip` code and classes. `phoenix-as2.js` is what it runs on:
libas3's `avm1.js` (the language as the first ActionScript virtual machine
has it: conversions, the scope chain, target paths, the global functions)
and `as2.js`, which gives the runtime's display objects their ActionScript 2
face and is the host `avm1.js` asks:

- Each movie clip, button and text field a script reaches has an
  ActionScript 2 object (`phoenix.as2Face(displayObject)`, and back with
  `phoenix.as2DisplayOf`): a proxy whose own properties are the timeline's
  variables and whose named children are found by name, its prototype
  `MovieClip.prototype` (or the class `Object.registerClass` linked to its
  symbol's identifier, which is called on it once it is made), `Button`'s or
  `TextField`'s. `_x`, `_alpha` (0 to 100), `gotoAndPlay` (frames counted
  through the scenes), `attachMovie` and the rest work on the display
  object; depths made by scripts go above the timeline's objects.
- `as2.js` registers the translated frame scripts with `as3.frameScripts`,
  and hooks the runtime where display objects come and go
  (`DisplayObjectContainer.prototype._insert` and `_removeAt`): a newcomer
  gets its instance's handlers (the entry's `h`), its class and `onLoad`.
  On the stage it listens for `enterFrame` (`onEnterFrame`,
  `onClipEvent(enterFrame)`, text fields showing variables), the pointer
  (button events on the outermost clip or button with handlers under it,
  `onMouseDown`, `Mouse` listeners) and the keys (`Key`, `keyPress`
  handlers).
- `_global` has `MovieClip`, `Button`, `TextField`, `TextFormat`, `Key`,
  `Mouse`, `Stage`, `Selection`, `System`, `Color`, `Sound`, `LoadVars`,
  `XML`, `XMLNode`, `SharedObject` (in `localStorage`), `MovieClipLoader`
  (pictures) and `flash` (the runtime's packages, for `flash.filters` and
  `flash.geom`). `getURL` opens a window, `trace` writes as the runtime's
  `trace` does; `fscommand` and SWF files to load are noted in the console.

## Content

`movie.content.js` is `phoenix.content(movie)`, `movie` being JSON:

```
{
  "version": 1, "width": 1280, "height": 720, "frameRate": 24, "background": 0xRRGGBB,
  "documentClass": "Main",            // when the publish settings name one
  "javaScript": true,                 // an HTML5 Canvas document
  "images": [{"src": "movie_assets/image0.png", "w": 64, "h": 64, "name": "...", "linkage": "..."}],
  "sounds": [{"src": "movie_assets/sound0.mp3", "name": "...", "linkage": "..."}],
  "art": [Shape, ...],                // shapes, by index
  "texts": [TextField, ...],          // text fields, by index
  "symbols": [Symbol, ...],           // by index
  "root": Timeline                    // the scenes, one after another
}
```

**Symbol**: `{"name", "type": "mc" | "graphic" | "button", "linkage"?,
"ident"?, "base"?, "scale9"?: [left, top, right, bottom], "timeline":
Timeline}`. The symbols published are those the scenes show, at any depth,
and those exported for ActionScript. `ident` is an ActionScript 1 and 2
movie's linkage identifier, the name `attachMovie` makes the symbol by
(`linkage` is then its ActionScript 2 class).

**Timeline**: `{"n": frames, "labels"?: [[frame, name]], "scenes"?:
[{"name", "at", "n"}], "sounds"?: [Sound], "js"?: [[frame, code]],
"once"?: true, "states": [[Entry, ...], ...], "map": [state, ...]}`.
Frames count from 0. `map[f]` is the index in `states` of what frame `f`
shows: the list of what is on it, bottom to top. Frames that show the same
share a state. A button's frames 0 to 3 are its up, over, down and hit
states. `js` is an HTML5 Canvas document's frame scripts, as function
bodies. `once` stops the main timeline at its end.

**Entry**: one object on a frame:

- `k`: its key. An object on one frame is the same object on the next when
  their keys (and what they place) are the same: it is moved, not made
  again, and a movie clip plays on.
- `d`: its depth, which orders it among the timeline's objects (and before
  those scripts added on top).
- `t` and `id`: what it is: `"s"` a symbol (`id` into `symbols`), `"a"` a
  shape (into `art`), `"x"` a text field (into `texts`), `"b"` a bitmap
  (into `images`).
- `m`: its matrix `[a, b, c, d, tx, ty]`; none for the identity. Tweens
  are worked out frame by frame, so each frame has its own.
- `ct`: its color transform `[rm, gm, bm, am, ro, go, bo, ao]`.
- `f`: its filters: `{"t": "blur" | "shadow" | "glow" | "bevel" | "gglow"
  | "gbevel" | "matrix", ...}` with the filters' own settings (`bx`, `by`,
  `q` blur and quality; `d`, `an` distance and angle; `c`, `a` color and
  alpha; `s` strength; `in`, `ko`, `ho` inner, knockout, hide object;
  `hc`, `ha`, `sc`, `sa` a bevel's colors; `bt` its type; `cs`, `as`, `rs`
  a gradient's colors, alphas and ratios; `m` a color matrix's twenty
  values).
- `bm`: its blend mode, as `BlendMode` names them.
- `n`: its instance name.
- `it`: how an instance behaves when it is not as its symbol is
  (`"graphic"`, `"mc"`, `"button"`).
- `gf`: the frame (from 0) a graphic shows.
- `v`: 0 when it is hidden; `cab`: 1 for cache as bitmap.
- `mk`, `mb`: a timeline mask: `mk` on the mask layer's objects, `mb` on
  those it masks, with the same number.
- `h`: in an ActionScript 1 and 2 movie, the id of the instance's `on()` and
  `onClipEvent()` handlers in the translated movie (`movie.handlers(id, ...)`).

**Shape**: `{"o": [Op, ...], "b": [x0, y0, x1, y1]}`, its ops drawn in
order in the shape's own space:

- a fill: `{"p": SVG path data, "f": Fill, "e": 1 for even-odd}`;
- a stroke: `{"p": path, "s": {"w": width, "f": Fill, "cap": "round" |
  "square" | "none", "join": "round" | "bevel" | "miter", "ml": miter
  limit, "sm": "none" for a width that does not scale}}`;
- a picture: `{"i": image, "m": matrix}`;
- text: `{"t": text, "font": CSS font, "x", "y": baseline, "f": Fill,
  "m": matrix, "ls": letter spacing}`.

Any op may have `"b"` (its box), `"o"` (opacity), `"bm"` (blend mode) and
`"clip"` (path data it is clipped to).

**Fill**: `{"c": 0xRRGGBB, "a": alpha}`; a gradient `{"g": "l" | "r", "s":
[[ratio 0..1, 0xRRGGBB, alpha], ...], "m": matrix, "sp": "reflect" |
"repeat", "fp": focal point, "i": "linearRGB"}`, its matrix taking the
gradient square (-819.2 to 819.2) to the shape, as `beginGradientFill`'s
does; a picture `{"b": image, "m": matrix, "r": 1 to repeat}`.

**TextField**: `{"x", "y", "w", "h", "type": "input"?, "ml": multiline,
"wrap", "pw": password, "border", "sel": 0 when not selectable, "max":
characters, "auto": autoSize, "var": the variable it shows (ActionScript 1
and 2), "html": 1 to show it as HTML, "runs": [{"t": text, "f": font, "s": size,
"c": color, "b": bold, "i": italic, "a": align, "ls": letter spacing,
"ld": leading, "u": URL}]}`.

**Sound**: `{"f": frame, "s": sound, "y": "event" | "start" | "stop" |
"stream", "l": loops, "e": the frame a stream stops at}`.
