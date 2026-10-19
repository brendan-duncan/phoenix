#!/usr/bin/env python3
"""Builds Phoenix for the web with Qt for WebAssembly.

  python tools/web/build_web.py [--arch wasm32|wasm64] [--config Release|Debug]
                                [--emsdk DIR] [--qt DIR] [--qt-host DIR]
                                [--configure-only] [--clean]

It needs three things, each found from its option, then its environment
variable, then the default folders tools/web/setup_web.py installs into:

  Emscripten  --emsdk    PHOENIX_EMSDK, EMSDK          third_party/emsdk
  Qt for wasm --qt       PHOENIX_QT_WASM32 / _WASM64   third_party/qt/wasm32 (wasm64)
  desktop Qt  --qt-host  PHOENIX_QT_HOST, Qt6_ROOT     third_party/qt/<this machine>,
                                                       C:/Qt/<version>/msvc*_64

The Emscripten and both Qts must be the versions cmake/QtArchives.cmake
and tools/web/setup_web.py name. Each architecture builds into its own
folder, build-wasm32 or build-wasm64, so both can be kept side by side;
which one a build is comes only from the Qt kit it is given. The folder is
the site: serve it with tools/web/serve.py.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


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
                  str(ROOT / "third_party" / "qt" / host_target())]
    if sys.platform == "win32":
        candidates += sorted(glob.glob("C:/Qt/6.*/msvc*_64"), reverse=True)
    for candidate in candidates:
        # The host Qt has to have the code generators (moc, rcc, uic).
        if candidate and (Path(candidate) / "lib" / "cmake" / "Qt6HostInfo").is_dir():
            return Path(candidate)
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--arch", choices=["wasm32", "wasm64"], default="wasm32")
    parser.add_argument("--config", default="Release", help="Release (the default), Debug or RelWithDebInfo")
    parser.add_argument("--emsdk", help="the emsdk folder")
    parser.add_argument("--qt", help="the Qt for WebAssembly kit for --arch")
    parser.add_argument("--qt-host", help="the desktop Qt of the same version, for its code generators")
    parser.add_argument("--build-dir", help="where to build (default build-<arch>)")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--clean", action="store_true", help="start the build folder over")
    parser.add_argument("cmake_args", nargs="*", help="more arguments for CMake's configure step")
    args = parser.parse_args()

    emsdk = first_existing(args.emsdk, os.environ.get("PHOENIX_EMSDK"), os.environ.get("EMSDK"),
                           str(ROOT / "third_party" / "emsdk"))
    if not emsdk or not (emsdk / "upstream" / "emscripten").is_dir():
        sys.exit("Emscripten was not found: run tools/web/setup_web.py, or pass --emsdk")

    kit_var = "PHOENIX_QT_" + args.arch.upper()
    qt = first_existing(args.qt, os.environ.get(kit_var), str(ROOT / "third_party" / "qt" / args.arch))
    if not qt or not (qt / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake").is_file():
        sys.exit("Qt for WebAssembly (%s) was not found: run tools/web/setup_web.py, or pass --qt" % args.arch)

    host = find_host_qt(args.qt_host)
    if not host:
        sys.exit("A desktop Qt with its code generators was not found: pass --qt-host")

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
    print("\nBuilt %s. Serve it with:\n  python tools/web/serve.py %s" % (build / "Phoenix.html", build))
    return 0


if __name__ == "__main__":
    sys.exit(main())
