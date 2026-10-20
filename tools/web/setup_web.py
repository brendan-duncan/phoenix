#!/usr/bin/env python3
"""Installs what the web build needs: Emscripten and Qt for WebAssembly.

  python tools/web/setup_web.py [--arch wasm32|wasm64] [--prebuilt] [--emsdk DIR] [--qt DIR]

Emscripten goes into third_party/emsdk (a clone of emsdk with the version
below activated), and Qt for WebAssembly into third_party/qt/<arch>, where
tools/web/build_web.py looks first. All of it is ignored by git.

By default Qt is built from source (qtbase and qtsvg, about ten minutes),
because Qt's prebuilt kits leave out what makes Phoenix fast in a browser:
WebAssembly's own exceptions (the prebuilt kits emulate them in JavaScript,
which slows every call that might throw), SIMD, and Qt's threaded image and
painting helpers (which Qt turns off for WebAssembly; this enables them, and
the web build makes enough threads for them). The build needs the desktop
Qt of the same version, for its tools; the one the desktop build uses is
found as build_web.py finds it, or given with --qt-host. --arch wasm64
builds the 64-bit kit, which Qt does not publish at all.

--prebuilt instead unpacks Qt's prebuilt multithreaded kit, which is quick
and builds a slower editor (wasm32 only).

The versions go together: each Qt release is built with one Emscripten
(see Qt's "Qt for WebAssembly" page), and code built with another may not
link.
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_web  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent

QT_VERSION = "6.11.2"
EMSCRIPTEN_VERSION = "4.0.7"
QT_MODULES = ["qtbase", "qtsvg"]
QT_REPOSITORY = "https://download.qt.io/online/qtsdkrepository/all_os/wasm"
QT_SOURCES = "https://download.qt.io/official_releases/qt/%s/%s/submodules" % (
    QT_VERSION.rsplit(".", 1)[0], QT_VERSION)

# What Phoenix needs of a Qt it builds.
QT_FEATURES = ["-feature-thread", "-feature-wasm-simd128", "-feature-wasm-exceptions", "-feature-qtgui-threadpool"]

# Qt turns its threaded painting helpers off for WebAssembly, where the
# page's own thread must not wait on others. Phoenix makes the threads they
# need when the page loads, so waiting on them is short; the condition goes.
QT_PATCHES = [
    ("qtbase", "src/gui/configure.cmake",
     '    PURPOSE "Multi-threaded image transforms and QPainter fills."\n    CONDITION QT_FEATURE_thread AND NOT WASM',
     '    PURPOSE "Multi-threaded image transforms and QPainter fills."\n    CONDITION QT_FEATURE_thread'),
]


def run(command, cwd=None, env=None):
    print("+ " + " ".join(str(part) for part in command))
    subprocess.run(command, cwd=cwd, env=env, check=True,
                   shell=sys.platform == "win32" and str(command[0]).lower().endswith(".bat"))


def seven_zip():
    found = shutil.which("7z") or shutil.which("7za")
    if not found and sys.platform == "win32" and Path("C:/Program Files/7-Zip/7z.exe").exists():
        found = "C:/Program Files/7-Zip/7z.exe"
    if not found:
        sys.exit("Unpacking Qt's archives needs 7-Zip (7z) on the PATH")
    return found


def install_emscripten(folder):
    if not (folder / "emsdk.py").exists():
        run(["git", "clone", "--depth", "1", "https://github.com/emscripten-core/emsdk.git", str(folder)])
    emsdk = folder / ("emsdk.bat" if sys.platform == "win32" else "emsdk")
    run([str(emsdk), "install", EMSCRIPTEN_VERSION], cwd=folder)
    run([str(emsdk), "activate", EMSCRIPTEN_VERSION], cwd=folder)


def download(url, path):
    with urllib.request.urlopen(url) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out)


def installed(folder):
    return (folder / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake").exists()


def install_prebuilt_qt(folder):
    compact = QT_VERSION.replace(".", "")
    base = "%s/qt6_%s/qt6_%s_wasm_multithread" % (QT_REPOSITORY, compact, compact)
    package = "qt.qt6.%s.wasm_multithread" % compact
    downloads = folder / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)

    updates = downloads / "Updates.xml"
    download(base + "/Updates.xml", updates)
    archives = []
    for update in ElementTree.parse(updates).getroot().findall("PackageUpdate"):
        if update.findtext("Name") == package:
            version = update.findtext("Version")
            archives = [name.strip() for name in update.findtext("DownloadableArchives").split(",")]
            break
    if not archives:
        sys.exit("%s is not in Qt's repository" % package)

    unpack = seven_zip()
    for archive in archives:
        if not any(archive.startswith(module + "-") for module in QT_MODULES):
            continue
        name = version + archive
        local = downloads / archive
        print("Downloading %s" % archive)
        download("%s/%s/%s" % (base, package, name), local)
        expected = urllib.request.urlopen("%s/%s/%s.sha1" % (base, package, name)).read().decode().split()[0]
        actual = hashlib.sha1(local.read_bytes()).hexdigest()
        if actual != expected:
            sys.exit("%s does not match its checksum" % archive)
        run([unpack, "x", "-y", "-o" + str(folder), str(local)])
    shutil.rmtree(downloads)


def fetch_source(module, sources):
    """Downloads, checks and unpacks a Qt module's source; returns its folder."""
    name = "%s-everywhere-src-%s" % (module, QT_VERSION)
    folder = sources / name
    done = sources / (name + ".ready")
    if folder.is_dir() and done.exists():
        return folder
    if folder.is_dir():
        shutil.rmtree(folder)
    archive = sources / (name + ".tar.xz")
    print("Downloading %s" % archive.name)
    download("%s/%s.tar.xz" % (QT_SOURCES, name), archive)
    expected = urllib.request.urlopen("%s/%s.tar.xz.sha256" % (QT_SOURCES, name)).read().decode().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        sys.exit("%s does not match its checksum" % archive.name)
    unpack = seven_zip()
    run([unpack, "x", "-y", "-o" + str(sources), str(archive)])
    run([unpack, "x", "-y", "-o" + str(sources), str(sources / (name + ".tar"))])
    (sources / (name + ".tar")).unlink()
    archive.unlink()
    for patched_module, path, old, new in QT_PATCHES:
        if patched_module != module:
            continue
        file = folder / path
        text = file.read_text(encoding="utf-8")
        if old not in text:
            sys.exit("%s has changed; update QT_PATCHES in %s" % (file, Path(__file__).name))
        file.write_text(text.replace(old, new, 1), encoding="utf-8", newline="")
    done.write_text("unpacked and patched\n", encoding="utf-8")
    return folder


