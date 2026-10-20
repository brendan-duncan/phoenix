#!/usr/bin/env python3
"""Builds Phoenix's web editor with Qt for WebAssembly.

  python tools/web/build_web.py --serve     # build, then try it in the browser
  python tools/web/build_web.py             # just build

  more: [--arch wasm32|wasm64] [--config Release|Debug] [--port 8000]
        [--emsdk DIR] [--qt DIR] [--qt-host DIR] [--configure-only] [--clean]

Every build is packaged into <build>/webeditor: the page, the program, Qt's
loader, the icon and an .htaccess that has Apache send the headers Phoenix's
threads need. That folder is the site: --serve serves it on localhost and
opens it, and tools/web/deploy_web.py uploads it, so what goes up is what
was tried.

It needs Emscripten, Qt for WebAssembly and the desktop Qt of the same
version (for its code generators), each found from its option, then its
environment variable, then tools/web/web.ini, then the default folders:

  Emscripten  --emsdk    PHOENIX_EMSDK, EMSDK          emsdk       third_party/emsdk
  Qt for wasm --qt       PHOENIX_QT_WASM32 / _WASM64   qt_wasm32   third_party/qt/wasm32 (wasm64)
  desktop Qt  --qt-host  PHOENIX_QT_HOST, Qt6_ROOT     qt_host     third_party/qt/<this machine>,
                                                                   C:/Qt/<version>/msvc*_64

tools/web/setup_web.py installs the first two and writes web.ini (kept out
of the repository); to use ones installed elsewhere, write it by hand:

  [paths]
  emsdk = D:/src/emsdk
  qt_wasm32 = D:/src/qt-wasm32

Each architecture builds into its own folder, build-wasm32 or build-wasm64,
so both can be kept side by side; which one a build is comes only from the
Qt kit it is given.
"""

import argparse
import configparser
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SETTINGS = Path(__file__).resolve().with_name("web.ini")

# What the page loads, from the build folder; Qt's own page (Phoenix.html)
# and its logo stay behind.
PACKAGE_FILES = ["index.html", "Phoenix.js", "Phoenix.wasm", "qtloader.js", "phoenix.png"]
PACKAGE_FOLDER = "webeditor"

# Apache: the headers that let the page use threads, the wasm type, and the
# program compressed on the way (it shrinks to about a quarter). Revalidated
# on every visit, so a new build is picked up at once.
HTACCESS = """\
# Phoenix's web editor (tools/web/build_web.py).
AddType application/wasm .wasm
<IfModule mod_headers.c>
    Header always set Cross-Origin-Opener-Policy "same-origin"
    Header always set Cross-Origin-Embedder-Policy "require-corp"
    Header always set Cross-Origin-Resource-Policy "same-origin"
    Header set Cache-Control "no-cache"
</IfModule>
<IfModule mod_deflate.c>
    AddOutputFilterByType DEFLATE application/wasm text/javascript application/javascript text/html
</IfModule>
"""


def settings():
    """The paths tools/web/web.ini names, by key."""
    parser = configparser.ConfigParser()
    if SETTINGS.exists():
        parser.read(SETTINGS, encoding="utf-8")
    return dict(parser["paths"]) if parser.has_section("paths") else {}


def remember(**paths):
    """Writes paths into tools/web/web.ini, keeping the ones already there."""
    parser = configparser.ConfigParser()
    if SETTINGS.exists():
        parser.read(SETTINGS, encoding="utf-8")
    if not parser.has_section("paths"):
        parser.add_section("paths")
    for key, value in paths.items():
        if value:
            parser.set("paths", key, str(value).replace("\\", "/"))
    with open(SETTINGS, "w", encoding="utf-8") as out:
        out.write("; Where tools/web/build_web.py finds what it builds with. Kept out of the repository.\n")
        parser.write(out)


def first_existing(*candidates):
    for candidate in candidates:
        if candidate and Path(candidate).is_dir():
            return Path(candidate)
    return None


def host_target():
    if sys.platform == "win32":
        return "windows-x64"
    if sys.platform == "darwin":
        return "macos"
    return "linux-x64"


def find_host_qt(asked):
    candidates = [asked, os.environ.get("PHOENIX_QT_HOST"), os.environ.get("Qt6_ROOT"),
                  settings().get("qt_host"), str(ROOT / "third_party" / "qt" / host_target())]
    if sys.platform == "win32":
        candidates += sorted(glob.glob("C:/Qt/6.*/msvc*_64"), reverse=True)
    for candidate in candidates:
        # The host Qt has to have the code generators (moc, rcc, uic).
        if candidate and (Path(candidate) / "lib" / "cmake" / "Qt6HostInfo").is_dir():
            return Path(candidate)
    return None


