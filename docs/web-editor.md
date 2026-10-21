# The web editor

Phoenix runs in a browser as the web editor, at
[phoenixanimate.com/webeditor](https://phoenixanimate.com/webeditor/). It
is the same source as the desktop app, built with Qt for WebAssembly. The
web build leaves out what a browser cannot do and works around the places
where a browser behaves differently; the desktop build compiles none of the
web-only code and runs as it always has.

## Building, trying and deploying

```sh
python tools/web/setup_web.py          # once
python tools/web/build_web.py --serve  # build, then try it at http://localhost:8000/
python tools/web/deploy_web.py         # put that build on phoenixanimate.com/webeditor
```

- **setup_web.py** clones emsdk into `third_party/emsdk` and activates
  Emscripten 5.0.5, the version Qt 6.12 is made for (the two have to
  match), which has the `emdawnwebgpu` port the stage's WebGPU renderer
  needs, then builds Qt 6.12.0 for WebAssembly from source (qtbase
  and qtsvg, about ten minutes) into `third_party/qt/wasm32`. Qt's prebuilt
  kits are slower for Phoenix, so the build turns on what they leave out:
  WebAssembly's own exceptions (the prebuilt kits emulate them in
  JavaScript, which costs every call that might throw), SIMD, and Qt's
  threaded image and painting helpers, which Qt disables for WebAssembly
  and the script enables with a one-line patch. It needs the desktop Qt of
  the same version, for its tools, and 7-Zip. `--prebuilt` unpacks Qt's
  prebuilt multithreaded kit instead (quick, slower editor); `--arch
  wasm64` builds the 64-bit kit. Stopped part way, it carries on from
  there when run again. It writes where everything went into
  `tools/web/web.ini`.
- **build_web.py** configures with Qt's toolchain file and builds with
  Ninja, Release unless `--config` says otherwise, then packages what the
  site needs into `<build>/webeditor`: the page
  (`resources/web/index.html`), `Phoenix.js`, `Phoenix.wasm`, Qt's
  `qtloader.js`, the icon, and an `.htaccess` for Apache, with Brotli and
  gzip copies of the program and scripts beside them (`Phoenix.wasm` is
  about 36 MB, 11 MB as Brotli; Brotli needs `pip install brotli`, and
  without it only gzip copies are made). `--serve` then
  serves that folder and opens it in the browser. Besides Emscripten and the
  wasm kit it needs the desktop Qt of the same version, for moc, rcc and
  uic. It finds all three in `tools/web/web.ini`, which setup_web.py writes
  (and which can name ones installed elsewhere), or from an option or an
  environment variable (`PHOENIX_EMSDK`, `PHOENIX_QT_WASM32` or `_WASM64`,
  `PHOENIX_QT_HOST`). It compiles everything the way the Qt kit was
  compiled: native exceptions and SIMD with a kit built by setup_web.py,
  JavaScript exceptions with a prebuilt one.
