#!/usr/bin/env python3
"""Installs what the web build needs: Emscripten and Qt for WebAssembly.

  python tools/web/setup_web.py [--emsdk DIR] [--qt DIR]

Emscripten goes into third_party/emsdk (a clone of emsdk with the version
below activated) and Qt's prebuilt multithreaded WebAssembly kit into
third_party/qt/wasm32, where tools/web/build_web.py looks first. Both
folders are ignored by git. The desktop Qt of the same version, which the
build takes its code generators from, is the one the desktop build uses.

The versions go together: each Qt release is built with one Emscripten
(see Qt's "Qt for WebAssembly" page), and code built with another may not
link. Qt publishes no wasm64 kit; a wasm64 build needs Qt built from source
with -feature-wasm64 (docs/web-editor.md).
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

QT_VERSION = "6.11.2"
EMSCRIPTEN_VERSION = "4.0.7"
QT_MODULES = ["qtbase", "qtsvg"]
QT_REPOSITORY = "https://download.qt.io/online/qtsdkrepository/all_os/wasm"


def run(command, cwd=None):
    print("+ " + " ".join(str(part) for part in command))
    subprocess.run(command, cwd=cwd, check=True, shell=sys.platform == "win32" and str(command[0]).endswith(".bat"))


def install_emscripten(folder):
    if not (folder / "emsdk.py").exists():
        run(["git", "clone", "--depth", "1", "https://github.com/emscripten-core/emsdk.git", str(folder)])
    emsdk = folder / ("emsdk.bat" if sys.platform == "win32" else "emsdk")
    run([str(emsdk), "install", EMSCRIPTEN_VERSION], cwd=folder)
    run([str(emsdk), "activate", EMSCRIPTEN_VERSION], cwd=folder)


def download(url, path):
    with urllib.request.urlopen(url) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out)


def install_qt(folder):
    if (folder / "lib" / "cmake" / "Qt6" / "qt.toolchain.cmake").exists():
        print("Qt for WebAssembly is already in %s" % folder)
        return
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

    seven_zip = shutil.which("7z") or shutil.which("7za")
    if not seven_zip and sys.platform == "win32" and Path("C:/Program Files/7-Zip/7z.exe").exists():
        seven_zip = "C:/Program Files/7-Zip/7z.exe"
    if not seven_zip:
        sys.exit("Unpacking Qt's archives needs 7-Zip (7z) on the PATH")

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
        run([seven_zip, "x", "-y", "-o" + str(folder), str(local)])
    shutil.rmtree(downloads)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--emsdk", default=str(ROOT / "third_party" / "emsdk"))
    parser.add_argument("--qt", default=str(ROOT / "third_party" / "qt" / "wasm32"))
    args = parser.parse_args()
    install_emscripten(Path(args.emsdk))
    install_qt(Path(args.qt))
    print("\nReady. Build with:\n  python tools/web/build_web.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
