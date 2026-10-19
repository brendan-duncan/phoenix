# The web build

Phoenix builds for browsers with Qt for WebAssembly. The same sources
compile for the desktop and the web; the web build leaves out what a
browser cannot do and turns the rest around where a browser works
differently.

## Building

```sh
python tools/web/setup_web.py     # once: Emscripten and Qt for WebAssembly into third_party/
python tools/web/build_web.py     # build-wasm32/
python tools/web/serve.py --open  # http://localhost:8000/
```

`setup_web.py` clones emsdk into `third_party/emsdk` with the Emscripten
the Qt release was built with (Qt 6.11 uses 4.0.7; they have to match), and
unpacks Qt's prebuilt multithreaded WebAssembly kit (qtbase and qtsvg) into
`third_party/qt/wasm32`. The build also needs the desktop Qt of the same
version for its code generators (moc, rcc, uic); `build_web.py` finds the
one the desktop build uses, or takes `--qt-host`. Each of the three can be
given with an option or an environment variable instead (see the script).

`serve.py` serves the build folder on localhost with the
`Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers.
Threads need them: without them the page has no SharedArrayBuffer and
stops at startup. Any host for the web build has to send the same headers.

## Wasm32 and wasm64

Nothing in the sources asks which one it is built for. `build_web.py
--arch wasm64` builds into `build-wasm64` with the kit in
`third_party/qt/wasm64` (or `PHOENIX_QT_WASM64`); the Qt kit decides the
rest, as Qt's toolchain file passes `-sMEMORY64` for a wasm64 kit. Qt
publishes no wasm64 kit, so one has to be built from source
(`configure -platform wasm-emscripten -feature-thread -feature-wasm64`).
Both can be built and served side by side.

Wasm32 has 4 GB of memory at most, and 32-bit `size_t` and pointers, which
the desktop builds never have. File formats keep 64-bit sizes and offsets
in `uint64_t`, never `size_t`.

## What the web build does differently

- **Dialogs and menus answer through callbacks** (`src/gui/modal.h`). A
  browser page cannot wait in an event loop of its own, and Qt stops the
  page when one starts, so no `exec()` may run in the web build: dialogs
  go through `modal::open`, message boxes through `modal::ask` and
  `modal::information`, input dialogs through `modal::getText` and the
  like, and pop-up menus through `modal::popup`. On the desktop these wait
  and call back before they return, as before, so the desktop runs the
  same code the web build does.
- **Files** (`src/gui/file_pick.h`, `src/gui/web_files.h`). A file opened
  comes from the browser's picker and is copied into the page's own file
  system (in memory, gone on reload); one saved is written there and
  downloaded. Folders cannot be chosen, so XFL folders are neither opened
  nor saved.
- **Drags** (`src/gui/web_drag.cpp`). `QDrag::exec` is linked wrapped:
  drags run in the page and send the usual drag and drop events, and
  `exec()` returns at once.
- **Other programs** (`src/gui/process.h`). A browser runs none: video
  export and import (ffmpeg), lip sync analysis, render queues, publish
  previews and the Claude Code terminal report that they cannot run.
- **Threads** are web workers made when the page loads
  (`QT_WASM_PTHREAD_POOL_SIZE`); `src/util/parallel.cpp` keeps its pool
  within them.
- **Left out**: Python scripting, the updater, HTML extension panels' web
  view, the terminal (libvterm), crash dumps.