def build_qt(folder, arch, emsdk, host):
    """Builds qtbase and qtsvg for WebAssembly from source into folder.

    The sources and the build go into third_party/qt/source-<arch> (ignored by
    git), so a build stopped part way goes on from where it was when run
    again; the folder is removed once Qt is installed."""
    work = ROOT / "third_party" / "qt" / ("source-" + arch)
    work.mkdir(parents=True, exist_ok=True)
    env = build_web.emsdk_environment(emsdk)
    qtbase = fetch_source("qtbase", work)
    qtsvg = fetch_source("qtsvg", work)
    configure = qtbase / ("configure.bat" if sys.platform == "win32" else "configure")
    options = ["-qt-host-path", str(host), "-platform", "wasm-emscripten", "-prefix", str(folder), "-release",
               *QT_FEATURES, "-nomake", "examples", "-nomake", "tests", "-no-warnings-are-errors"]
    if arch == "wasm64":
        options.append("-feature-wasm64")
    base_build = work / "build-qtbase"
    base_build.mkdir(exist_ok=True)
    if not (base_build / "build.ninja").exists():
        run([str(configure), *options], cwd=base_build, env=env)
    run(["cmake", "--build", ".", "--parallel"], cwd=base_build, env=env)
    run(["cmake", "--install", "."], cwd=base_build, env=env)
    svg_build = work / "build-qtsvg"
    svg_build.mkdir(exist_ok=True)
    if not (svg_build / "build.ninja").exists():
        configure_module = folder / "bin" / ("qt-configure-module.bat" if sys.platform == "win32" else "qt-configure-module")
        run([str(configure_module), str(qtsvg)], cwd=svg_build, env=env)
    run(["cmake", "--build", ".", "--parallel"], cwd=svg_build, env=env)
    run(["cmake", "--install", "."], cwd=svg_build, env=env)
    shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--arch", choices=["wasm32", "wasm64"], default="wasm32")
    parser.add_argument("--prebuilt", action="store_true", help="unpack Qt's prebuilt kit rather than building one")
    parser.add_argument("--emsdk", help="where Emscripten goes (default third_party/emsdk)")
    parser.add_argument("--qt", help="where Qt for WebAssembly goes (default third_party/qt/<arch>)")
    parser.add_argument("--qt-host", help="the desktop Qt of the same version")
    args = parser.parse_args()

    # What tools/web/web.ini already names is kept rather than installed again.
    known = build_web.settings()
    emsdk = Path(args.emsdk or known.get("emsdk") or ROOT / "third_party" / "emsdk")
    install_emscripten(emsdk)
    qt = Path(args.qt or known.get("qt_" + args.arch) or ROOT / "third_party" / "qt" / args.arch)
    if installed(qt):
        print("Qt for WebAssembly is already in %s" % qt)
    elif args.prebuilt:
        if args.arch != "wasm32":
            sys.exit("Qt publishes no prebuilt %s kit; build it from source" % args.arch)
        install_prebuilt_qt(qt)
    else:
        host = build_web.find_host_qt(args.qt_host)
        if not host:
            sys.exit("Building Qt for WebAssembly needs the desktop Qt %s: pass --qt-host" % QT_VERSION)
        build_qt(qt, args.arch, emsdk, host)
    build_web.remember(emsdk=emsdk, qt_host=build_web.find_host_qt(args.qt_host), **{"qt_" + args.arch: qt})
    print("\nReady (tools/web/web.ini says where). Build and try it with:\n  python tools/web/build_web.py --serve%s"
          % ("" if args.arch == "wasm32" else " --arch " + args.arch))
    return 0


if __name__ == "__main__":
    sys.exit(main())