- **serve.py** serves a build folder (or the package) on localhost with the
  `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers,
  uncached, and the compressed copies where the browser takes them, as the
  site does. Threads need those headers: without them the page has no
  SharedArrayBuffer and says so instead of starting. Any host for the
  editor has to send them.
- **deploy_web.py** uploads the last build's package (what `--serve`
  tried; `--build` builds it again first) to the `webeditor` folder of the
  site, using the server in `tools/deploy_site.ini` (as `deploy_site.py`
  does), and swaps it in whole. It then checks that the live page sends the
  isolation headers. The package's `.htaccess` sets them, and serves the
  compressed copies (mod_rewrite), where Apache lets the folder have one;
  `--configure-apache` lets it, and enables mod_headers, mod_rewrite and
  mod_deflate (with sudo, once). `deploy_site.py
  --mirror` leaves the `webeditor` folder alone.

## Trying settings from the address

The page takes settings in its address, for measuring what makes the editor
faster before building it in (`resources/web/index.html`):

| Setting | What it does |
|---|---|
| `?dpr=1` | the whole window at 1 pixel per point, whatever the screen |
| `?stagescale=1` | the stage alone drawn at 1 pixel per point and scaled up |
| `?memory=1024` | start with that many MB of memory rather than growing to it |
| `?threads=4` | the threads Phoenix draws and decodes with (1 for none) |
| `?guithreads=0` | without Qt's threaded image and painting helpers |
| `?audiobuffer=8192` | the sound buffer in frames (256 to 16384; 4096 by default) |
| `?debugdocks` | log the panels and their tab bars to the console after each change to them |
| `?renderer=cpu` | the stage drawn on the CPU rather than the GPU (`?renderer=gpu` the other way) |

Settings combine (`?dpr=1&memory=2048`), and the browser console lists the
ones in use. Measured on 2026-10-19 playing a 1920×1080 production scene
(25 fps, with sound) in headless Edge at 2 device pixels per point, in
frames reaching the screen each second: Qt's prebuilt kit 12.7; the kit
setup_web.py builds 16.5; that without Qt's painting threads 11.0; with
`?stagescale=1` 17.5; with `?dpr=1` 21.7; `?memory=2048` and `?threads=1`
made no difference.

## Wasm32 and wasm64

Nothing in the source asks which one it is built for: `build_web.py --arch
wasm64` builds into `build-wasm64` with a wasm64 Qt kit, and Qt's toolchain
file passes `-sMEMORY64` for it. Qt publishes no wasm64 kit, so one has to
be built from source (`configure -platform wasm-emscripten -feature-thread
-feature-wasm64`). Both can be built and kept side by side.

Wasm32 has at most 4 GB of memory, and 32-bit `size_t` and pointers, which
no desktop build has. Keep sizes and offsets read from files in `uint64_t`,
never `size_t`.

## What is different on the web

### Nothing may wait in an event loop

A browser page cannot block: its thread has to return to the browser for
anything to draw or be clicked, and Qt stops the page when an event loop of
its own starts. So no `exec()` may run in the web build. Phoenix's code asks
through callbacks instead:

- dialogs: `modal::open(dialog, [](int result) { ... })`
- questions: `modal::ask(...)`; messages: `modal::information`,
  `modal::warning`, `modal::critical`
- input, color and font dialogs: `modal::getText`, `getInt`, `getDouble`,
  `getItem`, `getColor`, `getFont`, and `SwatchPopup::getColor`'s callback
  form
- pop-up menus: `modal::popup(menu, pos, [](QAction* chosen) { ... })`
- files: `file_pick::open`, `openMany`, `save`, `directory`

(`src/gui/modal.h`, `src/gui/file_pick.h`.) On the desktop each of these
calls `exec()` and calls back before it returns, so code written this way
does there exactly what it did when it waited itself, and the desktop runs
the same path the web build takes. Dialogs and menus handed to them are
made with `new` and deleted after the callback. In a callback, never use a
local captured by reference: on the web the function has long returned.
Where something could have changed while a dialog was up (the document
closed, a layer deleted), check it again in the callback.

A few calls must answer at once and cannot take a callback, such as a JSFL
script's `confirm()` or a CEP panel's open dialog. Those check
`modal::cannotWait()` and answer as if canceled on the web.

Qt's own widgets open some menus and dialogs with `exec()` too (a tool
button's menu, a drag). The web build links with `QMenu::exec`,
`QMenuPrivate::exec`, `QDialog::exec` and `QDrag::exec` wrapped
(`-Wl,--wrap` in CMakeLists.txt): menus pop up without waiting
(`src/gui/web_exec.cpp`), drags run in the page and send the usual drag and
drop events (`src/gui/web_drag.cpp`), and a dialog still waited on is
answered as canceled with a warning in the browser console. That last one
is a safety net: a new `exec()` in Phoenix's code is a bug in the web
build.

### Files

The page's file system lives in memory and is gone on reload. A file opened
comes from the browser's picker and is copied in; a file saved is written
there and offered as a download (`src/gui/web_files.h`). Exports download
what they write. Folders cannot be chosen, so XFL folders are neither opened
nor saved, and Open Recent, Auto-Save and anything that writes a folder of
files is left out. Settings (QSettings) are kept in the browser's local
storage.

### Left out

Menu items for what a browser cannot do are hidden with
`web_page::desktopOnly()` (`src/gui/web_page.h`, a no-op on the desktop):
the Commands menu (scripts, the script editor, Claude Code), video import,
Share, Render from Data, Export Shots, Publish, the render queue, HTML
extension panels, updates, reveal in the file manager and Exit. Running
other programs goes through `src/gui/process.h`, which on the web is a
stand-in whose programs never start. Python, the updater, the native web
view, the terminal (libvterm) and crash dumps are not built.

### The page

`resources/web/index.html` loads Phoenix, shows progress, and explains
when the page is not cross-origin isolated. Qt draws the frames of dialogs
and floating panels as HTML in a shadow root; `src/gui/web_page.cpp` styles
them in the theme's colors and keeps pop-up menus above floating panels.
The theme's dark palette is Phoenix's own on the web, since the browser
gives Qt only a light one.

### The stage on WebGPU

The stage is drawn by the Rive renderer on WebGPU (`src/gpu`, as on the
desktop), through Emscripten's `emdawnwebgpu` port. The browser gives no
way to wait for the GPU without Asyncify, so the frame is not read back
into Qt's window as it is on the desktop: `src/gpu/web_stage.cpp` lays a
canvas of its own over the stage widget, inside the main window's element
in Qt's shadow root, and the whole view (art, handles, rulers) is painted
into it through Rive's paint engine. The canvas lets the pointer through,
so the widget under it still takes the clicks and keys, and panels, menus
and dialogs, which are windows of their own, stay above it. It hides when
the widget does.

The adapter and device come back from the browser's promises after the
page starts (`GpuDevice::whenReady`); until then the CPU (QPainter) draws
the stage. What decides it is what WebGPU offers, not which browser it is:
the CPU draws the stage for good when there is no WebGPU adapter or device,
when the device lacks the storage buffers the renderer's exact coverage
needs (it would fall back to MSAA, whose edges fall short of the CPU's;
`RiveCanvas::valid`), and when the device reports an error or is lost
(`GpuDevice::broken`). The status bar says why, and View > Stage Renderer
shows CPU. The same holds on the desktop. View > Stage Renderer switches
between GPU and CPU, `?renderer=` does for one visit, and
`PHOENIX_GPU_DISABLE=1` turns WebGPU off as if there were none.

### Panels

Qt's main window leaves the tab bars of panel groups up in a page after the
panels in them close or move: a second row of tabs beside the real one, or
the tabs of hidden panels over the stage. `src/gui/web_page.cpp` hides any
tab bar over no open panel of its own after each change to the panels,
draws the window again, keeps every main window filling the page (a saved
size taken back would leave part of the page undrawn), and turns panel
animation off. The interface's font is 10 points: a browser gives Qt no
size of its own, and Qt's default, 12, is larger than any desktop's.

### Phones and tablets

A touch screen (the browser reports a coarse pointer) whose shorter side is
under 600 points gets the phone layout (`src/gui/main_window_phone.cpp`):
the stage alone, two rows of large buttons along the bottom (the common
tools, the stroke and fill swatches, undo and redo; step, play, the frame, Loop Playback, Onion Skin,
a slider through the frames and a menu), and a bar over them for the timeline and one down the right
for Properties. Each bar opens its panel as a drawer over the stage
(`PhoneDrawers`): the timeline from the bottom, in its phone view (taller
rows, wider frames, a narrow layers list and a header without the
buttons the bar below has), and Properties from the side. A press
anywhere else in the window closes the drawer, and one on the stage goes
no further, so closing a drawer never draws. The drawers, and the button
that sets a turned stage straight (shown over its corner only while it is
turned), are windows of their own: the stage's WebGPU canvas covers
anything the main window draws over it. The other panels (Color,
Swatches, Library) come one at a time as a sheet over the bottom of the
stage from the menu, which also holds the menu bar's menus, which the
layout puts away, and Full Layout.

A press held still on the stage for half a second opens the pop-up
palette, as a right-click does on the desktop (`StageHold`): the press is
held back until it moves, is let go or is held long enough, so a held one
never starts a stroke, and a second finger makes it a gesture. Qt takes a
press soon after a tap elsewhere for the second of a double click and
sends the stage only the double click, which is held back the same way.

The stage's least size is lowered while the layout is on, so a phone's
width holds the side bar and the rows; the page fits the main window to
it again once its least size comes down (`MainWindowsFillPage` also
watches layout requests), and the stage is fitted to the view once it
has. View > Phone Layout switches the layout on any screen, and the
choice is kept. A phone starts on a new document rather than the start
screen. While the layout is on, the window saves the panels as they were
before it, so they come back as they were.

A larger touch screen (a tablet) starts in the Simple workspace, until
another is chosen, with everything 1.2 times larger unless the theme sets
a scale. A dialog shown larger than the page is made to fit it
(`web_page.cpp`).

### Keys, the clipboard, touch and leaving

- A browser keeps some keys for itself and never passes them on: Ctrl+N,
  Ctrl+T, Ctrl+W and Ctrl+Tab, with Shift too. Their commands get the same
  key with Alt (or Alt and Shift, where Alt is taken), and Ctrl+Tab's get
  Ctrl+Alt+Page Down and Up, as their defaults, so the menus and Keyboard
  Shortcuts show them.
- Copying puts only plain text on the system clipboard (a text field's
  words): Qt hands the browser pictures in a form it turns down, and
  shapes paste within the page from Phoenix's own copies.
- A pen arrives as tablet events with pressure and tilt; touches as touch
  events. Two fingers zoom and turn the stage from where the fingers are
  (a browser moves one finger at a time, which Qt's pinch steps get wrong).
- Closing or reloading the page with changes not saved: the browser asks
  first (`MainWindow::hasUnsavedWork`, through `phoenix_has_unsaved_work`).
- Sound is filled in on the page's own thread between frames, so its
  buffer is 4096 frames (93 ms), large enough to cover a slow frame.

### Threads

Threads are web workers the page makes when it loads
(`QT_WASM_PTHREAD_POOL_SIZE`, 16): `src/util/parallel.cpp` keeps its pool to
six of them, and Qt's painting helpers use up to eight. A thread asked for
beyond those waits for the page's own thread to make a worker, so code on
that thread must not wait for one. Joining a thread from the page's own
thread blocks it, so keep such waits short.

### main() returns

A page cannot stay in an event loop, so on the web `main()` makes the
application and the main window on the heap and returns; the page goes on
handing Phoenix its events. (`QApplication::exec()` gets the same effect by
throwing past `main()`, which with WebAssembly's own exceptions would
destroy everything on the way.)
