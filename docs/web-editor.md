# The web editor

Phoenix runs in a browser as the web editor, at
[phoenixanimate.com/webeditor](https://phoenixanimate.com/webeditor/). It
is the same source as the desktop app, built with Qt for WebAssembly. The
web build leaves out what a browser cannot do and works around the places
where a browser behaves differently; the desktop build compiles none of the
web-only code and runs as it always has.

## Building, trying and deploying

```sh
python tools/web/setup_web.py            # once
python tools/web/build_web.py            # build-wasm32/
python tools/web/serve.py --open         # http://localhost:8000/
python tools/web/build_web.py --package  # build-wasm32/webeditor/
python tools/web/deploy_web.py           # phoenixanimate.com/webeditor
```

- **setup_web.py** clones emsdk into `third_party/emsdk` and activates the
  Emscripten the Qt release was built with (Qt 6.11 uses 4.0.7; the two
  have to match), and unpacks Qt's prebuilt multithreaded WebAssembly kit
  (qtbase and qtsvg) into `third_party/qt/wasm32`. aqtinstall cannot fetch
  that kit at the moment, so the script downloads it from Qt's repository
  itself and checks each archive's SHA-1. Unpacking needs 7-Zip.
- **build_web.py** configures with Qt's toolchain file and builds with
  Ninja, Release unless `--config` says otherwise. Besides Emscripten and
  the wasm kit it needs the desktop Qt of the same version, for moc, rcc
  and uic; it finds the one the desktop build uses, or takes `--qt-host`.
  Each of the three can also come from an environment variable
  (`PHOENIX_EMSDK`, `PHOENIX_QT_WASM32` or `_WASM64`, `PHOENIX_QT_HOST`).
  `--package` gathers what the site needs into `<build>/webeditor`: the
  page (`resources/web/index.html`), `Phoenix.js`, `Phoenix.wasm`, Qt's
  `qtloader.js`, the icon, and an `.htaccess` for Apache.
- **serve.py** serves a build folder (or the package) on localhost with the
  `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers,
  uncached. Threads need those headers: without them the page has no
  SharedArrayBuffer and says so instead of starting. Any host for the
  editor has to send them.
- **deploy_web.py** uploads the package to the `webeditor` folder of the
  site, using the server in `tools/deploy_site.ini` (as `deploy_site.py`
  does), and swaps it in whole. It then checks that the live page sends the
  isolation headers. The package's `.htaccess` sets them where Apache
  allows it; `--configure-apache` instead adds them to Apache's own
  configuration for `/webeditor` (with sudo, once). `deploy_site.py
  --mirror` leaves the `webeditor` folder alone.

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

### Threads

Threads are web workers the page makes when it loads
(`QT_WASM_PTHREAD_POOL_SIZE`, 8); `src/util/parallel.cpp` keeps its pool to
six of them. Joining a thread from the page's own thread blocks it, so keep
such waits short.