def find_emsdk(asked):
    emsdk = first_existing(asked, os.environ.get("PHOENIX_EMSDK"), os.environ.get("EMSDK"),
                           settings().get("emsdk"), str(ROOT / "third_party" / "emsdk"))
    if emsdk and (emsdk / "upstream" / "emscripten").is_dir():
        return emsdk
    return None


def find_qt(asked, arch):
    qt = first_existing(asked, os.environ.get("PHOENIX_QT_" + arch.upper()), settings().get("qt_" + arch),
                        str(ROOT / "third_party" / "qt" / arch))
    if qt and (qt / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake").is_file():
        return qt
    return None


def emsdk_environment(emsdk):
    env = dict(os.environ)
    emscripten = emsdk / "upstream" / "emscripten"
    nodes = sorted(glob.glob(str(emsdk / "node" / "*" / "node.exe")) +
                   glob.glob(str(emsdk / "node" / "*" / "bin" / "node")))
    env["EMSDK"] = str(emsdk).replace("\\", "/")
    if nodes:
        env["EMSDK_NODE"] = nodes[-1]
    env["PATH"] = os.pathsep.join([str(emsdk), str(emscripten), env.get("PATH", "")])
    return env


def package(build):
    """Gathers the site into <build>/webeditor and returns that folder."""
    out = build / PACKAGE_FOLDER
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    for name in PACKAGE_FILES:
        source = build / name
        if not source.is_file():
            sys.exit("%s is missing: build first" % source)
        shutil.copy2(source, out / name)
    (out / ".htaccess").write_text(HTACCESS, encoding="utf-8", newline="\n")
    size = sum(path.stat().st_size for path in out.iterdir())
    print("Packaged %s (%.1f MB)" % (out, size / 1e6))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--serve", action="store_true", help="then serve it on localhost and open it in the browser")
    parser.add_argument("--port", type=int, default=8000, help="the port --serve uses (default 8000)")
    parser.add_argument("--arch", choices=["wasm32", "wasm64"], default="wasm32")
    parser.add_argument("--config", default="Release", help="Release (the default), Debug or RelWithDebInfo")
    parser.add_argument("--emsdk", help="the emsdk folder")
    parser.add_argument("--qt", help="the Qt for WebAssembly kit for --arch")
    parser.add_argument("--qt-host", help="the desktop Qt of the same version, for its code generators")
    parser.add_argument("--build-dir", help="where to build (default build-<arch>)")
    parser.add_argument("--package", action="store_true", help=argparse.SUPPRESS)  # every build is packaged now
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--clean", action="store_true", help="start the build folder over")
    parser.add_argument("cmake_args", nargs="*", help="more arguments for CMake's configure step")
    args = parser.parse_args()

    emsdk = find_emsdk(args.emsdk)
    if not emsdk:
        sys.exit("Emscripten was not found: run tools/web/setup_web.py, or name it in tools/web/web.ini")
    qt = find_qt(args.qt, args.arch)
    if not qt:
        sys.exit("Qt for WebAssembly (%s) was not found: run tools/web/setup_web.py, or name it in "
                 "tools/web/web.ini" % args.arch)
    host = find_host_qt(args.qt_host)
    if not host:
        sys.exit("A desktop Qt with its code generators was not found: name it in tools/web/web.ini (qt_host)")

    build = Path(args.build_dir) if args.build_dir else ROOT / ("build-" + args.arch)
    if args.clean and build.exists():
        shutil.rmtree(build)

    env = emsdk_environment(emsdk)
    print("Emscripten: %s\nQt (%s): %s\nHost Qt: %s\nBuilding in %s" % (emsdk, args.arch, qt, host, build))

    configure = [
        "cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=" + args.config,
        "-DCMAKE_TOOLCHAIN_FILE=" + str(qt / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake"),
        "-DQT_HOST_PATH=" + str(host),
    ] + args.cmake_args
    subprocess.run(configure, env=env, check=True)
    if args.configure_only:
        return 0
    subprocess.run(["cmake", "--build", str(build), "--target", "Phoenix"], env=env, check=True)
    site = package(build)
    if args.serve:
        serve = [sys.executable, str(Path(__file__).with_name("serve.py")), str(site), "--port", str(args.port), "--open"]
        return subprocess.call(serve)
    print("\nBuilt. Try it with:\n  python tools/web/serve.py %s --open\nand put it on the site with:\n"
          "  python tools/web/deploy_web.py%s" % (site, "" if args.arch == "wasm32" else " --arch " + args.arch))
    return 0


if __name__ == "__main__":
    sys.exit(main())
