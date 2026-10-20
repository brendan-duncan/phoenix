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

- **setup_web.py** clones emsdk into `third_party/emsdk` and activates the
  Emscripten the Qt release was built with (Qt 6.11 uses 4.0.7; the two
  have to match), then builds Qt 6.11.2 for WebAssembly from source (qtbase
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
| `?debugdocks` | log the panels and their tab bars to the console after each change to them |

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

### Panels

Qt's main window leaves the tab bars of panel groups up in a page after the
panels in them close or move: a second row of tabs beside the real one, or
the tabs of hidden panels over the stage. `src/gui/web_page.cpp` hides any
tab bar over no open panel of its own after each change to the panels,
draws the window again, keeps every main window filling the page (a saved
size taken back would leave part of the page undrawn), and turns panel
animation off. The interface's font is 10 points: a browser gives Qt no
size of its own, and Qt's default, 12, is larger than any desktop's.

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
