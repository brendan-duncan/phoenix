# Building Phoenix

Phoenix is a C++17 app built with CMake and Qt 6. The first configure downloads what it needs, so a compiler, CMake, Git and Python are enough to start.

## What you need

| | Windows | macOS | Linux |
|---|---|---|---|
| Compiler | Visual Studio 2022 or newer, with "Desktop development with C++" | Xcode or the Command Line Tools (`xcode-select --install`) | GCC or Clang with C++17 |
| Also | CMake 3.16 or newer, Git, Python 3 | the same | the same, plus the packages below |

On Debian or Ubuntu, Qt needs:

```sh
sudo apt-get install -y libgl1-mesa-dev libxkbcommon-dev libfontconfig1-dev libegl1-mesa-dev
```

The Linux build is newer than the Windows and macOS ones and has been tried less on desktops.

## Build

```sh
git clone https://github.com/brendan-duncan/phoenix.git
cd phoenix
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Phoenix phoenix_tests --parallel 8
```

On Windows, add `-A x64` to the first command to pick 64-bit (`-A ARM64` for Arm). Visual Studio generators put the program in `build/Release/Phoenix.exe`; on macOS it is `build/Phoenix.app`, and on Linux `build/Phoenix`.

`RelWithDebInfo` is a good everyday configuration: optimized, with debugging symbols. Debug builds work, but large documents are much slower in them.

## What the first configure downloads

Each of these is pinned to a version or commit, and checked against a hash where it is an archive:

- **Qt 6.11.2.** CMake uses an installed Qt if it finds one, in this order:
  1. `-DQt6_ROOT=...` or `-DCMAKE_PREFIX_PATH=...`
  2. the `Qt6_ROOT` or `QTDIR` environment variable
  3. a trimmed Qt already unpacked in `third_party/qt`
  4. on Windows, the newest `C:/Qt/6.*/msvc*_64`

  Failing all of those, it downloads a trimmed Qt (about 45 MB) from this repository's `qt-6.11.2` release into `third_party/qt`.
- **ffmpeg**, the program Phoenix encodes and reads video with. It ships beside Phoenix rather than being linked in. It is downloaded into `third_party/ffmpeg`.
- **libas3**, the ActionScript compiler and virtual machine. If a checkout sits beside this one (`../libas3`), that one is built as it stands; otherwise the pinned commit is fetched.
- **QuickJS** for JSFL scripts, **Velopack** for installing and updating, and the **WebView2 SDK** on Windows.

## Options

Pass these to the first `cmake` command as `-DNAME=VALUE`.

| Option | Default | What it does |
|---|---|---|
| `PHOENIX_USE_SYSTEM_QT` | `ON` | Look for an installed Qt before the trimmed one. `OFF` always uses the trimmed Qt, as CI does. |
| `PHOENIX_FETCH_QT` | `ON` | Download the trimmed Qt when no Qt is found. |
| `PHOENIX_QT_ARCHIVE_DIR` | | A folder already holding the Qt archive, used instead of downloading. |
| `PHOENIX_FETCH_FFMPEG` | `ON` | Download ffmpeg when it is not unpacked yet. |
| `PHOENIX_FFMPEG_EXECUTABLE` | | An ffmpeg program to use instead of the pinned one. |
| `PHOENIX_FFMPEG_ARCHIVE_DIR` | | A folder already holding the ffmpeg archive. |
| `PHOENIX_FFMPEG_REQUIRED` | `OFF` | Fail when there is no ffmpeg, rather than warn. Release builds turn it on. |
| `PHOENIX_LIBAS3_SOURCE` | | A libas3 checkout to build instead of the pinned commit. |
| `PHOENIX_PYTHON` | `ON` | Python scripting, using the person's own Python when a script first runs. Building never needs it. |
| `PHOENIX_WEBVIEW` | `ON` | HTML extension panels (WebView2 on Windows, WKWebView on macOS; Linux goes without). |
| `PHOENIX_UPDATER` | `ON` | The built-in updater. |

Without a network, set `PHOENIX_FETCH_FFMPEG=OFF` and point `PHOENIX_FFMPEG_EXECUTABLE` at any ffmpeg; video export and import then use that one.

## Tests

`phoenix_tests` holds the unit and round-trip tests:

```sh
ctest --test-dir build -C Release -R phoenix_tests --output-on-failure
```

or run the program itself, with a name filter if you like:

```sh
build/Release/phoenix_tests            # every test
build/Release/phoenix_tests merge      # tests with "merge" in their name
```

Some tests read real documents when you point them at a folder of FLA files, and skip themselves otherwise:

| Variable | What it enables |
|---|---|
| `PHOENIX_FLA_CORPUS` | Opening, saving and round-tripping every FLA in the folder. |
| `PHOENIX_JSFL_CORPUS` | Running a folder of JSFL scripts. |
| `PHOENIX_BINARY_FLA_LIST` | A sweep of old binary FLA files listed in a text file (with `PHOENIX_BINARY_FLA_REPORT` for the results). |

The round-trip tests write to the system's temporary folder. When running several test runs at once, give each its own `TEMP` and `TMP` (or `TMPDIR`).

## Checking your work without a window

The Phoenix program has a command line for checking changes headlessly. Set `QT_QPA_PLATFORM=offscreen` so no window opens:

```sh
QT_QPA_PLATFORM=offscreen build/Release/Phoenix render my.fla --range 1-24 --out frames/
QT_QPA_PLATFORM=offscreen build/Release/Phoenix --export-image my.fla frame1.png
```

`capture-window` and `capture-dialog` draw the window, a panel, a menu or a dialog to a picture, which is how the guide's screenshots are made (run them without `offscreen`, which loses the theme). See the [command line guide](https://phoenixanimate.com/command-line.html) for every command.

## The web editor

Phoenix also builds for browsers with Qt for WebAssembly, as the web editor at [phoenixanimate.com/webeditor](https://phoenixanimate.com/webeditor/). It needs Emscripten and Qt's WebAssembly kit as well as the desktop Qt above (for its code generators); `tools/web/setup_web.py` installs the first two into `third_party/`.

```sh
python tools/web/setup_web.py            # once: Emscripten 4.0.7 and Qt 6.11.2 for WebAssembly
python tools/web/build_web.py            # builds into build-wasm32/
python tools/web/serve.py --open         # tries it at http://localhost:8000/
python tools/web/build_web.py --package  # gathers the site into build-wasm32/webeditor/
python tools/web/deploy_web.py           # uploads that to phoenixanimate.com/webeditor
```

Serve it with `serve.py`, not a plain file server: the page needs the `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers for threads. `--arch wasm64` builds a 64-bit version into `build-wasm64/`, given a Qt kit built for it. [web-editor.md](web-editor.md) covers how the web editor differs from the desktop app and what to keep in mind when changing code that it shares.

## Releases

Releases are built by the Release workflow (`.github/workflows/release.yml`) when a `v*` tag is pushed. It builds on each platform with `-DPHOENIX_FFMPEG_REQUIRED=ON`, runs the tests, and packs installers with Velopack's `vpk` tool, which must be the version pinned in `cmake/PhoenixVelopack.cmake`. The version comes from `project(Phoenix VERSION ...)` in `CMakeLists.txt`; `tools/sync_docs_version.py` copies it into the guide.

## The guide

The website and guide are plain HTML in `website/`. Open `website/index.html` in a browser, or serve the folder (`python -m http.server --directory website`). `tools/build_changelog.py` makes `website/changelog.html` from `CHANGELOG.md`.
